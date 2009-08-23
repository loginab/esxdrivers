/* **********************************************************
 * Copyright 1998, 2007-2008 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * linux_proc.c --
 *
 *      Linux proc filesystem emulation.	
 */

#include <linux/proc_fs.h>
#include "linux_stubs.h"
#include "vmkapi.h"

#define VMKLNX_LOG_HANDLE LinProc
#include "vmklinux26_log.h"

/*
 * Global data
 */

struct proc_dir_entry proc_root;
struct proc_dir_entry proc_driver_dummy;
struct proc_dir_entry *proc_root_driver = &proc_driver_dummy;
struct proc_dir_entry proc_net_dummy;
struct proc_dir_entry *proc_net = &proc_net_dummy;
struct proc_dir_entry proc_scsi_dummy;
struct proc_dir_entry *proc_scsi = &proc_scsi_dummy;
struct proc_dir_entry proc_bus_dummy;
struct proc_dir_entry *proc_bus = &proc_bus_dummy;


static vmk_ProcEntry linuxRoot;
static vmk_ProcEntry linuxDrvRoot;
static vmk_ProcEntry linuxNet;
static vmk_ProcEntry linuxScsi;
static vmk_ProcEntry linuxBus;

/*
 * Local data
 */

#define MAX_NAME_LEN 256
#define PROC_BLOCK_SIZE (VMK_PAGE_SIZE - 1024)

typedef struct LinuxProcEntry {
   struct LinuxProcEntry *next;
   char name[MAX_NAME_LEN];
   struct proc_dir_entry *parent;
   struct proc_dir_entry *drvEntry;
   vmk_ProcEntry vmkEntry;
} LinuxProcEntry;

/*
 * linuxEntries maintains the mapping between the proc_dir_entry and its 
 * corresponding vmk Proc_Entry. 
 */
static LinuxProcEntry *linuxEntries;

/*
 * Locks and ranks 
 * 
 * 1 - linuxProcLock; protects linuxEntries elements.
 * 9 - procLock; read-remove synch. [ LinuxProcAdd/RemoveEntry]
 */
static vmk_Spinlock linuxProcLock;
static char scratchPage[VMK_PAGE_SIZE];

/*
 * Local functions
 */
static LinuxProcEntry* LinuxProcCreateEntry(const char* name,
                                            struct proc_dir_entry* parent,
                                            vmk_Bool isDir);
static VMK_ReturnStatus LinuxProcRemoveEntry(const char *name, 
                                 struct proc_dir_entry *parent);
static LinuxProcEntry* LinuxProcAddEntry(const char* name,
                                          struct proc_dir_entry* parent,
                                          struct proc_dir_entry* entry,
                                          vmk_ProcEntry vmkEntry);
static LinuxProcEntry* LinuxProcFindEntryByPath(const char* path,
                                                struct proc_dir_entry* parent);
static LinuxProcEntry* LinuxProcFindEntryByPtr(struct proc_dir_entry* parent);
static void LinuxProcDeleteEntry(LinuxProcEntry* entry);

static int LinuxProcRead(vmk_ProcEntry entry, char *buffer, int *len);
static int LinuxProcWrite(vmk_ProcEntry entry, char *buffer, int *len);


static void LinuxProcAttachPDE(struct proc_dir_entry *pde,
                               struct proc_dir_entry *parent);
static void LinuxProcDetachPDE(struct proc_dir_entry *pde,
                               struct proc_dir_entry *parent);

/*
 *-----------------------------------------------------------------------------
 *
 *  LinuxProc_Init/Cleanup --
 *
 *      Init and shutdown the Linux proc emulation.
 *      Registers a "vmkdev" proc dir, which will be the proc root for all nodes
 *      registered by drivers.
 *
 * Results:
 *      None. 
 *
 * Side effects:
 *      None. 
 *
 *-----------------------------------------------------------------------------
 */

