/*
 * Broadcom SiliconBackplane SDIO/PCMCIA hardware-specific
 * device core support
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
 * $Id: sbsdpcmdev.h,v 13.29.4.1.4.6 2008/07/11 21:26:55 Exp $
 */

#ifndef	_sbsdpcmdev_h_
#define	_sbsdpcmdev_h_


#ifndef PAD
#define	_PADLINE(line)	pad ## line
#define	_XSTR(line)	_PADLINE(line)
#define	PAD		_XSTR(__LINE__)
#endif	


typedef volatile struct {
	dma64regs_t	xmt;		
	uint32 PAD[2];
	dma64regs_t	rcv;		
	uint32 PAD[2];
} dma64p_t;


typedef volatile struct {
	dma64p_t dma64regs[2];
	dma64diag_t dmafifo;		
	uint32 PAD[92];

} sdiodma64_t;


typedef volatile struct {
	dma32regp_t	dma32regs[2];		
	dma32diag_t dmafifo;		
	uint32 PAD[108];
} sdiodma32_t;


typedef volatile struct {
	dma32regp_t dmaregs;		
	dma32diag_t dmafifo;		
	uint32 PAD[116];
} pcmdma32_t;


typedef volatile struct {
	uint32 corecontrol;		
	uint32 corestatus;		
	uint32 PAD[1];
	uint32 biststatus;		

	
	uint16 pcmciamesportaladdr;	
	uint16 PAD[1];
	uint16 pcmciamesportalmask;	
	uint16 PAD[1];
	uint16 pcmciawrframebc;		
	uint16 PAD[1];
	uint16 pcmciaunderflowtimer;	
	uint16 PAD[1];

	
	uint32 intstatus;		
	uint32 hostintmask;		
	uint32 intmask;			
	uint32 sbintstatus;		
	uint32 sbintmask;		
	uint32 PAD[3];
	uint32 tosbmailbox;		
	uint32 tohostmailbox;		
	uint32 tosbmailboxdata;		
	uint32 tohostmailboxdata;	


	
	uint32 sdioaccess;		
	uint32 PAD[3];

	
	uint8  pcmciaframectrl;		
	uint8  PAD[3];
	uint8  pcmciawatermark;		
	uint8  PAD[155];

	
	uint32 intrcvlazy;		
	uint32 PAD[3];

	
	uint32 cmd52rd;			
	uint32 cmd52wr;			
	uint32 cmd53rd;			
	uint32 cmd53wr;			
	uint32 abort;			
	uint32 datacrcerror;		
	uint32 rdoutofsync;		
	uint32 wroutofsync;		
	uint32 writebusy;		
	uint32 readwait;		
	uint32 readterm;		
	uint32 writeterm;		
	uint32 PAD[40];
	uint32 clockctlstatus;		
	uint32 PAD[7];

	
	volatile union {
		pcmdma32_t pcm32;
		sdiodma32_t sdiod32;
		sdiodma64_t sdiod64;
	} dma;

	
	char cis[512];			

	
	char pcmciafcr[256];		
	uint16 PAD[55];

	
	uint16 backplanecsr;		
	uint16 backplaneaddr0;		
	uint16 backplaneaddr1;		
	uint16 backplaneaddr2;		
	uint16 backplaneaddr3;		
	uint16 backplanedata0;		
	uint16 backplanedata1;		
	uint16 backplanedata2;		
	uint16 backplanedata3;		
	uint16 PAD[31];

	
	uint16 spromstatus;		
	uint32 PAD[464];

	
	sbconfig_t sbconfig;		
} sdpcmd_regs_t;


#define CC_CISRDY	(1 << 0)	
#define CC_BPRESEN	(1 << 1)	
#define CC_F2RDY	(1 << 2)	
#define CC_CLRPADSISO	(1 << 3)	


#define CS_PCMCIAMODE	(1 << 0)	
#define CS_SMARTDEV	(1 << 1)	
#define CS_F2ENABLED	(1 << 2)	

#define PCMCIA_MES_PA_MASK	0x7fff	
#define PCMCIA_MES_PM_MASK	0x7fff	
#define PCMCIA_WFBC_MASK	0xffff	
#define PCMCIA_UT_MASK		0x07ff	


