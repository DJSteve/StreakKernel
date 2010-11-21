/*
 *  'Standard' SDIO HOST CONTROLLER driver
 *
 * Copyright (C) 2010, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: bcmsdstd.h,v 13.16.18.1.16.3 2009/12/10 01:09:23 Exp $
 */



#ifdef BCMDBG
#define sd_err(x)	do { if (sd_msglevel & SDH_ERROR_VAL) printf x; } while (0)
#define sd_trace(x)	do { if (sd_msglevel & SDH_TRACE_VAL) printf x; } while (0)
#define sd_info(x)	do { if (sd_msglevel & SDH_INFO_VAL)  printf x; } while (0)
#define sd_debug(x)	do { if (sd_msglevel & SDH_DEBUG_VAL) printf x; } while (0)
#define sd_data(x)	do { if (sd_msglevel & SDH_DATA_VAL)  printf x; } while (0)
#define sd_ctrl(x)	do { if (sd_msglevel & SDH_CTRL_VAL)  printf x; } while (0)
#define sd_dma(x)	do { if (sd_msglevel & SDH_DMA_VAL)  printf x; } while (0)
#else
#define sd_err(x)	do { if (sd_msglevel & SDH_ERROR_VAL) printf x; } while (0)
#define sd_trace(x)
#define sd_info(x)
#define sd_debug(x)
#define sd_data(x)
#define sd_ctrl(x)
#define sd_dma(x)
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


#if defined(linux)
#define BCMSDYIELD
#endif


#define SDIOH_CMD7_EXP_STATUS   0x00001E00

#define RETRIES_LARGE 100000
#define RETRIES_SMALL 100


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

	bool 		sd_blockmode;		
						
	bool 		use_client_ints;	
						
	int 		adapter_slot;		
	int 		sd_mode;		
	int 		client_block_size[SDIOD_MAX_IOFUNCS];		
	uint32 		data_xfer_count;	
	uint16 		card_rca;		
	int8		sd_dma_mode;		
	uint8 		num_funcs;		
	uint32 		com_cis_ptr;
	uint32 		func_cis_ptr[SDIOD_MAX_IOFUNCS];
	void		*dma_buf;		
	ulong		dma_phys;		
	void		*adma2_dscr_buf;	
	ulong		adma2_dscr_phys;	

	
	void		*dma_start_buf;
	ulong		dma_start_phys;
	uint		alloced_dma_size;
	void		*adma2_dscr_start_buf;
	ulong		adma2_dscr_start_phys;
	uint		alloced_adma2_dscr_size;

	int 		r_cnt;			
	int 		t_cnt;			
	bool		got_hcint;		
	uint16		last_intrstatus;	
};

#define DMA_MODE_NONE	0
#define DMA_MODE_SDMA	1
#define DMA_MODE_ADMA1	2
#define DMA_MODE_ADMA2	3
#define DMA_MODE_ADMA2_64 4
#define DMA_MODE_AUTO	-1

#define USE_DMA(sd)		((bool)((sd->sd_dma_mode > 0) ? TRUE : FALSE))


#define SDIOH_SDMA_MODE			0
#define SDIOH_ADMA1_MODE		1
#define SDIOH_ADMA2_MODE		2
#define SDIOH_ADMA2_64_MODE		3

#define ADMA2_ATTRIBUTE_VALID		(1 << 0)	
#define ADMA2_ATTRIBUTE_END			(1 << 1)	
#define ADMA2_ATTRIBUTE_INT			(1 << 2)	
#define ADMA2_ATTRIBUTE_ACT_NOP		(0 << 4)	
#define ADMA2_ATTRIBUTE_ACT_RSV		(1 << 4)	
#define ADMA1_ATTRIBUTE_ACT_SET		(1 << 4)	
#define ADMA2_ATTRIBUTE_ACT_TRAN	(2 << 4)	
#define ADMA2_ATTRIBUTE_ACT_LINK	(3 << 4)	


typedef struct adma2_dscr_32b {
	uint32 len_attr;
	uint32 phys_addr;
} adma2_dscr_32b_t;


typedef struct adma1_dscr {
	uint32 phys_addr_attr;
} adma1_dscr_t;




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
