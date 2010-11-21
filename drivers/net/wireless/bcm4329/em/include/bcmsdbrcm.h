/*
 *  BRCM SDIO HOST CONTROLLER driver
 *
 * Copyright (C) 2010, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: bcmsdbrcm.h,v 13.12 2007/02/27 22:04:12 Exp $
 */


#ifdef BCMDBG
#define sd_err(x)	do { if (sd_msglevel & SDH_ERROR_VAL) printf x; } while (0)
#define sd_trace(x)	do { if (sd_msglevel & SDH_TRACE_VAL) printf x; } while (0)
#define sd_info(x)	do { if (sd_msglevel & SDH_INFO_VAL)  printf x; } while (0)
#define sd_debug(x)	do { if (sd_msglevel & SDH_DEBUG_VAL) printf x; } while (0)
#define sd_data(x)	do { if (sd_msglevel & SDH_DATA_VAL)  printf x; } while (0)
#define sd_ctrl(x)	do { if (sd_msglevel & SDH_CTRL_VAL)  printf x; } while (0)
#else
#define sd_err(x)
#define sd_trace(x)
#define sd_info(x)
#define sd_debug(x)
#define sd_data(x)
#define sd_ctrl(x)
#endif

#ifdef BCMPERFSTATS
#define sd_log(x)	do { if (sd_msglevel & SDH_LOG_VAL)	 bcmlog x; } while (0)
#else
#define sd_log(x)
#endif


struct sdioh_pubinfo {
	int 		local_intrcount;	
	bool 		dev_init_done;		
	bool		host_init_done;		
	bool		intr_registered;	
	bool		dev_intr_enabled;	
	uint		lockcount;		
	void 		*sdos_info;		
};


#define BLOCK_SIZE_4318 64
#define BLOCK_SIZE_4328 512



#define SDIOH_MODE_SPI		0
#define SDIOH_MODE_SD1		1
#define SDIOH_MODE_SD4		2


#define SDIOH_CMD7_EXP_STATUS   0x00001E00

#define RETRIES_SMALL		20
#define RETRIES_LARGE		500000
#define SD_TIMEOUT		250000	

#define ARVM_MASK		0xFF10

#define USE_PIO			0x0		
#define USE_DMA			0x1

#define NTXD			4
#define NRXD			4
#define RXBUFSZ			8192
#define NRXBUFPOST		4
#define	HWRXOFF			8

#define USE_BLOCKMODE		0x2		
#define USE_MULTIBLOCK		0x4

#define EXT_CLK			0xffffffff	

extern int isr_sdbrcm_check_dev_intr(struct sdioh_pubinfo *sd);
extern uint sd_msglevel;

extern uint32 *sdbrcm_reg_map(osl_t *osh, int32 addr, int size);
extern void sdbrcm_reg_unmap(osl_t *osh, int32 addr, int size);
extern int sdbrcm_register_irq(sdioh_info_t *sd, uint irq);
extern void sdbrcm_free_irq(uint irq, sdioh_info_t *sd);

extern void sdbrcm_lock(sdioh_info_t *sd);
extern void sdbrcm_unlock(sdioh_info_t *sd);

extern void sdbrcm_devintr_on(sdioh_info_t *sd);
extern void sdbrcm_devintr_off(sdioh_info_t *sd);


extern int sdbrcm_osinit(sdioh_info_t *sd, osl_t *osh);
extern void sdbrcm_osfree(sdioh_info_t *sd, osl_t *osh);
