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
 * $Id: fc_scsi_lun.h 20931 2008-11-18 23:06:58Z herman $
 */

#ifndef _LIBFC_SCSI_LUN_H_
#define _LIBFC_SCSI_LUN_H_

/*
 * SCSI utility interface.
 * This includes per-LUN state and CDB interfaces to
 * exchange commmands with a LUN.
 */

#include <linux/types.h>
#include "sa_event.h"
#include "sa_hash.h"
#include "queue.h"
#include "fc_sess.h"
#include "fc_fcp.h"
#include "fc_scsi.h"

struct fc_virt_fab;

#define FC_LUN_PATHS    8	/* max paths per LUN */

typedef unsigned long long fc_lun_id_t;	/* SCSI logical unit ID */

/*
 * LUN state management.
 */
enum fc_lun_state {
	LUN_INVALID = 0,	/* invalid state */
	LUN_RESET,		/* starting state */
	LUN_REPORT_LUNS,	/* report other LUNs on target */
	LUN_INQUIRY,		/* get device information */
	LUN_START,		/* start request */
	LUN_TEST_READY,		/* test unit ready */
	LUN_READ_CAP,		/* read capacity */
	LUN_READY,		/* normal idle state */
	LUN_EMPTY,		/* no device attached */
};

/*
 * Hash lookup key for fc_lun_path.
 */
struct fc_lun_path_key {
	fc_fid_t	lpk_fid;	/* target fibre channel ID */
	fc_lun_id_t	lpk_lun;	/* 64-bit LUN ID */
};

/*
 * SCSI logical unit path.
 */
struct fc_lun_path {
	struct fc_sess	*lp_sess;	/* session */
	struct fc_lun	*lp_lun;	/* LUN structure (held) */
	fc_lun_id_t	lp_lun_id;	/* LUN ID for this path */
	struct sa_hash_link lp_hash_link; /* hash table linkage */
	TAILQ_ENTRY(fc_lun_path) lp_link; /* list linkage in fc_virt_fab */
};

/*
 * SCSI logical unit.
 */
struct fc_lun {
	enum fc_lun_state lun_state;
	u_int		lun_retries;	/* retries count for current cmd */
	TAILQ_ENTRY(fc_lun) lun_list;	/* linkage for discovery client */
	void		*lun_client_priv; /* private for upper level */
	struct fc_lun_path *lun_path[FC_LUN_PATHS]; /* paths to the LU */
	fc_wwn_t	lun_wwlid;	/* world-wide unique LUN ID */
	struct sa_hash_link lun_hash_link; /* hash table linkage */
	uint		lun_cdb_count;	/* CDBs allocated */
	uint		lun_refcnt;	/* reference count */
	struct sa_event_list *lun_events; /* event list */
	struct fc_virt_fab *lun_vf;	/* virtual fabric */

	/*
	 * Inquiry data.
	 */
	uint16_t	lun_inq_valid;	/* mask for completed inquiries */
	uint16_t	lun_inq_pending; /* mask for pending inquiry */
	uint16_t	lun_inq_support; /* mask for supported inquiries */
	uint16_t	lun_flags;	/* flags for fc_lun.c */
	struct scsi_inquiry_std lun_inq_std;	/* standard inquiry data */
	char lun_serial[40];		/* serial number from page 0x80 */

	/*
	 * Capacity.
	 */
	u_int64_t lun_capacity;		/* capacity in blocks */
	u_int32_t lun_block_len;	/* block length in bytes */
};

/*
 * lun_flags.
 */
#define	FC_LUNF_TRACE	1		/* trace state transitions */

/*
 * SCSI command descriptor block.
 */
struct fc_cdb {
	struct fc_cdb	*cdb_next;		/* next CDB on list */
	struct fc_lun	*cdb_lun;
	void		(*cdb_handler) (struct fc_cdb *); /* completion func */
	void		*cdb_handler_data;	/* data for completion func */
	void		*cdb_data;		/* data buffer */
	size_t		cdb_len;		/* data length */
	size_t		cdb_xfer_len;		/* transferred length */
	size_t		cdb_resid;		/* residual length */
	u_int		cdb_retries;		/* number of times retried */
	u_char		cdb_status;		/* SCSI status */
	struct fcp_cmnd cdb_cmd;
	char		cdb_dbuf[256 - 4];	/* short data buffer */
};

/*
 * Get the base CDB inside the fc_cdb.
 */
static inline void *fc_cdb_cmd(struct fc_cdb *cdb)
{
	return (void *) cdb->cdb_cmd.fc_cdb;
}

/*
 * Allocate an fc_cdb for a LUN.
 */
struct fc_cdb *fc_scsi_cdb_alloc(struct fc_lun *);

/*
 * Free an fc_cdb.
 */
void fc_scsi_cdb_free(struct fc_cdb *);

/*
 * Send a SCSI command.
 */
void fc_scsi_send(struct fc_cdb *, u_int rw_flags,
		  void (*handler) (struct fc_cdb *), void *buf,
		  size_t len);

/*
 * Operations on LUNs and LUN paths.
 */
struct fc_lun_path *fc_lun_path_create(struct fc_sess *, fc_lun_id_t);
void fc_lun_path_delete(struct fc_lun_path *);

static inline fc_fid_t fc_lun_path_get_did(const struct fc_lun_path *path)
{
	return fc_sess_get_did(path->lp_sess);
}

static inline fc_fid_t fc_lun_path_get_sid(const struct fc_lun_path *path)
{
	return fc_sess_get_sid(path->lp_sess);
}

/*
 * LUN discovery calls back for each LUN at the session's target,
 * then calls back with a NULL LUN to indicate completion.
 */
int fc_lun_discover(struct fc_sess *,
		    void (*)(void *, struct fc_lun *), void *);

void fc_lun_event_enq(struct fc_lun *, sa_event_handler_t *, void *);
void fc_lun_event_deq(struct fc_lun *, sa_event_handler_t *, void *);

void fc_lun_hold(struct fc_lun *);
void fc_lun_release(struct fc_lun *);

/*
 * Lookup LUN by WWLID.
 */
struct fc_lun *fc_lun_lookup(struct fc_virt_fab *, fc_wwn_t wwlid);

void fc_lun_table_create(struct fc_virt_fab *);
void fc_lun_table_destroy(struct fc_virt_fab *);

/*
 * Get name of LUN for debug printfs, etc.
 */
char *fc_lun_get_name(const struct fc_lun *, char *buf, size_t);

#endif /* _LIBFC_SCSI_LUN_H_ */
