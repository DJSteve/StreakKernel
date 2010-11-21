/*
 * BRCM SDIOH (host controller) core hardware definitions.
 *
 * SDIOH support 1bit, 4 bit SDIO mode as well as SPI mode.
 *
 * $Id: sbsdioh.h,v 13.20 2006/06/26 21:41:05 Exp $
 * Copyright(c) 2003 Broadcom Corporation
 */

#ifndef	_SBSDIOH_H
#define	_SBSDIOH_H


#ifndef PAD
#define	_PADLINE(line)	pad ## line
#define	_XSTR(line)	_PADLINE(line)
#define	PAD		_XSTR(__LINE__)
#endif	

typedef volatile struct {
	uint32 devcontrol;	
	uint32 PAD[15];		

	
	uint32 mode;		
	uint32 delay;		
	uint32 rdto;		
	uint32 rbto;		
	uint32 test;		
	uint32 arvm;		

	uint32 error;		
	uint32 errormask;	
	uint32 cmddat;		
	uint32 cmdl;		
	uint32 fifodata;	
	uint32 respq;		

	uint32 ct_cmddat;	
	uint32 ct_cmdl;		
	uint32 ct_fifodata;	
	uint32 PAD;

	uint32 ap_cmddat;	
	uint32 ap_cmdl;		
	uint32 ap_fifodata;	
	uint32 PAD;

	uint32 intstatus;	
	uint32 intmask;		
	uint32 PAD;

	uint32 debuginfo;	
	uint32 fifoctl;		
	uint32 blocksize;	

	uint32 PAD[86];

	dma32regp_t dmaregs;	

} sdioh_regs_t;


#define CODEC_DEVCTRL_SDIOH	0x4000		


#define MODE_DONT_WAIT_DMA	0x2000		
#define MODE_BIG_ENDIAN		0x1000		
#define MODE_STOP_ALL_CLK	0x0800		
#define MODE_PRECMD_CNT_EN	0x0400		
#define	MODE_CLK_OUT_EN		0x0200		
#define	MODE_USE_EXT_CLK	0x0100		
#define	MODE_CLK_DIV_MASK	0x00f0		
#define	MODE_OP_MASK		0x000f		
#define	MODE_OP_SDIO4BIT	2		
#define	MODE_OP_SDIO1BIT	1		
#define	MODE_OP_SPI		0		
#define	MODE_HIGHSPEED_EN	0x10000		


#define DLY_CLK_COUNT_PRE_M	0x0000ffff	
#define DLY_CLK_COUNT_PRE_O	0
#define DLY_TX_START_COUNT_M	0xffff0000	
#define DLY_TX_START_COUNT_O	23


#define TEST_BAD_CMD_CRC	0x1		
#define TEST_BAD_DAT_CRC	0x2		


#define	AR_MASK_OFT		8		
#define	AR_VAL			0x00ff		


#define CMDAT_INDEX_M		0x0000003f	
#define CMDAT_EXP_RSPTYPE_M	0x000001c0	
#define CMDAT_EXP_RSPTYPE_O	6
#define CMDAT_DAT_EN_M		0x00000200	
#define CMDAT_DAT_EN_O		9
#define CMDAT_DAT_WR_M		0x00000400	
#define CMDAT_DAT_WR_O		10
#define CMDAT_DMA_MODE_M	0x00000800	
#define CMDAT_DMA_MODE_O	11
#define CMDAT_ARC_EN_M		0x00001000	
#define CMDAT_ARC_EN_O		12
#define CMDAT_EXP_BUSY_M	0x00002000	
#define CMDAT_EXP_BUSY_O	13
#define CMDAT_NO_RSP_CRC_CHK_M	0x00004000	
#define CMDAT_NO_RSP_CRC_CHK_O	14
#define CMDAT_NO_RSP_CDX_CHK_M	0x00008000	
#define CMDAT_NO_RSP_CDX_CHK_O	15
#define CMDAT_DAT_TX_CNT_M	0x1fff0000	
#define CMDAT_DAT_TX_CNT_O	16
#define CMDAT_DATLEN_PIO	64		
#define CMDAT_DATLEN_DMA_NON53	512		
#define CMDAT_DATLEN_DMA_53	8096		
#define CMDAT_APPEND_EN_M	0x20000000	
#define CMDAT_APPEND_EN_O	29
#define CMDAT_ABORT_M		0x40000000	
#define CMDAT_ABORT_O		30
#define CMDAT_BLK_EN_M		0x80000000	
#define CMDAT_BLK_EN_O		31


