/*
 * sysfs.h - definitions for the device driver filesystem
 *
 * Copyright (c) 2001,2002 Patrick Mochel
 * Copyright (c) 2004 Silicon Graphics, Inc.
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007 Tejun Heo <teheo@suse.de>
 *
 * Please see Documentation/filesystems/sysfs.txt for more information.
 */

#ifndef _SYSFS_H_
#define _SYSFS_H_

#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <asm/atomic.h>

struct kobject;
struct module;

/* FIXME
 * The *owner field is no longer used, but leave around
 * until the tree gets cleaned up fully.
 */
struct attribute {
	const char		*name;
	struct module		*owner;
	mode_t			mode;
};

struct attribute_group {
	const char		*name;
	struct attribute	**attrs;
};



/**
 * Use these macros to make defining attributes easier. See include/linux/device.h
 * for examples..
 */

#define __ATTR(_name,_mode,_show,_store) { \
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show	= _show,					\
	.store	= _store,					\
}

#define __ATTR_RO(_name) { \
	.attr	= { .name = __stringify(_name), .mode = 0444 },	\
	.show	= _name##_show,					\
}

#define __ATTR_NULL { .attr = { .name = NULL } }

#define attr_name(_attr) (_attr).attr.name

struct vm_area_struct;

struct bin_attribute {
	struct attribute	attr;
	size_t			size;
	void			*private;
	ssize_t (*read)(struct kobject *, struct bin_attribute *,
			char *, loff_t, size_t);
	ssize_t (*write)(struct kobject *, struct bin_attribute *,
			 char *, loff_t, size_t);
	int (*mmap)(struct kobject *, struct bin_attribute *attr,
		    struct vm_area_struct *vma);
};

struct sysfs_ops {
	ssize_t	(*show)(struct kobject *, struct attribute *,char *);
	ssize_t	(*store)(struct kobject *,struct attribute *,const char *, size_t);
};

#ifdef CONFIG_SYSFS

int sysfs_schedule_callback(struct kobject *kobj, void (*func)(void *),
			    void *data, struct module *owner);

int __must_check sysfs_create_dir(struct kobject *kobj);
void sysfs_remove_dir(struct kobject *kobj);
int __must_check sysfs_rename_dir(struct kobject *kobj, const char *new_name);
int __must_check sysfs_move_dir(struct kobject *kobj,
				struct kobject *new_parent_kobj);

int __must_check sysfs_create_file(struct kobject *kobj,
				   const struct attribute *attr);
int __must_check sysfs_chmod_file(struct kobject *kobj, struct attribute *attr,
				  mode_t mode);
void sysfs_remove_file(struct kobject *kobj, const struct attribute *attr);

int __must_check sysfs_create_bin_file(struct kobject *kobj,
				       struct bin_attribute *attr);
void sysfs_remove_bin_file(struct kobject *kobj, struct bin_attribute *attr);

int __must_check sysfs_create_link(struct kobject *kobj, struct kobject *target,
				   const char *name);
void sysfs_remove_link(struct kobject *kobj, const char *name);

int __must_check sysfs_create_group(struct kobject *kobj,
				    const struct attribute_group *grp);
void sysfs_remove_group(struct kobject *kobj,
			const struct attribute_group *grp);
int sysfs_add_file_to_group(struct kobject *kobj,
			const struct attribute *attr, const char *group);
void sysfs_remove_file_from_group(struct kobject *kobj,
			const struct attribute *attr, const char *group);

void sysfs_notify(struct kobject *kobj, char *dir, char *attr);

extern int __must_check sysfs_init(void);

#else /* CONFIG_SYSFS */

static inline int sysfs_schedule_callback(struct kobject *kobj,
		void (*func)(void *), void *data, struct module *owner)
{
	return -ENOSYS;
}

static inline int sysfs_create_dir(struct kobject *kobj)
{
	return 0;
}

static inline void sysfs_remove_dir(struct kobject *kobj)
{
	;
}

static inline int sysfs_rename_dir(struct kobject *kobj, const char *new_name)
{
	return 0;
}

static inline int sysfs_move_dir(struct kobject *kobj,
				 struct kobject *new_parent_kobj)
{
	return 0;
}

