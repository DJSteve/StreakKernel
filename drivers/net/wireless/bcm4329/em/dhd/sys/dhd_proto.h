/*
 * Header file describing the internal (inter-module) DHD interfaces.
 *
 * Provides type definitions and function prototypes used to link the
 * DHD OS, bus, and protocol modules.
 *
 * Copyright (C) 2010, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: dhd_proto.h,v 1.2.82.1.4.1.16.7 2010/05/10 12:54:59 Exp $
 */

#ifndef _dhd_proto_h_
#define _dhd_proto_h_

#include <dhdioctl.h>
#include <wlioctl.h>

#ifndef IOCTL_RESP_TIMEOUT
#define IOCTL_RESP_TIMEOUT  2000 
#endif

#ifndef IOCTL_CHIP_ACTIVE_TIMEOUT
#define IOCTL_CHIP_ACTIVE_TIMEOUT  10 
#endif




extern int dhd_prot_attach(dhd_pub_t *dhdp);


extern void dhd_prot_detach(dhd_pub_t *dhdp);


extern int dhd_prot_init(dhd_pub_t *dhdp);


extern void dhd_prot_stop(dhd_pub_t *dhdp);

extern bool dhd_proto_fcinfo(dhd_pub_t *dhd, void *pktbuf, uint8 *fcbits);


extern void dhd_prot_hdrpush(dhd_pub_t *, int ifidx, void *txp);


extern int dhd_prot_hdrpull(dhd_pub_t *, int *ifidx, void *rxp);


extern int dhd_prot_ioctl(dhd_pub_t *dhd, int ifidx, wl_ioctl_t * ioc, void * buf, int len);


extern int dhd_prot_iovar_op(dhd_pub_t *dhdp, const char *name,
                             void *params, int plen, void *arg, int len, bool set);


extern void dhd_prot_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf);


extern void dhd_prot_dstats(dhd_pub_t *dhdp);

extern int dhd_ioctl(dhd_pub_t * dhd_pub, dhd_ioctl_t *ioc, void * buf, uint buflen);

extern int dhd_preinit_ioctls(dhd_pub_t *dhd);


#if defined(BDC)
#define DHD_PROTOCOL "bdc"
#elif defined(CDC)
#define DHD_PROTOCOL "cdc"
#elif defined(RNDIS)
#define DHD_PROTOCOL "rndis"
#else
#define DHD_PROTOCOL "unknown"
#endif 

#endif 
