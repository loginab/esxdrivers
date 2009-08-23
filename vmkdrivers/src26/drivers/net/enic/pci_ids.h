/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ident "$Id: pci_ids.h 20563 2008-11-10 22:51:23Z orosz $"

#ifndef _PCI_IDS_H_
#define _PCI_IDS_H_

#define PCI_VENDOR_ID_CISCO                  0x1137  /* Cisco vendor id */

#define PCI_DEVICE_ID_CISCO_PALO_SWITCH      0x0023  /* palo pcie switch */

#define PCI_DEVICE_ID_CISCO_PALO_VP2PUP      0x0040  /* palo virt p2p up bridge */
#define PCI_DEVICE_ID_CISCO_PALO_VP2PDOWN    0x0041  /* palo virt p2p down bridge */
#define PCI_DEVICE_ID_CISCO_PALO_MGMT        0x0042  /* mgmt vnic, iommu ctrl */
#define PCI_DEVICE_ID_CISCO_PALO_ENET        0x0043  /* ethernet vnic */
#define PCI_DEVICE_ID_CISCO_PALO_ENET_PT     0x0044  /* enet for pass-thru */
#define PCI_DEVICE_ID_CISCO_PALO_FC          0x0045  /* fc vnic */
#define PCI_DEVICE_ID_CISCO_PALO_SCSI        0x0046  /* scsi vnic */
#define PCI_DEVICE_ID_CISCO_PALO_IPC         0x0047  /* ipc vnic */

/* subsys device id for all palo vnics */
#define PCI_SUBDEVICE_ID_CISCO_PALO          0x0048
/* subsys device id for oplin mezz card in CA */
#define PCI_SUBDEVICE_ID_CISCO_OPLIN         0x0049
/* subsys device id for menlo mezz cards in CA */
#define PCI_SUBDEVICE_ID_CISCO_MENLO_Q       0x004a
#define PCI_SUBDEVICE_ID_CISCO_MENLO_E       0x004b

#define PCI_DEVICE_ID_CISCO_WOODSIDE         0x004c
#define PCI_DEVICE_ID_CISCO_CARMEL           0x004d
#define PCI_DEVICE_ID_CISCO_SERENO           0x004e

/* subsys device id for all sereno vnics */
#define PCI_SUBDEVICE_ID_CISCO_SERENO        0x004f


/* These are fake IDs for debug vnics */
#define PCI_DEVICE_ID_CISCO_PALO_DEBUG       0x8000  /* dbg vnic, maps all
                                                        palo */
#define PCI_DEVICE_ID_CISCO_PALO_DECHO       0x8001  /* memdir,memind,iodir,
                                                        ioind */
#define PCI_DEVICE_ID_CISCO_PALO_DVERIFY     0x8002  /* vnic to access pkts */
#define PCI_DEVICE_ID_CISCO_PALO_HWIO        0x8003  /* hwio vnic reserves
                                                        rsrcs */
#define PCI_DEVICE_ID_CISCO_PALO_TEST        0x8004  /* test vnic */
#define PCI_DEVICE_ID_CISCO_PALO_NULL        0x8005  /* null vnic */
#define PCI_DEVICE_ID_CISCO_PALO_BRIDGE      0x8040  /* palo virt pcie/pci bridge */

#endif /* _PCI_IDS_H_ */
