/*
 * Copyright (C) 2003 - 2007 NetXen, Inc.
 * All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA.
 * 
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.
 * 
 * Contact Information:
 *    licensing@netxen.com
 * NetXen, Inc.
 * 3965 Freedom Circle, Fourth floor,
 * Santa Clara, CA 95054
 */
/*
 * Source file for NIC routines to initialize the Phantom Hardware
 *
 * $Id: //depot/vmkdrivers/esx40/src26/drivers/net/nx_nic/unm_nic_init.c#5 $
 *
 */
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include "queue.h"
#include "unm_nic.h"
#include "unm_nic_hw.h"
#include "nic_cmn.h"
#include "nic_phan_reg.h"
#include "unm_nic_ioctl.h"
#include "nic_phan_reg.h"
#include "unm_version.h"
#include "unm_brdcfg.h"
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,28)
#include <linux/firmware.h>
#endif
#ifdef NX_FUSED_FW
#include "p2_nx_fw_hdr.h"
#include "p3_cut_thru_nx_fw_hdr.h"
#endif

struct crb_addr_pair {
        long addr;
        long data;
};

#define MAX_CRB_XFORM 60
static unsigned int crb_addr_xform[MAX_CRB_XFORM];
#define ADDR_ERROR ((unsigned long ) 0xffffffff )
int crb_table_initialized=0;

