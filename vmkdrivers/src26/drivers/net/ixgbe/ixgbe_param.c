/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2007 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#include <linux/types.h>
#include <linux/module.h>

#include "ixgbe.h"

/* This is the only thing that needs to be changed to adjust the
 * maximum number of ports that the driver can manage.
 */

#define IXGBE_MAX_NIC 8

#define OPTION_UNSET    -1
#define OPTION_DISABLED 0
#define OPTION_ENABLED  1

/* All parameters are treated the same, as an integer array of values.
 * This macro just reduces the need to repeat the same declaration code
 * over and over (plus this helps to avoid typo bugs).
 */

#define IXGBE_PARAM_INIT { [0 ... IXGBE_MAX_NIC] = OPTION_UNSET }
#ifndef module_param_array
/* Module Parameters are always initialized to -1, so that the driver
 * can tell the difference between no user specified value or the
 * user asking for the default value.
 * The true default values are loaded in when ixgbe_check_options is called.
 *
 * This is a GCC extension to ANSI C.
 * See the item "Labeled Elements in Initializers" in the section
 * "Extensions to the C Language Family" of the GCC documentation.
 */

#define IXGBE_PARAM(X, desc) \
	static const int __devinitdata X[IXGBE_MAX_NIC+1] = IXGBE_PARAM_INIT; \
	MODULE_PARM(X, "1-" __MODULE_STRING(IXGBE_MAX_NIC) "i"); \
	MODULE_PARM_DESC(X, desc);
