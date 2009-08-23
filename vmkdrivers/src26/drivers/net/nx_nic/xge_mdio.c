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
/******************************************************************************
*
*  xge_mdio.c   Routines to control the Management Data Input Ouput (MDIO) port
*               for the Aeluros 10Gps Transceiver connected to Phantom II's
*               XGe interface.
*
*******************************************************************************
*/

/* #include <unm_inc.h> */
#include "unm_nic.h"
#include "unm_nic_hw.h"

/* opcodes defines by Aerulos */
#define OPCODE_ADDR  0
#define OPCODE_WRITE 1
#define OPCODE_RINCR 2
#define OPCODE_READ  3

/* Bit 1 of the GPIO reg is Output Enable */
#define GPIO_OE 2

/* Bit 0 of GPIO reg is data bit */
#define GPIO_LO 0
#define GPIO_HI 1

#define NX_P2_C0                0x24
#define NX_P2_C1                0x25
#define NX_P3_A0                0x30
#define NX_P3_A2                0x32
#define NX_P3_B0                0x40
#define NX_P3_B1		0x41

#define NX_IS_REVISION_P2(REVISION)     (REVISION <= NX_P2_C1)
#define NX_IS_REVISION_P3(REVISION)     (REVISION >= NX_P3_A0)

/* Set MDC high or low for clocking data */
static inline void MDC_LOW(struct unm_adapter_s *adapter)		
{			
	long MDC_GPIO = 0;
	
	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		MDC_GPIO = 16; 
	else
		MDC_GPIO = 7; 
	
	UNM_CRB_WRITELIT_ADAPTER(UNM_ROMUSB_GPIO(MDC_GPIO),
				 (GPIO_OE | GPIO_LO), adapter);
}

static inline void MDC_HI(struct unm_adapter_s *adapter)
{
	long MDC_GPIO = 0;
	
	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) 
		MDC_GPIO = 16; 
	else
		MDC_GPIO = 7;
	
	UNM_CRB_WRITELIT_ADAPTER(UNM_ROMUSB_GPIO(MDC_GPIO),	
				 (GPIO_OE | GPIO_HI), adapter);
}

/* set the data bit */
static inline void SET_MDIO(struct unm_adapter_s *adapter,long BIT)
{
	long MDIO_GPIO = 0;
	
	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) 
		MDIO_GPIO = 17; 
	else
		MDIO_GPIO = 8;
	
	UNM_CRB_WRITELIT_ADAPTER(UNM_ROMUSB_GPIO(MDIO_GPIO),
				 (GPIO_OE | (BIT)), adapter);
}
static inline int GET_BIT(struct unm_adapter_s *adapter)
{
	long MDIO_GPIO = 0;
	
	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) 
		MDIO_GPIO = 17; 
	else
		MDIO_GPIO = 8;
	return (((UNM_CRB_READ_VAL_ADAPTER(UNM_ROMUSB_GLB_PAD_GPIO_I,
				   adapter) >> MDIO_GPIO) & 1));
}
/* If necessary, delay beteen each clock */
#define MDIO_DELAY()

/* The preamble is 32 clocks of high data */
void PREAMBLE(struct unm_adapter_s *adapter)
{
	long	i;

	for (i = 0; i < 32; i++) {
		MDC_LOW(adapter);
		SET_MDIO(adapter, 1);
		MDIO_DELAY();
		MDC_HI(adapter);
		MDIO_DELAY();
	}
}

/* data transitions when MDC is low */
void CLOCK_IN_BITS(struct unm_adapter_s *adapter, long data, long len)
{
	long	i;

	for (i = 0; i < len; i++) {
		MDC_LOW(adapter);
		SET_MDIO(adapter, (data >>(len - (i + 1))) & 1);
		MDIO_DELAY();
		MDC_HI(adapter);
		MDIO_DELAY();
	}
}