#define crb_addr_transform(name) \
        crb_addr_xform[UNM_HW_PX_MAP_CRB_##name] = \
        UNM_HW_CRB_HUB_AGT_ADR_##name << 20
static void
crb_addr_transform_setup(void)
{
        crb_addr_transform(XDMA);
        crb_addr_transform(TIMR);
        crb_addr_transform(SRE);
        crb_addr_transform(SQN3);
        crb_addr_transform(SQN2);
        crb_addr_transform(SQN1);
        crb_addr_transform(SQN0);
        crb_addr_transform(SQS3);
        crb_addr_transform(SQS2);
        crb_addr_transform(SQS1);
        crb_addr_transform(SQS0);
        crb_addr_transform(RPMX7);
        crb_addr_transform(RPMX6);
        crb_addr_transform(RPMX5);
        crb_addr_transform(RPMX4);
        crb_addr_transform(RPMX3);
        crb_addr_transform(RPMX2);
        crb_addr_transform(RPMX1);
        crb_addr_transform(RPMX0);
        crb_addr_transform(ROMUSB);
        crb_addr_transform(SN);
        crb_addr_transform(QMN);
        crb_addr_transform(QMS);
        crb_addr_transform(PGNI);
        crb_addr_transform(PGND);
        crb_addr_transform(PGN3);
        crb_addr_transform(PGN2);
        crb_addr_transform(PGN1);
        crb_addr_transform(PGN0);
        crb_addr_transform(PGSI);
        crb_addr_transform(PGSD);
        crb_addr_transform(PGS3);
        crb_addr_transform(PGS2);
        crb_addr_transform(PGS1);
        crb_addr_transform(PGS0);
        crb_addr_transform(PS);
        crb_addr_transform(PH);
        crb_addr_transform(NIU);
        crb_addr_transform(I2Q);
        crb_addr_transform(EG);
        crb_addr_transform(MN);
        crb_addr_transform(MS);
        crb_addr_transform(CAS2);
        crb_addr_transform(CAS1);
        crb_addr_transform(CAS0);
        crb_addr_transform(CAM);
        crb_addr_transform(C2C1);
        crb_addr_transform(C2C0);
        crb_addr_transform(SMB);
        crb_addr_transform(OCM0);
	/*
	 * Used only in P3 just define it for P2 also.
	 */
	crb_addr_transform(I2C0);

    	crb_table_initialized = 1;
}

/*
 * decode_crb_addr(0 - utility to translate from internal Phantom CRB address
 * to external PCI CRB address.
 */
unsigned long
decode_crb_addr (unsigned long addr)
{
        int i;
        unsigned long base_addr, offset, pci_base;

	if (!crb_table_initialized)
        	crb_addr_transform_setup();

        pci_base = ADDR_ERROR;
        base_addr = addr & 0xfff00000;
        offset = addr & 0x000fffff;

        for (i=0; i< MAX_CRB_XFORM; i++) {
                if (crb_addr_xform[i] == base_addr) {
                        pci_base = i << 20;
                        break;
                }
        }
        if (pci_base == ADDR_ERROR) {
                return pci_base;
        } else {
                return (pci_base + offset);
        }
}

unsigned long
crb_pci_to_internal(unsigned long addr)
{
	if (!crb_table_initialized)
	    	crb_addr_transform_setup();
	return (crb_addr_xform[((addr >> 20) & 0x3f)] + (addr & 0xfffff));
}

static long rom_max_timeout= 100;
static long rom_lock_timeout= 10000;

int
rom_lock(unm_adapter *adapter)
{
    int i;
    int done = 0, timeout = 0;

    while (!done) {
        /* acquire semaphore2 from PCI HW block */
        unm_nic_read_w0(adapter, UNM_PCIE_REG(PCIE_SEM2_LOCK), &done);
        if (done == 1)
            break;
        if (timeout >= rom_lock_timeout) {
            return -1;
        }
        timeout++;
        /*
         * Yield CPU
         */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
        if(!in_atomic())
                schedule();
        else {
#endif
                for(i = 0; i < 20; i++)
                        cpu_relax();    /*This a nop instr on i386*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
        }
#endif
    }
    unm_nic_reg_write(adapter, UNM_ROM_LOCK_ID, ROM_LOCK_DRIVER);
    return 0;
}

int
rom_unlock(unm_adapter *adapter)
{
    int val;

    /* release semaphore2 */
    unm_nic_read_w0(adapter, UNM_PCIE_REG(PCIE_SEM2_UNLOCK), &val);

    return 0;
}

int
wait_rom_done (unm_adapter *adapter)
{
        long timeout=0;
        long done=0 ;

        /*DPRINTK(1,INFO,"WAIT ROM DONE \n");*/
        /*printk(KERN_INFO "WAIT ROM DONE \n");*/

        while (done == 0) {
                done = unm_nic_reg_read(adapter, UNM_ROMUSB_GLB_STATUS);
                done &=2;
                timeout++;
                if (timeout >= rom_max_timeout) {
                        printk( "%s: Timeout reached  waiting for rom done",
					unm_nic_driver_name);
                        return -1;
                }
        }
        return 0;
}

int
do_rom_fast_read (unm_adapter *adapter, int addr, int *valp)
{
        /*DPRINTK(1,INFO,"ROM FAST READ \n");*/
        unm_nic_reg_write(adapter, UNM_ROMUSB_ROM_ADDRESS, addr);
        unm_nic_reg_write(adapter, UNM_ROMUSB_ROM_ABYTE_CNT, 3);
        udelay(100);   /* prevent bursting on CRB */
        unm_nic_reg_write(adapter, UNM_ROMUSB_ROM_DUMMY_BYTE_CNT, 0);
        unm_nic_reg_write(adapter, UNM_ROMUSB_ROM_INSTR_OPCODE, 0xb);
        if (wait_rom_done(adapter)) {
                printk("%s: Error waiting for rom done\n",unm_nic_driver_name);
                return -1;
        }
        //reset abyte_cnt and dummy_byte_cnt
        unm_nic_reg_write(adapter, UNM_ROMUSB_ROM_ABYTE_CNT, 0);
        udelay(100);   /* prevent bursting on CRB */
        unm_nic_reg_write(adapter, UNM_ROMUSB_ROM_DUMMY_BYTE_CNT, 0);

        *valp = unm_nic_reg_read(adapter, UNM_ROMUSB_ROM_RDATA);
        return 0;
}

int
rom_fast_read (struct unm_adapter_s *adapter, int addr, int *valp)
{
    int ret, loops = 0;

    while ((rom_lock(adapter) != 0) && (loops < 50000)) {
        udelay(100);
        schedule();
        loops++;
    }
    if (loops >= 50000) {
        printk("%s: rom_lock failed\n",unm_nic_driver_name);
        return -1;
    }
    ret = do_rom_fast_read(adapter, addr, valp);
    rom_unlock(adapter);
    return ret;
}

/* Error codes */
#define FPGA_NO_ERROR 0
#define FPGA_FILE_ERROR 1
#define FPGA_ILLEGAL_PARAMETER 2
#define FPGA_MEMORY_ERROR 3

/* macros */
#ifdef VIRTEX
#define RESET_STATE  changeState(adapter,1,32)
#else
#define RESET_STATE  changeState(adapter,1,5)
#endif

#define GO_TO_RTI RESET_STATE;\
                  changeState(adapter,0,1)

/*
 *  TCLK = GPIO0 = 0x60
 *  TRST = GPIO1 = 0x64
 *  TMS  = GPIO4 = 0x70
 *  TDO  = GPIO5 = 0xc
 *  TDI  = GPIO8 = 0x80
 *
 */
#define TCLK (UNM_CRB_ROMUSB + 0x60)
#define TRST (UNM_CRB_ROMUSB + 0x64)
#define TMS  (UNM_CRB_ROMUSB + 0x70)
#define TDI  (UNM_CRB_ROMUSB + 0x80)
#define TDO  (UNM_CRB_ROMUSB + 0xc)

#define TDO_SHIFT 5

#define ASSERT_TRST   \
        do { \
                unm_nic_reg_write(adapter, TRST, 2);\
                unm_nic_reg_write(adapter, TMS, 2); \
                unm_nic_reg_write(adapter, TDI, 2); \
                unm_nic_reg_write(adapter, TCLK, 2);\
                unm_nic_reg_write(adapter, TRST, 3);\
        } while (0)

#define CLOCK_IN_BIT(tdi,tms)  \
        do { \
                unm_nic_reg_write(adapter, TRST, 3); \
                unm_nic_reg_write(adapter, TMS, 2 | (tms)); \
                unm_nic_reg_write(adapter, TDI, 2 | (tdi)); \
                unm_nic_reg_write(adapter, TCLK, 2); \
                unm_nic_reg_write(adapter, TCLK, 3); \
                unm_nic_reg_write(adapter, TCLK, 2);\
        } while (0)

#define CLOCK_OUT_BIT(bit,tms) \
        do { \
                unm_nic_reg_write(adapter, TRST, 3); \
                unm_nic_reg_write(adapter, TMS, 2 | (tms)); \
                unm_nic_reg_write(adapter, TDI, 2); \
                unm_nic_reg_write(adapter, TCLK, 2); \
                unm_nic_reg_write(adapter, TCLK, 3); \
                unm_nic_reg_write(adapter, TCLK, 2); \
                (bit) = (unm_nic_reg_read(adapter, TDO) >> TDO_SHIFT) & 1; \
        } while (0)

/* boundary scan instructions */
#define CMD_EXTEST    0x0
#define CMD_CAPTURE   0x1
#define CMD_IDCODE    0x2
#define CMD_SAMPLE    0x3

//Memory BIST
#define CMD_MBSEL     0x4
#define CMD_MBRES     0x5

//Logic BIST
#define CMD_LBSEL     0x6
#define CMD_LBRES     0x7
#define CMD_LBRUN    0x18

//Memory Interface
#define CMD_MEM_WDAT  0x8
#define CMD_MEM_ACTL  0x9
#define CMD_MEM_READ  0xa

//Memory Interface
#define CMD_CRB_WDAT  0xb
#define CMD_CRB_ACTL  0xc
#define CMD_CRB_READ  0xd

#define CMD_TRISTATE  0xe
#define CMD_CLAMP     0xf

#define CMD_STATUS    0x10
#define CMD_XG_SCAN   0x11
#define CMD_BYPASS    0x1f

#ifdef VIRTEX
#define CMD_LENGTH_BITS 6
#else
#define CMD_LENGTH_BITS 5
#endif


/* This is the TDI bit that will be clocked out for don't care value */
#define TDI_DONT_CARE 0

#define TMS_LOW  0
#define TMS_HIGH 1

#define ID_REGISTER_SIZE_BITS 32

#define EXIT_IR 1
#define NO_EXIT_IR 0

#define TAP_DELAY()

__inline__ void
changeState(unm_adapter *adapter,int tms,int tck)
{
        int i;
        DPRINTK(1, INFO, "changing state tms: %d tck: %d\n", tms, tck);
        for (i = 0; i< tck; i++)
        {
                CLOCK_IN_BIT(TDI_DONT_CARE,tms);
        }
}

// Assumes we are in RTI, will return to RTI
__inline__ int
loadInstr(unm_adapter *adapter,int instr,int exit_ir)
{
        int i,j;

        DPRINTK(1, INFO, "in loaderinstr instr: %d exit_ir %d\n", instr,
                                exit_ir);

        // go to Select-IR
        changeState(adapter,1,2);

        // go to Shift-IR
        changeState(adapter,0,2);

#ifdef VIRTEX
        for (i = 0; i< (CMD_LENGTH_BITS * 7); i++)
        {
                CLOCK_IN_BIT(1,TMS_LOW);
        }
#endif
        for (i = 0; i< (CMD_LENGTH_BITS-1); i++)
        {
                j= (instr>>i) & 1;
                CLOCK_IN_BIT(j,TMS_LOW);
        }

        /* clock out last bit, and transition to next state */
        j= (instr >> (CMD_LENGTH_BITS-1)) & 1;
        // last bit, exit into Exit1-IR
        CLOCK_IN_BIT(j,1);
        // go to Update-IR
        changeState(adapter,1,1);
        // go to RTI
        changeState(adapter,0,1);
        return FPGA_NO_ERROR;
}

int
getData(unm_adapter *adapter,u32 *data,int len, int more)
{
        u32 temp=0;
        int i, bit;
        DPRINTK(1, INFO, "doing getData data: %p len: %d more: %d\n",
                                data, len,more);
        // go to Select-DR-Scan
        changeState(adapter,1,1);
        // go to shift-DR
        changeState(adapter,0,1);
#ifdef VIRTEX
        // dummy reads
        for (i=0; i< 6; i++) {
                CLOCK_OUT_BIT(bit,0);
        }
#endif
        for (i=0; i< (len); i++) {
                CLOCK_OUT_BIT(bit,0);
                temp |= (bit << i);
        }
        if (!more) {
                // go to Exit1-DR
                changeState(adapter,1,1);
                // go to Update DR
                changeState(adapter,1,1);
                // go to RTI
                changeState(adapter,0,1);
        }

        *data = temp;

        return 0;
}


int
get_status(unm_adapter *adapter)
{
        int status;

        DPRINTK(1, INFO, "doing get_status: %p\n", adapter);
        // tap_inst_wr(INST_STATUS)
        loadInstr(adapter,CMD_STATUS,EXIT_IR);
        getData(adapter,&status,1,0);
        //printf("Status: 0x%02x\n",status);
        return status;
}
// assumes start in RTI, will return to RTI unless more is set
int
getData64(unm_adapter *adapter,u64 *data,int len, int more)
{
        u64 temp=0;
        u64 i, bit;
        DPRINTK(1, INFO, "getData64 data %p, len %d more %d\n",
                                data, len, more);
        // go to Select-DR-Scan
        changeState(adapter,1,1);
        // go to shift-DR
        changeState(adapter,0,1);
#ifdef VIRTEX
        // dummy reads
        for (i=0; i< 6; i++) {
                CLOCK_OUT_BIT(bit,0);
        }
#endif
        for (i=0; i< (len); i++) {
                CLOCK_OUT_BIT(bit,0);
                temp |= (bit << i);
        }

        if (!more) {
                // go to Exit1-DR
                changeState(adapter,1,1);
                // go to Update DR
                changeState(adapter,1,1);
                // go to RTI
                changeState(adapter,0,1);
        }

        //temp |= (bit << (len - 1));
        //printf("Data read: 0x%016llx\n",temp);
        *data = temp;
        return 0;
}

// assumes start in shift_DR, will return to RTI unless more is set
int
loadData (unm_adapter *adapter,u64 data, int len, int more)
{
        int i, bit;

        DPRINTK(1, INFO, "loading data %llx data %d more %d\n",
                data, len, more);
        for (i=0; i< (len-1); i++) {
                bit = (data >> i) & 1;
                CLOCK_IN_BIT(bit,0);
        }
        if (more) {
                bit = (data >> (len-1)) & 1;
                CLOCK_IN_BIT(bit,0);
        } else {
                bit = (data>>(len - 1)) & 1;
                // last data, go to Exit1-DR
                CLOCK_IN_BIT(bit,1);
                // go to Update DR
                changeState(adapter,1,1);
                // go to RTI
                changeState(adapter,0,1);
        }

        return 0;
}

#define CRB_REG_EX_PC                   0x3c

void nx_msleep(unsigned long msecs)
{
        set_current_state(TASK_UNINTERRUPTIBLE);
        schedule_timeout((HZ * msecs + 999) / 1000);
}

int pinit_from_rom(unm_adapter *adapter, int verbose)
{
        int addr, val,status;
        int i ;
        int init_delay=0;
        struct crb_addr_pair *buf;
        unsigned long off;
        unsigned offset, n;

        struct crb_addr_pair {
                long addr;
                long data;
        };

        // resetall
        status = unm_nic_get_board_info(adapter);
        if (status)
                printk ("%s: pinit_from_rom: Error getting board info\n",
				unm_nic_driver_name);

        UNM_CRB_WRITELIT_ADAPTER(UNM_ROMUSB_GLB_SW_RESET, 0xffffffff, adapter);

        if (verbose) {
                int val;
                if (rom_fast_read(adapter, 0x4008, &val) == 0)
                        printk("ROM board type: 0x%08x\n", val);
                else
                        printk("Could not read board type\n");
                if (rom_fast_read(adapter, 0x400c, &val) == 0)
                        printk("ROM board  num: 0x%08x\n", val);
                else
                        printk("Could not read board number\n");
                if (rom_fast_read(adapter, 0x4010, &val) == 0)
                        printk("ROM chip   num: 0x%08x\n", val);
                else
                        printk("Could not read chip number\n");
        }

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		if (rom_fast_read(adapter, 0, &n) != 0 || n != 0xcafecafeUL ||
		    rom_fast_read(adapter, 4, &n) != 0) {
			printk("NX_NIC: ERROR Reading crb_init area: "
			       "n: %08x\n", n);
			return -1;
		}

		offset = n & 0xffffU;
		n = (n >> 16) & 0xffffU;
	} else {
		if (rom_fast_read(adapter, 0, &n) == 0 &&
		    (n & 0x800000000ULL)) {
			printk("NX_NIC: ERROR Reading crb_init area: "
			       "n: %08x\n", n);
			return -1;
		}
		offset = 1;
		n &= ~0x80000000U;
	}

        if (n  >= 1024) {
                printk("%s: %s:n=0x%x Error! UNM card flash not initialized.\n",
                        unm_nic_driver_name,__FUNCTION__, n);
                return -1;
        }
        if (verbose)
                printk("%s: %d CRB init values found in ROM.\n",
                        unm_nic_driver_name, n);

        buf = kmalloc(n*sizeof(struct crb_addr_pair), GFP_KERNEL);
        if (buf==NULL) {
                printk("%s: pinit_from_rom: Unable to malloc memory.\n",
                        unm_nic_driver_name);
                return -1;
        }
        for (i=0; i< n; i++) {
                if (rom_fast_read(adapter, 8*i + 4*offset, &val) != 0 ||
                    rom_fast_read(adapter, 8*i + 4*offset + 4, &addr) != 0) {
                        kfree(buf);
                        return -1;
                }

                buf[i].addr=addr;
                buf[i].data=val;

                if (verbose)
                        printk("%s: PCI:     0x%08x == 0x%08x\n",
                               unm_nic_driver_name,
                               (unsigned int)decode_crb_addr(
                                        (unsigned long)addr), val);
        }

        for (i=0; i< n; i++) {
                off = decode_crb_addr((unsigned long)buf[i].addr) +
                        UNM_PCI_CRBSPACE;
                /* skipping cold reboot MAGIC */
                if (off == UNM_CAM_RAM(0x1fc)) {
                        continue;
                }

		if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
			/* do not reset PCI */
			if (off == (ROMUSB_GLB + 0xbc)) {
				continue;
			}
			if (off == (UNM_CRB_PEG_NET_1 + 0x18)) {
				buf[i].data = 0x1020;
			}
			/* skip the function enable register */
			if (off == UNM_PCIE_REG(PCIE_SETUP_FUNCTION)) {
				continue;
			}
			if (off == UNM_PCIE_REG(PCIE_SETUP_FUNCTION2)) {
				continue;
			}
                        if ((off & 0x0ff00000) == UNM_CRB_SMB) {
                                continue;
                        }
#if 1
			if ((off & 0x0ff00000) == UNM_CRB_DDR_NET) {
				continue;
			}
#endif
		}

                if (off == ADDR_ERROR) {
                        printk("%s: Err: Unknown addr: 0x%08lx\n",
                               unm_nic_driver_name, buf[i].addr);
                        continue;
                }

                /* After writing this register, HW needs time for CRB */
                /* to quiet down (else crb_window returns 0xffffffff) */
                if (off == UNM_ROMUSB_GLB_SW_RESET) {
                        init_delay=1;

			if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
				/* hold xdma in reset also */
				buf[i].data = 0x8000ff;
			}
                }

		if (ADDR_IN_WINDOW1(off)) {
//                read_lock(&adapter->adapter_lock);
			adapter->unm_nic_hw_write_wx(adapter, off, &buf[i].data, 4);
			// note: tried write_normalize call with read_locks, but it crashes
//                adapter->unm_nic_pci_write_normalize(adapter, off, &buf[i].data);
//                read_unlock(&adapter->adapter_lock);
                } else {
//                        write_lock_irqsave(&adapter->adapter_lock, flags);
                        //_unm_nic_write_crb(adapter, off, buf[i].data);
			adapter->unm_nic_hw_write_wx(adapter, off, &buf[i].data, 4);
 //                       unm_nic_pci_change_crbwindow(adapter, 1);
//                        write_unlock_irqrestore(&adapter->adapter_lock, flags);
                }
                if (init_delay==1) {
			nx_msleep(1000);
			init_delay=0;
                }

                nx_msleep(1);
        }
        kfree(buf);

        // disable_peg_cache_all
        // unreset_net_cache
	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		val = UNM_CRB_READ_VAL_ADAPTER(UNM_ROMUSB_GLB_SW_RESET,
					       adapter);
		UNM_CRB_WRITELIT_ADAPTER(UNM_ROMUSB_GLB_SW_RESET,
					 (val & 0xffffff0f), adapter);
	}

        // p2dn replyCount
        UNM_CRB_WRITELIT_ADAPTER(UNM_CRB_PEG_NET_D+0xec, 0x1e, adapter);
        // disable_peg_cache 0
        UNM_CRB_WRITELIT_ADAPTER(UNM_CRB_PEG_NET_D+0x4c,8, adapter);
        // disable_peg_cache 1
        UNM_CRB_WRITELIT_ADAPTER(UNM_CRB_PEG_NET_I+0x4c,8, adapter);

        // peg_clr_all
        // peg_clr 0
        UNM_CRB_WRITELIT_ADAPTER(UNM_CRB_PEG_NET_0+0x8,0, adapter);
        UNM_CRB_WRITELIT_ADAPTER(UNM_CRB_PEG_NET_0+0xc,0, adapter);
        // peg_clr 1
        UNM_CRB_WRITELIT_ADAPTER(UNM_CRB_PEG_NET_1+0x8,0, adapter);
        UNM_CRB_WRITELIT_ADAPTER(UNM_CRB_PEG_NET_1+0xc,0, adapter);
        // peg_clr 2
        UNM_CRB_WRITELIT_ADAPTER(UNM_CRB_PEG_NET_2+0x8,0, adapter);
        UNM_CRB_WRITELIT_ADAPTER(UNM_CRB_PEG_NET_2+0xc,0, adapter);
        // peg_clr 3
        UNM_CRB_WRITELIT_ADAPTER(UNM_CRB_PEG_NET_3+0x8,0, adapter);
        UNM_CRB_WRITELIT_ADAPTER(UNM_CRB_PEG_NET_3+0xc,0, adapter);

        return 0;
}