#else
#define IXGBE_PARAM(X, desc) \
	static int __devinitdata X[IXGBE_MAX_NIC+1] = IXGBE_PARAM_INIT; \
	static unsigned int num_##X; \
	module_param_array_named(X, X, int, &num_##X, 0); \
	MODULE_PARM_DESC(X, desc);
#endif

/* Interrupt Type
 *
 * Valid Range: 0-2
 *  - 0 - Legacy Interrupt
 *  - 1 - MSI Interrupt
 *  - 2 - MSI-X Interrupt(s)
 *
 * Default Value: 2
 */
IXGBE_PARAM(InterruptType, "Change Interrupt Mode (0=Legacy, 1=MSI, 2=MSI-X)");
#define IXGBE_INT_LEGACY		      0
#define IXGBE_INT_MSI			      1
#define IXGBE_INT_MSIX			      2
#define IXGBE_DEFAULT_INT	 IXGBE_INT_MSIX

/* MQ - Multiple Queue enable/disable
 *
 * Valid Range: 0, 1
 *  - 0 - disables MQ
 *  - 1 - enables MQ
 *
 * Default Value: 1
 */

IXGBE_PARAM(MQ, "Disable or enable Multiple Queues (MQ)");

#ifdef IXGBE_DCA
/* DCA - Direct Cache Access (DCA) Enable/Disable
 *
 * Valid Range: 0, 1
 *  - 0 - disables DCA
 *  - 1 - enables DCA
 *
 * Default Value: 1
 */

IXGBE_PARAM(DCA, "Disable or enable Direct Cache Access (DCA)");

#endif
/* RSS - Receive-Side Scaling (RSS) Descriptor Queues
 *
 * Valid Range: 0-16
 *  - 0 - disables RSS
 *  - 1 - enables RSS and sets the Desc. Q's to min(16, num_online_cpus()).
 *  - 2-16 - enables RSS and sets the Desc. Q's to the specified value.
 *
 * Default Value: 1
 */

IXGBE_PARAM(RSS, "Number of Receive-Side Scaling (RSS) Descriptor Queues");

#ifdef CONFIG_IXGBE_VMDQ
/* VMDQ - Virtual Machine Device Queues (VMDQ)
 *
 * Valid Range: 0-16
 *  - 0 - disables VMDQ
 *  - 1 - enables VMDQ and sets the Desc. Q's to min(16, num_online_cpus()).
 *  - 2-16 - enables RSS and sets the Desc. Q's to the specified value.
 *
 * Default Value: 0
 */

#if defined(__VMKLNX__)
IXGBE_PARAM(VMDQ, "Number of Virtual Machine Device Queues (VMDQ) (Max value 8)");
#else /* defined(__VMKLNX__) */
IXGBE_PARAM(VMDQ, "Number of Virtual Machine Device Queues (VMDQ)");
#endif /* !defined(__VMKLNX__) */

/* VMDQTX - Flag for TX Enable of VMDQ
 *
 * Valid Range: TRUE - FALSE
 * Specifying parameter enables VMDQ Tx
 * 
 * Default Value: FALSE
 */

IXGBE_PARAM(VMDQTX, "Enable Transmit VMDQ");
#endif

/* Interrupt Throttle Rate (interrupts/sec)
 *
 * Valid Range: 100-500000 (0=off)
 *
 * Default Value: 8000
 */
IXGBE_PARAM(InterruptThrottleRate, "Maximum interrupts per second, per vector");
#if defined(__VMKLNX__)
/* We changed the default value to 16000 based on performance evaluation on ESX
 * Server (see PR 309216).  
 *
 * We found that setting ITR to 16000 will improve the performance of vmxnet3
 * and iSCSI significantly, in particular for small socket size networking
 * tests on both UP and vSMP VMs.  The default value set by Intel seems to be
 * low for driving 10G networking throughtput in the VMs.
 */
#define DEFAULT_ITR                16000
#else
#define DEFAULT_ITR                 8000
#endif
#define MAX_ITR                   500000
#define MIN_ITR                      100

#ifndef IXGBE_NO_LLI
/* LLIPort (Low Latency Interrupt TCP Port)
 *
 * Valid Range: 0 - 65535
 *
 * Default Value: 0 (disabled)
 */
IXGBE_PARAM(LLIPort, "Low Latency Interrupt TCP Port");

#define DEFAULT_LLIPORT                0
#define MAX_LLIPORT               0xFFFF
#define MIN_LLIPORT                    0

/* LLIPush (Low Latency Interrupt on TCP Push flag)
 *
 * Valid Range: 0,1
 *
 * Default Value: 0 (disabled)
 */
IXGBE_PARAM(LLIPush, "Low Latency Interrupt on TCP Push flag");

#define DEFAULT_LLIPUSH                0
#define MAX_LLIPUSH                    1
#define MIN_LLIPUSH                    0

/* LLISize (Low Latency Interrupt on Packet Size)
 *
 * Valid Range: 0 - 1500
 *
 * Default Value: 0 (disabled)
 */
IXGBE_PARAM(LLISize, "Low Latency Interrupt on Packet Size");

#define DEFAULT_LLISIZE                0
#define MAX_LLISIZE                 1500
#define MIN_LLISIZE                    0
#endif /* IXGBE_NO_LLI */

/* Rx buffer mode
 *
 * Valid Range: 0-2 0 = 1buf_mode_always, 1 = ps_mode_always and 2 = optimal
 *
 * Default Value: 2
 */
IXGBE_PARAM(RxBufferMode, "Rx Buffer Mode - Packet split or one buffer in rx");

#define IXGBE_RXBUFMODE_1BUF_ALWAYS			0
#define IXGBE_RXBUFMODE_PS_ALWAYS			1
#define IXGBE_RXBUFMODE_OPTIMAL				2
#define IXGBE_DEFAULT_RXBUFMODE	  IXGBE_RXBUFMODE_1BUF_ALWAYS


struct ixgbe_option {
	enum { enable_option, range_option, list_option } type;
	const char *name;
	const char *err;
	int def;
	union {
		struct { /* range_option info */
			int min;
			int max;
		} r;
		struct { /* list_option info */
			int nr;
			struct ixgbe_opt_list {
				int i;
				char *str;
			} *p;
		} l;
	} arg;
};

static int __devinit ixgbe_validate_option(unsigned int *value,
                                           struct ixgbe_option *opt)
{
	if (*value == OPTION_UNSET) {
		*value = opt->def;
		return 0;
	}

	switch (opt->type) {
	case enable_option:
		switch (*value) {
		case OPTION_ENABLED:
			printk(KERN_INFO "ixgbe: %s Enabled\n", opt->name);
			return 0;
		case OPTION_DISABLED:
			printk(KERN_INFO "ixgbe: %s Disabled\n", opt->name);
			return 0;
		}
		break;
	case range_option:
		if (*value >= opt->arg.r.min && *value <= opt->arg.r.max) {
			printk(KERN_INFO "ixgbe: %s set to %d\n", opt->name, *value);
			return 0;
		}
		break;
	case list_option: {
		int i;
		struct ixgbe_opt_list *ent;

		for (i = 0; i < opt->arg.l.nr; i++) {
			ent = &opt->arg.l.p[i];
			if (*value == ent->i) {
				if (ent->str[0] != '\0')
					printk(KERN_INFO "%s\n", ent->str);
				return 0;
			}
		}
	}
		break;
	default:
		BUG();
	}

	printk(KERN_INFO "Invalid %s specified (%d) %s\n",
	       opt->name, *value, opt->err);
	*value = opt->def;
	return -1;
}

#define LIST_LEN(l) (sizeof(l) / sizeof(l[0]))

/**
 * ixgbe_check_options - Range Checking for Command Line Parameters
 * @adapter: board private structure
 *
 * This routine checks all command line parameters for valid user
 * input.  If an invalid value is given, or if no user specified
 * value exists, a default value is used.  The final value is stored
 * in a variable in the adapter structure.
 **/
void __devinit ixgbe_check_options(struct ixgbe_adapter *adapter)
{
	int bd = adapter->bd_number;

	if (bd >= IXGBE_MAX_NIC) {
		printk(KERN_NOTICE
		       "Warning: no configuration for board #%d\n", bd);
		printk(KERN_NOTICE "Using defaults for all values\n");
#ifndef module_param_array
		bd = IXGBE_MAX_NIC;
#endif
	}

	{ /* Interrupt Type */
		unsigned int i_type;
		struct ixgbe_option opt = {
			.type = range_option,
			.name = "Interrupt Type",
			.err =
			  "using default of "__MODULE_STRING(IXGBE_DEFAULT_INT),
			.def = IXGBE_DEFAULT_INT,
			.arg = { .r = { .min = IXGBE_INT_LEGACY,
					.max = IXGBE_INT_MSIX}}
		};

#ifdef module_param_array
		if (num_InterruptType > bd) {
#endif
			i_type = InterruptType[bd];
			ixgbe_validate_option(&i_type, &opt);
			switch (i_type) {
			case IXGBE_INT_MSIX:
				if (!adapter->flags & IXGBE_FLAG_MSIX_CAPABLE)
					printk(KERN_INFO
					       "Ignoring MSI-X setting; "
					       "support unavailable.\n");
				break;
			case IXGBE_INT_MSI:
				if (!adapter->flags & IXGBE_FLAG_MSI_CAPABLE)
					printk(KERN_INFO
					       "Ignoring MSI setting; "
					       "support unavailable.\n");
				else
					adapter->flags &= ~IXGBE_FLAG_MSIX_CAPABLE;
				break;
			case IXGBE_INT_LEGACY:
			default:
				adapter->flags &= ~IXGBE_FLAG_MSIX_CAPABLE;
				adapter->flags &= ~IXGBE_FLAG_MSI_CAPABLE;
				break;
			}
#ifdef module_param_array
		} else {
			adapter->flags |= IXGBE_FLAG_MSIX_CAPABLE;
			adapter->flags |= IXGBE_FLAG_MSI_CAPABLE;
		}
#endif
	}
	{ /* Multiple Queue Support */
		struct ixgbe_option opt = {
			.type = enable_option,
			.name = "Multiple Queue Support",
			.err  = "defaulting to Enabled",
			.def  = OPTION_ENABLED
		};

#ifdef module_param_array
		if (num_MQ > bd) {
#endif
			unsigned int mq = MQ[bd];
			ixgbe_validate_option(&mq, &opt);
			if (mq)
				adapter->flags |= IXGBE_FLAG_MQ_CAPABLE;
			else
				adapter->flags &= ~IXGBE_FLAG_MQ_CAPABLE;
#ifdef module_param_array
		} else {
			if (opt.def == OPTION_ENABLED)
				adapter->flags |= IXGBE_FLAG_MQ_CAPABLE;
			else
				adapter->flags &= ~IXGBE_FLAG_MQ_CAPABLE;
		}
#endif
#ifdef CONFIG_IXGBE_NAPI
#if !defined(__VMKLNX__)
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24) )
		/* Must disable multiqueue for NAPI operation on
		 * kernels that don't have multiqueue NAPI support
		 */
		if (adapter->flags & IXGBE_FLAG_MQ_CAPABLE) {
			DPRINTK(PROBE, ERR,
			        "Multiple queues are not supported while NAPI "
			        "is enabled.  Disabling Multiple Queues.\n");
			adapter->flags &= ~IXGBE_FLAG_MQ_CAPABLE;
		}
#endif
#endif /* !defined(__VMKLNX__) */
#endif
		/* Check Interoperability */
		if ((adapter->flags & IXGBE_FLAG_MQ_CAPABLE) &&
		    !(adapter->flags & IXGBE_FLAG_MSIX_CAPABLE)) {
			DPRINTK(PROBE, INFO,
			        "Multiple queues are not supported while MSI-X "
			        "is disabled.  Disabling Multiple Queues.\n");
			adapter->flags &= ~IXGBE_FLAG_MQ_CAPABLE;
		}
	}