void
LinuxProc_Init(void)
{
   VMKLNX_CREATE_LOG();

   vmk_SPCreate(&linuxProcLock, vmklinuxModID, "linuxProcLock",
                          NULL, VMK_SP_RANK_LEAF);

   /* TODO: pre-defined nodes setup should be done in vmkernel procfs */
   /* Create proc entry structs for pre-defined nodes */
   vmk_ProcEntryCreate(&linuxRoot, vmklinuxModID, "root", VMK_TRUE);
   vmk_ProcEntryCreate(&linuxDrvRoot, vmklinuxModID, "driver", VMK_TRUE);
   vmk_ProcEntryCreate(&linuxNet, vmklinuxModID, "net", VMK_TRUE);
   vmk_ProcEntryCreate(&linuxScsi, vmklinuxModID, "scsi", VMK_TRUE);
   if (is_vmvisor) {
      vmk_ProcEntryCreate(&linuxBus, vmklinuxModID, "bus", VMK_TRUE);
   } else {
      linuxBus = (vmk_ProcEntry)-1;
   }
   if (linuxRoot == NULL || linuxDrvRoot == NULL || linuxNet == NULL || linuxScsi == NULL || linuxBus == NULL) {
      vmk_WarningMessage("No memory for pre-defined proc nodes");
      VMK_ASSERT(0);
      return;
   }
   vmk_ProcSetupPredefinedNode(VMK_PROC_ROOT, linuxRoot, VMK_TRUE);
   vmk_ProcSetupPredefinedNode(VMK_PROC_ROOT_DRIVER, linuxDrvRoot, VMK_FALSE);
   vmk_ProcSetupPredefinedNode(VMK_PROC_ROOT_NET, linuxNet, VMK_FALSE);
   vmk_ProcSetupPredefinedNode(VMK_PROC_ROOT_SCSI, linuxScsi, VMK_FALSE);
   if (is_vmvisor) {
      vmk_ProcSetupPredefinedNode(VMK_PROC_ROOT_BUS, linuxBus, VMK_FALSE);
   }
}

void
LinuxProc_Cleanup(void)
{
   vmk_SPDestroy(&linuxProcLock);

   vmk_ProcRemovePredefinedNode(VMK_PROC_ROOT, linuxRoot, VMK_TRUE);
   vmk_ProcRemovePredefinedNode(VMK_PROC_ROOT_DRIVER, linuxDrvRoot, VMK_FALSE);
   vmk_ProcRemovePredefinedNode(VMK_PROC_ROOT_NET, linuxNet, VMK_FALSE);
   vmk_ProcRemovePredefinedNode(VMK_PROC_ROOT_SCSI, linuxScsi, VMK_FALSE);
   if (is_vmvisor) {
      vmk_ProcRemovePredefinedNode(VMK_PROC_ROOT_BUS, linuxBus, VMK_FALSE);
   }

   vmk_ProcEntryDestroy(linuxRoot);
   vmk_ProcEntryDestroy(linuxDrvRoot);
   vmk_ProcEntryDestroy(linuxNet);
   vmk_ProcEntryDestroy(linuxScsi);
   if (is_vmvisor) {
      vmk_ProcEntryDestroy(linuxBus);
   }

   VMKLNX_DESTROY_LOG();
}

/*
 * proc_fs interface (global functions)
 */

/**                                          
 *  create_proc_entry - Creates new entry under proc
 *  @name: proc node
 *  @mode: mode for creation of the node
 *  @parent: pointer to the parent node
 *                                           
 *  This function creates an entry in the proc system under the parent with the 
 *  specified 'name'. Important fields are filled and a pointer to the newly 
 *  created proc entry is returned.
 *
 *  RETURN VALUE:                     
 *  Pointer to the newly created proc entry of type proc_dir_entry. 
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: create_proc_entry */
struct proc_dir_entry *
create_proc_entry(const char *name,
                  mode_t mode,
                  struct proc_dir_entry *parent)
{
   LinuxProcEntry *lpe;
                                                                                
   lpe = LinuxProcCreateEntry(name, parent, S_ISDIR(mode));
                                                                                
   if (lpe == NULL) {
      return NULL;
   }
   return lpe->drvEntry;
}

/**
 *  proc_mkdir - create an entry in the proc file system.
 *  @name: the name of the proc entry
 *  @parent: the node under which the proc entry is created
 *
 *  Creates a proc entry @name in the proc filesystem under node @parent. @name
 *  can be a path and @parent can be NULL.
 *
 *  RETURN VALUE:
 *  Returns a pointer to the newly created proc_dir_entry, NULL otherwise.
 *
 */
/* _VMKLNX_CODECHECK_: proc_mkdir */
struct proc_dir_entry *
proc_mkdir(const char *name,
           struct proc_dir_entry *parent)
{
   LinuxProcEntry *lpe;
   lpe = LinuxProcCreateEntry(name, parent, VMK_TRUE);

