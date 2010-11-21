/*
 * Broadcom PCI-SPI Host Controller Register Definitions
 *
 * Copyright (C) 2009, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: bcmpcispi.h,v 13.11.8.3 2008/07/09 21:23:29 Exp $
 */


#ifndef PAD
#define	_PADLINE(line)	pad ## line
#define	_XSTR(line)	_PADLINE(line)
#define	PAD		_XSTR(__LINE__)
#endif	



typedef volatile struct {
	uint32 spih_ctrl;		
	uint32 spih_stat;		
	uint32 spih_data;		
	uint32 spih_ext;		
	uint32 PAD[4];			

	uint32 spih_gpio_ctrl;		
	uint32 spih_gpio_data;		
	uint32 PAD[6];			

	uint32 spih_int_edge;		
	uint32 spih_int_pol;		
							
	uint32 spih_int_mask;		
	uint32 spih_int_status;		
	uint32 PAD[4];			

	uint32 spih_hex_disp;		
	uint32 spih_current_ma;		
	uint32 PAD[1];			
	uint32 spih_disp_sel;		
	uint32 PAD[4];			
	uint32 PAD[8];			
	uint32 PAD[8];			
	uint32 spih_pll_ctrl;	
	uint32 spih_pll_status;	
	uint32 spih_xtal_freq;	
	uint32 spih_clk_count;	

} spih_regs_t;

typedef volatile struct {
	uint32 cfg_space[0x40];		
	uint32 P_IMG_CTRL0;		

	uint32 P_BA0;			
	uint32 P_AM0;			
	uint32 P_TA0;			
	uint32 P_IMG_CTRL1;		
	uint32 P_BA1;			
	uint32 P_AM1;			
	uint32 P_TA1;			
	uint32 P_IMG_CTRL2;		
	uint32 P_BA2;			
	uint32 P_AM2;			
	uint32 P_TA2;			
	uint32 P_IMG_CTRL3;		
	uint32 P_BA3;			
	uint32 P_AM3;			
	uint32 P_TA3;			
	uint32 P_IMG_CTRL4;		
	uint32 P_BA4;			
	uint32 P_AM4;			
	uint32 P_TA4;			
	uint32 P_IMG_CTRL5;		
	uint32 P_BA5;			
	uint32 P_AM5;			
	uint32 P_TA5;			
	uint32 P_ERR_CS;		
	uint32 P_ERR_ADDR;		
	uint32 P_ERR_DATA;		

	uint32 PAD[5];			

	uint32 WB_CONF_SPC_BAR;		
	uint32 W_IMG_CTRL1;		
	uint32 W_BA1;			
	uint32 W_AM1;			
	uint32 W_TA1;			
	uint32 W_IMG_CTRL2;		
	uint32 W_BA2;			
	uint32 W_AM2;			
	uint32 W_TA2;			
	uint32 W_IMG_CTRL3;		
	uint32 W_BA3;			
	uint32 W_AM3;			
	uint32 W_TA3;			
	uint32 W_IMG_CTRL4;		
	uint32 W_BA4;			
	uint32 W_AM4;			
	uint32 W_TA4;			
	uint32 W_IMG_CTRL5;		
	uint32 W_BA5;			
	uint32 W_AM5;			
	uint32 W_TA5;			
	uint32 W_ERR_CS;		
	uint32 W_ERR_ADDR;		
	uint32 W_ERR_DATA;		
	uint32 CNF_ADDR;		
	uint32 CNF_DATA;		

	uint32 INT_ACK;			
	uint32 ICR;			
	uint32 ISR;			
} spih_pciregs_t;




#define PCI_INT_PROP_EN		(1 << 0)	
#define PCI_WB_ERR_INT_EN	(1 << 1)	
#define PCI_PCI_ERR_INT_EN	(1 << 2)	
#define PCI_PAR_ERR_INT_EN	(1 << 3)	
#define PCI_SYS_ERR_INT_EN	(1 << 4)	
#define PCI_SOFTWARE_RESET	(1U << 31)	



#define PCI_INT_PROP_ST		(1 << 0)	
#define PCI_WB_ERR_INT_ST	(1 << 1)	
#define PCI_PCI_ERR_INT_ST	(1 << 2)	
#define PCI_PAR_ERR_INT_ST	(1 << 3)	
#define PCI_SYS_ERR_INT_ST	(1 << 4)	



#define SPIH_CTLR_INTR		(1 << 0)	
#define SPIH_DEV_INTR		(1 << 1)	
#define SPIH_WFIFO_INTR		(1 << 2)	


#define SPIH_CS				(1 << 0)	
#define SPIH_SLOT_POWER		(1 << 1)	
#define SPIH_CARD_DETECT	(1 << 2)	


#define SPIH_STATE_MASK		0x30		
#define SPIH_STATE_SHIFT	4		
#define SPIH_WFFULL			(1 << 3)	
#define SPIH_WFEMPTY		(1 << 2)	
#define SPIH_RFFULL			(1 << 1)	
#define SPIH_RFEMPTY		(1 << 0)	

#define SPIH_EXT_CLK		(1U << 31)	

#define SPIH_PLL_NO_CLK		(1 << 1)	
#define SPIH_PLL_LOCKED		(1 << 3)	


#define SPI_SPIN_BOUND		0xf4240		