#ifdef IXGBE_DCA
	{ /* Direct Cache Access (DCA) */
		struct ixgbe_option opt = {
			.type = enable_option,
			.name = "Direct Cache Access (DCA)",
			.err  = "defaulting to Enabled",
			.def  = OPTION_ENABLED
		};

#ifdef module_param_array
		if (num_DCA > bd) {
#endif
			unsigned int dca = DCA[bd];
			ixgbe_validate_option(&dca, &opt);
			if (dca)
				adapter->flags |= IXGBE_FLAG_DCA_ENABLED;
			else
				adapter->flags &= ~IXGBE_FLAG_DCA_ENABLED;
#ifdef module_param_array
		} else {
			if (opt.def == OPTION_ENABLED)
				adapter->flags |= IXGBE_FLAG_DCA_ENABLED;
			else
				adapter->flags &= ~IXGBE_FLAG_DCA_ENABLED;
		}
#endif
		/* Check Interoperability */
		if ((adapter->flags & IXGBE_FLAG_DCA_ENABLED) &&
		    !(adapter->flags & IXGBE_FLAG_DCA_CAPABLE)) {
			DPRINTK(PROBE, ERR,
			        "DCA is not supported on this hardware.  "
			        "Disabling DCA.\n");
			adapter->flags &= ~IXGBE_FLAG_DCA_ENABLED;
		}
	}
