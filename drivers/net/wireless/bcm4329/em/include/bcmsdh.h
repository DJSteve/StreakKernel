/*
 * SDIO host client driver interface of Broadcom HNBU
 *     export functions to client drivers
 *     abstract OS and BUS specific details of SDIO
 *
 * Copyright (C) 2010, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: bcmsdh.h,v 13.35.14.7.6.8 2009/10/14 04:22:25 Exp $
 */

#ifndef	_bcmsdh_h_
#define	_bcmsdh_h_

#define BCMSDH_ERROR_VAL	0x0001 
#define BCMSDH_INFO_VAL		0x0002 
extern const uint bcmsdh_msglevel;

#ifdef BCMDBG
#define BCMSDH_ERROR(x)	do { if (bcmsdh_msglevel & BCMSDH_ERROR_VAL) printf x; } while (0)
#define BCMSDH_INFO(x)	do { if (bcmsdh_msglevel & BCMSDH_INFO_VAL) printf x; } while (0)
#else 
#define BCMSDH_ERROR(x)
#define BCMSDH_INFO(x)
#endif 


typedef struct bcmsdh_info bcmsdh_info_t;
typedef void (*bcmsdh_cb_fn_t)(void *);


extern bcmsdh_info_t *bcmsdh_attach(osl_t *osh, void *cfghdl, void **regsva, uint irq);


extern int bcmsdh_detach(osl_t *osh, void *sdh);


extern bool bcmsdh_intr_query(void *sdh);


extern int bcmsdh_intr_enable(void *sdh);
extern int bcmsdh_intr_disable(void *sdh);


extern int bcmsdh_intr_reg(void *sdh, bcmsdh_cb_fn_t fn, void *argh);
extern int bcmsdh_intr_dereg(void *sdh);

#if defined(DHD_DEBUG) || defined(BCMDBG)

extern bool bcmsdh_intr_pending(void *sdh);
#endif

#ifdef BCMLXSDMMC
extern int bcmsdh_claim_host_and_lock(void *sdh);
extern int bcmsdh_release_host_and_unlock(void *sdh);
#endif 


extern int bcmsdh_devremove_reg(void *sdh, bcmsdh_cb_fn_t fn, void *argh);


extern uint8 bcmsdh_cfg_read(void *sdh, uint func, uint32 addr, int *err);
extern void bcmsdh_cfg_write(void *sdh, uint func, uint32 addr, uint8 data, int *err);


extern uint32 bcmsdh_cfg_read_word(void *sdh, uint fnc_num, uint32 addr, int *err);
extern void bcmsdh_cfg_write_word(void *sdh, uint fnc_num, uint32 addr, uint32 data, int *err);


extern int bcmsdh_cis_read(void *sdh, uint func, uint8 *cis, uint length);


extern uint32 bcmsdh_reg_read(void *sdh, uint32 addr, uint size);
extern uint32 bcmsdh_reg_write(void *sdh, uint32 addr, uint size, uint32 data);


extern bool bcmsdh_regfail(void *sdh);


typedef void (*bcmsdh_cmplt_fn_t)(void *handle, int status, bool sync_waiting);
extern int bcmsdh_send_buf(void *sdh, uint32 addr, uint fn, uint flags,
                           uint8 *buf, uint nbytes, void *pkt,
                           bcmsdh_cmplt_fn_t complete, void *handle);
extern int bcmsdh_recv_buf(void *sdh, uint32 addr, uint fn, uint flags,
                           uint8 *buf, uint nbytes, void *pkt,
                           bcmsdh_cmplt_fn_t complete, void *handle);


#define SDIO_REQ_4BYTE	0x1	
#define SDIO_REQ_FIXED	0x2	
#define SDIO_REQ_ASYNC	0x4	


#define BCME_PENDING	1


extern int bcmsdh_rwdata(void *sdh, uint rw, uint32 addr, uint8 *buf, uint nbytes);


extern int bcmsdh_abort(void *sdh, uint fn);


extern int bcmsdh_start(void *sdh, int stage);


extern int bcmsdh_stop(void *sdh);


extern int bcmsdh_query_device(void *sdh);


extern uint bcmsdh_query_iofnum(void *sdh);


extern int bcmsdh_iovar_op(void *sdh, const char *name,
                           void *params, int plen, void *arg, int len, bool set);


extern int bcmsdh_reset(bcmsdh_info_t *sdh);



extern void *bcmsdh_get_sdioh(bcmsdh_info_t *sdh);


typedef struct {
	
	void *(*attach)(uint16 vend_id, uint16 dev_id, uint16 bus, uint16 slot,
	                uint16 func, uint bustype, void * regsva, osl_t * osh,
	                void * param);
	
	void (*detach)(void *ch);
} bcmsdh_driver_t;


extern int bcmsdh_register(bcmsdh_driver_t *driver);
extern void bcmsdh_unregister(void);
extern bool bcmsdh_chipmatch(uint16 vendor, uint16 device);
extern void bcmsdh_device_remove(void * sdh);

#if defined(OOB_INTR_ONLY)
extern int bcmsdh_register_oob_intr(void * dhdp);
extern void bcmsdh_unregister_oob_intr(void);
extern void bcmsdh_oob_intr_set(bool enable);
#endif 

extern uint32 bcmsdh_get_dstatus(void *sdh);


extern uint32 bcmsdh_cur_sbwad(void *sdh);


extern void bcmsdh_chipinfo(void *sdh, uint32 chip, uint32 chiprev);

#ifdef BCMSPI
extern void bcmsdh_dwordmode(void *sdh, bool set);
#endif 

#endif	