   if (lpe == NULL) {
      return NULL;
   }
   return lpe->drvEntry;
}


/**                                          
 *  remove_proc_entry - remove a /proc entry and free it if it's not currently in use. 
 *  @name: Proc entry name to remove
 *  @parent: Parent proc node to search under
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: remove_proc_entry */
void 
remove_proc_entry(const char *name, 
                  struct proc_dir_entry *parent)
{
   VMK_ReturnStatus ret;
   struct proc_dir_entry *pde, **ptr;

   ret = LinuxProcRemoveEntry(name, parent);
   if (ret == VMK_OK) {
      return;
   }

   /*
    * This should probably scan the name to get the parent but
    * it is probably okay since we only expect proc_mknod() nodes
    * to remain.
    */
   if (!parent) {
      parent = &proc_root;
   }
   // If not a regular proc node, could be a proc_mknod() node.
   pde = NULL;
   vmk_SPLock(&linuxProcLock);
   for (ptr = &(parent->subdir); *ptr; ptr = &((*ptr)->next)) {
      if (strncmp(name, (*ptr)->name, (*ptr)->namelen) == 0) {
         pde = *ptr;
         *ptr = pde->next;
         pde->next = NULL;
         pde->parent = NULL;
         break;
      }
   }
   vmk_SPUnlock(&linuxProcLock);
   if (pde) {
      VMK_ASSERT(parent->parent == NULL);
      LinuxProc_FreePDE(pde);
   } else {
      VMKLNX_WARN("proc dir entry not found for %s", name);
   }
}

/*
 *-----------------------------------------------------------------------------
 *
 *  LinuxProc_AllocPDE --
 *
 *      Creates a bare entry with just the name and namelen in it.
 *
 * Results:
 *      proc_dir_entry 
 *
 * Side effects:
 *      None. 
 *
 *-----------------------------------------------------------------------------
 */

struct proc_dir_entry*
LinuxProc_AllocPDE(const char* name)
{
   struct proc_dir_entry* pde;
   uint32_t len;
   
   if (name == NULL) {
      return NULL;
   }
   len = strlen(name);

   pde = vmk_HeapAlloc(VMK_MODULE_HEAP_ID, (sizeof(struct proc_dir_entry) 
                                             + len + 1));
   if (pde == NULL) {
      VMKLNX_WARN("No memory.");
      return NULL;
   }

   memset(pde, 0, sizeof(struct proc_dir_entry));
   pde->name = ((char *) pde + sizeof(struct proc_dir_entry));
   strncpy((char *) pde->name, name, len + 1);
   pde->namelen = len; 
   return pde;
}

/*
 *-----------------------------------------------------------------------------
 *
 *  LinuxProc_FreePDE --
 *
 *      Free entry allocated by LinuxProc_AllocPDE. 
 *
 * Results:
 *      None. 
 *
 * Side effects:
 *      None. 
 *
 *-----------------------------------------------------------------------------
 */

void
LinuxProc_FreePDE(struct proc_dir_entry* pde)
{
   VMK_ASSERT(pde->parent == NULL);
   VMK_ASSERT(pde->next == NULL);
   VMK_ASSERT(pde->subdir == NULL);
   vmk_HeapFree(VMK_MODULE_HEAP_ID, pde);
}

/*
 * Local functions
 */

/*
 *-----------------------------------------------------------------------------
 *
 *  LinuxProcAttachPDE --
 *
 *      Attach a pde to it's parent and siblings. 
 *
 * Results:
 *      None. 
 *
 * Side effects:
 *      None. 
 *
 *-----------------------------------------------------------------------------
 */

void
LinuxProcAttachPDE(struct proc_dir_entry *pde,
                     struct proc_dir_entry *parent)
{
   VMK_ASSERT_SPLOCK_LOCKED(&linuxProcLock);
   pde->parent = parent;
   pde->next = parent->subdir;
   parent->subdir = pde;
}

/*
 *-----------------------------------------------------------------------------
 *
 *  LinuxProcDetachPDE --
 *
 *      Detach a pde from it's parent and siblings. 
 *
 * Results:
 *      None. 
 *
 * Side effects:
 *      None. 
 *
 *-----------------------------------------------------------------------------
 */