#endif /* IXGBE_DCA */
	{ /* Receive-Side Scaling (RSS) */
		struct ixgbe_option opt = {
			.type = range_option,
			.name = "Receive-Side Scaling (RSS)",
			.err  = "defaulting to Disabled",
			.def  = OPTION_DISABLED,
			.arg  = { .r = { .min = OPTION_DISABLED,
					 .max = IXGBE_MAX_RSS_INDICES}}
		};
		unsigned int rss = RSS[bd];

#ifdef module_param_array
		if (num_RSS > bd) {
#endif
			switch (rss) {
			case 1:
				/*
				 * Base it off num_online_cpus() with
				 * a hardware limit cap.
				 */
				rss = min(IXGBE_MAX_RSS_INDICES,
				          (int)num_online_cpus());
				break;
			default:
				ixgbe_validate_option(&rss, &opt);
				break;
			}
			adapter->ring_feature[RING_F_RSS].indices = rss;
			if (rss)
				adapter->flags |= IXGBE_FLAG_RSS_ENABLED;
			else
				adapter->flags &= ~IXGBE_FLAG_RSS_ENABLED;
#ifdef module_param_array
		} else {
			if (opt.def == OPTION_DISABLED) {
				adapter->flags &= ~IXGBE_FLAG_RSS_ENABLED;
			} else {
				rss = min(IXGBE_MAX_RSS_INDICES,
				          (int)num_online_cpus());
				adapter->ring_feature[RING_F_RSS].indices = rss;
				adapter->flags |= IXGBE_FLAG_RSS_ENABLED;
			}
		}
#endif
		/* Check Interoperability */
		if (adapter->flags & IXGBE_FLAG_RSS_ENABLED) {
			if (!(adapter->flags & IXGBE_FLAG_RSS_CAPABLE)) {
				DPRINTK(PROBE, INFO,
				        "RSS is not supported on this "
				        "hardware.  Disabling RSS.\n");
				adapter->flags &= ~IXGBE_FLAG_RSS_ENABLED;
				adapter->ring_feature[RING_F_RSS].indices = 0;
			} else if (!(adapter->flags & IXGBE_FLAG_MQ_CAPABLE)) {
				DPRINTK(PROBE, INFO,
				        "RSS is not supported while multiple "
				        "queues are disabled.  "
				        "Disabling RSS.\n");
				adapter->flags &= ~IXGBE_FLAG_RSS_ENABLED;
				adapter->ring_feature[RING_F_RSS].indices = 0;
			}
		}
	}