#define I_SMB_SW0	(1 << 0)	
#define I_SMB_SW1	(1 << 1)	
#define I_SMB_SW2	(1 << 2)	
#define I_SMB_SW3	(1 << 3)	
#define I_SMB_SW_MASK	0x0000000f	
#define I_SMB_SW_SHIFT	0		
#define I_HMB_SW0	(1 << 4)	
#define I_HMB_SW1	(1 << 5)	
#define I_HMB_SW2	(1 << 6)	
#define I_HMB_SW3	(1 << 7)	
#define I_HMB_SW_MASK	0x000000f0	
#define I_HMB_SW_SHIFT	4		
#define I_WR_OOSYNC	(1 << 8)	
#define I_RD_OOSYNC	(1 << 9)	
#define	I_PC		(1 << 10)	
#define	I_PD		(1 << 11)	
#define	I_DE		(1 << 12)	
#define	I_RU		(1 << 13)	
#define	I_RO		(1 << 14)	
#define	I_XU		(1 << 15)	
#define	I_RI		(1 << 16)	
#define I_BUSPWR	(1 << 17)	
#define	I_XI		(1 << 24)	
#define I_RF_TERM	(1 << 25)	
#define I_WF_TERM	(1 << 26)	
#define I_PCMCIA_XU	(1 << 27)	
#define I_SBINT		(1 << 28)	
#define I_CHIPACTIVE	(1 << 29)	
#define I_SRESET	(1 << 30)	
#define I_IOE2		(1U << 31)	
#define	I_ERRORS	(I_PC | I_PD | I_DE | I_RU | I_RO | I_XU)	
#define I_DMA		(I_RI | I_XI | I_ERRORS)

#define I_TOSBMAIL	(I_SMB_NAK | I_SMB_INT_ACK | I_SMB_USE_OOB | I_SMB_DEV_INT)
#define I_TOHOSTMAIL	(I_HMB_FC_CHANGE | I_HMB_FRAME_IND | I_HMB_HOST_INT) 


#define I_SB_SERR	(1 << 8)	
#define I_SB_RESPERR	(1 << 9)	
#define I_SB_SPROMERR	(1 << 10)	


#define SDA_DATA_MASK	0x000000ff	
#define SDA_ADDR_MASK	0x000fff00	
#define SDA_ADDR_SHIFT	8		
#define SDA_WRITE	0x01000000	
#define SDA_READ	0x00000000	
#define SDA_BUSY	0x80000000	


#define SDA_CCCR_SPACE		0x000	
#define SDA_F1_FBR_SPACE	0x100	
#define SDA_F2_FBR_SPACE	0x200	
#define SDA_F1_REG_SPACE	0x300	


#define SDA_CHIPCONTROLDATA	0x006	
#define SDA_CHIPCONTROLENAB	0x007	
#define SDA_F2WATERMARK		0x008	
#define SDA_DEVICECONTROL	0x009	
#define SDA_SBADDRLOW		0x00a	
#define SDA_SBADDRMID		0x00b	
#define SDA_SBADDRHIGH		0x00c	
#define SDA_FRAMECTRL		0x00d	
#define SDA_CHIPCLOCKCSR	0x00e	
#define SDA_SDIOPULLUP		0x00f	
#define SDA_SDIOWRFRAMEBCLOW	0x019	
#define SDA_SDIOWRFRAMEBCHIGH	0x01a	
#define SDA_SDIORDFRAMEBCLOW	0x01b	
#define SDA_SDIORDFRAMEBCHIGH	0x01c	


#define SDA_F2WATERMARK_MASK	0x7f	


#define SDA_SBADDRLOW_MASK	0x80	


#define SDA_SBADDRMID_MASK	0xff	


#define SDA_SBADDRHIGH_MASK	0xff	


#define SFC_RF_TERM	(1 << 0)	
#define SFC_WF_TERM	(1 << 1)	
#define SFC_CRC4WOOS	(1 << 2)	
#define SFC_ABORTALL	(1 << 3)	


#define PFC_RF_TERM	(1 << 0)	
#define PFC_WF_TERM	(1 << 1)	


#define	IRL_TO_MASK	0x00ffffff	
#define	IRL_FC_MASK	0xff000000	
#define	IRL_FC_SHIFT	24		


typedef volatile struct {
	uint16 len;
	uint16 flags;
} sdpcmd_rxh_t;