void
LinuxProcDetachPDE(struct proc_dir_entry *pde,
                   struct proc_dir_entry *parent)
{
   struct proc_dir_entry **ptr;
   VMK_ASSERT_SPLOCK_LOCKED(&linuxProcLock);
   for (ptr = &(parent->subdir); *ptr; ptr = &((*ptr)->next)) {
      if (*ptr == pde) {
         *ptr = pde->next;
         pde->next = NULL;
         pde->parent = NULL;
         break;
      }
   }
}

/*
 *-----------------------------------------------------------------------------
 *
 * LinuxProcRemoveEntry --
 *
 *      Handle a proc entry remove from the driver. 
 *
 * Results:
 *      VMK_OK; VMK_NOT_FOUND;
 *      VMK_BUSY if node has children and hence should not be removed.
 *
 * Side effects:
 *      May spin if the entry is being used (read/write in progress). 
 *
 *-----------------------------------------------------------------------------
 */

VMK_ReturnStatus
LinuxProcRemoveEntry(const char *name,
                     struct proc_dir_entry *parent)
{
   struct proc_dir_entry *pde; 
   vmk_ProcEntry pe;
   LinuxProcEntry *lpe;

   VMKLNX_DEBUG(4, "name=%s, parent=%p.", name, parent);

   vmk_SPLock(&linuxProcLock);

   // Find entry in local list.
   lpe = LinuxProcFindEntryByPath(name, parent);

   if (lpe == NULL) {
      VMKLNX_DEBUG(0, "Couldn't find proc node %s.", name); 
      vmk_SPUnlock(&linuxProcLock);
      return VMK_NOT_FOUND;
   }

   if (parent == NULL) {
      parent = lpe->parent;
   }

   pe = lpe->vmkEntry;
   pde = lpe->drvEntry;
   pde->deleted = 1;

   // If a read/write on this node is in progress, this call will spin. 
   vmk_ProcUnRegister(pe);

   // Don't want to free a node with children.
   if (pde->subdir) {
      VMKLNX_WARN("Trying to remove node with children.\n");
      vmk_SPUnlock(&linuxProcLock);
      VMK_ASSERT(0);   
      return VMK_BUSY;
   }
   
   LinuxProcDetachPDE(pde, parent);
   LinuxProcDeleteEntry(lpe);

   vmk_SPUnlock(&linuxProcLock);

   vmk_ProcEntryDestroy(pe);   
   LinuxProc_FreePDE(pde);
   return VMK_OK;
}

/*
 *-----------------------------------------------------------------------------
 *
 * LinuxProcCreateEntry --
 *
 *      Creates a proc entry "name" in the proc file system  under node 
 *      "parent."  "name" can be a path and "parent" can be NULL.
 *
 * Results:
 *      Local struct describing the entry created. 
 *
 * Side effects:
 *      None. 
 *
 *-----------------------------------------------------------------------------
 */