#ifdef CONFIG_IXGBE_VMDQ
	{ /* Virtual Machine Device Queues (VMDQ) */
		struct ixgbe_option opt = {
			.type = range_option,
			.name = "Virtual Machine Device Queues (VMDQ)",
#if !defined(__VMKLNX__)
			.err  = "defaulting to Enabled",
			.def  = OPTION_ENABLED,
#else /* !defined(__VMKLNX__) */
			.err  = "using default of "__MODULE_STRING(IXGBE_MAX_VMDQ_INDICES),
			.def  = IXGBE_MAX_VMDQ_INDICES,
#endif /* defined(__VMKLNX__) */
			.arg  = { .r = { .min = OPTION_DISABLED,
					 .max = IXGBE_MAX_VMDQ_INDICES}}
		};

#ifdef module_param_array
		if (num_VMDQ > bd) {
#endif
			unsigned int vmdq = VMDQ[bd];
			ixgbe_validate_option(&vmdq, &opt);
			adapter->ring_feature[RING_F_VMDQ].indices = vmdq;
			if (vmdq)
				adapter->flags |= IXGBE_FLAG_VMDQ_ENABLED;
			else
				adapter->flags &= ~IXGBE_FLAG_VMDQ_ENABLED;
#ifdef module_param_array
		} else {
			if (opt.def == OPTION_DISABLED) {
				adapter->flags &= ~IXGBE_FLAG_VMDQ_ENABLED;
			} else {
				adapter->ring_feature[RING_F_VMDQ].indices = 8;
				adapter->flags |= IXGBE_FLAG_VMDQ_ENABLED;
			}
		}
#endif
		/* Check Interoperability */
		if (adapter->flags & IXGBE_FLAG_VMDQ_ENABLED) {
			if (!(adapter->flags & IXGBE_FLAG_VMDQ_CAPABLE)) {
				DPRINTK(PROBE, INFO,
				        "VMDQ is not supported on this "
				        "hardware.  Disabling VMDQ.\n");
				adapter->flags &= ~IXGBE_FLAG_VMDQ_ENABLED;
				adapter->ring_feature[RING_F_VMDQ].indices = 0;
			} else if (!(adapter->flags & IXGBE_FLAG_MQ_CAPABLE)) {
				DPRINTK(PROBE, INFO,
				        "VMDQ is not supported while multiple "
				        "queues are disabled.  "
				        "Disabling VMDQ.\n");
				adapter->flags &= ~IXGBE_FLAG_VMDQ_ENABLED;
				adapter->ring_feature[RING_F_VMDQ].indices = 0;
			}
		}
	}
	/* VMDQTX */
	{
		struct ixgbe_option opt = {
			.type = enable_option,
			.name = "Enable Transmit VMDQ",
			.err  = "defaulting to Enabled",
			.def  = OPTION_ENABLED,
			.arg  = { .r = { .min = OPTION_DISABLED,
					 .max = OPTION_ENABLED}}
		};

#ifdef module_param_array
		if (num_VMDQTX > bd) {
#endif
			int vmdq_tx = VMDQTX[bd];
			ixgbe_validate_option(&vmdq_tx, &opt);
			/* Only valid if VMDQ is enabled */
			if (adapter->flags & IXGBE_FLAG_VMDQ_ENABLED) {
				if (vmdq_tx)
					adapter->flags |=
						IXGBE_FLAG_VMDQ_TX_ENABLED;
				else
					adapter->flags &=
						~IXGBE_FLAG_VMDQ_TX_ENABLED;
			}
#ifdef module_param_array
		} else {
			if (opt.def == OPTION_ENABLED)
				adapter->flags |= IXGBE_FLAG_VMDQ_TX_ENABLED;
			else
				adapter->flags &= ~IXGBE_FLAG_VMDQ_TX_ENABLED;
		}
#endif
	}
