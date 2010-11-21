/*
 * Common stats definitions for clients of dongle
 * ports
 *
 * Copyright (C) 2010, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: dngl_stats.h,v 1.2.140.3 2008/05/26 16:52:08 Exp $
 */

#ifndef _dngl_stats_h_
#define _dngl_stats_h_

typedef struct {
	unsigned long	rx_packets;		
	unsigned long	tx_packets;		
	unsigned long	rx_bytes;		
	unsigned long	tx_bytes;		
	unsigned long	rx_errors;		
	unsigned long	tx_errors;		
	unsigned long	rx_dropped;		
	unsigned long	tx_dropped;		
	unsigned long   multicast;      
} dngl_stats_t;

#endif 
