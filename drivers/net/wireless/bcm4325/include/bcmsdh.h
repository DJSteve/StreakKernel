/*
 * SDIO host client driver interface of Broadcom HNBU
 *     export functions to client drivers
 *     abstract OS and BUS specific details of SDIO
 *
 * Copyright (C) 1999-2009, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http:
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: bcmsdh.h,v 13.35.14.7.16.5 2009/09/30 05:11:12 Exp $
 */

#ifndef	_bcmsdh_h_
#define	_bcmsdh_h_

#define BCMSDH_ERROR_VAL	0x0001 
#define BCMSDH_INFO_VAL		0x0002 
extern const uint bcmsdh_msglevel;

#define BCMSDH_ERROR(x)
#define BCMSDH_INFO(x)


typedef struct bcmsdh_info bcmsdh_info_t;
typedef void (*bcmsdh_cb_fn_t)(void *);


extern bcmsdh_info_t *bcmsdh_attach(osl_t *osh, void *cfghdl, void **regsva, uint irq);


extern int bcmsdh_detach(osl_t *osh, void *sdh);


extern bool bcmsdh_intr_query(void *sdh);


extern int bcmsdh_intr_enable(void *sdh);
extern int bcmsdh_intr_disable(void *sdh);


extern int bcmsdh_intr_reg(void *sdh, bcmsdh_cb_fn_t fn, void *argh);
extern int bcmsdh_intr_dereg(void *sdh);

#if defined(DHD_DEBUG)

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
    void (*suspend)(void *ch);
    void (*resume)(void *ch);
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

#endif	