#endif /* CONFIG_IXGBE_VMDQ */
	{ /* Interrupt Throttling Rate */
		struct ixgbe_option opt = {
			.type = range_option,
			.name = "Interrupt Throttling Rate (ints/sec)",
			.err  = "using default of "__MODULE_STRING(DEFAULT_ITR),
			.def  = DEFAULT_ITR,
			.arg  = { .r = { .min = MIN_ITR,
					 .max = MAX_ITR }}
		};
		u32 eitr;

#ifdef module_param_array
		if (num_InterruptThrottleRate > bd) {
#endif
			eitr = InterruptThrottleRate[bd];
			switch (eitr) {
			case 0:
				DPRINTK(PROBE, INFO, "%s turned off\n",
				        opt.name);
				/* zero is a special value, we don't want to
				 * turn off ITR completely, just set it to an
				 * insane interrupt rate (like 3.5 Million
				 * ints/s */
				eitr = EITR_REG_TO_INTS_PER_SEC(1);
				break;
			case 1:
				DPRINTK(PROBE, INFO, "dynamic interrupt "
                                        "throttling enabled\n");
				adapter->itr_setting = 1;
				eitr = DEFAULT_ITR;
				break;
			default:
				ixgbe_validate_option(&eitr, &opt);
				break;
			}
#ifdef module_param_array
		} else {
			eitr = DEFAULT_ITR;
		}
#endif
		adapter->eitr_param = eitr;
	}