#define CACHE_DISABLE 1
#define CACHE_ENABLE  0

int nx_phantom_init(struct unm_adapter_s *adapter, int pegtune_val)
{
	u32 val = 0;
	int retries = 60;

	if (!pegtune_val) {
		do {
			val = adapter->unm_nic_pci_read_normalize(adapter,
					CRB_CMDPEG_STATE);

			if ((val == PHAN_INITIALIZE_COMPLETE) ||
			    (val == PHAN_INITIALIZE_ACK))
				return 0;

			msleep(500);

		} while (--retries);

		if (!retries) {
			pegtune_val = adapter->unm_nic_pci_read_normalize(adapter,
					UNM_ROMUSB_GLB_PEGTUNE_DONE);
			printk(KERN_WARNING "%s: init failed, "
				"pegtune_val = %x\n", __FUNCTION__, pegtune_val);
			return -1;
		}
	}

	return 0;
}

int
load_from_flash(struct unm_adapter_s *adapter)
{
        int  i;
        long data, size = 0;
        long flashaddr = BOOTLD_START, memaddr = BOOTLD_START;

        size = (IMAGE_START - BOOTLD_START)/4;

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		read_lock(&adapter->adapter_lock);
		UNM_NIC_PCI_WRITE_32(1, CRB_NORMALIZE(adapter,
						      UNM_ROMUSB_GLB_CAS_RST));
		read_unlock(&adapter->adapter_lock);
	}

        for (i = 0; i < size; i++) {
                if (rom_fast_read(adapter, flashaddr, (int *)&data) != 0) {
                        DPRINTK(1,ERR, "Error in rom_fast_read(). Will skip"
                                "loading flash image\n");
                        return -1;
                }
                adapter->unm_nic_pci_mem_write(adapter, memaddr, &data, 4);
                flashaddr += 4;
                memaddr   += 4;
		if(i%0x1000==0){
			nx_msleep(1);
		}
        }
       udelay(100);
       read_lock(&adapter->adapter_lock);

       if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
            data = 0x80001d;
            adapter->unm_nic_hw_write_wx(adapter, UNM_ROMUSB_GLB_SW_RESET, &data, 4);
       } else {
	       UNM_NIC_PCI_WRITE_32(0x3fff, CRB_NORMALIZE(adapter,
						UNM_ROMUSB_GLB_CHIP_CLK_CTRL));
	       UNM_NIC_PCI_WRITE_32(0, CRB_NORMALIZE(adapter,
						UNM_ROMUSB_GLB_CAS_RST));
       }

       read_unlock(&adapter->adapter_lock);
       return 0;

}

