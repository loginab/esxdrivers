/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2006 Nuova Systems, Inc.  All rights reserved.
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
 *
 * $Id: openfc_scsi_pkt.h 18557 2008-09-14 22:36:38Z jre $
 */
#ifndef _OPENFC_SCSI_PKT_H_
#define _OPENFC_SCSI_PKT_H_

#include "fc_fcp.h"
#include "fc_frame.h"
#include "fc_exch.h"

struct openfc_softc;

/*
 * SRB state definitions
 */
#define OPENFC_SRB_FREE		0		/* cmd is free */
#define OPENFC_SRB_CMD_SENT	(1 << 0)	/* cmd is sent */
#define OPENFC_SRB_RCV_STATUS	(1 << 1)	/* response recvd or canceled */
#define OPENFC_SRB_ABORT_PENDING (1 << 2)	/* cmd abort sent to device */
#define OPENFC_SRB_ABORTED	(1 << 3)	/* abort acknowledged */
#define OPENFC_SRB_DISCONTIG	(1 << 4)	/* non-sequential data recvd */

#define OPENFC_SRB_READ		   (1 << 1)
#define OPENFC_SRB_WRITE	   (1 << 0)

/*
 * command status code
 */
#define OPENFC_COMPLETE		    0
#define OPENFC_CMD_ABORTED	    1
#define OPENFC_CMD_RESET	    2
#define OPENFC_CMD_PLOGO	    3
#define OPENFC_SNS_RCV		    4
#define OPENFC_TRANS_ERR	    5
#define OPENFC_DATA_OVRRUN	    6
#define OPENFC_DATA_UNDRUN	    7
#define OPENFC_ERROR		    8
#define OPENFC_HRD_ERROR	    9
#define OPENFC_CMD_TIME_OUT	    10

#define OPENFC_MAX_CMD_SIZE	    16

/*
 * openfc scsi request structure,
 * it is one for each scsi request
 */

struct fc_scsi_pkt {
	/*
	 * housekeeping stuff
	 */
	struct openfc_softc *openfcp;	/* handle to hba struct */
	uint16_t	state;		/* scsi_pkt state state */
	uint16_t	tgt_flags;	/* target flags  */
	atomic_t	ref_cnt;        /* only used byr REC ELS */
	uint32_t	idx;		/* host given value */
	/*
	 * SCSI I/O related stuff 
	 */
	unsigned int	id;
	uint64_t	lun;
	void		(*done) (struct fc_scsi_pkt *);
	struct scsi_cmnd *cmd;		/* scsi command pointer */
	/*
	 * timeout related stuff
	 */
	struct timer_list timer;	/* command timer */
	struct completion tm_done;
	int	wait_for_comp;
	unsigned long	start_time;	/* start jiffie */
	unsigned long	end_time;	/* end jiffie */
	unsigned long	last_pkt_time;   /* jiffies of last frame received */
	/*
	 * scsi cmd and data transfer information
	 */
	uint32_t	data_len;
	/*
	 * transport related veriables
	 */
	struct fcp_cmnd cdb_cmd;
	size_t		xfer_len;
	size_t		cmd_len;
	uint32_t	xfer_contig_end; /* offset of end of contiguous xfer */
	/*
	 * scsi/fcp return status
	 */
	uint32_t	io_status;	/* SCSI result upper 24 bits */
	uint8_t		cdb_status;
	uint8_t		status_code;	/* OPENFC I/O status */
	/* bit 3 Underrun bit 2: overrun */
	uint8_t		scsi_comp_flags;
	uint32_t	req_flags;	/* bit 0: read bit:1 write */
	uint32_t	scsi_resid;	/* residule length */
	/*
	 * FCS related data struct
	 */
	struct fc_remote_port *rp;	/* remote port pointer */
	struct fc_seq  *seq_ptr;	/* current sequence pointer */
	struct os_lun  *disk;		/* ptr to the Lun structure */
	/*
	 * Error Processing
	 */
	u_int8_t	recov_retry;	/* count of recovery retries */
	uint16_t	old_state;		/* scsi_pkt state state */
	struct fc_scsi_pkt *old_pkt;	
	uint32_t        d_id;           /* remote DID */
	unsigned int sense_len;
	void 	*private;		        /* ptr to transport data */
};
#endif /* _OPENFC_SCSI_PKT_H_ */
