/*
 * Definitions for API from sdio common code (bcmsdh) to individual
 * host controller drivers.
 *
 * Copyright (C) 2010, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: bcmsdbus.h,v 13.11.14.2.6.6 2009/10/27 17:20:28 Exp $
 */

#ifndef	_sdio_api_h_
#define	_sdio_api_h_


#define SDIOH_API_RC_SUCCESS                          (0x00)
#define SDIOH_API_RC_FAIL	                      (0x01)
#define SDIOH_API_SUCCESS(status) (status == 0)

#define SDIOH_READ              0	
#define SDIOH_WRITE             1	

#define SDIOH_DATA_FIX          0	
#define SDIOH_DATA_INC          1	

#define SDIOH_CMD_TYPE_NORMAL   0       
#define SDIOH_CMD_TYPE_APPEND   1       
#define SDIOH_CMD_TYPE_CUTTHRU  2       

#define SDIOH_DATA_PIO          0       
#define SDIOH_DATA_DMA          1       


typedef int SDIOH_API_RC;


typedef struct sdioh_info sdioh_info_t;


typedef void (*sdioh_cb_fn_t)(void *);


extern sdioh_info_t * sdioh_attach(osl_t *osh, void *cfghdl, uint irq);
extern SDIOH_API_RC sdioh_detach(osl_t *osh, sdioh_info_t *si);
extern SDIOH_API_RC sdioh_interrupt_register(sdioh_info_t *si, sdioh_cb_fn_t fn, void *argh);
extern SDIOH_API_RC sdioh_interrupt_deregister(sdioh_info_t *si);


extern SDIOH_API_RC sdioh_interrupt_query(sdioh_info_t *si, bool *onoff);


extern SDIOH_API_RC sdioh_interrupt_set(sdioh_info_t *si, bool enable_disable);

#if defined(DHD_DEBUG) || defined(BCMDBG)
extern bool sdioh_interrupt_pending(sdioh_info_t *si);
#endif


extern SDIOH_API_RC sdioh_request_byte(sdioh_info_t *si, uint rw, uint fnc, uint addr, uint8 *byte);


extern SDIOH_API_RC sdioh_request_word(sdioh_info_t *si, uint cmd_type, uint rw, uint fnc,
	uint addr, uint32 *word, uint nbyte);


extern SDIOH_API_RC sdioh_request_buffer(sdioh_info_t *si, uint pio_dma, uint fix_inc,
	uint rw, uint fnc_num, uint32 addr, uint regwidth, uint32 buflen, uint8 *buffer,
	void *pkt);


extern SDIOH_API_RC sdioh_cis_read(sdioh_info_t *si, uint fuc, uint8 *cis, uint32 length);

extern SDIOH_API_RC sdioh_cfg_read(sdioh_info_t *si, uint fuc, uint32 addr, uint8 *data);
extern SDIOH_API_RC sdioh_cfg_write(sdioh_info_t *si, uint fuc, uint32 addr, uint8 *data);


extern uint sdioh_query_iofnum(sdioh_info_t *si);


extern int sdioh_iovar_op(sdioh_info_t *si, const char *name,
                          void *params, int plen, void *arg, int len, bool set);


extern int sdioh_abort(sdioh_info_t *si, uint fnc);


extern int sdioh_start(sdioh_info_t *si, int stage);
extern int sdioh_stop(sdioh_info_t *si);


extern int sdioh_sdio_reset(sdioh_info_t *si);


void *bcmsdh_get_sdioh(bcmsdh_info_t *sdh);


#ifdef BCMSPI

extern uint32 sdioh_get_dstatus(sdioh_info_t *si);


extern void sdioh_chipinfo(sdioh_info_t *si, uint32 chip, uint32 chiprev);
extern void sdioh_dwordmode(sdioh_info_t *si, bool set);
#endif 

#endif 