#define RXF_CRC		0x0001		
#define RXF_WOOS	0x0002		
#define RXF_WF_TERM	0x0004		
#define RXF_ABORT	0x0008		
#define RXF_DISCARD	(RXF_CRC | RXF_WOOS | RXF_WF_TERM | RXF_ABORT)	


#define SDPCM_FRAMETAG_LEN	4	




#define I_SMB_NAK	(1 << 0)	
#define I_SMB_INT_ACK	(1 << 1)	
#define I_SMB_USE_OOB	(1 << 2)	
#define I_SMB_DEV_INT	(1 << 3)	
#define I_HMB_FC_STATE	(1 << 4)	
#define I_HMB_FC_CHANGE	(1 << 5)	
#define I_HMB_FRAME_IND	(1 << 6)	
#define I_HMB_HOST_INT	(1 << 7)	


#define SMB_MASK	0x0000000f	
#define HMB_MASK	0x000000f0	
#define HMB_SHIFT	4		


#define MB_MASK		0x0000000f	
#define SMB_NAK		(1 << 0)	
#define SMB_INT_ACK	(1 << 1)	
#define SMB_USE_OOB	(1 << 2)	
#define SMB_DEV_INT	(1 << 3)	
#define HMB_FC_ON	(1 << 0)	
#define HMB_FC_CHANGE	(1 << 1)	
#define HMB_FRAME_IND	(1 << 2)	
#define HMB_HOST_INT	(1 << 3)	


#define HMB_DATA_NAKHANDLED	1	
#define HMB_DATA_DEVREADY	2	
#define HMB_DATA_FC		4	
#define HMB_DATA_FWREADY	8	

#define HMB_DATA_FCDATA_MASK	0xff	
#define HMB_DATA_FCDATA_SHIFT	24	

#define HMB_DATA_VERSION_MASK	0xff	
#define HMB_DATA_VERSION_SHIFT	16	


#define SMB_DATA_VERSION_MASK	0xff	
#define SMB_DATA_VERSION_SHIFT	16	


#define SDPCM_PROT_VERSION	4


#define SDPCM_SEQUENCE_MASK		0x000000ff	
#define SDPCM_PACKET_SEQUENCE(p) (((uint8 *)p)[0] & 0xff) 

#define SDPCM_CHANNEL_MASK		0x00000f00	
#define SDPCM_CHANNEL_SHIFT		8		
#define SDPCM_PACKET_CHANNEL(p) (((uint8 *)p)[1] & 0x0f) 

#define SDPCM_FLAGS_MASK		0x0000f000	
#define SDPCM_FLAGS_SHIFT		12		
#define SDPCM_PACKET_FLAGS(p) ((((uint8 *)p)[1] & 0xf0) >> 4) 


#define SDPCM_NEXTLEN_MASK		0x00ff0000	
#define SDPCM_NEXTLEN_SHIFT		16		
#define SDPCM_NEXTLEN_VALUE(p) ((((uint8 *)p)[2] & 0xff) << 4) 
#define SDPCM_NEXTLEN_OFFSET		2


#define SDPCM_DOFFSET_OFFSET		3		
#define SDPCM_DOFFSET_VALUE(p) 		(((uint8 *)p)[SDPCM_DOFFSET_OFFSET] & 0xff)
#define SDPCM_DOFFSET_MASK		0xff000000
#define SDPCM_DOFFSET_SHIFT		24

#define SDPCM_FCMASK_OFFSET		4		
#define SDPCM_FCMASK_VALUE(p)		(((uint8 *)p)[SDPCM_FCMASK_OFFSET ] & 0xff)
#define SDPCM_WINDOW_OFFSET		5		
#define SDPCM_WINDOW_VALUE(p)		(((uint8 *)p)[SDPCM_WINDOW_OFFSET] & 0xff)
#define SDPCM_VERSION_OFFSET		6		
#define SDPCM_VERSION_VALUE(p)		(((uint8 *)p)[SDPCM_VERSION_OFFSET] & 0xff)
#define SDPCM_UNUSED_OFFSET		7		
#define SDPCM_UNUSED_VALUE(p)		(((uint8 *)p)[SDPCM_UNUSED_OFFSET] & 0xff)

#define SDPCM_SWHEADER_LEN	8	