/* data transitions when MDC is low */
long CLOCK_OUT_BITS(struct unm_adapter_s *adapter)
{
	long	i;
	long	result = 0;
	long MDIO_GPIO = 0;
	
	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) 
		MDIO_GPIO = 17; 
	else
		MDIO_GPIO = 8;

	/* Don't drive MDIO output */
	UNM_CRB_WRITELIT_ADAPTER(UNM_ROMUSB_GPIO(MDIO_GPIO), 0x0, adapter);

	for (i = 0; i < 16; i++) {
		MDC_LOW(adapter);
		MDIO_DELAY();
		result |= (GET_BIT(adapter) << (15-i));
		MDIO_DELAY();
		MDC_HI(adapter);
		MDIO_DELAY();
	}
	return result;
}

/* Turn off Output Enable on the GPIO */
static inline void HI_Z(struct unm_adapter_s *adapter)				
{
	long MDIO_GPIO = 0;
	long MDC_GPIO = 0;
	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		MDIO_GPIO = 17;
		MDC_GPIO = 16;
	}
	else {
		MDIO_GPIO = 8;
		MDC_GPIO =  7;
	}
        UNM_CRB_WRITELIT_ADAPTER(UNM_ROMUSB_GPIO(MDC_GPIO), 0x0, adapter);
        UNM_CRB_WRITELIT_ADAPTER(UNM_ROMUSB_GPIO(MDIO_GPIO), 0x0, adapter);
} 


/* The STart of Frame (SOF) is two clocks of low data */
#define SOF(ADAPTER)			CLOCK_IN_BITS(ADAPTER, 0, 2)

/* The opcode is two bits, high bit first  */
#define OPCODE(ADAPTER, opcode)		CLOCK_IN_BITS(ADAPTER, (opcode), 2)

/* The port is five bits, high bit first  */
#define PORT_ADDRESS(ADAPTER, port)	CLOCK_IN_BITS(ADAPTER, (port), 5)

/* The devaddr is five bits, high bit first  */
#define DEV_ADDRESS(ADAPTER, dev)	CLOCK_IN_BITS(ADAPTER, (dev), 5)

/* The turnaround is two clocks - high, then low */
#define TURNAROUND(ADAPTER)		CLOCK_IN_BITS(ADAPTER, 2, 2)

#define ADDR_DATA(ADAPTER, addr_data)	CLOCK_IN_BITS(ADAPTER, (addr_data), 16)

#define MDIO_WR_CYCLE(ADAPTER, opcode, port, dev, addr_data)	\
	do {							\
		PREAMBLE(ADAPTER);				\
		SOF(ADAPTER);					\
		OPCODE(ADAPTER, (opcode));			\
		PORT_ADDRESS(ADAPTER, (port));			\
		DEV_ADDRESS(ADAPTER, (dev));			\
		TURNAROUND(ADAPTER);				\
		ADDR_DATA(ADAPTER, (addr_data));		\
	} while (0)

#define RD_DATA(ADAPTER)	CLOCK_OUT_BITS(ADAPTER)

#define MDIO_RD_CYCLE(ADAPTER, opcode, port, dev, data)	\
	do {						\
		PREAMBLE(ADAPTER);			\
		SOF(ADAPTER);				\
		OPCODE(ADAPTER, (opcode));		\
		PORT_ADDRESS(ADAPTER, (port));		\
		DEV_ADDRESS(ADAPTER, (dev));		\
		TURNAROUND(ADAPTER);			\
		data = RD_DATA(ADAPTER);		\
	} while (0)

/*
 *
 */
void unm_xge_mdio_wr(struct unm_adapter_s *adapter, long devaddr, long addr,
		     long data)
{
	/* first, write the address */
	MDIO_WR_CYCLE(adapter, OPCODE_ADDR, adapter->portnum, devaddr, addr);

	/* then, write the data */
	MDIO_WR_CYCLE(adapter, OPCODE_WRITE, adapter->portnum, devaddr, data);
}

/*
 *
 */
long unm_xge_mdio_rd(struct unm_adapter_s *adapter, long devaddr, long addr)
{
	long	data;

	/* first, write the address */
	MDIO_WR_CYCLE(adapter, OPCODE_ADDR, adapter->portnum, devaddr, addr);

	/* then, read the data */
	MDIO_RD_CYCLE(adapter, OPCODE_READ, adapter->portnum, devaddr, data);

	return (data);
}