#ifndef IXGBE_NO_LLI
	{ /* Low Latency Interrupt TCP Port*/
		struct ixgbe_option opt = {
			.type = range_option,
			.name = "Low Latency Interrupt TCP Port",
			.err  = "using default of "
					__MODULE_STRING(DEFAULT_LLIPORT),
			.def  = DEFAULT_LLIPORT,
			.arg  = { .r = { .min = MIN_LLIPORT,
					 .max = MAX_LLIPORT }}
		};

#ifdef module_param_array
		if (num_LLIPort > bd) {
#endif
			adapter->lli_port = LLIPort[bd];
			if (adapter->lli_port) {
				ixgbe_validate_option(&adapter->lli_port, &opt);
			} else {
				DPRINTK(PROBE, INFO, "%s turned off\n",
					opt.name);
			}
#ifdef module_param_array
		} else {
			adapter->lli_port = opt.def;
		}
#endif
	}
	{ /* Low Latency Interrupt on Packet Size */
		struct ixgbe_option opt = {
			.type = range_option,
			.name = "Low Latency Interrupt on Packet Size",
			.err  = "using default of "
					__MODULE_STRING(DEFAULT_LLISIZE),
			.def  = DEFAULT_LLISIZE,
			.arg  = { .r = { .min = MIN_LLISIZE,
					 .max = MAX_LLISIZE }}
		};

#ifdef module_param_array
		if (num_LLISize > bd) {
#endif
			adapter->lli_size = LLISize[bd];
			if (adapter->lli_size) {
				ixgbe_validate_option(&adapter->lli_size, &opt);
			} else {
				DPRINTK(PROBE, INFO, "%s turned off\n",
					opt.name);
			}
#ifdef module_param_array
		} else {
			adapter->lli_size = opt.def;
		}
#endif
	}
	{ /*Low Latency Interrupt on TCP Push flag*/
		struct ixgbe_option opt = {
			.type = enable_option,
			.name = "Low Latency Interrupt on TCP Push flag",
			.err  = "defaulting to Disabled",
			.def  = OPTION_DISABLED
		};

#ifdef module_param_array
		if (num_LLIPush > bd) {
#endif
			unsigned int lli_push = LLIPush[bd];
			ixgbe_validate_option(&lli_push, &opt);
			if (lli_push)
				adapter->flags |= IXGBE_FLAG_LLI_PUSH;
			else
				adapter->flags &= ~IXGBE_FLAG_LLI_PUSH;
#ifdef module_param_array
		} else {
			if (opt.def == OPTION_ENABLED)
				adapter->flags |= IXGBE_FLAG_LLI_PUSH;
			else
				adapter->flags &= ~IXGBE_FLAG_LLI_PUSH;
		}
#endif
	}
#endif /* IXGBE_NO_LLI */
	{ /* Rx buffer mode */
		unsigned int rx_buf_mode;
		struct ixgbe_option opt = {
			.type = range_option,
			.name = "Rx buffer mode",
			.err = "using default of "
				__MODULE_STRING(IXGBE_DEFAULT_RXBUFMODE),
			.def = IXGBE_DEFAULT_RXBUFMODE,
			.arg = {.r = {.min = IXGBE_RXBUFMODE_1BUF_ALWAYS,
				      .max = IXGBE_RXBUFMODE_OPTIMAL}}
		};

#ifdef module_param_array
		if (num_RxBufferMode > bd) {
#endif
			rx_buf_mode = RxBufferMode[bd];
			ixgbe_validate_option(&rx_buf_mode, &opt);
			switch (rx_buf_mode) {
			case IXGBE_RXBUFMODE_OPTIMAL:
				adapter->flags |= IXGBE_FLAG_RX_1BUF_CAPABLE;
#if !defined(__VMKLNX__)
				adapter->flags |= IXGBE_FLAG_RX_PS_CAPABLE;
#endif /* !defined(__VMKLNX__) */
				break;
			case IXGBE_RXBUFMODE_PS_ALWAYS:
#if !defined(__VMKLNX__)
				adapter->flags |= IXGBE_FLAG_RX_PS_CAPABLE;
#endif /* !defined(__VMKLNX__) */
				break;
			case IXGBE_RXBUFMODE_1BUF_ALWAYS:
				adapter->flags |= IXGBE_FLAG_RX_1BUF_CAPABLE;
			default:
				break;
			}
#ifdef module_param_array
		} else {
			adapter->flags |= IXGBE_FLAG_RX_1BUF_CAPABLE;
#if !defined(__VMKLNX__)
			adapter->flags |= IXGBE_FLAG_RX_PS_CAPABLE;
#endif /* !defined(__VMKLNX__) */
		}
#endif
	}
}

