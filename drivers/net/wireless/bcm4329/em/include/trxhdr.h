/*
 * TRX image file header format.
 *
 * Copyright (C) 2010, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: trxhdr.h,v 13.11.310.1 2008/08/17 12:58:58 Exp $
 */

#include <typedefs.h>

#define TRX_MAGIC	0x30524448	
#define TRX_VERSION	1		
#define TRX_MAX_LEN	0x3A0000	
#define TRX_NO_HEADER	1		
#define TRX_GZ_FILES	0x2     
#define TRX_MAX_OFFSET	3		
#define TRX_UNCOMP_IMAGE	0x20	

struct trx_header {
	uint32 magic;		
	uint32 len;		
	uint32 crc32;		
	uint32 flag_version;	
	uint32 offsets[TRX_MAX_OFFSET];	
};


typedef struct trx_header TRXHDR, *PTRXHDR;