void unm_xge_mdio_wr_port (struct unm_adapter_s *adapter, long port, long devaddr, long addr, long data) {

    /* first, write the address */
    MDIO_WR_CYCLE(adapter, OPCODE_ADDR, port, devaddr, addr);

    /* then, write the data */
    MDIO_WR_CYCLE(adapter, OPCODE_WRITE, port, devaddr, data);
}

long unm_xge_mdio_rd_port (struct unm_adapter_s *adapter, long port, long devaddr, long addr) {
    long data;

    /* first, write the address */
    MDIO_WR_CYCLE(adapter, OPCODE_ADDR, port, devaddr, addr);

    /* then, read the data */
    MDIO_RD_CYCLE(adapter, OPCODE_READ, port, devaddr, data);

    return data;
}


/* AEL1002 supports 3 device addresses: 1(PMA/PMD), 3(PCS), and 4(PHY XS) */
#define DEV_PMA_PMD 1
#define DEV_PCS     3
#define DEV_PHY_XS  4

/* Aeluros-specific registers use device address 1 */
#define AEL_POWERDOWN_REG   0xc011
#define AEL_TX_CONFIG_REG_1 0xc002
#define AEL_LOOPBACK_EN_REG 0xc017
#define AEL_MODE_SEL_REG    0xc001

#define PMD_RESET          0
#define PMD_STATUS         1
#define PMD_IDENTIFIER     2
#define PCS_STATUS_REG     0x20
#define PMD_PRODUCT_CODE   0xD000
#define PMD_PRODUCT_CODE_2 0xD001

#define PMD_ID_QUAKE 0x43
#define PMD_ID_MYSTICOM 0x240
#define PMD_ID_QUAKE_KR_PHY 0x2025
#define PMD_ID_QUAKE_KR_PHY_PC2_A2 0xA2A0
#define PMD_ID_QUAKE_KR_PHY_PC2_B1 0xB110
#define PMD_ID_QUAKE_KR_PHY_PC2_B2 0xB210
#define PCS_CONTROL 0


#define PHY_XS_LANE_STATUS_REG 0x18
#define PHY_RX_LINE_STATUS_REG 0x21
#define PHY_EDCS_STATUS_REG 0xD7fD

/* Turn on the GPIO lines used for MDC/MDIO. They can alternately be used as
 *  test mux data.
 */
void unm_xge_mdio_enable(struct unm_adapter_s *adapter)
{
	long	data;
	long MDIO_GPIO = 0;
	long MDC_GPIO = 0;
	
	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		MDIO_GPIO = 17;
		MDC_GPIO = 16;
	}
	else {
		MDIO_GPIO = 8;
		MDC_GPIO =  7;
	}

	/* GPIO bits are [31:16] in the test mux sel register */
	/* Turn off the bit to enable GPIO */
	data = UNM_CRB_READ_VAL_ADAPTER(UNM_ROMUSB_GLB_TEST_MUX_SEL, adapter);
	data &= ~(0x10000 << MDC_GPIO);
	data &= ~(0x10000 << MDIO_GPIO);
	UNM_CRB_WRITELIT_ADAPTER(UNM_ROMUSB_GLB_TEST_MUX_SEL, data, adapter);

	/* Turn off OE for MDC and MDIO */
	HI_Z(adapter);
}

void do_xge_link_status(struct unm_adapter_s *adapter, long *status)
{
	long	data;

	*status = XG_LINK_DOWN;
	data = unm_xge_mdio_rd(adapter, DEV_PMA_PMD, PMD_IDENTIFIER);
	if (data == PMD_ID_QUAKE || data == PMD_ID_MYSTICOM) {
		/* Quake PHY */

		/* check link - read twice to clear sticky bits */
		data = unm_xge_mdio_rd(adapter, DEV_PMA_PMD, PMD_STATUS);
		data = unm_xge_mdio_rd(adapter, DEV_PMA_PMD, PMD_STATUS);

		if (data & 0x4) {
			/* Rx link is up */
			*status = XG_LINK_UP;
		} else {
			*status = XG_LINK_DOWN;
		}

	} else {
		data = unm_xge_mdio_rd(adapter, DEV_PCS, PCS_STATUS_REG);
		if ((data & 0x1000) == 0x1000) {
			*status = XG_LINK_UP;
		}
	}
}