#ifdef NX_FUSED_FW
int
load_fused_fw(struct unm_adapter_s *adapter, int cmp_versions)
{
        int  i, size=0;
        long memaddr = BOOTLD_START, imgaddr=IMAGE_START;
        long data;
        unsigned int temp = 0;
        u32 first_boot;
        unsigned int *bootld = NULL, *fw_img = NULL;
        int fw_size = 0, file_firmware_incompat = 0;

        unsigned int file_major=0, file_minor=0, file_sub=0;
        unsigned int flash_major=0, flash_minor=0, flash_sub=0;
        u32 file_ver, flash_ver;


        if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
                file_ver = P3_CUT_THRU_FUSED_FW_VER;
                bootld = p3_cut_thru_bootld;
                fw_img = p3_cut_thru_fw_img;
                fw_size = sizeof(p3_cut_thru_fw_img);
        } else {
                file_ver = P2_FUSED_FW_VER;
                bootld = p2_bootld;
                fw_img = p2_fw_img;
                fw_size = sizeof(p2_fw_img);
        }

        file_major = file_ver & 0xff;
        file_minor = (file_ver >> 8) & 0xff;
        file_sub   = (file_ver >> 16) &0xffff;

        nx_nic_print4(NULL, "The file version = %d.%d.%d\n", file_major, file_minor, file_sub);

        if((file_major != _UNM_NIC_LINUX_MAJOR) ||
                       (file_minor != _UNM_NIC_LINUX_MINOR)){
                if(cmp_versions == LOAD_FW_NO_COMPARISON) { //If loading unconditionally from file, then don't fall back to flash firmware.
                        printk(KERN_ALERT "%s: File firmware and driver versions are incompatible\n",
                                unm_nic_driver_name);
                        return -1;
                }
                file_firmware_incompat = 1;
        }

        if(cmp_versions == LOAD_FW_WITH_COMPARISON) {
                // Read the romimage/phantom_obj version from flash.
                if (rom_fast_read(adapter, FW_VERSION_OFFSET, (int *)&flash_ver) != 0) {
                        printk("%s: Error in reading firmware version"
                                        " from flash\n",unm_nic_driver_name);
                        if(!file_firmware_incompat) {
                                goto LOAD_FROM_FILE;
                        } else {
                                printk("%s: Error in reading flash firmware version and file firmware is "
                                                "incompatible with driver, so exiting\n", unm_nic_driver_name);
                                return -1;
                        }
                }

                flash_ver = cpu_to_le32(flash_ver);

                flash_major = flash_ver & 0xff;
                flash_minor = (flash_ver >> 8) & 0xff;
                flash_sub   = flash_ver >> 16;

                if((flash_major != _UNM_NIC_LINUX_MAJOR) ||
                                (flash_minor != _UNM_NIC_LINUX_MINOR)){
                        if(!file_firmware_incompat) {
                                goto LOAD_FROM_FILE;
                        } else {
                                printk("%s: Both file firmware and flash firmware are incompatible with driver, "
                                                "so exiting\n", unm_nic_driver_name);
                                return -1;
                        }
                }

                nx_nic_print4(NULL, "The flash version = %d.%d.%d\n", flash_major, flash_minor, flash_sub);

                /* Code for comparison goes here. */

                if(file_firmware_incompat) { //No comparison can be done.
                        goto LOAD_FROM_FLASH;
                } else if (file_major < flash_major ||
                                (file_major == flash_major &&
                                 file_minor < flash_minor) ||
                                (file_major == flash_major &&
                                 file_minor == flash_minor &&
                                 file_sub < flash_sub)) { //Flash firmware is latest, so load from flash.
LOAD_FROM_FLASH:
                        first_boot = adapter->unm_nic_pci_read_normalize(adapter,
                                                 UNM_CAM_RAM(0x1fc));
                        if (first_boot != 0x55555555) {
                                pinit_from_rom(adapter, 0);
                                udelay(500);
                                nx_nic_print4(NULL, "Loading the firmware from flash\n");
                                load_from_flash(adapter);
                        }
                } else { //File firmware is latest.
                        goto LOAD_FROM_FILE;
                }
        } else { //Load from file.
LOAD_FROM_FILE:
                temp = 0;
                adapter->unm_nic_hw_write_wx(adapter, CRB_CMDPEG_STATE, &temp, 4);
                pinit_from_rom(adapter,0);
                udelay(500);

                size = IMAGE_START - BOOTLD_START;

                if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
                        read_lock(&adapter->adapter_lock);
                        UNM_NIC_PCI_WRITE_32(1, CRB_NORMALIZE(adapter,
                                                UNM_ROMUSB_GLB_CAS_RST));
                        read_unlock(&adapter->adapter_lock);
                }

                for (i = 0; i < (size/4); i++) {
                        data = bootld[i];//read 4 bytes as an integer
                        data = cpu_to_le32(data);
                        adapter->unm_nic_pci_mem_write(adapter, memaddr, &data, 4);
                        memaddr += 4;
                }

                size = fw_size;
                for(i = 0; i < (size/4); i++){
                        data = fw_img[i];
                        data = cpu_to_le32(data);
                        adapter->unm_nic_pci_mem_write(adapter, imgaddr, &data, 4);
                        imgaddr += 4;
                }

                udelay(100);
                read_lock(&adapter->adapter_lock);
                temp = UNM_BDINFO_MAGIC;
                adapter->unm_nic_hw_write_wx(adapter, UNM_CAM_RAM(0x1fc), &temp, 4);

                if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
                temp = 0x80001d;
                adapter->unm_nic_hw_write_wx(adapter, UNM_ROMUSB_GLB_SW_RESET, &temp, 4);
                } else {
                        UNM_NIC_PCI_WRITE_32(0x3fff, CRB_NORMALIZE(adapter,
                                                UNM_ROMUSB_GLB_CHIP_CLK_CTRL));
                        UNM_NIC_PCI_WRITE_32(0, CRB_NORMALIZE(adapter,
                                                UNM_ROMUSB_GLB_CAS_RST));
                }
                read_unlock(&adapter->adapter_lock);
        }
        return 0;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,28)
