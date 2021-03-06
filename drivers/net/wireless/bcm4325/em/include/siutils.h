/*
 * Misc utility routines for accessing the SOC Interconnects
 * of Broadcom HNBU chips.
 *
 * Copyright (C) 2009, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: siutils.h,v 13.197.4.2.4.4.6.1 2008/12/22 01:19:33 Exp $
 */


#ifndef	_siutils_h_
#define	_siutils_h_


struct si_pub {
	uint	socitype;		

	uint	bustype;		
	uint	buscoretype;		
	uint	buscorerev;		
	uint	buscoreidx;		
	int	ccrev;			
	uint32	cccaps;			
	int	pmurev;			
	uint32	pmucaps;		
	uint	boardtype;		
	uint	boardvendor;		
	uint	boardflags;		
	uint	chip;			
	uint	chiprev;		
	uint	chippkg;		
	uint32	chipst;			
	bool	issim;			
	uint    socirev;		
	bool	pci_pr32414;
};

#if defined(WLC_HIGH) && !defined(WLC_LOW)
typedef struct si_pub si_t;
#else
typedef const struct si_pub si_t;
#endif


#define	SI_OSH		NULL	


#define	XTAL			0x1	
#define	PLL			0x2	


#define	CLK_FAST		0	
#define	CLK_DYNAMIC		2	


#define GPIO_DRV_PRIORITY	0	
#define GPIO_APP_PRIORITY	1	
#define GPIO_HI_PRIORITY	2	


#define GPIO_PULLUP		0
#define GPIO_PULLDN		1


#define GPIO_REGEVT		0	
#define GPIO_REGEVT_INTMSK	1	
#define GPIO_REGEVT_INTPOL	2	


#define SI_DEVPATH_BUFSZ	16	


#define	SI_DOATTACH	1
#define SI_PCIDOWN	2
#define SI_PCIUP	3

#define	ISSIM_ENAB(sih)	0


#if defined(BCMPMUCTL)
#define PMUCTL_ENAB(sih)	(BCMPMUCTL)
#else
#define PMUCTL_ENAB(sih)	((sih)->cccaps & CC_CAP_PMU)
#endif


#if defined(BCMPMUCTL) && BCMPMUCTL
#define CCCTL_ENAB(sih)		(0)
#define CCPLL_ENAB(sih)		(0)
#else
#define CCCTL_ENAB(sih)		((sih)->cccaps & CC_CAP_PWR_CTL)
#define CCPLL_ENAB(sih)		((sih)->cccaps & CC_CAP_PLL_MASK)
#endif

typedef void (*gpio_handler_t)(uint32 stat, void *arg);



extern si_t *si_attach(uint pcidev, osl_t *osh, void *regs, uint bustype,
                       void *sdh, char **vars, uint *varsz);
extern si_t *si_kattach(osl_t *osh);
extern void si_detach(si_t *sih);
extern bool si_pci_war16165(si_t *sih);

extern uint si_corelist(si_t *sih, uint coreid[]);
extern uint si_coreid(si_t *sih);
extern uint si_flag(si_t *sih);
extern uint si_intflag(si_t *sih);
extern uint si_coreidx(si_t *sih);
extern uint si_coreunit(si_t *sih);
extern uint si_corevendor(si_t *sih);
extern uint si_corerev(si_t *sih);
extern void *si_osh(si_t *sih);
extern void si_setosh(si_t *sih, osl_t *osh);
extern uint si_corereg(si_t *sih, uint coreidx, uint regoff, uint mask, uint val);
extern void *si_coreregs(si_t *sih);
extern uint32 si_core_cflags(si_t *sih, uint32 mask, uint32 val);
extern void si_core_cflags_wo(si_t *sih, uint32 mask, uint32 val);
extern uint32 si_core_sflags(si_t *sih, uint32 mask, uint32 val);
extern bool si_iscoreup(si_t *sih);
extern uint si_findcoreidx(si_t *sih, uint coreid, uint coreunit);
extern void *si_setcoreidx(si_t *sih, uint coreidx);
extern void *si_setcore(si_t *sih, uint coreid, uint coreunit);
extern void *si_switch_core(si_t *sih, uint coreid, uint *origidx, uint *intr_val);
extern void si_restore_core(si_t *sih, uint coreid, uint intr_val);
extern int si_numaddrspaces(si_t *sih);
extern uint32 si_addrspace(si_t *sih, uint asidx);
extern uint32 si_addrspacesize(si_t *sih, uint asidx);
extern int si_corebist(si_t *sih);
extern void si_core_reset(si_t *sih, uint32 bits, uint32 resetbits);
extern void si_core_tofixup(si_t *sih);
extern void si_core_disable(si_t *sih, uint32 bits);
extern uint32 si_clock_rate(uint32 pll_type, uint32 n, uint32 m);
extern uint32 si_clock(si_t *sih);
extern void si_clock_pmu_spuravoid(si_t *sih, bool spuravoid);
extern uint32 si_alp_clock(si_t *sih);
extern uint32 si_ilp_clock(si_t *sih);
extern void si_pci_setup(si_t *sih, uint coremask);
extern void si_pcmcia_init(si_t *sih);
extern void si_setint(si_t *sih, int siflag);
extern bool si_backplane64(si_t *sih);
extern void si_register_intr_callback(si_t *sih, void *intrsoff_fn, void *intrsrestore_fn,
	void *intrsenabled_fn, void *intr_arg);