#define SDPCM_CONTROL_CHANNEL	0	
#define SDPCM_EVENT_CHANNEL	1	
#define SDPCM_DATA_CHANNEL	2	
#define SDPCM_GLOM_CHANNEL	3	
#define SDPCM_TEST_CHANNEL	15	
#define SDPCM_MAX_CHANNEL	15

#define SDPCM_SEQUENCE_WRAP	256	

#define SDPCM_FLAG_RESVD0	0x01
#define SDPCM_FLAG_RESVD1	0x02
#define SDPCM_FLAG_GSPI_TXENAB	0x04
#define SDPCM_FLAG_GLOMDESC	0x08	


#define SDPCM_GLOMDESC_FLAG	(SDPCM_FLAG_GLOMDESC << SDPCM_FLAGS_SHIFT)

#define SDPCM_GLOMDESC(p)	(((uint8 *)p)[1] & 0x80)


#define SDPCM_TEST_HDRLEN	4	
#define SDPCM_TEST_DISCARD	0x01	
#define SDPCM_TEST_ECHOREQ	0x02	
#define SDPCM_TEST_ECHORSP	0x03	
#define SDPCM_TEST_BURST	0x04	
#define SDPCM_TEST_SEND		0x05	


#define SDPCM_TEST_FILL(byteno, id)	((uint8)(id + byteno))



typedef volatile struct {
	uint32 cmd52rd;		
	uint32 cmd52wr;		
	uint32 cmd53rd;		
	uint32 cmd53wr;		
	uint32 abort;		
	uint32 datacrcerror;	
	uint32 rdoutofsync;	
	uint32 wroutofsync;	
	uint32 writebusy;	
	uint32 readwait;	
	uint32 readterm;	
	uint32 writeterm;	
	uint32 rxdescuflo;	
	uint32 rxfifooflo;	
	uint32 txfifouflo;	
	uint32 runt;		
	uint32 badlen;		
	uint32 badcksum;	
	uint32 seqbreak;	
	uint32 rxfcrc;		
	uint32 rxfwoos;		
	uint32 rxfwft;		
	uint32 rxfabort;	
	uint32 woosint;		
	uint32 roosint;		
	uint32 rftermint;	
	uint32 wftermint;	
} sdpcmd_cnt_t;

#define SDIODREV_IS(var, val)	((var) == (val))
#define SDIODREV_GE(var, val)	((var) >= (val))
#define SDIODREV_GT(var, val)	((var) > (val))
#define SDIODREV_LT(var, val)	((var) < (val))
#define SDIODREV_LE(var, val)	((var) <= (val))

#define SDIODDMAREG(h, dir, chnl)	(SDIODREV_LT(h->corerev, 1) ? \
	((dir == DMA_TX) ? \
		(void*)(uintptr)&(h->regs->dma.sdiod32.dma32regs[chnl].xmt) : \
		(void*)(uintptr)&(h->regs->dma.sdiod32.dma32regs[chnl].rcv)) : \
	((dir == DMA_TX) ? \
		(void*)(uintptr)&(h->regs->dma.sdiod64.dma64regs[chnl].xmt) : \
		(void*)(uintptr)&(h->regs->dma.sdiod64.dma64regs[chnl].rcv)))

#define PCMDDMAREG(h, dir, chnl) \
	((dir == DMA_TX) ? \
		(void*)(uintptr)&(h->regs->dma.pcm32.dmaregs.xmt) : \
		(void*)(uintptr)&(h->regs->dma.pcm32.dmaregs.rcv))

#define SDPCMDMAREG(h, dir, chnl, coreid)	((coreid == SDIOD_CORE_ID) ? \
	(SDIODDMAREG(h, dir, chnl)) : (PCMDDMAREG(h, dir, chnl)))

#define SDIODFIFOREG(h, corerev)	(SDIODREV_LT(corerev, 1) ? \
	((dma32diag_t *)(uintptr)&(h->regs->dma.sdiod32.dmafifo)) : \
	((dma32diag_t *)(uintptr)&(h->regs->dma.sdiod64.dmafifo)))


#define PCMDFIFOREG(h) \
	  ((dma32diag_t*)(uintptr)&(h->regs->dma.pcm32.dmafifo))

#define SDPCMFIFOREG(h, coreid, corerev)	((coreid == SDIOD_CORE_ID) ? \
	(SDIODFIFOREG(h, corerev)) : (PCMDFIFOREG(h)))


#endif	
