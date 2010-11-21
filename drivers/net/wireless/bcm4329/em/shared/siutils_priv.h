/*
 * Include file private to the SOC Interconnect support files.
 *
 * Copyright (C) 2010, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: siutils_priv.h,v 1.3.10.5.4.2 2009/09/22 13:28:16 Exp $
 */

#ifndef	_siutils_priv_h_
#define	_siutils_priv_h_


#ifdef BCMDBG_ERR
#define	SI_ERROR(args)	printf args
#else
#define	SI_ERROR(args)
#endif	

#ifdef BCMDBG
#define	SI_MSG(args)	printf args
#else
#define	SI_MSG(args)
#endif	

#define	IS_SIM(chippkg)	((chippkg == HDLSIM_PKG_ID) || (chippkg == HWSIM_PKG_ID))

typedef uint32 (*si_intrsoff_t)(void *intr_arg);
typedef void (*si_intrsrestore_t)(void *intr_arg, uint32 arg);
typedef bool (*si_intrsenabled_t)(void *intr_arg);

typedef struct gpioh_item {
	void			*arg;
	bool			level;
	gpio_handler_t		handler;
	uint32			event;
	struct gpioh_item	*next;
} gpioh_item_t;


typedef struct si_common_info {
	void	*regs[SI_MAXCORES];	
	void	*regs2[SI_MAXCORES];	
	uint	coreid[SI_MAXCORES];	
	uint32	cia[SI_MAXCORES];	
	uint32	cib[SI_MAXCORES];	
	uint32	coresba_size[SI_MAXCORES]; 
	uint32	coresba2_size[SI_MAXCORES]; 
	uint32	coresba[SI_MAXCORES];	
	uint32	coresba2[SI_MAXCORES];	
	void	*wrappers[SI_MAXCORES];	
	uint32	wrapba[SI_MAXCORES];	
	uint32	oob_router;		
	uint8	attach_count;
} si_common_info_t;

typedef struct si_info {
	struct si_pub pub;		

	void	*osh;			
	void	*sdh;			
	void *pch;			
	uint	dev_coreid;		
	void	*intr_arg;		
	si_intrsoff_t intrsoff_fn;	
	si_intrsrestore_t intrsrestore_fn; 
	si_intrsenabled_t intrsenabled_fn; 


	gpioh_item_t *gpioh_head; 	

	bool	memseg;			

	char *vars;
	uint varsz;

	void	*curmap;		

	uint	curidx;			
	uint	numcores;		
	void	*curwrap;		
	si_common_info_t	*common_info;	
} si_info_t;

#define	SI_INFO(sih)	(si_info_t *)(uintptr)sih

#define	GOODCOREADDR(x, b) (((x) >= (b)) && ((x) < ((b) + SI_MAXCORES * SI_CORE_SIZE)) && \
		ISALIGNED((x), SI_CORE_SIZE))
#define	GOODREGS(regs)	((regs) != NULL && ISALIGNED((uintptr)(regs), SI_CORE_SIZE))
#define BADCOREADDR	0
#define	GOODIDX(idx)	(((uint)idx) < SI_MAXCORES)
#define	BADIDX		(SI_MAXCORES + 1)
#define	NOREV		-1		

#define PCI(si)		((BUSTYPE((si)->pub.bustype) == PCI_BUS) &&	\
			 ((si)->pub.buscoretype == PCI_CORE_ID))
#define PCIE(si)	((BUSTYPE((si)->pub.bustype) == PCI_BUS) &&	\
			 ((si)->pub.buscoretype == PCIE_CORE_ID))
#define PCMCIA(si)	((BUSTYPE((si)->pub.bustype) == PCMCIA_BUS) && ((si)->memseg == TRUE))


#define SI_FAST(si) (((si)->pub.buscoretype == PCIE_CORE_ID) ||	\
		     (((si)->pub.buscoretype == PCI_CORE_ID) && (si)->pub.buscorerev >= 13))

#define PCIEREGS(si) (((char *)((si)->curmap) + PCI_16KB0_PCIREGS_OFFSET))
#define CCREGS_FAST(si) (((char *)((si)->curmap) + PCI_16KB0_CCREGS_OFFSET))


#define INTR_OFF(si, intr_val) \
	if ((si)->intrsoff_fn && (si)->common_info->coreid[(si)->curidx] == (si)->dev_coreid) {	\
		intr_val = (*(si)->intrsoff_fn)((si)->intr_arg); }
#define INTR_RESTORE(si, intr_val) \
	if ((si)->intrsrestore_fn && (si)->common_info->coreid[(si)->curidx] == (si)->dev_coreid) {\
		(*(si)->intrsrestore_fn)((si)->intr_arg, intr_val); }


#define	LPOMINFREQ		25000		
#define	LPOMAXFREQ		43000		
#define	XTALMINFREQ		19800000	
#define	XTALMAXFREQ		20200000	
#define	PCIMINFREQ		25000000	
#define	PCIMAXFREQ		34000000	

#define	ILP_DIV_5MHZ		0		
#define	ILP_DIV_1MHZ		4		