static LinuxProcEntry*
LinuxProcCreateEntry(const char* name,              // IN: name of proc node
                     struct proc_dir_entry* parent, // IN: parent node
                     vmk_Bool isDir)                // IN: directory or file
{
   struct proc_dir_entry *pde;
   vmk_ProcEntry pe = NULL, ppe = NULL;
   vmk_ProcRead read = NULL;
   vmk_ProcWrite write = NULL;
   vmk_Bool canBlock = VMK_FALSE;
   LinuxProcEntry *lpe;
   struct proc_dir_entry **ptr;
 
   VMKLNX_DEBUG(4, "name=%s, parent=%p, type=%c", 
                   name, parent, isDir ? 'd' : 'f');
 
   vmk_SPLock(&linuxProcLock);

   // Look for the parent (this might be a path)
   // If we can't find it assume proc_root.
   if (parent == NULL) {
      char parentName[MAX_NAME_LEN], *lastSlash;

      strncpy(parentName, name, MAX_NAME_LEN);
      parentName[MAX_NAME_LEN - 1] = 0;

      VMK_ASSERT(parentName[strlen(parentName) - 1] != '/');
      VMK_ASSERT(parentName[0] != '/');

      lastSlash = strrchr(parentName, '/');

      // Detected slash, this must be a path
      if (lastSlash) {
         *lastSlash = 0;
         lpe = LinuxProcFindEntryByPath(parentName, parent);
         if (lpe) {
            parent = lpe->drvEntry;
            name = lastSlash + 1;
         }
      } else {
         parent = &proc_root;
      }
   } else if (strchr(name, '/') != NULL) {
      // A path is passed in as well as a non-NULL direct parent
      VMK_ASSERT(strlen(name) - 1 != '/');
      name = strrchr(name, '/') + 1;
   }

   if (isDir) { // don't create a directory node if it already exists
      for (ptr = &(parent->subdir); *ptr; ptr = &((*ptr)->next)) {
         if (strncmp(name, (*ptr)->name, (*ptr)->namelen) == 0) {
            lpe = LinuxProcFindEntryByPtr(*ptr);
            vmk_SPUnlock(&linuxProcLock);
            VMKLNX_INFO("Directory %s/%s already exists. "
                        "Not creating a new node",
                        parent->name, name);
            return lpe;
         }
      }
   }

   vmk_ProcEntryCreate(&pe, vmklinuxModID, (char *) name, isDir);
   pde = LinuxProc_AllocPDE(name); 

   if (pe == NULL || pde == NULL) {
      VMKLNX_DEBUG(0, "No memory.");
      vmk_SPUnlock(&linuxProcLock);
      goto error;
   }

   /* Set up vmkernel proc entry */
   if (parent == &proc_root) { 
      ppe = linuxRoot;
   } else if (parent == proc_root_driver) {
      ppe = linuxDrvRoot;
   } else if (parent == proc_net) {
      ppe = linuxNet;
   } else if (parent == proc_scsi) {
      ppe = linuxScsi;
   } else if (is_vmvisor && parent == proc_bus) {
      ppe = linuxBus;
   } else {
      // Make sure parent is valid.
      lpe = LinuxProcFindEntryByPtr(parent);
      if (lpe == NULL || parent->deleted) {
         VMKLNX_DEBUG(0, "Couldn't find parent %p for %s", parent, name);
         vmk_SPUnlock(&linuxProcLock);
         goto error;
      }
      ppe = lpe->vmkEntry;
   }
   if (!isDir) {
      read = LinuxProcRead; 
      write = LinuxProcWrite; 
      canBlock = VMK_TRUE;
   }

   // Some device drivers need to block/sleep in /proc code.
   canBlock = VMK_TRUE;

   /* now make vmkapi call to setup proc entry */
   vmk_ProcEntrySetup(pe, ppe, read, write, canBlock, (void *) pde);

   // Set the module id from the modStk, will be used when calling a driver func
   pde->module_id = vmk_ModuleStackTop();
   VMK_ASSERT(pde->module_id != VMK_INVALID_MODULE_ID);

   // Set up a reasonable Linux-style proc entry
   LinuxProcAttachPDE(pde, parent);

   vmk_ProcRegister(pe);
   lpe = LinuxProcAddEntry(name, parent, pde, pe); 

   if (lpe == NULL) {
      vmk_ProcUnRegister(pe);
      LinuxProcDetachPDE(pde, parent);
      vmk_SPUnlock(&linuxProcLock);
      goto error;
   }

   vmk_SPUnlock(&linuxProcLock);

   return lpe; 

error:
   if (pe) {
      vmk_ProcEntryDestroy(pe);
   }
   if (pde) {
      LinuxProc_FreePDE(pde);
   }
   return NULL;
}

/*
 *-----------------------------------------------------------------------------
 *
 *  LinuxProcAddEntry --
 *
 *      Adds an entry to the local list of proc entries. 
 *      linuxProcLock must be held.
 *
 * Results:
 *      Local struct pointer, or NULL. 
 *
 * Side effects:
 *      None. 
 *
 *-----------------------------------------------------------------------------
 */

static LinuxProcEntry *
LinuxProcAddEntry(const char* name,
                  struct proc_dir_entry* parent,
                  struct proc_dir_entry* entry,
                  vmk_ProcEntry vmkEntry)
{
   LinuxProcEntry *ptr;

   VMK_ASSERT_SPLOCK_LOCKED(&linuxProcLock);
   VMK_ASSERT(strchr(name, '/') == NULL);

   ptr = (LinuxProcEntry *)vmk_HeapAlloc(VMK_MODULE_HEAP_ID, sizeof(LinuxProcEntry));
   if (ptr == NULL) {
      VMKLNX_DEBUG(0, "No memory."); 
      return NULL;
   } 
   strncpy(ptr->name, name, MAX_NAME_LEN);  
   ptr->parent = parent;
   ptr->drvEntry = entry;
   ptr->vmkEntry = vmkEntry;
   ptr->next = NULL;

   if (linuxEntries == NULL) {
      linuxEntries = ptr;
   } else {
      ptr->next = linuxEntries;
      linuxEntries = ptr;
   }

   return ptr;
}

