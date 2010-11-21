/*
 *  'Standard' SDIO HOST CONTROLLER driver
 *
 * Copyright (C) 2009, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: bcmsdstd.h,v 13.16.18.3 2008/09/30 17:52:35 Exp $
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

#define sd_sync_dma(sd, read, nbytes)
#define sd_init_dma(sd)
#define sd_ack_intr(sd)
#define sd_wakeup(sd);

extern int sdstd_osinit(sdioh_info_t *sd);
extern void sdstd_osfree(sdioh_info_t *sd);

#ifdef BCMPERFSTATS
#define sd_log(x)	do { if (sd_msglevel & SDH_LOG_VAL)	 bcmlog x; } while (0)
#else
#define sd_log(x)
#endif

#define SDIOH_ASSERT(exp) \
	do { if (!(exp)) \
		printf("!!!ASSERT fail: file %s lines %d", __FILE__, __LINE__); \
	} while (0)

#define BLOCK_SIZE_4318 64
#define BLOCK_SIZE_4328 512


#define SUCCESS	0
#define ERROR	1


#define SDIOH_MODE_SPI		0
#define SDIOH_MODE_SD1		1
#define SDIOH_MODE_SD4		2

#define MAX_SLOTS 6 	
#define SDIOH_REG_WINSZ	0x100 

#define SDIOH_TYPE_ARASAN_HDK	1
#define SDIOH_TYPE_BCM27XX	2
#define SDIOH_TYPE_TI_PCIXX21	4	
#define SDIOH_TYPE_RICOH_R5C822	5	
#define SDIOH_TYPE_JMICRON	6	


#if defined(linux) && defined(BCMDONGLEHOST)
#define BCMSDYIELD
#endif


#define SDIOH_CMD7_EXP_STATUS   0x00001E00

#define RETRIES_LARGE 100000
#define RETRIES_SMALL 100

#define USE_PIO			0x0	
#define USE_DMA			0x1

#define USE_BLOCKMODE		0x2	
#define USE_MULTIBLOCK		0x4

#define USE_FIFO		0x8	

#define CLIENT_INTR 		0x100	


struct sdioh_info {
	uint cfg_bar;                   	
	uint32 caps;                    	
	uint32 curr_caps;                    	

	osl_t 		*osh;			
	volatile char 	*mem_space;		
	uint		lockcount; 		
	bool		client_intr_enabled;	
	bool		intr_handler_valid;	
	sdioh_cb_fn_t	intr_handler;		
	void		*intr_handler_arg;	
	bool		initialized;		
	uint		target_dev;		
	uint16		intmask;		
	void		*sdos_info;		

	uint32		controller_type;	
	uint8		version;		
	uint 		irq;			
	int 		intrcount;		
	int 		local_intrcount;	
	bool 		host_init_done;		
	bool 		card_init_done;		
	bool 		polled_mode;		

	bool		sd_use_dma;		
	bool 		sd_blockmode;		
						
	bool 		use_client_ints;	
						
	int 		adapter_slot;		
	int 		sd_mode;		
	int 		client_block_size[SDIOD_MAX_IOFUNCS];		
	uint32 		data_xfer_count;	
	uint16 		card_rca;		
	uint8 		num_funcs;		
	uint32 		com_cis_ptr;
	uint32 		func_cis_ptr[SDIOD_MAX_IOFUNCS];
	void		*dma_buf;
	ulong		dma_phys;
	int 		r_cnt;			
	int 		t_cnt;			
	bool		got_hcint;		
	uint16		last_intrstatus;	
};




extern uint sd_msglevel;


extern bool check_client_intr(sdioh_info_t *sd);


extern void sdstd_devintr_on(sdioh_info_t *sd);
extern void sdstd_devintr_off(sdioh_info_t *sd);


extern void sdstd_intrs_on(sdioh_info_t *sd, uint16 norm, uint16 err);
extern void sdstd_intrs_off(sdioh_info_t *sd, uint16 norm, uint16 err);


extern void sdstd_spinbits(sdioh_info_t *sd, uint16 norm, uint16 err);





extern uint32 *sdstd_reg_map(osl_t *osh, int32 addr, int size);
extern void sdstd_reg_unmap(osl_t *osh, int32 addr, int size);


extern int sdstd_register_irq(sdioh_info_t *sd, uint irq);
extern void sdstd_free_irq(uint irq, sdioh_info_t *sd);


extern void sdstd_lock(sdioh_info_t *sd);
extern void sdstd_unlock(sdioh_info_t *sd);


extern uint16 sdstd_waitbits(sdioh_info_t *sd, uint16 norm, uint16 err, bool yield);