int xge_link_status (struct unm_adapter_s *adapter, long *status) {
	int rv = 0;	

	if (phy_lock(adapter) != 0){
		return -1;
	}
	do_xge_link_status (adapter, status);	
	phy_unlock(adapter);
	return rv;	
}


/* Initialize the Aeluros device. To be done when link first comes up */
long xge_mdio_init(struct unm_adapter_s *adapter)
{
	long	rv = 0;
	long	data;

	unm_xge_mdio_enable(adapter);
	data = unm_xge_mdio_rd(adapter, DEV_PMA_PMD, PMD_IDENTIFIER);
	if (data == PMD_ID_QUAKE ) {
		/* Quake PHY */
		/* reset PHY */
		unm_xge_mdio_wr(adapter, DEV_PCS, PCS_CONTROL, 0x8000);

		/* check link - read twice to clear sticky bits */
		data = unm_xge_mdio_rd(adapter, DEV_PMA_PMD, PMD_STATUS);
		data = unm_xge_mdio_rd(adapter, DEV_PMA_PMD, PMD_STATUS);

		if(! (data & 0x4)) {
			rv |= 2;
		}

	} else if (data == PMD_ID_MYSTICOM) {
		/* Mysticom PHY */
		/* reset PHY */
		unm_xge_mdio_wr(adapter, DEV_PCS, PCS_CONTROL, 0x8000);

		/* check link - read twice to clear sticky bits */
		data = unm_xge_mdio_rd(adapter, DEV_PMA_PMD, PMD_STATUS);
		data = unm_xge_mdio_rd(adapter, DEV_PMA_PMD, PMD_STATUS);

		if (!(data & 0x4)) {
			rv |= 2;
		}
		/* For Mysticom M3128, clear 3.c005 that shows link up/down
		   event. */
		unm_xge_mdio_rd(adapter, 3, 0xc005);
		unm_xge_mdio_wr(adapter, 3, 0xc005, -1);
		/* Enable the mask for link up/down events in 3.c006 */
		unm_xge_mdio_wr(adapter, 3, 0xc006, 0xc000);
	} else {

		unm_xge_mdio_wr(adapter, DEV_PMA_PMD, AEL_POWERDOWN_REG, 0);
		unm_xge_mdio_wr(adapter, DEV_PMA_PMD, AEL_TX_CONFIG_REG_1, 1);
		unm_xge_mdio_wr(adapter, DEV_PMA_PMD, AEL_LOOPBACK_EN_REG,
				0xfe30);
		unm_xge_mdio_wr(adapter, DEV_PMA_PMD, AEL_MODE_SEL_REG, 0x24);

		data = unm_xge_mdio_rd(adapter, DEV_PCS, PCS_STATUS_REG);

		if (!(data & 0x1000)) {
			rv |= 2;
		}

		data = unm_xge_mdio_rd(adapter, DEV_PHY_XS,
				       PHY_XS_LANE_STATUS_REG);

		if (!(data & 0x000f)) {
			rv |= 1;
		}
	}

	return rv;
}

