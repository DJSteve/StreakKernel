/*
 * Trace messages sent over HBUS
 *
 * Copyright (C) 2010, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: msgtrace.h,v 1.1.2.4 2009/01/27 04:09:40 Exp $
 */

#ifndef	_MSGTRACE_H
#define	_MSGTRACE_H

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif



#include <packed_section_start.h>

#define MSGTRACE_VERSION 1


typedef BWL_PRE_PACKED_STRUCT struct msgtrace_hdr {
	uint8	version;
	uint8   spare;
	uint16	len;	
	uint32	seqnum;	
	uint32  discarded_bytes;  
	uint32  discarded_printf; 
} BWL_POST_PACKED_STRUCT msgtrace_hdr_t;

#define MSGTRACE_HDRLEN 	sizeof(msgtrace_hdr_t)


extern bool msgtrace_hbus_trace;

typedef void (*msgtrace_func_send_t)(void *hdl1, void *hdl2, uint8 *hdr,
                                     uint16 hdrlen, uint8 *buf, uint16 buflen);

extern void msgtrace_sent(void);
extern void msgtrace_put(char *buf, int count);
extern void msgtrace_init(void *hdl1, void *hdl2, msgtrace_func_send_t func_send);


#include <packed_section_end.h>

#endif	