int do_load_fw_file(struct unm_adapter_s *adapter, const struct firmware *fw)
{
	int  i, size=0;
	long flashaddr = BOOTLD_START, memaddr = BOOTLD_START, imgaddr=IMAGE_START;
	int data;

	size = IMAGE_START - BOOTLD_START;

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		read_lock(&adapter->adapter_lock);
		UNM_NIC_PCI_WRITE_32(1, CRB_NORMALIZE(adapter,
						      UNM_ROMUSB_GLB_CAS_RST));
		read_unlock(&adapter->adapter_lock);
	}

	for (i = 0; i < (size/4); i++) {
		data = *(int *)&fw->data[flashaddr];//read 4 bytes as an integer
		data = cpu_to_le32(data);
		adapter->unm_nic_pci_mem_write(adapter, memaddr, &data, 4);
		flashaddr += 4;
		memaddr   += 4;
	}
	size = *(int *)&fw->data[FW_SIZE_OFFSET]; //read 4 bytes into size
	size = cpu_to_le32(size);
	for(i = 0; i < (size/4); i++){
		data = *(int *)&fw->data[IMAGE_START + i*4];
		data = cpu_to_le32(data);
		adapter->unm_nic_pci_mem_write(adapter, imgaddr, &data, 4);
		imgaddr += 4;
	}
	//The size of firmware may not be a multiple of 4
	size %= 4;
	if(size){
		data = *(int *)&fw->data[IMAGE_START + i*4];
		data = cpu_to_le32(data);
		adapter->unm_nic_pci_mem_write(adapter, imgaddr, &data, 4);
	}
	udelay(100);
	read_lock(&adapter->adapter_lock);
	data = UNM_BDINFO_MAGIC;
	adapter->unm_nic_hw_write_wx(adapter, UNM_CAM_RAM(0x1fc), &data, 4);

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		data = 0x80001d;
		adapter->unm_nic_hw_write_wx(adapter, UNM_ROMUSB_GLB_SW_RESET,
					     &data, 4);
	} else {
		UNM_NIC_PCI_WRITE_32(0x3fff, CRB_NORMALIZE(adapter,
						UNM_ROMUSB_GLB_CHIP_CLK_CTRL));
		UNM_NIC_PCI_WRITE_32(0, CRB_NORMALIZE(adapter,
						UNM_ROMUSB_GLB_CAS_RST));
	}
	read_unlock(&adapter->adapter_lock);
	return 0;
}