#define ERROR_RSP_CRC		0x0001		
#define ERROR_RSP_TIME		0x0002		
#define ERROR_RSP_DBIT		0x0004		
#define ERROR_RSP_EBIT		0x0008		
#define ERROR_DAT_CRC		0x0010		
#define ERROR_DAT_SBIT		0x0020		
#define ERROR_DAT_EBIT		0x0040		
#define ERROR_DAT_RSP_S		0x0080		
#define ERROR_DAT_RSP_E		0x0100		
#define ERROR_DAT_RSP_UNKNOWN	0x0200		
#define ERROR_DAT_RSP_TURNARD	0x0400		
#define ERROR_DAT_READ_TO	0x0800		
#define ERROR_SPI_TOKEN_UNK	0x1000		
#define ERROR_SPI_TOKEN_BAD	0x2000		
#define ERROR_SPI_ET_OUTRANGE	0x4000		
#define ERROR_SPI_ET_ECC	0x8000		
#define ERROR_SPI_ET_CC		0x010000	
#define ERROR_SPI_ET_ERR	0x020000	
#define ERROR_AUTO_RSP_CHK	0x040000	
#define ERROR_RSP_BUSY_TO	0x080000	
#define ERROR_RSP_CMDIDX_BAD	0x100000	


#define INT_CMD_DAT_DONE	0x0001		
#define INT_HOST_BUSY		0x0002		
#define INT_DEV_INT		0x0004		
#define INT_ERROR_SUM		0x0008		
#define INT_CARD_INS		0x0010		
#define INT_CARD_GONE		0x0020		
#define INT_CMDBUSY_CUTTHRU	0x0040		
#define INT_CMDBUSY_APPEND	0x0080		
#define INT_CARD_PRESENT	0x0100		
#define INT_STD_PCI_DESC	0x0400		
#define INT_STD_PCI_DATA	0x0800		
#define INT_STD_DESC_ERR	0x1000		
#define INT_STD_RCV_DESC_UF	0x2000		
#define INT_STD_RCV_FIFO_OF	0x4000		
#define INT_STD_XMT_FIFO_UF	0x8000		
#define INT_RCV_INT		0x00010000		
#define INT_XMT_INT		0x01000000		


#define DBGI_REMAIN_COUNT	0x00001fff	
#define DBGI_CUR_ADDR		0xCfffE000	
#define DBGI_CARD_WASBUSY	0x40000000	
#define DBGI_R1B_DETECTED	0x80000000	


#define FIFO_RCV_BUF_RDY	0x10		
#define FIFO_XMT_BYTE_VALID	0x0f		
#define FIFO_VALID_BYTE1	0x01		
#define FIFO_VALID_BYTE2	0x02		
#define FIFO_VALID_BYTE3	0x04		
#define FIFO_VALID_BYTE4	0x08		
#define FIFO_VALID_ALL		0x0f		

#define SDIOH_MODE_PIO		0		
#define SDIOH_MODE_DMA		1		

#define SDIOH_CMDTYPE_NORMAL	0		
#define SDIOH_CMDTYPE_APPEND	1		
#define SDIOH_CMDTYPE_CUTTHRU	2		

#define SDIOH_DMA_START_EARLY	0
#define SDIOH_DMA_START_LATE	1

#define SDIOH_DMA_TX		1
#define SDIOH_DMA_RX		2