extern void si_deregister_intr_callback(si_t *sih);
extern void si_clkctl_init(si_t *sih);
extern uint16 si_clkctl_fast_pwrup_delay(si_t *sih);
extern bool si_clkctl_cc(si_t *sih, uint mode);
extern int si_clkctl_xtal(si_t *sih, uint what, bool on);
extern uint32 si_gpiotimerval(si_t *sih, uint32 mask, uint32 val);
extern bool si_backplane64(si_t *sih);
extern void si_btcgpiowar(si_t *sih);
extern bool si_deviceremoved(si_t *sih);
extern uint32 si_socram_size(si_t *sih);

extern void si_watchdog(si_t *sih, uint ticks);
extern void si_watchdog_ms(si_t *sih, uint32 ms);
extern void *si_gpiosetcore(si_t *sih);
extern uint32 si_gpiocontrol(si_t *sih, uint32 mask, uint32 val, uint8 priority);
extern uint32 si_gpioouten(si_t *sih, uint32 mask, uint32 val, uint8 priority);
extern uint32 si_gpioout(si_t *sih, uint32 mask, uint32 val, uint8 priority);
extern uint32 si_gpioin(si_t *sih);
extern uint32 si_gpiointpolarity(si_t *sih, uint32 mask, uint32 val, uint8 priority);
extern uint32 si_gpiointmask(si_t *sih, uint32 mask, uint32 val, uint8 priority);
extern uint32 si_gpioled(si_t *sih, uint32 mask, uint32 val);
extern uint32 si_gpioreserve(si_t *sih, uint32 gpio_num, uint8 priority);
extern uint32 si_gpiorelease(si_t *sih, uint32 gpio_num, uint8 priority);
extern uint32 si_gpiopull(si_t *sih, bool updown, uint32 mask, uint32 val);
extern uint32 si_gpioevent(si_t *sih, uint regtype, uint32 mask, uint32 val);
extern uint32 si_gpio_int_enable(si_t *sih, bool enable);


extern void *si_gpio_handler_register(si_t *sih, uint32 e, bool lev, gpio_handler_t cb, void *arg);
extern void si_gpio_handler_unregister(si_t *sih, void* gpioh);
extern void si_gpio_handler_process(si_t *sih);


extern bool si_pci_pmecap(si_t *sih);
struct osl_info;
extern bool si_pci_fastpmecap(struct osl_info *osh);
extern bool si_pci_pmeclr(si_t *sih);
extern void si_pci_pmeen(si_t *sih);
extern uint si_pcie_readreg(void *sih, uint addrtype, uint offset);

extern void si_sdio_init(si_t *sih);

extern uint16 si_d11_devid(si_t *sih);
extern int si_corepciid(si_t *sih, uint func, uint16 *pcivendor, uint16 *pcidevice,
	uint8 *pciclass, uint8 *pcisubclass, uint8 *pciprogif, uint8 *pciheader);

#ifdef BCMECICOEX
extern void *si_eci_init(si_t *sih);
extern void si_eci_notify_bt(si_t *sih, uint32 mask, uint32 val, bool interrupt);
#else
#define si_eci_init(sih) (0)
#define si_eci_notify_bt(sih, type, val, interrupt)  (0)
#endif 

#if !defined(BCMDONGLEHOST)

extern bool si_is_otp_disabled(si_t *sih);
extern bool si_is_otp_powered(si_t *sih);
extern void si_otp_power(si_t *sih, bool on);


extern bool si_is_sprom_available(si_t *sih);
extern bool si_is_sprom_enabled(si_t *sih);
extern void si_sprom_enable(si_t *sih, bool enable);


extern int si_cis_source(si_t *sih);
#define CIS_DEFAULT	0
#define CIS_SROM	1
#define CIS_OTP		2
#endif 


extern int si_devpath(si_t *sih, char *path, int size);

extern char *si_getdevpathvar(si_t *sih, const char *name);
extern int si_getdevpathintvar(si_t *sih, const char *name);


extern uint8 si_pcieclkreq(si_t *sih, uint32 mask, uint32 val);
extern void si_war42780_clkreq(si_t *sih, bool clkreq);
extern void si_pci_sleep(si_t *sih);
extern void si_pci_down(si_t *sih);
extern void si_pci_up(si_t *sih);
extern void si_pcie_war_ovr_disable(si_t *sih);
extern void si_pcie_extendL1timer(si_t *sih, bool extend);
extern int si_pci_fixcfg(si_t *sih);
#ifndef BCMDONGLEHOST
extern bool si_ldo_war(si_t *sih, uint devid);
#endif



#if defined(BCMDBG_ASSERT) || defined(BCMDBG_ERR) || defined(BCMDBG_DUMP)
extern bool si_taclear(si_t *sih, bool details);
#endif

#ifdef BCMDBG
extern void si_view(si_t *sih, bool verbose);
extern void si_viewall(si_t *sih, bool verbose);
extern uint32 si_pcielcreg(si_t *sih, uint32 mask, uint32 val);
#endif

#if defined(BCMDBG_DUMP)
extern void si_dump(si_t *sih, struct bcmstrbuf *b);
extern void si_ccreg_dump(si_t *sih, struct bcmstrbuf *b);
extern void si_clkctl_dump(si_t *sih, struct bcmstrbuf *b);
extern int si_gpiodump(si_t *sih, struct bcmstrbuf *b);
#endif
#if defined(BCMDBG) || defined(BCMDBG_DUMP)
extern void si_dumpregs(si_t *sih, struct bcmstrbuf *b);
#endif


#if !defined(BCMDONGLEHOST)
extern void si_4329_tweak(si_t *sih, uint32 mask, uint32 val);
extern void si_4329_vbatmeas_on(si_t *sih, uint32 *save_reg0, uint32 *save_reg5);
extern void si_4329_vbatmeas_off(si_t *sih, uint32 save_reg0, uint32 save_reg5);
#endif 

#endif	
