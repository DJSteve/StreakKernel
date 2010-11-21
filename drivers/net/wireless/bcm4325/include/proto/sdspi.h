/*
 * SD-SPI Protocol Standard
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
 * $Id: sdspi.h,v 9.1.20.1 2008/05/06 22:59:19 Exp $
 */

#define SPI_START_M		BITFIELD_MASK(1)	
#define SPI_START_S		31
#define SPI_DIR_M		BITFIELD_MASK(1)	
#define SPI_DIR_S		30
#define SPI_CMD_INDEX_M		BITFIELD_MASK(6)	
#define SPI_CMD_INDEX_S		24
#define SPI_RW_M		BITFIELD_MASK(1)	
#define SPI_RW_S		23
#define SPI_FUNC_M		BITFIELD_MASK(3)	
#define SPI_FUNC_S		20
#define SPI_RAW_M		BITFIELD_MASK(1)	
#define SPI_RAW_S		19
#define SPI_STUFF_M		BITFIELD_MASK(1)	
#define SPI_STUFF_S		18
#define SPI_BLKMODE_M		BITFIELD_MASK(1)	
#define SPI_BLKMODE_S		19
#define SPI_OPCODE_M		BITFIELD_MASK(1)	
#define SPI_OPCODE_S		18
#define SPI_ADDR_M		BITFIELD_MASK(17)	
#define SPI_ADDR_S		1
#define SPI_STUFF0_M		BITFIELD_MASK(1)	
#define SPI_STUFF0_S		0

#define SPI_RSP_START_M		BITFIELD_MASK(1)	
#define SPI_RSP_START_S		7
#define SPI_RSP_PARAM_ERR_M	BITFIELD_MASK(1)	
#define SPI_RSP_PARAM_ERR_S	6
#define SPI_RSP_RFU5_M		BITFIELD_MASK(1)	
#define SPI_RSP_RFU5_S		5
#define SPI_RSP_FUNC_ERR_M	BITFIELD_MASK(1)	
#define SPI_RSP_FUNC_ERR_S	4
#define SPI_RSP_CRC_ERR_M	BITFIELD_MASK(1)	
#define SPI_RSP_CRC_ERR_S	3
#define SPI_RSP_ILL_CMD_M	BITFIELD_MASK(1)	
#define SPI_RSP_ILL_CMD_S	2
#define SPI_RSP_RFU1_M		BITFIELD_MASK(1)	
#define SPI_RSP_RFU1_S		1
#define SPI_RSP_IDLE_M		BITFIELD_MASK(1)	
#define SPI_RSP_IDLE_S		0


#define SDSPI_COMMAND_LEN	6	
#define SDSPI_START_BLOCK	0xFE	
#define SDSPI_IDLE_PAD		0xFF	
#define SDSPI_START_BIT_MASK	0x80