/**                                          
 *  sysfs_create_file - non-operational function
 *  @kobj: pointer to kobject to associate with attribute
 *  @attr: pointer to attribute structure
 *                                           
 *  This is a non-operational function provided to help reduce kernel ifdefs. It is not supported in this release of ESX. 
 *
 *  return value: zero
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: sysfs_create_file */
static inline int sysfs_create_file(struct kobject *kobj,
				    const struct attribute *attr)
{
	return 0;
}

static inline int sysfs_chmod_file(struct kobject *kobj,
				   struct attribute *attr, mode_t mode)
{
	return 0;
}

/**                                          
 *  sysfs_remove_file - non-operational function
 *  @kobj: pointer to kobject from which to remove attribute
 *  @attr: pointer to attribute structure
 *                                           
 *  This is a non-operational function provided to help reduce kernel ifdefs. It is not supported in this release of ESX. 
 *
 *  return value: none
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: sysfs_remove_file */
static inline void sysfs_remove_file(struct kobject *kobj,
				     const struct attribute *attr)
{
	;
}

/**                                          
 *  sysfs_create_bin_file - non-operational function
 *  @kobj: pointer to kobject to associate with attribute
 *  @attr: pointer to attribute structure
 *                                           
 *  This is a non-operational function provided to help reduce kernel ifdefs. It is not supported in this release of ESX. 
 *
 *  return value: zero
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: sysfs_create_bin_file */
static inline int sysfs_create_bin_file(struct kobject *kobj,
					struct bin_attribute *attr)
{
	return 0;
}

/**                                          
 *  sysfs_remove_bin_file - non-operational function   
 *  @kobj: pointer to kobject from which to remove attribute
 *  @attr: pointer to attribute structure to remove
 *                                           
 *  This is a non-operational function provided to help reduce kernel ifdefs. It is not supported in this release of ESX. 
 *
 *  return value: zero
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: sysfs_remove_bin_file */
static inline int sysfs_remove_bin_file(struct kobject *kobj,
					struct bin_attribute *attr)
{
	return 0;
}

/**
 *  sysfs_create_link - non-operational function
 *  @kobj: Ignored
 *  @target: Ignored
 *  @name: Ignored
 *
 *  A non-operational function provided to help reduce
 *  kernel ifdefs. It is not supported in this release of ESX.
 *
 *  ESX Deviation Notes:
 *  A non-operational function provided to help reduce
 *  kernel ifdefs. It is not supported in this release of ESX.
 *
 *  RETURN VALUE:
 *  zero
 *
 */
/* _VMKLNX_CODECHECK_: sysfs_create_link */
static inline int sysfs_create_link(struct kobject *kobj,
				    struct kobject *target, const char *name)
{
	return 0;
}

static inline void sysfs_remove_link(struct kobject *kobj, const char *name)
{
	;
}

/**
 *  sysfs_create_group - non-operational function
 *  @kobj: Ignored
 *  @grp: Ignored
 *
 *  A non-operational function provided to help reduce
 *  kernel ifdefs. It is not supported in this release of ESX.
 *
 *  ESX Deviation Notes:
 *  A non-operational function provided to help reduce
 *  kernel ifdefs. It is not supported in this release of ESX.
 *
 *  RETURN VALUE:
 *  zero
 *
 */
/* _VMKLNX_CODECHECK_: sysfs_create_group */
static inline int sysfs_create_group(struct kobject *kobj,
				     const struct attribute_group *grp)
{
	return 0;
}

static inline void sysfs_remove_group(struct kobject *kobj,
				      const struct attribute_group *grp)
{
	;
}

static inline int sysfs_add_file_to_group(struct kobject *kobj,
		const struct attribute *attr, const char *group)
{
	return 0;
}

static inline void sysfs_remove_file_from_group(struct kobject *kobj,
		const struct attribute *attr, const char *group)
{
}

static inline void sysfs_notify(struct kobject *kobj, char *dir, char *attr)
{
}

static inline int __must_check sysfs_init(void)
{
	return 0;
}

#endif /* CONFIG_SYSFS */

#endif /* _SYSFS_H_ */
