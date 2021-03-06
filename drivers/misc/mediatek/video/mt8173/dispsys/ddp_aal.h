/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#ifndef __DDP_AAL_H__
#define __DDP_AAL_H__

#define AAL_HIST_BIN        33	/* [0..32] */
#define AAL_DRE_POINT_NUM   29

#define AAL_SERVICE_FORCE_UPDATE 0x1

typedef struct {
	/* DRE */
	int dre_map_bypass;
	/* CABC */
	int cabc_gainlmt[33];
} DISP_AAL_INITREG;

typedef struct {
	unsigned int serviceFlags;
	int backlight;
	int colorHist;
	int fps;
	unsigned int maxHist[AAL_HIST_BIN];
} DISP_AAL_HIST;

typedef struct {
	int DREGainFltStatus[AAL_DRE_POINT_NUM];
	int cabc_fltgain_force;	/* 10-bit ; [0,1023] */
	int cabc_gainlmt[33];
	int FinalBacklight;	/* 10-bit ; [0,1023] */
} DISP_AAL_PARAM;


void disp_aal_on_end_of_frame(void);

extern int aal_dbg_en;
void aal_test(const char *cmd, char *debug_output);
int disp_aal_write_init_regs(void *cmdq);
void disp_aal_notify_backlight_changed(int bl_1024);

#endif