#define SDIOH_BLOCK_SIZE_MIN	4
#define SDIOH_BLOCK_SIZE_MAX	0x200

#define SDIOH_SB_ENUM_OFFSET	0x1000		

#define SDIOH_HOST_SUPPORT_OCR	0xfff000	

#define RESP_TYPE_NONE 		0
#define RESP_TYPE_R1  		1
#define RESP_TYPE_R2  		2
#define RESP_TYPE_R3  		3
#define RESP_TYPE_R4  		4
#define RESP_TYPE_R5  		5
#define RESP_TYPE_R6  		6


#define SDIOH_CMD_INDEX_M	BITFIELD_MASK(6)	
#define SDIOH_CMD_INDEX_S	0
#define SDIOH_CMD_RESP_TYPE_M	BITFIELD_MASK(3)	
#define SDIOH_CMD_RESP_TYPE_S	6
#define SDIOH_CMD_DATA_EN_M	BITFIELD_MASK(1)	
#define SDIOH_CMD_DATA_EN_S	9

#define SDIOH_CMD_DATWR_M	BITFIELD_MASK(1)	
#define SDIOH_CMD_DATWR_S	10
#define SDIOH_CMD_DMAMODE_M	BITFIELD_MASK(1)	
#define SDIOH_CMD_DMAMODE_S	11
#define SDIOH_CMD_ARC_EN_M	BITFIELD_MASK(1)	
#define SDIOH_CMD_ARC_EN_S	12
#define SDIOH_CMD_EXP_BSY_M	BITFIELD_MASK(1)	
#define SDIOH_CMD_EXP_BSY_S	13

#define SDIOH_CMD_CRC_DIS_M	BITFIELD_MASK(1)	
#define SDIOH_CMD_CRC_DIS_S	14
#define SDIOH_CMD_INDEX_DIS_M	BITFIELD_MASK(1)	
#define SDIOH_CMD_INDEX_DIS_S	15

#define SDIOH_CMD_TR_COUNT_M 	BITFIELD_MASK(13)	
#define SDIOH_CMD_TR_COUNT_S	16

#define SDIOH_CMD_APPEND_EN_M	BITFIELD_MASK(1)	
#define SDIOH_CMD_APPEND_EN_S	29
#define SDIOH_CMD_ABORT_EN_M	BITFIELD_MASK(1)	
#define SDIOH_CMD_ABORT_EN_S	30
#define SDIOH_CMD_BLKMODE_EN_M	BITFIELD_MASK(1)	
#define SDIOH_CMD_BLKMODE_EN_S	31



#define INT_CMD_DAT_DONE_M	BITFIELD_MASK(1)	
#define INT_CMD_DAT_DONE_S	0
#define INT_HOST_BUSY_M		BITFIELD_MASK(1)	
#define INT_HOST_BUSY_S		1
#define INT_DEV_INT_M		BITFIELD_MASK(1)	
#define INT_DEV_INT_S		2
#define INT_ERROR_SUM_M		BITFIELD_MASK(1)	
#define INT_ERROR_SUM_S		3
#define INT_CARD_INS_M		BITFIELD_MASK(1)	
#define INT_CARD_INS_S		4
#define INT_CARD_GONE_M		BITFIELD_MASK(1)	
#define INT_CARD_GONE_S		5
#define INT_CMDBUSY_CUTTHRU_M	BITFIELD_MASK(1)	
#define INT_CMDBUSY_CUTTHRU_S	6
#define INT_CMDBUSY_APPEND_M	BITFIELD_MASK(1)	
#define INT_CMDBUSY_APPEND_S	7	

#define INT_RCV_INT_M		BITFIELD_MASK(1)	
#define INT_RCV_INT_S		16
#define INT_XMT_INT_M		BITFIELD_MASK(1)	
#define INT_XMT_INT_S		24



#define SDBLOCK_M		BITFIELD_MASK(10)	
#define SDBLOCK_S		0

#define SD1_MODE 0x1				
#define SD4_MODE 0x2				

#endif	
