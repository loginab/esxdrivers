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
#ifndef _driver_info_h_
#define _driver_info_h_
#define DRIVER_NAME            "nx_nic"
#define TOE_DRIVER_NAME        "nx_lsa"
#define DRIVER_VERSION_STRING  "NetXen Driver version "

#include "unm_brdcfg.h"

static const unm_brdinfo_t unm_boards[] = {
	{UNM_BRDTYPE_P2_SB31_10G_CX4,  1, NX_P2_MN_TYPE_ROMIMAGE,
			"XGb CX4"},
	{UNM_BRDTYPE_P2_SB31_10G_HMEZ, 2, NX_P2_MN_TYPE_ROMIMAGE,
			"XGb HMEZ"},
	{UNM_BRDTYPE_P2_SB31_10G_IMEZ, 2, NX_P2_MN_TYPE_ROMIMAGE,
			"XGb IMEZ"},
	{UNM_BRDTYPE_P2_SB31_10G,      1, NX_P2_MN_TYPE_ROMIMAGE,
			"XGb XFP"},
	{UNM_BRDTYPE_P2_SB35_4G,       4, NX_P2_MN_TYPE_ROMIMAGE,
			"Quad Gb"},
	{UNM_BRDTYPE_P2_SB31_2G,       2, NX_P2_MN_TYPE_ROMIMAGE,
			"Dual Gb"},
	{UNM_BRDTYPE_P3_REF_QG,        4, NX_P3_MN_TYPE_ROMIMAGE,
			"Reference card - Quad Gig "},
	{UNM_BRDTYPE_P3_HMEZ,          2, NX_P3_MN_TYPE_ROMIMAGE,
			"Dual XGb HMEZ"},
	{UNM_BRDTYPE_P3_10G_CX4_LP,    2, NX_P3_MN_TYPE_ROMIMAGE,
			"Dual XGb CX4 LP"},
	{UNM_BRDTYPE_P3_4_GB,          4, NX_P3_MN_TYPE_ROMIMAGE,
			"Quad Gig LP"},
	{UNM_BRDTYPE_P3_IMEZ,          2, NX_P3_MN_TYPE_ROMIMAGE,
			"Dual XGb IMEZ"},
	{UNM_BRDTYPE_P3_10G_SFP_PLUS,  2, NX_P3_MN_TYPE_ROMIMAGE,
			"Dual XGb SFP+ LP"},
	{UNM_BRDTYPE_P3_10000_BASE_T,  1, NX_P3_MN_TYPE_ROMIMAGE,
			"XGB 10G BaseT LP"},
	{UNM_BRDTYPE_P3_XG_LOM,        2, NX_P3_MN_TYPE_ROMIMAGE,
			"Dual XGb LOM"},
	{UNM_BRDTYPE_P3_4_GB_MM,       4, NX_P3_MN_TYPE_ROMIMAGE,
	"HP NC375i Integrated Quad Port Multifunction Gigabit Server Adapter "},
	{UNM_BRDTYPE_P3_10G_CX4,       2, NX_P3_MN_TYPE_ROMIMAGE,
			"Reference card - Dual CX4 Option"},
	{UNM_BRDTYPE_P3_10G_XFP,       1, NX_P3_MN_TYPE_ROMIMAGE,
			"Reference card - Single XFP Option"},
	{UNM_BRDTYPE_P3_10G_TROOPER,   2, NX_P3_MN_TYPE_ROMIMAGE,
			"HP NC375i 1G w/NC524SFP 10G Module"},
};

#define DRIVER_AUTHOR		"Copyright (C) 2003 - 2008 NetXen, Inc."
#define DRIVER_DESCRIPTION	"NetXen Multi port (1/10) Gigabit Network Driver"
#define LSA_DRIVER_DESCRIPTION	"NetXen LSA Driver"
#define LSA_DRIVER_LICENSE	"NetXen Proprietary"

#endif