/*
        This function works like this:
        1)      (if(cmp_versions == LOAD_FW_WITH_COMPARISON)) and (Both file and flash firmwares are compatible 
		with the driver) then only do the comparison of them and load the latest one.
        2)      If (cmp_versions == LOAD_FW_NO_COMPARISON), then we will load the firmware unconditionally from the file.
                If we hit any error in doing this, then we won't fall back to flash firmware.
        3)      A firmware is compatible with the driver only when both's major and minor numbers are same.
        4)      We will be doing the comparison of the versions of file and flash firmwares, only when they both
                are compatible with the driver.
        5)      The flash firmware version will be read only when the (cmp_versions == LOAD_FW_WITH_COMPARISON)
*/

int try_load_fw_file(struct unm_adapter_s *adapter, int cmp_versions)
{
        int ret = 0, flash = 0;
        u32 file_ver, flash_ver;
        u32 major = 0, minor = 0, sub = 0;
        u32 file_major=0, file_minor=0, file_sub=0;
        u32 flash_major=0, flash_minor=0, flash_sub=0;
        const struct firmware *fw = NULL ;
        int tmp;
        int file_firmware_incompat = 0, orig_cmp_versions = cmp_versions;

	char	*romimage_array[] = NX_ROMIMAGE_NAME_ARRAY;

	if (adapter->fwtype == NX_UNKNOWN_TYPE_ROMIMAGE ||
	    adapter->fwtype >= NX_UNKNOWN_TYPE_ROMIMAGE_LAST) {
		printk("%s: Firmware type[%u] to load is invalid, "
		       "using flash\n", unm_nic_driver_name, adapter->fwtype);
			if(cmp_versions == LOAD_FW_NO_COMPARISON) { //This is unconditional load of file,
                                //so don't fall back to flash.
                                printk(KERN_ALERT "%s: Firmware type to load is invalid, and not loading from"
                                                "flash, so exiting\n", unm_nic_driver_name);
                                ret = -1;
                                goto OUT;
                        }
		cmp_versions = 0;
		flash = 1;
	} else {

		unsigned int mn_present;

		adapter->unm_nic_hw_read_wx(adapter, NX_PEG_TUNE_CAPABILITY, &mn_present, 4);
		mn_present &= NX_PEG_TUNE_MN_PRESENT;

		if (adapter->fwtype == NX_P3_MN_TYPE_ROMIMAGE) {
			if (mn_present) {
				printk("%s: Loading %s FW\n", unm_nic_driver_name,
				       romimage_array[adapter->fwtype]);
				ret = request_firmware(&fw, romimage_array[adapter->fwtype],
						       &(adapter->pdev->dev));
			} else {
				printk("%s: No MN present\n", unm_nic_driver_name);
				ret = -1;
			}

			if (ret < 0) {
				/* MN dynamic FW load failed.  Try CT. */
				adapter->fwtype = NX_P3_CT_TYPE_ROMIMAGE;
			} else {
				goto P3_MN_FW_LOAD_SUCCESS;
			}
		}
		printk("%s: Loading %s FW\n", unm_nic_driver_name,
		       romimage_array[adapter->fwtype]);
		ret = request_firmware(&fw, romimage_array[adapter->fwtype],
				       &(adapter->pdev->dev));
P3_MN_FW_LOAD_SUCCESS:
		if (ret < 0) {
			printk("%s: Could not read the firmware file %s from filesystem\n",
				unm_nic_driver_name, romimage_array[adapter->fwtype]);
			fw = NULL;
			if(cmp_versions == LOAD_FW_NO_COMPARISON) { //Don't fall back to flash.
				printk(KERN_ALERT "%s: Error in reading firmware file, not loading from"
						"flash, so exiting\n", unm_nic_driver_name);
				ret = -1;
				goto OUT;
			}
			cmp_versions = 0;
			flash = 1;
		}
	}
        /* Firmware size should be a minimum of 0x3fffff*/
        if((flash == 0) && (fw->size < 0x3fffff)){
                printk("%s: Firmware size mismatch\n", unm_nic_driver_name);
                if(cmp_versions == LOAD_FW_NO_COMPARISON) {
                        printk(KERN_ALERT "%s: Firmware size mismatch, not loading from"
                                                "flash, so exiting\n", unm_nic_driver_name);
                        ret = -1;
                        goto OUT;
                }
                cmp_versions = 0;
                flash = 1;
        }

        if(flash == 0){ //No error in reading file firmware.
                memcpy(&file_ver, (fw->data + FW_VERSION_OFFSET), 4);
                file_ver = cpu_to_le32(file_ver);
                file_major = file_ver & 0xff;
                file_minor = (file_ver >> 8) & 0xff;
                file_sub = file_ver >> 16;

                if((file_major != _UNM_NIC_LINUX_MAJOR) ||
                                (file_minor != _UNM_NIC_LINUX_MINOR)){
                        printk(KERN_ALERT "%s: Driver and File firmware are not compatible.\n", unm_nic_driver_name);
                        if(cmp_versions == LOAD_FW_NO_COMPARISON) {
                                printk(KERN_ALERT "%s: Driver and File firmware are not compatible, and not loading from"
                                                        "flash, so exiting\n", unm_nic_driver_name);
                                ret = -1;
                                goto OUT;
                        }
                        file_firmware_incompat = 1;
                        cmp_versions = 0;
                        flash = 1;
                }
                nx_nic_print4(NULL, "The file version = %d.%d.%d\n", file_major, file_minor, file_sub);
        }

        if(orig_cmp_versions == LOAD_FW_WITH_COMPARISON) {
                if (rom_fast_read(adapter, FW_VERSION_OFFSET, (int *)&flash_ver) != 0) {
                        printk("%s: Error in reading firmware version"
                                        " from flash\n",unm_nic_driver_name);
                        if(flash || file_firmware_incompat) { //Both flash and file firmware are corrupt.
                                ret = -1;
                                goto OUT;
                        }
                        cmp_versions = 0;
                        //At this point, value of flash is 0;
                } else {
                        flash_major = flash_ver & 0xff;
                        flash_minor = (flash_ver >> 8) & 0xff;
                        flash_sub = flash_ver >> 16;

                        if((flash_major != _UNM_NIC_LINUX_MAJOR) ||
                                        (flash_minor != _UNM_NIC_LINUX_MINOR)){
                                if(flash || file_firmware_incompat) { //Both firmwares are incompatible with driver.
                                        printk(KERN_ERR "%s: There is a mismatch in Driver"
                                                        "(%d.%d.%d) and Firmware (%d.%d.%d) version, please "
                                                        "use compatible firmware. \n",
                                                        unm_nic_driver_name, _UNM_NIC_LINUX_MAJOR,
                                                        _UNM_NIC_LINUX_MINOR, _UNM_NIC_LINUX_SUBVERSION,
                                                        flash_major, flash_minor, flash_sub);
                                        //ret = -1;
                                        //goto OUT;
                                }
                                //No need to compare this flash firmware with file firmware.
                                cmp_versions = 0;
                        }
                        nx_nic_print4(NULL, "The flash version = %d.%d.%d\n", flash_major, flash_minor, flash_sub);
                }
        }

        if(cmp_versions == LOAD_FW_WITH_COMPARISON) {
                /*
                        We will come here only when both file and flash firmwares are compatible with the
                        driver.
                */
                if (file_major < flash_major ||
                        (file_major == flash_major &&
                        file_minor < flash_minor) ||
                        (file_major == flash_major &&
                        file_minor == flash_minor &&
                        file_sub < flash_sub)) { //Flash firmware is latest, so load from flash.

                        flash = 1;
                } else { //File firmware is latest.
                        flash = 0;
                }
        }

        if(flash == 0) {
                major = file_major;
                minor = file_minor;
                sub = file_sub;
        } else {
                major = flash_major;
                minor = flash_minor;
                sub = flash_sub;
        }

        if (sub != _UNM_NIC_LINUX_SUBVERSION ){
                printk(KERN_INFO "%s: There is a mismatch in the Driver(%d.%d.%d)"
                        "and Firmware (%d.%d.%d) sub version \n",
                        unm_nic_driver_name, _UNM_NIC_LINUX_MAJOR,
                        _UNM_NIC_LINUX_MINOR, _UNM_NIC_LINUX_SUBVERSION,
                        major, minor, sub);
        }

        tmp = 0;
        adapter->unm_nic_hw_write_wx(adapter, CRB_CMDPEG_STATE, &tmp, 4);
        pinit_from_rom(adapter,0);
        udelay(500);
        if (flash){
                nx_nic_print4(NULL, "%s: Loading the firmware from flash\n", unm_nic_driver_name);
                load_from_flash(adapter);
        } else {
                nx_nic_print4(NULL, "%s: Loading the firmware from file\n", unm_nic_driver_name);
                do_load_fw_file(adapter, fw);
        }
OUT:
        if(fw) {
                release_firmware(fw);
        }
        return ret;
}
#endif
