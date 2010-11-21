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
 * $Id: dhd_bus.h,v 1.4.6.3.2.3.20.5 2009/04/29 04:40:15 Exp $
 */

#ifndef _dhd_bus_h_
#define _dhd_bus_h_




extern int dhd_bus_register(void);
extern void dhd_bus_unregister(void);


extern bool dhd_bus_download_firmware(struct dhd_bus *bus, osl_t *osh,
	char *fw_path, char *nv_path);


extern void dhd_bus_stop(struct dhd_bus *bus, bool enforce_mutex);


extern int dhd_bus_init(dhd_pub_t *dhdp, bool enforce_mutex);


extern int dhd_bus_txdata(struct dhd_bus *bus, void *txp);


extern int dhd_bus_txctl(struct dhd_bus *bus, uchar *msg, uint msglen);
extern int dhd_bus_rxctl(struct dhd_bus *bus, uchar *msg, uint msglen);


extern bool dhd_bus_watchdog(dhd_pub_t *dhd);


extern bool dhd_bus_dpc(struct dhd_bus *bus);
extern void dhd_bus_isr(bool * InterruptRecognized, bool * QueueMiniportHandleInterrupt, void *arg);



extern int dhd_bus_iovar_op(dhd_pub_t *dhdp, const char *name,
                            void *params, int plen, void *arg, int len, bool set);


extern void dhd_bus_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf);


extern void dhd_bus_clearcounts(dhd_pub_t *dhdp);


extern uint dhd_bus_chip(struct dhd_bus *bus);


extern void dhd_bus_set_nvram_params(struct dhd_bus * bus, const char *nvram_params);

extern void *dhd_bus_pub(struct dhd_bus *bus);
extern void *dhd_bus_txq(struct dhd_bus *bus);
extern uint dhd_bus_hdrlen(struct dhd_bus *bus);

#endif 