void do_xge_loopback_p3(struct unm_adapter_s *adapter, int on)
{
        long    data;

        data = unm_xge_mdio_rd(adapter, DEV_PCS, PMD_IDENTIFIER);

        switch (data) {

        case PMD_ID_QUAKE:
                if (on) {
                        data = unm_xge_mdio_rd(adapter, DEV_PCS, 0);
                        unm_xge_mdio_wr(adapter, DEV_PCS, 0, data | 0x4000);
                        data = unm_xge_mdio_rd(adapter, DEV_PCS, 0xC000);
                        unm_xge_mdio_wr(adapter, DEV_PCS, 0xC000, data | 0x20);
                } else {
                        data = unm_xge_mdio_rd(adapter, DEV_PCS, 0xC000);
                        unm_xge_mdio_wr(adapter, DEV_PCS, 0xC000, data & ~0x20);
                        data = unm_xge_mdio_rd(adapter, DEV_PCS, 0);
                        unm_xge_mdio_wr(adapter, DEV_PCS, 0, data & ~0x4000);
                }
                break;

        case PMD_ID_MYSTICOM:
                        if (on) {
                                data = unm_xge_mdio_rd(adapter, DEV_PMA_PMD, 0);
                                unm_xge_mdio_wr(adapter, DEV_PMA_PMD, 0, data | 0x40);
                        } else {
                                unm_xge_mdio_wr(adapter, DEV_PMA_PMD, 0, data & ~0x40);
                        }
                        break;

         /* Puma PHY */
        default:
                        if (on) {
                                /* Get  - read twice to clear sticky bits */
                                data = unm_xge_mdio_rd(adapter, DEV_PMA_PMD, AEL_LOOPBACK_EN_REG);
                                unm_xge_mdio_wr(adapter, DEV_PMA_PMD, AEL_LOOPBACK_EN_REG, data | 4);
                                data = unm_xge_mdio_rd(adapter, DEV_PHY_XS, 0xC000);
                                unm_xge_mdio_wr(adapter, DEV_PHY_XS, 0xC000, data | 0x8000);
                        } else {
                                data = unm_xge_mdio_rd(adapter, DEV_PHY_XS, 0xC000);
                                unm_xge_mdio_wr(adapter, DEV_PHY_XS, 0xC000, data & ~0x8000);
                                data = unm_xge_mdio_rd(adapter, DEV_PMA_PMD, AEL_LOOPBACK_EN_REG);
                                unm_xge_mdio_wr(adapter, DEV_PMA_PMD, AEL_LOOPBACK_EN_REG, data & ~4);
                        }
                        break;

        }
}

void do_xge_loopback(struct unm_adapter_s *adapter, int on)
{
	long	data;

	data = unm_xge_mdio_rd(adapter, DEV_PMA_PMD, PMD_IDENTIFIER);

	switch (data) {
	case PMD_ID_QUAKE:
	case PMD_ID_MYSTICOM:
		if (on) {
			data = unm_xge_mdio_rd(adapter, DEV_PMA_PMD, 0);
			unm_xge_mdio_wr(adapter, DEV_PMA_PMD, 0, data | 1);
		} else {
			data = unm_xge_mdio_rd(adapter, DEV_PMA_PMD, 0);
			unm_xge_mdio_wr(adapter, DEV_PMA_PMD, 0, data & ~1);
		}
		break;

	/* Puma PHY */
	default:
		if (on) {
			data = unm_xge_mdio_rd(adapter, DEV_PMA_PMD,
					       AEL_LOOPBACK_EN_REG);
			unm_xge_mdio_wr(adapter, DEV_PMA_PMD,
					AEL_LOOPBACK_EN_REG, data | 4);
			data = unm_xge_mdio_rd(adapter, DEV_PHY_XS, 0xC000);
			unm_xge_mdio_wr(adapter, DEV_PHY_XS, 0xC000,
					data | 0x8000);
		} else {
			data = unm_xge_mdio_rd(adapter, DEV_PHY_XS, 0xC000);
			unm_xge_mdio_wr(adapter, DEV_PHY_XS, 0xC000,
					data & ~0x8000);
			data = unm_xge_mdio_rd(adapter, DEV_PMA_PMD,
					       AEL_LOOPBACK_EN_REG);
			unm_xge_mdio_wr(adapter, DEV_PMA_PMD,
					AEL_LOOPBACK_EN_REG, data & ~4);
		}
		break;
	}
}

int xge_loopback(struct unm_adapter_s *adapter, int on)
{
	int rv = 0;

	if (phy_lock(adapter) != 0){
		return -1;
	}
	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		do_xge_loopback_p3(adapter, on);
	else
		do_xge_loopback (adapter, on);
	phy_unlock(adapter);
	return rv;
}
