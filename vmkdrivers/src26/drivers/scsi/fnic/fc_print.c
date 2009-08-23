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
 * $Id: fc_print.c 18557 2008-09-14 22:36:38Z jre $
 */

/*
 * Debugging support for logging fibre channel frame headers, etc.
 */

#include "sa_kernel.h"
#include "sa_assert.h"
#include "net_types.h"
#include "sa_log.h"
#include "fc_fs.h"
#include "fc_ils.h"
#include "fc_els.h"
#include "fc_fcp.h"
#include "fc_scsi.h"
#include "fc_types.h"
#include "fc_print.h"
#include "fc_frame.h"


/*
 * Debugging tunable variables.
 * This might be set using the debugger or by editing this file.
 */
static int fc_print_verbose = 1;
static int fc_print_hex = 0;

static const char *fc_print_rctls[] = FC_RCTL_NAMES_INIT;
static const char *fc_print_types[] = FC_TYPE_NAMES_INIT;
static const char *fc_print_ils_cmds[] = FC_ILS_CMDS_INIT;
static const char *fc_print_els_cmds[] = FC_ELS_CMDS_INIT;
static const char *fc_print_scsi_cmds[] = SCSI_OP_NAME_INIT;

static void fc_print_lookup(u_int value, char *buf, size_t len,
		const char *table[], size_t size)
{
	u_int entries = size / sizeof(char *);

	if (value >= entries || !table[value])
		snprintf(buf, len, "%x", value);
	else
		snprintf(buf, len, "%s", table[value]);
}


/*
 * Handle fabric manager exchanges.
 */
void fc_print_frame_hdr(const char *msg, const struct fc_frame *fp)
{
	struct fc_frame_header *fh = fc_frame_header_get(fp);
	u_char *op;
	size_t len = fp->fr_len;
	struct fcp_cmnd *fcp;
	char r_ctl[20];
	char type[20];
	char opcode[20];

	op = (u_char *) (fh + 1);

	if (len < sizeof(*fh)) {
		sa_log("dump_frame: %s: short frame: len 0x%lx\n", msg, len);
	} else if (fc_print_verbose) {
		fc_print_lookup(fh->fh_r_ctl, r_ctl, sizeof(r_ctl),
				fc_print_rctls, sizeof(fc_print_rctls));
		fc_print_lookup(fh->fh_type, type, sizeof(type),
				fc_print_types, sizeof(fc_print_types));
		switch (fh->fh_type) {
		case FC_TYPE_ELS:
			fc_print_lookup(op[0], opcode, sizeof(opcode),
					fc_print_els_cmds,
					sizeof(fc_print_els_cmds));
			break;
		case FC_TYPE_ILS:
			fc_print_lookup(op[0], opcode, sizeof(opcode),
					fc_print_ils_cmds,
					sizeof(fc_print_ils_cmds));
			break;
		case FC_TYPE_FCP:
			opcode[0] = '\0';
			if (fh->fh_r_ctl == FC_RCTL_DD_UNSOL_CMD &&
					(fcp = fc_frame_payload_get(fp,
						sizeof(*fcp))) != NULL)
				fc_print_lookup(fcp->fc_cdb[0], opcode,
						sizeof(opcode),
						fc_print_scsi_cmds,
						sizeof
						(fc_print_scsi_cmds));
			break;
		default:
			snprintf(opcode, sizeof(opcode), "op %x", op[0]);
			break;
		}
		sa_log("%-15s %6.6x -> %6.6x xids %4.4x %4.4x %s %s %s",
			msg, net24_get(&fh->fh_s_id),
			net24_get(&fh->fh_d_id), net16_get(&fh->fh_ox_id),
			net16_get(&fh->fh_rx_id), r_ctl, type, opcode);
	} else if (fc_print_hex) {
		sa_log("%s frame: %zd (%zx) bytes sof %x eof %x\n",
			msg, len, len, fp->fr_sof, fp->fr_eof);
		sa_log("    r_ctl %2x  d_id  %6x  cs_ctl %2x  s_id %6x\n",
			fh->fh_r_ctl, net24_get(&fh->fh_d_id),
			fh->fh_cs_ctl, net24_get(&fh->fh_s_id));
		sa_log("    type  %2x  f_ctl %6x  seq_id %2x  "
			"seq_cnt %4x  df_ctl %2x\n",
			fh->fh_type, net24_get(&fh->fh_f_ctl),
			fh->fh_seq_id, net16_get(&fh->fh_seq_cnt),
			fh->fh_df_ctl);
		sa_log("    ox_id %4x  rx_id %4x  parm_offset %x  "
			"op %2.2x %2.2x %2.2x %2.2x\n",
			net16_get(&fh->fh_ox_id), net16_get(&fh->fh_rx_id),
			net32_get(&fh->fh_parm_offset), op[0], op[1], op[2],
			op[3]);
	} else {
		sa_log("%s frame: \n"
			"    %6.6x -> %6.6x ox %4.4x rx %4.4x "
			"r_ctl %2.2x type %2.2x op %2.2x %2.2x %2.2x %2.2x\n",
			msg, net24_get(&fh->fh_s_id),
			net24_get(&fh->fh_d_id), net16_get(&fh->fh_ox_id),
			net16_get(&fh->fh_rx_id), fh->fh_r_ctl, fh->fh_type,
			op[0], op[1], op[2], op[3]);
	}
}
EXPORT_SYMBOL(fc_print_frame_hdr);
