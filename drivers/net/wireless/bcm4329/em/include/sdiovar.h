/*
 * Structure used by apps whose drivers access SDIO drivers.
 * Pulled out separately so dhdu and wlu can both use it.
 *
 * Copyright (C) 2010, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: sdiovar.h,v 13.5.14.2.16.2 2009/12/08 22:34:21 Exp $
 */

#ifndef _sdiovar_h_
#define _sdiovar_h_

#include <typedefs.h>


#define BWL_DEFAULT_PACKING
#include <packed_section_start.h>

typedef struct sdreg {
	int func;
	int offset;
	int value;
} sdreg_t;


#define SDH_ERROR_VAL		0x0001	
#define SDH_TRACE_VAL		0x0002	
#define SDH_INFO_VAL		0x0004	
#define SDH_DEBUG_VAL		0x0008	
#define SDH_DATA_VAL		0x0010	
#define SDH_CTRL_VAL		0x0020	
#define SDH_LOG_VAL		0x0040	
#define SDH_DMA_VAL		0x0080	

#define NUM_PREV_TRANSACTIONS	16

#ifdef BCMSPI

struct spierrstats_t {
	uint32  dna;	
	uint32  rdunderflow;	
	uint32  wroverflow;	

	uint32  f2interrupt;	
	uint32  f3interrupt;	

	uint32  f2rxnotready;	
	uint32  f3rxnotready;	

	uint32  hostcmddataerr;	
	uint32  f2pktavailable;	
	uint32  f3pktavailable;	

	uint32	dstatus[NUM_PREV_TRANSACTIONS];	
	uint32  spicmd[NUM_PREV_TRANSACTIONS];
};
#endif 

#include <packed_section_end.h>

#endif 