#define PCI_FORCEHT(si)	\
	(((PCIE(si)) && (si->pub.chip == BCM4311_CHIP_ID) && ((si->pub.chiprev <= 1))) || \
	((PCI(si) || PCIE(si)) && (si->pub.chip == BCM4321_CHIP_ID)))


#define DEFAULT_GPIO_ONTIME	10		
#define DEFAULT_GPIO_OFFTIME	90		

#ifndef DEFAULT_GPIOTIMERVAL
#define DEFAULT_GPIOTIMERVAL  ((DEFAULT_GPIO_ONTIME << GPIO_ONTIME_SHIFT) | DEFAULT_GPIO_OFFTIME)
#endif


extern void sb_scan(si_t *sih, void *regs, uint devid);
extern uint sb_coreid(si_t *sih);
extern uint sb_flag(si_t *sih);
extern void sb_setint(si_t *sih, int siflag);
extern uint sb_corevendor(si_t *sih);
extern uint sb_corerev(si_t *sih);
extern uint sb_corereg(si_t *sih, uint coreidx, uint regoff, uint mask, uint val);
extern bool sb_iscoreup(si_t *sih);
extern void *sb_setcoreidx(si_t *sih, uint coreidx);
extern uint32 sb_core_cflags(si_t *sih, uint32 mask, uint32 val);
extern void sb_core_cflags_wo(si_t *sih, uint32 mask, uint32 val);
extern uint32 sb_core_sflags(si_t *sih, uint32 mask, uint32 val);
extern void sb_commit(si_t *sih);
extern uint32 sb_base(uint32 admatch);
extern uint32 sb_size(uint32 admatch);
extern void sb_core_reset(si_t *sih, uint32 bits, uint32 resetbits);
extern void sb_core_tofixup(si_t *sih);
extern void sb_core_disable(si_t *sih, uint32 bits);
extern uint32 sb_addrspace(si_t *sih, uint asidx);
extern uint32 sb_addrspacesize(si_t *sih, uint asidx);
extern int sb_numaddrspaces(si_t *sih);

extern uint32 sb_set_initiator_to(si_t *sih, uint32 to, uint idx);

#if defined(BCMDBG_ASSERT) || defined(BCMDBG_ERR) || defined(BCMDBG_DUMP)
extern bool sb_taclear(si_t *sih, bool details);
#endif 

#ifdef BCMDBG
extern void sb_view(si_t *sih, bool verbose);
extern void sb_viewall(si_t *sih, bool verbose);
#endif
#if defined(BCMDBG_DUMP)
extern void sb_dump(si_t *sih, struct bcmstrbuf *b);
#endif
#if defined(BCMDBG) || defined(BCMDBG_DUMP)
extern void sb_dumpregs(si_t *sih, struct bcmstrbuf *b);
#endif


extern bool sb_pci_pmecap(si_t *sih);
struct osl_info;
extern bool sb_pci_fastpmecap(struct osl_info *osh);
extern bool sb_pci_pmeclr(si_t *sih);
extern void sb_pci_pmeen(si_t *sih);
extern uint sb_pcie_readreg(void *sih, uint addrtype, uint offset);


extern si_t *ai_attach(uint pcidev, osl_t *osh, void *regs, uint bustype,
                       void *sdh, char **vars, uint *varsz);
extern si_t *ai_kattach(osl_t *osh);
extern void ai_scan(si_t *sih, void *regs, uint devid);

extern uint BCMROMFN(ai_flag)(si_t *sih);
extern void ai_setint(si_t *sih, int siflag);
extern uint ai_coreidx(si_t *sih);
extern uint ai_corevendor(si_t *sih);
extern uint BCMROMFN(ai_corerev)(si_t *sih);
extern bool BCMROMFN(ai_iscoreup)(si_t *sih);
extern void *BCMROMFN(ai_setcoreidx)(si_t *sih, uint coreidx);
extern uint32 BCMROMFN(ai_core_cflags)(si_t *sih, uint32 mask, uint32 val);
extern void ai_core_cflags_wo(si_t *sih, uint32 mask, uint32 val);
extern uint32 BCMROMFN(ai_core_sflags)(si_t *sih, uint32 mask, uint32 val);
extern uint BCMROMFN(ai_corereg)(si_t *sih, uint coreidx, uint regoff, uint mask, uint val);
extern void BCMROMFN(ai_core_reset)(si_t *sih, uint32 bits, uint32 resetbits);
extern void BCMROMFN(ai_core_disable)(si_t *sih, uint32 bits);
extern int ai_numaddrspaces(si_t *sih);
extern uint32 ai_addrspace(si_t *sih, uint asidx);
extern uint32 ai_addrspacesize(si_t *sih, uint asidx);
extern void ai_write_wrap_reg(si_t *sih, uint32 offset, uint32 val);

#ifdef BCMDBG
extern void ai_view(si_t *sih, bool verbose);
#endif
#if defined(BCMDBG) || defined(BCMDBG_DUMP)
extern void ai_dumpregs(si_t *sih, struct bcmstrbuf *b);
#endif

#endif	