/*
 *-----------------------------------------------------------------------------
 *
 * LinuxProcFindEntryByPtr --
 *
 *      Find the entry on the linuxEntries list given the driver entry pointer.
 *      linuxProcLock must be held.
 *
 * Results:
 *      Pointer to entry if found,
 *      NULL otherwise.
 *
 * Side effects:
 *      None. 
 *
 *-----------------------------------------------------------------------------
 */

static LinuxProcEntry *
LinuxProcFindEntryByPtr(struct proc_dir_entry* entry)
{
   LinuxProcEntry *ptr; 

   VMK_ASSERT_SPLOCK_LOCKED(&linuxProcLock);
   for (ptr = linuxEntries; 
        ptr && ptr->drvEntry != entry; 
        ptr = ptr->next) {
   }
   return ptr;
}

/*
 *-----------------------------------------------------------------------------
 *
 * LinuxProcFindEntryByPath --
 *
 *      Find the entry on the linuxEntries list with the given name and
 *      parent.  If parent is NULL, a relative path is passed, start
 *      from proc_root.
 *      linuxProcLock must be held.
 *
 * Results:
 *      Pointer to entry if found,
 *      NULL otherwise.
 *
 * Side effects:
 *      None. 
 *
 *-----------------------------------------------------------------------------
 */

static LinuxProcEntry *
LinuxProcFindEntryByPath(const char* path,
                         struct proc_dir_entry* parent)
{
   LinuxProcEntry *entry;
   char nameBuf[MAX_NAME_LEN];

   VMK_ASSERT(path != NULL);
   VMK_ASSERT_SPLOCK_LOCKED(&linuxProcLock);
   if (parent == NULL) {
      parent = &proc_root;
   }

   do {
      LinuxProcEntry *ptr, *prev; 
      int i;

      VMK_ASSERT(path[strlen(path) - 1] != '/');

      // Find the next element in the path, copy it over to nameBuf
      for (i = 0; path; i++) {
         if (path[i] == '/' || i == strlen(path)) {
            strncpy(nameBuf, path, i);
            nameBuf[i] = 0;
            if (i == strlen(path)) {
               path = NULL;
            }
            break;
         }
      }

      for (ptr = linuxEntries, entry = prev = NULL; ptr; prev = ptr, ptr = ptr->next) {
         if (strncmp(ptr->name, nameBuf, MAX_NAME_LEN) == 0 && ptr->parent == parent) {
            entry = ptr;
            break;
         }
      }

      if (path && entry) {
         path += i + 1;
         parent = entry->drvEntry;
      }
   } while (path && entry);

   return path ? NULL : entry;
}


/*
 *-----------------------------------------------------------------------------
 *
 * LinuxProcDeleteEntry --
 *
 *      Remove an entry from the local list as given by the pointer.
 *      linuxProcLock must be held.
 *
 * Results:
 *      None. 
 *
 * Side effects:
 *      Entry, if found, is removed from the list. 
 *
 *-----------------------------------------------------------------------------
 */

static void
LinuxProcDeleteEntry(LinuxProcEntry *entry)
{
   LinuxProcEntry *ptr, *prev; 

   VMK_ASSERT_SPLOCK_LOCKED(&linuxProcLock);
   for (ptr = linuxEntries, prev = NULL; 
        ptr; 
        prev = ptr, ptr = ptr->next) {
      if (ptr == entry) {
         if (ptr == linuxEntries) {
            VMK_ASSERT(prev == NULL);
            linuxEntries = ptr->next;
         } else {
            prev->next = ptr->next;   
         }
	 vmk_HeapFree(VMK_MODULE_HEAP_ID, ptr);
         return;
      }
   }
   VMKLNX_DEBUG(0, "%p not found.", entry);
}


