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
#ifndef _UNM_PSTATS_H_
#define _UNM_PSTATS_H_

#include "unm_compiler_defs.h"

/*
 * We use all unsigned longs. Linux will soon be so reliable that even these
 * will rapidly get too small 8-). Seriously consider the IpInReceives count
 * on the 20Gb/s + networks people expect in a few years time!
 */

#define NETXEN_NUM_PORTS        4
#define NETXEN_NUM_PEGS         4

typedef struct {
	__uint64_t IpInReceives;
	__uint64_t IpInHdrErrors;
	__uint64_t IpInAddrErrors;
	__uint64_t IpInNoRoutes;
	__uint64_t IpInDiscards;
	__uint64_t IpInDelivers;
	__uint64_t IpOutRequests;
	__uint64_t IpOutDiscards;
	__uint64_t IpOutNoRoutes;
	__uint64_t IpReasmTimeout;
	__uint64_t IpReasmReqds;
	__uint64_t IpReasmOKs;
	__uint64_t IpReasmFails;
	__uint64_t IpFragOKs;
	__uint64_t IpFragFails;
	__uint64_t IpFragCreates;
} nx_ip_mib_t;

typedef struct {
	__uint64_t PREALIGN(512) IcmpInMsgs POSTALIGN(512);
	__uint64_t IcmpInErrors;
	__uint64_t IcmpInDestUnreachs;
	__uint64_t IcmpInTimeExcds;
	__uint64_t IcmpInParmProbs;
	__uint64_t IcmpInSrcQuenchs;
	__uint64_t IcmpInRedirects;
	__uint64_t IcmpInTimestamps;
	__uint64_t IcmpInTimestampReps;
	__uint64_t IcmpInAddrMasks;
	__uint64_t IcmpInAddrMaskReps;
	__uint64_t IcmpOutMsgs;
	__uint64_t IcmpOutErrors;
	__uint64_t IcmpOutDestUnreachs;
	__uint64_t IcmpOutTimeExcds;
	__uint64_t IcmpOutParmProbs;
	__uint64_t IcmpOutSrcQuenchs;
	__uint64_t IcmpOutRedirects;
	__uint64_t IcmpOutTimestamps;
	__uint64_t IcmpOutTimestampReps;
	__uint64_t IcmpOutAddrMasks;
	__uint64_t IcmpOutAddrMaskReps;
	__uint64_t dummy;
} nx_icmp_mib_t;

typedef struct {
	__uint64_t TcpMaxConn;
	__uint64_t TcpActiveOpens;
	__uint64_t TcpPassiveOpens;
	__uint64_t TcpAttemptFails;
	__uint64_t TcpEstabResets;
	__uint64_t TcpCurrEstab;
	__uint64_t TcpInSegs;
	__uint64_t TcpOutSegs;
        __uint64_t TcpSlowOutSegs;
	__uint64_t TcpRetransSegs;
	__uint64_t TcpInErrs;
	__uint64_t TcpOutRsts;
	__uint64_t TcpOutCollapsed;
	__uint64_t TcpTimeWaitConns;
} nx_tcp_mib_t;

typedef struct {
	__uint64_t L2RxBytes;
	__uint64_t L2TxBytes;
} nx_l2_mib_t;

typedef struct {
	__uint64_t PREALIGN(512) SyncookiesSent POSTALIGN(512);
	__uint64_t SyncookiesRecv;
	__uint64_t SyncookiesFailed;
	__uint64_t EmbryonicRsts;
	__uint64_t PruneCalled;
	__uint64_t RcvPruned;
	__uint64_t OfoPruned;
	__uint64_t OutOfWindowIcmps;
	__uint64_t LockDroppedIcmps;
	__uint64_t ArpFilter;
	__uint64_t TimeWaited;
	__uint64_t TimeWaitRecycled;
	__uint64_t TimeWaitKilled;
	__uint64_t PAWSPassiveRejected;
	__uint64_t PAWSActiveRejected;
	__uint64_t PAWSEstabRejected;
	__uint64_t DelayedACKs;
	__uint64_t DelayedACKLocked;
	__uint64_t DelayedACKLost;
	__uint64_t ListenOverflows;
	__uint64_t ListenDrops;
	__uint64_t TCPPrequeued;
	__uint64_t TCPDirectCopyFromBacklog;
	__uint64_t TCPDirectCopyFromPrequeue;
	__uint64_t TCPPrequeueDropped;
	__uint64_t TCPHPHits;
	__uint64_t TCPPureAcks;
	__uint64_t TCPHPAcks;
	__uint64_t TCPRenoRecovery;
	__uint64_t TCPSackRecovery;
	__uint64_t TCPSACKReneging;
	__uint64_t TCPFACKReorder;
	__uint64_t TCPSACKReorder;
	__uint64_t TCPRenoReorder;
	__uint64_t TCPTSReorder;
	__uint64_t TCPFullUndo;
	__uint64_t TCPPartialUndo;
	__uint64_t TCPDSACKUndo;
	__uint64_t TCPLossUndo;
	__uint64_t TCPLoss;
	__uint64_t TCPLostRetransmit;
	__uint64_t TCPRenoFailures;
	__uint64_t TCPSackFailures;
	__uint64_t TCPLossFailures;
	__uint64_t TCPFastRetrans;
	__uint64_t TCPForwardRetrans;
	__uint64_t TCPSlowStartRetrans;
	__uint64_t TCPTimeouts;
	__uint64_t TCPRenoRecoveryFail;
	__uint64_t TCPSackRecoveryFail;
	__uint64_t TCPSchedulerFailed;
	__uint64_t TCPRcvCollapsed;
	__uint64_t TCPDSACKOldSent;
	__uint64_t TCPDSACKOfoSent;
	__uint64_t TCPDSACKRecv;
	__uint64_t TCPDSACKOfoRecv;
	__uint64_t TCPAbortOnSyn;
	__uint64_t TCPAbortOnData;
	__uint64_t TCPAbortOnClose;
	__uint64_t TCPAbortOnMemory;
	__uint64_t TCPAbortOnTimeout;
	__uint64_t TCPAbortOnLinger;
	__uint64_t TCPAbortFailed;
	__uint64_t TCPMemoryPressures;
} nx_misc_mib_t;


typedef struct {
	nx_ip_mib_t	ip_statistics;
	nx_tcp_mib_t	tcp_statistics;
	nx_l2_mib_t	l2_statistics;
/*
	icmp_mib_t		icmp_stats[NR_PORTS];
*/
} netxen_port_stats_t;

typedef struct {
	netxen_port_stats_t	PREALIGN(512) port[NETXEN_NUM_PORTS]	\
						POSTALIGN(512);
} netxen_peg_stats_t;

typedef struct {
        netxen_peg_stats_t      peg[NETXEN_NUM_PEGS];
} netxen_pstats_t;

#endif /* _UNM_PSTATS_H_ */

