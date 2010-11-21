/*
 * Header file describing the internal (inter-module) DHD interfaces.
 *
 * Provides type definitions and function prototypes used to link the
 * DHD OS, bus, and protocol modules.
 *
 * Copyright (C) 2009, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: dhd_proto.h,v 1.2.82.1.4.1.24.4 2009/06/11 19:20:04 Exp $
 */

#ifndef _dhd_proto_h_
#define _dhd_proto_h_

#include <dhdioctl.h>
#include <wlioctl.h>

#ifndef IOCTL_RESP_TIMEOUT
#define IOCTL_RESP_TIMEOUT  1000 
#endif




extern int dhd_prot_attach(dhd_pub_t *dhdp);


extern void dhd_prot_detach(dhd_pub_t *dhdp);


extern int dhd_prot_init(dhd_pub_t *dhdp);


extern void dhd_prot_stop(dhd_pub_t *dhdp);


extern void dhd_prot_hdrpush(dhd_pub_t *, int ifidx, void *txp);


extern int dhd_prot_hdrpull(dhd_pub_t *, uint *ifidx, void *rxp);


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
