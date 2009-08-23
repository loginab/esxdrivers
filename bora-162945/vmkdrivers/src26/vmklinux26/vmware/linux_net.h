/* ****************************************************************
 * Portions Copyright 2008 VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * ****************************************************************/

/*
 * linux_net.h --
 *
 *      Linux Network compatibility.
 */

#ifndef _LINUX_NET_H_
#define _LINUX_NET_H_

struct net_device;
struct pci_dev;

extern void LinNet_Init(void);
extern void LinNet_Cleanup(void);
extern int LinNet_ConnectUplink(struct net_device *dev, struct pci_dev *pdev);


#endif /* _LINUX_NET_H_ */



