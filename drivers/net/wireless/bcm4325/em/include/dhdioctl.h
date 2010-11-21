/*
 * Definitions for ioctls to access DHD iovars.
 * Based on wlioctl.h (for Broadcom 802.11abg driver).
 * (Moves towards generic ioctls for BCM drivers/iovars.)
 *
 * Definitions subject to change without notice.
 *
 * Copyright (C) 2009, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: dhdioctl.h,v 13.7.8.1.4.1.30.2 2008/11/13 17:33:39 Exp $
 */

#ifndef _dhdioctl_h_
#define	_dhdioctl_h_

#include <typedefs.h>

#ifdef __NetBSD__

#define SIOCDEVPRIVATE	_IOWR('i', 139, struct ifreq)
#endif


#define BWL_DEFAULT_PACKING
#include <packed_section_start.h>



typedef struct dhd_ioctl {
	uint cmd;	
	void *buf;	
	uint len;	
	bool set;	
	uint used;	
	uint needed;	
	uint driver;	
} dhd_ioctl_t;


#define DHD_IOCTL_MAGIC		0x00444944


#define DHD_IOCTL_VERSION	1

#define	DHD_IOCTL_MAXLEN	8192		
#define	DHD_IOCTL_SMLEN		256		


#define DHD_GET_MAGIC				0
#define DHD_GET_VERSION				1
#define DHD_GET_VAR				2
#define DHD_SET_VAR				3


#define DHD_ERROR_VAL	0x0001
#define DHD_TRACE_VAL	0x0002
#define DHD_INFO_VAL	0x0004
#define DHD_DATA_VAL	0x0008
#define DHD_CTL_VAL	0x0010
#define DHD_TIMER_VAL	0x0020
#define DHD_HDRS_VAL	0x0040
#define DHD_BYTES_VAL	0x0080
#define DHD_INTR_VAL	0x0100
#define DHD_LOG_VAL	0x0200
#define DHD_GLOM_VAL	0x0400
#define DHD_EVENT_VAL	0x0800
#define DHD_BTA_VAL	0x1000

#ifdef SDTEST

typedef struct dhd_pktgen {
	uint version;		
	uint freq;		
	uint count;		
	uint print;		
	uint total;		
	uint minlen;		
	uint maxlen;		
	uint numsent;		
	uint numrcvd;		
	uint numfail;		
	uint mode;		
	uint stop;		
} dhd_pktgen_t;


#define DHD_PKTGEN_VERSION 2


#define DHD_PKTGEN_ECHO		1 
#define DHD_PKTGEN_SEND 	2 
#define DHD_PKTGEN_RXBURST	3 
#define DHD_PKTGEN_RECV		4 
#endif 


#define DHD_IDLE_IMMEDIATE	(-1)


#define DHD_IDLE_ACTIVE	0	
#define DHD_IDLE_STOP   (-1)	



#include <packed_section_end.h>

#endif 