/*
 *-----------------------------------------------------------------------------
 *
 * LinuxProcRead --
 *
 *      Generic proc read handler. Calls the appropriate (driver) proc read 
 *      handler. 
 *      Read the full contents of the node into "buffer." 
 *      XXX The buffer is assumed to be large enough. 
 *
 * Results:
 *      Result of the lower level proc handler.
 *
 * Side effects:
 *      None. 
 *
 *-----------------------------------------------------------------------------
 */

static int 
LinuxProcRead(vmk_ProcEntry entry, // IN: entry being read
              char *buffer,        // OUT: buffer to read into (i.e. write)
              int *len)            // OUT: how many read
{
   struct proc_dir_entry* pde = (struct proc_dir_entry *) vmk_ProcEntryGetPrivate(entry);
   char *start;
   int32_t count, eof, nread;
   uint32_t off;
  
   vmk_SPLock(&linuxProcLock);
   if (pde == NULL || pde->read_proc == NULL) {
      if (pde == NULL) {
         VMKLNX_WARN("NULL dir ent for node uuid %x", vmk_ProcEntryGetGUID(entry));
      } else {
         VMKLNX_DEBUG(1, "NULL read_proc for node %s", pde->name);
      }
      vmk_SPUnlock(&linuxProcLock);
      return VMK_FAILURE;
   } else if (pde->deleted) {
      VMKLNX_WARN("Reading deleted pde.");
      vmk_SPUnlock(&linuxProcLock);
      return VMK_FAILURE;
   }
   vmk_SPUnlock(&linuxProcLock);

   /*
    * We want to emulate the Linux proc_read function as closely as possible;
    * which is to hand the proc handler a page to scribble every time and let us
    * know finally where the current chunk starts and how long it is. We then 
    * copy the correct chunks into the main proc buffer. We can't use the
    * proc buffer directly due to the possible overwrite on every read.
    */

   eof = 0;
   off = 0;
   // This is the size Linux uses on proc reads. 
   count = PROC_BLOCK_SIZE; 
   nread = 1;
   while ( !eof 
           && nread != 0
           && (off + count) < VMK_PROC_READ_MAX) {
      start = NULL;
      VMKAPI_MODULE_CALL(pde->module_id, nread, pde->read_proc,
                              scratchPage, &start, off, count, &eof, pde->data);
      VMK_ASSERT(nread <= PROC_BLOCK_SIZE);
      if (!start) {
         if (nread <= off) {
            break;
         }
         start = scratchPage + off;
         nread -= off;
      }
      memcpy(buffer + off, start, nread);
      off += nread;
   }

   VMKLNX_DEBUG(5, "read %d bytes", off);
   *len = off;

   return 0;
}

/*
 *-----------------------------------------------------------------------------
 *
 * LinuxProcWrite --
 *
 *      Generic proc write handler. Calls the appropriate (driver) proc write 
 *      handler. 
 *
 * Results:
 *      Result of the lower level proc handler.
 *
 * Side effects:
 *      None. 
 *
 *-----------------------------------------------------------------------------
 */

static int 
LinuxProcWrite(vmk_ProcEntry entry, // IN: entry being written to
               char *buffer,        // IN: buffer containing data
               int *len)            // IN: pointer to length of buffer
{
   struct proc_dir_entry* pde = (struct proc_dir_entry *) vmk_ProcEntryGetPrivate(entry);
   int32_t ret;

   VMKLNX_DEBUG(2, "Start"); 

   vmk_SPLock(&linuxProcLock);
   if (pde == NULL || pde->write_proc == NULL) {
      if (pde == NULL) {
         VMKLNX_WARN("NULL dir ent for node uuid %x", vmk_ProcEntryGetGUID(entry));
      } else {
         VMKLNX_WARN("NULL write_proc for node %s uuid %x", 
                     pde->name, vmk_ProcEntryGetGUID(entry));
      }
      vmk_SPUnlock(&linuxProcLock);
      return VMK_FAILURE;
   } else if (pde->deleted) {
      VMKLNX_WARN("Writing to deleted pde.");
      vmk_SPUnlock(&linuxProcLock);
      return VMK_FAILURE;
   }
   vmk_SPUnlock(&linuxProcLock);

   /*
    * write_proc takes struct file as the first arg.
    * Proc write handlers don't really use it.
    */
   VMKAPI_MODULE_CALL(pde->module_id, ret, pde->write_proc,
              NULL, buffer, *len, pde->data);

   return VMK_OK;
}

