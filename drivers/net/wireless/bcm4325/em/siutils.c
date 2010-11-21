/*
 * Misc utility routines for accessing chip-specific features
 * of the SiliconBackplane-based Broadcom chips.
 *
 * Copyright (C) 2009, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: siutils.c,v 1.662.4.4.4.22.4.1 2008/12/22 01:19:31 Exp $
 */

#include <typedefs.h>
#include <bcmdefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <siutils.h>
#include <bcmdevs.h>
#include <hndsoc.h>
#include <sbchipc.h>
#if !defined(BCMDONGLEHOST)
#include <pci_core.h>
#include <pcie_core.h>
#include <nicpci.h>
#include <bcmnvram.h>
#include <bcmsrom.h>
#endif 
#include <pcicfg.h>
#include <sbpcmcia.h>
#include <sbsocram.h>
#ifdef BCMECICOEX
#include <bcmotp.h>
#endif 
#include <bcmsdh.h>
#include <sdio.h>
#include <sbsdio.h>
#include <sbhnddma.h>
#include <sbsdpcmdev.h>
#include <hndpmu.h>
#ifdef BCMSPI
#include <spid.h>
#endif 

#include "siutils_priv.h"


static si_info_t *si_doattach(si_info_t *sii, uint devid, osl_t *osh, void *regs,
                              uint bustype, void *sdh, char **vars, uint *varsz);
static bool si_buscore_prep(si_info_t *sii, uint bustype, uint devid, void *sdh);
static bool si_buscore_setup(si_info_t *sii, chipcregs_t *cc, uint bustype, uint32 savewin,
	uint *origidx, void *regs);
#if !defined(BCMDONGLEHOST)
static void si_nvram_process(si_info_t *sii, char *pvars);


static char *si_devpathvar(si_t *sih, char *var, int len, const char *name);
static bool _si_clkctl_cc(si_info_t *sii, uint mode);
static bool si_ispcie(si_info_t *sii);
#endif 



static uint32 si_gpioreservation = 0;
static void *common_info_alloced = NULL;


#ifdef _HNDRTE_
static bool si_onetimeinit = FALSE;
#endif


si_t *
BCMATTACHFN(si_attach)(uint devid, osl_t *osh, void *regs,
                       uint bustype, void *sdh, char **vars, uint *varsz)
{
	si_info_t *sii;

	
	if ((sii = MALLOC(osh, sizeof (si_info_t))) == NULL) {
		SI_ERROR(("si_attach: malloc failed! malloced %d bytes\n", MALLOCED(osh)));
		return (NULL);
	}

	if (si_doattach(sii, devid, osh, regs, bustype, sdh, vars, varsz) == NULL) {
#ifndef DONGLEBUILD
		if (NULL != sii->common_info)
			MFREE(osh, sii->common_info, sizeof(si_common_info_t));
#endif
		MFREE(osh, sii, sizeof(si_info_t));
		return (NULL);
	}
	sii->vars = vars ? *vars : NULL;
	sii->varsz = varsz ? *varsz : 0;

	return (si_t *)sii;
}


static si_info_t ksii;

static uint32	wd_msticks;		


si_t *
BCMATTACHFN(si_kattach)(osl_t *osh)
{
	static bool ksii_attached = FALSE;

	if (!ksii_attached) {
		void *regs = REG_MAP(SI_ENUM_BASE, SI_CORE_SIZE);

		if (si_doattach(&ksii, BCM4710_DEVICE_ID, osh, regs,
		                SI_BUS, NULL,
		                osh != SI_OSH ? &ksii.vars : NULL,
		                osh != SI_OSH ? &ksii.varsz : NULL) == NULL) {
			if (NULL != ksii.common_info)
				MFREE(osh, ksii.common_info, sizeof(si_common_info_t));
			SI_ERROR(("si_kattach: si_doattach failed\n"));
			REG_UNMAP(regs);
			return NULL;
		}
		REG_UNMAP(regs);

		
		if (PMUCTL_ENAB(&ksii.pub)) {
			
			wd_msticks = 32;
		} else {
#ifndef	BCMDONGLEHOST
			if (ksii.pub.ccrev < 18)
				wd_msticks = si_clock(&ksii.pub) / 1000;
			else
				wd_msticks = si_alp_clock(&ksii.pub) / 1000;
#else
			wd_msticks = ALP_CLOCK / 1000;
#endif 
		}

		ksii_attached = TRUE;
		SI_MSG(("si_kattach done. ccrev = %d, wd_msticks = %d\n",
		        ksii.pub.ccrev, wd_msticks));
	}

	return &ksii.pub;
}

#ifndef	BCMDONGLEHOST
bool
si_ldo_war(si_t *sih, uint devid)
{
	si_info_t *sii = SI_INFO(sih);
	uint32 w;
	chipcregs_t *cc;
	void *regs = sii->curmap;
	uint32 rev_id, ccst;

	rev_id = OSL_PCI_READ_CONFIG(sii->osh, PCI_CFG_REV, sizeof(uint32));
	rev_id &= 0xff;
	if (!(((devid == BCM4322_CHIP_ID) ||
	      (devid == BCM4322_D11N_ID) ||
	      (devid == BCM4322_D11N2G_ID) ||
	      (devid == BCM4322_D11N5G_ID)) &&
	      (rev_id == 0)))
		return TRUE;

	SI_MSG(("si_ldo_war: PCI devid 0x%x rev %d, HACK to fix 4322a0 LDO/PMU\n", devid, rev_id));

	
	w = OSL_PCI_READ_CONFIG(sii->osh, PCI_BAR0_WIN, sizeof(uint32));
	OSL_PCI_WRITE_CONFIG(sii->osh, PCI_BAR0_WIN, sizeof(uint32), SI_ENUM_BASE);
	cc = (chipcregs_t *)regs;

	
	W_REG(sii->osh, &cc->regcontrol_addr, 0);
	
	W_REG(sii->osh, &cc->regcontrol_data, 0x3001);

	OSL_DELAY(5000);

	
	W_REG(sii->osh, &cc->min_res_mask, 0x0d);

	SPINWAIT(((ccst = OSL_PCI_READ_CONFIG(sii->osh, PCI_CLK_CTL_ST, 4)) & CCS_ALPAVAIL)
		 == 0, PMU_MAX_TRANSITION_DLY);

	if ((ccst & CCS_ALPAVAIL) == 0) {
		SI_ERROR(("ALP never came up clk_ctl_st: 0x%x\n", ccst));
		return FALSE;
	}
	SI_MSG(("si_ldo_war: 4322a0 HACK done\n"));

	OSL_PCI_WRITE_CONFIG(sii->osh, PCI_BAR0_WIN, sizeof(uint32), w);

	return TRUE;
}
#endif 

static bool
BCMATTACHFN(si_buscore_prep)(si_info_t *sii, uint bustype, uint devid, void *sdh)
{
	
	if (BUSTYPE(bustype) == PCMCIA_BUS)
		sii->memseg = TRUE;

#ifndef	BCMDONGLEHOST
	if (BUSTYPE(bustype) == PCI_BUS) {
		if (!si_ldo_war((si_t *)sii, devid))
			return FALSE;
	}

	
	if (BUSTYPE(bustype) == PCI_BUS && !si_ispcie(sii))
		si_clkctl_xtal(&sii->pub, XTAL|PLL, ON);
#endif 

#if defined(BCMDONGLEHOST)
	if (BUSTYPE(bustype) == SDIO_BUS) {
		int err;
		uint8 clkset;

		
		clkset = SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_ALP_AVAIL_REQ;
		bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, clkset, &err);
		if (!err) {
			uint8 clkval;

			
			clkval = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, NULL);
			if ((clkval & ~SBSDIO_AVBITS) == clkset) {
				SPINWAIT(((clkval = bcmsdh_cfg_read(sdh, SDIO_FUNC_1,
					SBSDIO_FUNC1_CHIPCLKCSR, NULL)), !SBSDIO_ALPAV(clkval)),
					PMU_MAX_TRANSITION_DLY);
				if (!SBSDIO_ALPAV(clkval)) {
					SI_ERROR(("timeout on ALPAV wait, clkval 0x%02x\n",
						clkval));
					return FALSE;
				}
				clkset = SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_FORCE_ALP;
				bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
					clkset, &err);
				OSL_DELAY(65);
			}
		}

		
		bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_SDIOPULLUP, 0, NULL);
	}

#ifdef BCMSPI
	
	if (BUSTYPE(bustype) == SPI_BUS) {

		int err;
		uint32 regdata;
		
		regdata = bcmsdh_cfg_read_word(sdh, SDIO_FUNC_0, SPID_CONFIG, NULL);
		SI_MSG(("F0 REG0 rd = 0x%x\n", regdata));
		regdata |= WAKE_UP;

		bcmsdh_cfg_write_word(sdh, SDIO_FUNC_0, SPID_CONFIG, regdata, &err);

		OSL_DELAY(100000);
	}
#endif 
#endif 

	return TRUE;
}

static bool
BCMATTACHFN(si_buscore_setup)(si_info_t *sii, chipcregs_t *cc, uint bustype, uint32 savewin,
	uint *origidx, void *regs)
{
	bool pci, pcie;
	uint i;
	uint pciidx, pcieidx, pcirev, pcierev;

	cc = si_setcoreidx(&sii->pub, SI_CC_IDX);
	ASSERT((uintptr)cc);

	
	sii->pub.ccrev = (int)si_corerev(&sii->pub);

	
	if (sii->pub.ccrev >= 11)
		sii->pub.chipst = R_REG(sii->osh, &cc->chipstatus);

	
	sii->pub.cccaps = R_REG(sii->osh, &cc->capabilities);

	
	if (sii->pub.cccaps & CC_CAP_PMU) {
		sii->pub.pmucaps = R_REG(sii->osh, &cc->pmucapabilities);
		sii->pub.pmurev = sii->pub.pmucaps & PCAP_REV_MASK;
	}

	SI_MSG(("Chipc: rev %d, caps 0x%x, chipst 0x%x pmurev %d, pmucaps 0x%x\n",
		sii->pub.ccrev, sii->pub.cccaps, sii->pub.chipst, sii->pub.pmurev,
		sii->pub.pmucaps));

	
	sii->pub.buscoretype = NODEV_CORE_ID;
	sii->pub.buscorerev = NOREV;
	sii->pub.buscoreidx = BADIDX;

	pci = pcie = FALSE;
	pcirev = pcierev = NOREV;
	pciidx = pcieidx = BADIDX;

	for (i = 0; i < sii->numcores; i++) {
		uint cid, crev;

		si_setcoreidx(&sii->pub, i);
		cid = si_coreid(&sii->pub);
		crev = si_corerev(&sii->pub);

		
		SI_MSG(("CORE[%d]: id 0x%x rev %d base 0x%x regs 0x%p\n",
		        i, cid, crev, sii->common_info->coresba[i], sii->common_info->regs[i]));

		if (BUSTYPE(bustype) == PCI_BUS) {
			if (cid == PCI_CORE_ID) {
				pciidx = i;
				pcirev = crev;
				pci = TRUE;
			} else if (cid == PCIE_CORE_ID) {
				pcieidx = i;
				pcierev = crev;
				pcie = TRUE;
			}
		} else if ((BUSTYPE(bustype) == PCMCIA_BUS) &&
		           (cid == PCMCIA_CORE_ID)) {
			sii->pub.buscorerev = crev;
			sii->pub.buscoretype = cid;
			sii->pub.buscoreidx = i;
		}
		else if (((BUSTYPE(bustype) == SDIO_BUS) ||
		          (BUSTYPE(bustype) == SPI_BUS)) &&
		         ((cid == PCMCIA_CORE_ID) ||
		          (cid == SDIOD_CORE_ID))) {
			sii->pub.buscorerev = crev;
			sii->pub.buscoretype = cid;
			sii->pub.buscoreidx = i;
		}

		
		if ((savewin && (savewin == sii->common_info->coresba[i])) ||
		    (regs == sii->common_info->regs[i]))
			*origidx = i;
	}

#if !defined(BCMDONGLEHOST)
	if (pci && pcie) {
		if (si_ispcie(sii))
			pci = FALSE;
		else
			pcie = FALSE;
	}
	if (pci) {
		sii->pub.buscoretype = PCI_CORE_ID;
		sii->pub.buscorerev = pcirev;
		sii->pub.buscoreidx = pciidx;
	} else if (pcie) {
		sii->pub.buscoretype = PCIE_CORE_ID;
		sii->pub.buscorerev = pcierev;
		sii->pub.buscoreidx = pcieidx;
	}
#endif 

	SI_MSG(("Buscore id/type/rev %d/0x%x/%d\n", sii->pub.buscoreidx, sii->pub.buscoretype,
		sii->pub.buscorerev));

	if (BUSTYPE(sii->pub.bustype) == SI_BUS && (CHIPID(sii->pub.chip) == BCM4712_CHIP_ID) &&
	    (sii->pub.chippkg != BCM4712LARGE_PKG_ID) && (sii->pub.chiprev <= 3))
		OR_REG(sii->osh, &cc->slow_clk_ctl, SCC_SS_XTAL);

#if !defined(BCMDONGLEHOST)
	
	if (BUSTYPE(sii->pub.bustype) == PCI_BUS) {
		if (SI_FAST(sii)) {
			if (!sii->pch &&
			    ((sii->pch = (void *)(uintptr)pcicore_init(&sii->pub, sii->osh,
				(void *)PCIEREGS(sii))) == NULL))
				return FALSE;
		}
		if (si_pci_fixcfg(&sii->pub)) {
			SI_ERROR(("si_doattach: sb_pci_fixcfg failed\n"));
			return FALSE;
		}
	}
#endif 

#if defined(BCMDONGLEHOST)
	
	if ((BUSTYPE(bustype) == SDIO_BUS) || (BUSTYPE(bustype) == SPI_BUS)) {
		if (si_setcore(&sii->pub, ARM7S_CORE_ID, 0) ||
		    si_setcore(&sii->pub, ARMCM3_CORE_ID, 0))
			si_core_disable(&sii->pub, 0);
	}
#endif 

	
	si_setcoreidx(&sii->pub, *origidx);

	return TRUE;
}

#if !defined(BCMDONGLEHOST)
static void
BCMATTACHFN(si_nvram_process)(si_info_t *sii, char *pvars)
{
	uint w = 0;
	if (BUSTYPE(sii->pub.bustype) == PCMCIA_BUS) {
		w = getintvar(pvars, "regwindowsz");
		sii->memseg = (w <= CFTABLE_REGWIN_2K) ? TRUE : FALSE;
	}

	
	switch (BUSTYPE(sii->pub.bustype)) {
	case PCI_BUS:
		
		w = OSL_PCI_READ_CONFIG(sii->osh, PCI_CFG_SVID, sizeof(uint32));
		
		if ((sii->pub.boardvendor = (uint16)si_getdevpathintvar(&sii->pub, "boardvendor"))
			== 0)
			sii->pub.boardvendor = w & 0xffff;
		else
			SI_ERROR(("Overriding boardvendor: 0x%x instead of 0x%x\n",
				sii->pub.boardvendor, w & 0xffff));
		if ((sii->pub.boardtype = (uint16)si_getdevpathintvar(&sii->pub, "boardtype"))
			== 0)
			sii->pub.boardtype = (w >> 16) & 0xffff;
		else
			SI_ERROR(("Overriding boardtype: 0x%x instead of 0x%x\n",
				sii->pub.boardtype, (w >> 16) & 0xffff));
		break;

	case PCMCIA_BUS:
	case SDIO_BUS:
		sii->pub.boardvendor = getintvar(pvars, "manfid");
		sii->pub.boardtype = getintvar(pvars, "prodid");
		break;

	case SPI_BUS:
		sii->pub.boardvendor = VENDOR_BROADCOM;
		sii->pub.boardtype = QT4710_BOARD;
		break;

	case SI_BUS:
	case JTAG_BUS:
		sii->pub.boardvendor = VENDOR_BROADCOM;
		if (pvars == NULL || ((sii->pub.boardtype = getintvar(pvars, "prodid")) == 0))
			if ((sii->pub.boardtype = getintvar(NULL, "boardtype")) == 0)
				sii->pub.boardtype = 0xffff;
		break;
	}

	if (sii->pub.boardtype == 0) {
		SI_ERROR(("si_doattach: unknown board type\n"));
		ASSERT(sii->pub.boardtype);
	}

	sii->pub.boardflags = getintvar(pvars, "boardflags");
}
#endif 

static si_info_t *
BCMATTACHFN(si_doattach)(si_info_t *sii, uint devid, osl_t *osh, void *regs,
                       uint bustype, void *sdh, char **vars, uint *varsz)
{
	struct si_pub *sih = &sii->pub;
	uint32 w, savewin;
	chipcregs_t *cc;
	char *pvars = NULL;
	uint origidx;

	ASSERT(GOODREGS(regs));

	bzero((uchar*)sii, sizeof(si_info_t));


#ifdef DONGLEBUILD
	if (NULL == common_info_alloced)
#endif
	{
		if (NULL == (common_info_alloced = (void *)MALLOC(osh, sizeof(si_common_info_t)))) {
			SI_ERROR(("si_doattach: malloc failed! malloced %dbytes\n", MALLOCED(osh)));
			return (NULL);
		}
		bzero((uchar*)(common_info_alloced), sizeof(si_common_info_t));
	}
	sii->common_info = (si_common_info_t *)common_info_alloced;
	sii->common_info->attach_count++;

	savewin = 0;

	sih->buscoreidx = BADIDX;

	sii->curmap = regs;
	sii->sdh = sdh;
	sii->osh = osh;

#if !defined(BCMDONGLEHOST)
	
	if ((bustype == PCI_BUS) &&
	    (OSL_PCI_READ_CONFIG(sii->osh, PCI_SPROM_CONTROL, sizeof(uint32)) == 0xffffffff)) {
		SI_ERROR(("%s: incoming bus is PCI but it's a lie, switching to SI "
		          "devid:0x%x\n", __FUNCTION__, devid));
		bustype = SI_BUS;
	}
#endif 

	
	if (bustype == PCI_BUS) {
		savewin = OSL_PCI_READ_CONFIG(sii->osh, PCI_BAR0_WIN, sizeof(uint32));
		if (!GOODCOREADDR(savewin, SI_ENUM_BASE))
			savewin = SI_ENUM_BASE;
		OSL_PCI_WRITE_CONFIG(sii->osh, PCI_BAR0_WIN, 4, SI_ENUM_BASE);
		cc = (chipcregs_t *)regs;
	} else
	if ((bustype == SDIO_BUS) || (bustype == SPI_BUS)) {
		cc = (chipcregs_t *)sii->curmap;
	} else {
		cc = (chipcregs_t *)REG_MAP(SI_ENUM_BASE, SI_CORE_SIZE);
	}

	sih->bustype = bustype;
	if (bustype != BUSTYPE(bustype)) {
		SI_ERROR(("si_doattach: bus type %d does not match configured bus type %d\n",
			bustype, BUSTYPE(bustype)));
		return NULL;
	}

	
	if (!si_buscore_prep(sii, bustype, devid, sdh)) {
		SI_ERROR(("si_doattach: si_core_clk_prep failed %d\n", bustype));
		return NULL;
	}

	
	w = R_REG(osh, &cc->chipid);
	sih->socitype = (w & CID_TYPE_MASK) >> CID_TYPE_SHIFT;
	
	sih->chip = w & CID_ID_MASK;
	sih->chiprev = (w & CID_REV_MASK) >> CID_REV_SHIFT;
	sih->chippkg = (w & CID_PKG_MASK) >> CID_PKG_SHIFT;
	if ((CHIPID(sih->chip) == BCM4329_CHIP_ID) && (sih->chippkg != BCM4329_289PIN_PKG_ID))
		sih->chippkg = BCM4329_182PIN_PKG_ID;
	sih->issim = IS_SIM(sih->chippkg);

	
	if (CHIPTYPE(sii->pub.socitype) == SOCI_SB) {
		SI_MSG(("Found chip type SB (0x%08x)\n", w));
		sb_scan(&sii->pub, regs, devid);
	} else if (CHIPTYPE(sii->pub.socitype) == SOCI_AI) {
		SI_MSG(("Found chip type AI (0x%08x)\n", w));
		
		ai_scan(&sii->pub, (void *)cc, devid);
	} else {
		SI_ERROR(("Found chip of unkown type (0x%08x)\n", w));
		return NULL;
	}
	
	if (sii->numcores == 0) {
		SI_ERROR(("si_doattach: could not find any cores\n"));
		return NULL;
	}
	
	origidx = SI_CC_IDX;
	if (!si_buscore_setup(sii, cc, bustype, savewin, &origidx, regs)) {
		SI_ERROR(("si_doattach: si_buscore_setup failed\n"));
		return NULL;
	}

#if !defined(BCMDONGLEHOST)
	
	nvram_init((void *)&(sii->pub));

	
	if (srom_var_init(&sii->pub, BUSTYPE(bustype), regs, sii->osh, vars, varsz)) {
		SI_ERROR(("si_doattach: srom_var_init failed: bad srom\n"));
		return (NULL);
	}
	pvars = vars ? *vars : NULL;
	si_nvram_process(sii, pvars);

	
#else
	pvars = NULL;
#endif 

#ifdef _HNDRTE_
	if (!si_onetimeinit) {
#endif
		if (sii->pub.ccrev >= 20) {
			cc = (chipcregs_t *)si_setcore(sih, CC_CORE_ID, 0);
			W_REG(osh, &cc->gpiopullup, 0);
			W_REG(osh, &cc->gpiopulldown, 0);
			si_setcoreidx(sih, origidx);
		}

		
#if !defined(BCMDONGLEHOST)

		
		if (PMUCTL_ENAB(sih)) {
			si_pmu_init(sih, sii->osh);
			si_pmu_chip_init(sih, sii->osh);
			si_pmu_pll_init(sih, sii->osh, getintvar(pvars, "xtalfreq"));
			si_pmu_res_init(sih, sii->osh);
			si_pmu_swreg_init(sih, sii->osh);
		}
#endif 
#ifdef _HNDRTE_
		si_onetimeinit = TRUE;
	}
#endif

#if !defined(BCMDONGLEHOST)
	
	if (sii->pub.ccrev >= 16) {
		if ((w = getintvar(pvars, "leddc")) == 0)
			w = DEFAULT_GPIOTIMERVAL;
		si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, gpiotimerval), ~0, w);
	}

	if (PCI_FORCEHT(sii)) {
		SI_MSG(("si_doattach: force HT\n"));
		sih->pci_pr32414 = TRUE;
		si_clkctl_init(sih);
		_si_clkctl_cc(sii, CLK_FAST);
	}

	if (PCIE(sii)) {
		ASSERT(sii->pch != NULL);

		pcicore_attach(sii->pch, pvars, SI_DOATTACH);

		if (((sih->chip == BCM4311_CHIP_ID) && (sih->chiprev == 2)) ||
		    (sih->chip == BCM4312_CHIP_ID)) {
			SI_MSG(("si_doattach: clear initiator timeout\n"));
			sb_set_initiator_to(sih, 0x3, si_findcoreidx(sih, D11_CORE_ID, 0));
		}
	}
#endif 

#ifdef BCMDBG
	
	si_taclear(sih, FALSE);
#endif	

	return (sii);
}


void
si_detach(si_t *sih)
{
	si_info_t *sii;
	uint idx;

	sii = SI_INFO(sih);

	if (sii == NULL)
		return;

	if (BUSTYPE(sih->bustype) == SI_BUS)
		for (idx = 0; idx < SI_MAXCORES; idx++)
			if (sii->common_info->regs[idx]) {
				REG_UNMAP(sii->common_info->regs[idx]);
				sii->common_info->regs[idx] = NULL;
			}

#if !defined(BCMDONGLEHOST)
	if (BUSTYPE(sih->bustype) == PCI_BUS) {
		if (sii->pch)
			pcicore_deinit(sii->pch);
		sii->pch = NULL;
	}
#endif 

	if (1 == sii->common_info->attach_count--) {
		MFREE(sii->osh, sii->common_info, sizeof(si_common_info_t));
		common_info_alloced = NULL;
	}

#if !defined(BCMBUSTYPE) || (BCMBUSTYPE == SI_BUS)
	if (sii != &ksii)
#endif	
		MFREE(sii->osh, sii, sizeof(si_info_t));
}

void *
si_osh(si_t *sih)
{
	si_info_t *sii;

	sii = SI_INFO(sih);
	return sii->osh;
}

void
si_setosh(si_t *sih, osl_t *osh)
{
	si_info_t *sii;

	sii = SI_INFO(sih);
	if (sii->osh != NULL) {
		SI_ERROR(("osh is already set....\n"));
		ASSERT(!sii->osh);
	}
	sii->osh = osh;
}


void
si_register_intr_callback(si_t *sih, void *intrsoff_fn, void *intrsrestore_fn,
                          void *intrsenabled_fn, void *intr_arg)
{
	si_info_t *sii;

	sii = SI_INFO(sih);
	sii->intr_arg = intr_arg;
	sii->intrsoff_fn = (si_intrsoff_t)intrsoff_fn;
	sii->intrsrestore_fn = (si_intrsrestore_t)intrsrestore_fn;
	sii->intrsenabled_fn = (si_intrsenabled_t)intrsenabled_fn;
	
	sii->dev_coreid = sii->common_info->coreid[sii->curidx];
}

void
si_deregister_intr_callback(si_t *sih)
{
	si_info_t *sii;

	sii = SI_INFO(sih);
	sii->intrsoff_fn = NULL;
}

uint
si_intflag(si_t *sih)
{
	si_info_t *sii = SI_INFO(sih);
	if (CHIPTYPE(sih->socitype) == SOCI_SB) {
		sbconfig_t *ccsbr = (sbconfig_t *)((uintptr)((ulong)
			    (sii->common_info->coresba[SI_CC_IDX]) + SBCONFIGOFF));
		return R_REG(sii->osh, &ccsbr->sbflagst);
	} else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		return R_REG(sii->osh, ((uint32 *)(uintptr)
			    (sii->common_info->oob_router + OOB_STATUSA)));
	else {
		ASSERT(0);
		return 0;
	}
}

uint
si_flag(si_t *sih)
{
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		return sb_flag(sih);
	else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		return ai_flag(sih);
	else {
		ASSERT(0);
		return 0;
	}
}

void
si_setint(si_t *sih, int siflag)
{
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		sb_setint(sih, siflag);
	else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		ai_setint(sih, siflag);
	else
		ASSERT(0);
}

uint
si_coreid(si_t *sih)
{
	si_info_t *sii;

	sii = SI_INFO(sih);
	return sii->common_info->coreid[sii->curidx];
}

uint
si_coreidx(si_t *sih)
{
	si_info_t *sii;

	sii = SI_INFO(sih);
	return sii->curidx;
}


uint
si_coreunit(si_t *sih)
{
	si_info_t *sii;
	uint idx;
	uint coreid;
	uint coreunit;
	uint i;

	sii = SI_INFO(sih);
	coreunit = 0;

	idx = sii->curidx;

	ASSERT(GOODREGS(sii->curmap));
	coreid = si_coreid(sih);

	
	for (i = 0; i < idx; i++)
		if (sii->common_info->coreid[i] == coreid)
			coreunit++;

	return (coreunit);
}

uint
si_corevendor(si_t *sih)
{
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		return sb_corevendor(sih);
	else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		return ai_corevendor(sih);
	else {
		ASSERT(0);
		return 0;
	}
}

bool
si_backplane64(si_t *sih)
{
	return ((sih->cccaps & CC_CAP_BKPLN64) != 0);
}

uint
si_corerev(si_t *sih)
{
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		return sb_corerev(sih);
	else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		return ai_corerev(sih);
	else {
		ASSERT(0);
		return 0;
	}
}


uint
si_findcoreidx(si_t *sih, uint coreid, uint coreunit)
{
	si_info_t *sii;
	uint found;
	uint i;

	sii = SI_INFO(sih);

	found = 0;

	for (i = 0; i < sii->numcores; i++)
		if (sii->common_info->coreid[i] == coreid) {
			if (found == coreunit)
				return (i);
			found++;
		}

	return (BADIDX);
}


uint
si_corelist(si_t *sih, uint coreid[])
{
	si_info_t *sii;

	sii = SI_INFO(sih);

	bcopy((uchar*)sii->common_info->coreid, (uchar*)coreid, (sii->numcores * sizeof(uint)));
	return (sii->numcores);
}


void *
si_coreregs(si_t *sih)
{
	si_info_t *sii;

	sii = SI_INFO(sih);
	ASSERT(GOODREGS(sii->curmap));

	return (sii->curmap);
}


void *
si_setcore(si_t *sih, uint coreid, uint coreunit)
{
	uint idx;

	idx = si_findcoreidx(sih, coreid, coreunit);
	if (!GOODIDX(idx))
		return (NULL);

	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		return sb_setcoreidx(sih, idx);
	else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		return ai_setcoreidx(sih, idx);
	else {
		ASSERT(0);
		return NULL;
	}
}

void *
si_setcoreidx(si_t *sih, uint coreidx)
{
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		return sb_setcoreidx(sih, coreidx);
	else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		return ai_setcoreidx(sih, coreidx);
	else {
		ASSERT(0);
		return NULL;
	}
}


void *si_switch_core(si_t *sih, uint coreid, uint *origidx, uint *intr_val)
{
	void *cc;
	si_info_t *sii;

	sii = SI_INFO(sih);

	INTR_OFF(sii, *intr_val);
	*origidx = sii->curidx;
	cc = si_setcore(sih, coreid, 0);
	ASSERT(cc != NULL);

	return cc;
}


void si_restore_core(si_t *sih, uint coreid, uint intr_val)
{
	si_info_t *sii;

	sii = SI_INFO(sih);

	si_setcoreidx(sih, coreid);
	INTR_RESTORE(sii, intr_val);
}

int
si_numaddrspaces(si_t *sih)
{
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		return sb_numaddrspaces(sih);
	else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		return ai_numaddrspaces(sih);
	else {
		ASSERT(0);
		return 0;
	}
}

uint32
si_addrspace(si_t *sih, uint asidx)
{
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		return sb_addrspace(sih, asidx);
	else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		return ai_addrspace(sih, asidx);
	else {
		ASSERT(0);
		return 0;
	}
}

uint32
si_addrspacesize(si_t *sih, uint asidx)
{
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		return sb_addrspacesize(sih, asidx);
	else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		return ai_addrspacesize(sih, asidx);
	else {
		ASSERT(0);
		return 0;
	}
}

uint32
si_core_cflags(si_t *sih, uint32 mask, uint32 val)
{
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		return sb_core_cflags(sih, mask, val);
	else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		return ai_core_cflags(sih, mask, val);
	else {
		ASSERT(0);
		return 0;
	}
}

void
si_core_cflags_wo(si_t *sih, uint32 mask, uint32 val)
{
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		sb_core_cflags_wo(sih, mask, val);
	else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		ai_core_cflags_wo(sih, mask, val);
	else
		ASSERT(0);
}

uint32
si_core_sflags(si_t *sih, uint32 mask, uint32 val)
{
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		return sb_core_sflags(sih, mask, val);
	else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		return ai_core_sflags(sih, mask, val);
	else {
		ASSERT(0);
		return 0;
	}
}

bool
si_iscoreup(si_t *sih)
{
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		return sb_iscoreup(sih);
	else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		return ai_iscoreup(sih);
	else {
		ASSERT(0);
		return FALSE;
	}
}

uint
si_corereg(si_t *sih, uint coreidx, uint regoff, uint mask, uint val)
{
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		return sb_corereg(sih, coreidx, regoff, mask, val);
	else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		return ai_corereg(sih, coreidx, regoff, mask, val);
	else {
		ASSERT(0);
		return 0;
	}
}

void
si_core_disable(si_t *sih, uint32 bits)
{
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		sb_core_disable(sih, bits);
	else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		ai_core_disable(sih, bits);
}

void
si_core_reset(si_t *sih, uint32 bits, uint32 resetbits)
{
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		sb_core_reset(sih, bits, resetbits);
	else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		ai_core_reset(sih, bits, resetbits);
}

void
si_core_tofixup(si_t *sih)
{
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		sb_core_tofixup(sih);
}


int
si_corebist(si_t *sih)
{
	uint32 cflags;
	int result = 0;

	
	cflags = si_core_cflags(sih, 0, 0);

	
	si_core_cflags(sih, 0, (SICF_BIST_EN | SICF_FGC));

	
	SPINWAIT(((si_core_sflags(sih, 0, 0) & SISF_BIST_DONE) == 0), 100000);

	if (si_core_sflags(sih, 0, 0) & SISF_BIST_ERROR)
		result = BCME_ERROR;

	
	si_core_cflags(sih, 0xffff, cflags);

	return result;
}

static uint32
BCMINITFN(factor6)(uint32 x)
{
	switch (x) {
	case CC_F6_2:	return 2;
	case CC_F6_3:	return 3;
	case CC_F6_4:	return 4;
	case CC_F6_5:	return 5;
	case CC_F6_6:	return 6;
	case CC_F6_7:	return 7;
	default:	return 0;
	}
}


uint32
BCMINITFN(si_clock_rate)(uint32 pll_type, uint32 n, uint32 m)
{
	uint32 n1, n2, clock, m1, m2, m3, mc;

	n1 = n & CN_N1_MASK;
	n2 = (n & CN_N2_MASK) >> CN_N2_SHIFT;

	if (pll_type == PLL_TYPE6) {
		if (m & CC_T6_MMASK)
			return CC_T6_M1;
		else
			return CC_T6_M0;
	} else if ((pll_type == PLL_TYPE1) ||
	           (pll_type == PLL_TYPE3) ||
	           (pll_type == PLL_TYPE4) ||
	           (pll_type == PLL_TYPE7)) {
		n1 = factor6(n1);
		n2 += CC_F5_BIAS;
	} else if (pll_type == PLL_TYPE2) {
		n1 += CC_T2_BIAS;
		n2 += CC_T2_BIAS;
		ASSERT((n1 >= 2) && (n1 <= 7));
		ASSERT((n2 >= 5) && (n2 <= 23));
	} else if (pll_type == PLL_TYPE5) {
		return (100000000);
	} else
		ASSERT(0);
	
	if ((pll_type == PLL_TYPE3) ||
	    (pll_type == PLL_TYPE7)) {
		clock = CC_CLOCK_BASE2 * n1 * n2;
	} else
		clock = CC_CLOCK_BASE1 * n1 * n2;

	if (clock == 0)
		return 0;

	m1 = m & CC_M1_MASK;
	m2 = (m & CC_M2_MASK) >> CC_M2_SHIFT;
	m3 = (m & CC_M3_MASK) >> CC_M3_SHIFT;
	mc = (m & CC_MC_MASK) >> CC_MC_SHIFT;

	if ((pll_type == PLL_TYPE1) ||
	    (pll_type == PLL_TYPE3) ||
	    (pll_type == PLL_TYPE4) ||
	    (pll_type == PLL_TYPE7)) {
		m1 = factor6(m1);
		if ((pll_type == PLL_TYPE1) || (pll_type == PLL_TYPE3))
			m2 += CC_F5_BIAS;
		else
			m2 = factor6(m2);
		m3 = factor6(m3);

		switch (mc) {
		case CC_MC_BYPASS:	return (clock);
		case CC_MC_M1:		return (clock / m1);
		case CC_MC_M1M2:	return (clock / (m1 * m2));
		case CC_MC_M1M2M3:	return (clock / (m1 * m2 * m3));
		case CC_MC_M1M3:	return (clock / (m1 * m3));
		default:		return (0);
		}
	} else {
		ASSERT(pll_type == PLL_TYPE2);

		m1 += CC_T2_BIAS;
		m2 += CC_T2M2_BIAS;
		m3 += CC_T2_BIAS;
		ASSERT((m1 >= 2) && (m1 <= 7));
		ASSERT((m2 >= 3) && (m2 <= 10));
		ASSERT((m3 >= 2) && (m3 <= 7));

		if ((mc & CC_T2MC_M1BYP) == 0)
			clock /= m1;
		if ((mc & CC_T2MC_M2BYP) == 0)
			clock /= m2;
		if ((mc & CC_T2MC_M3BYP) == 0)
			clock /= m3;

		return (clock);
	}
}

#ifndef	BCMDONGLEHOST
uint32
BCMINITFN(si_clock)(si_t *sih)
{
	si_info_t *sii;
	chipcregs_t *cc;
	uint32 n, m;
	uint idx;
	uint32 pll_type, rate;
	uint intr_val = 0;

	sii = SI_INFO(sih);
	INTR_OFF(sii, intr_val);
	if (PMUCTL_ENAB(sih)) {
		rate = si_pmu_si_clock(sih, sii->osh);
		goto exit;
	}

	idx = sii->curidx;
	cc = (chipcregs_t *)si_setcore(sih, CC_CORE_ID, 0);
	ASSERT(cc != NULL);

	n = R_REG(sii->osh, &cc->clockcontrol_n);
	pll_type = sih->cccaps & CC_CAP_PLL_MASK;
	if (pll_type == PLL_TYPE6)
		m = R_REG(sii->osh, &cc->clockcontrol_m3);
	else if (pll_type == PLL_TYPE3)
		m = R_REG(sii->osh, &cc->clockcontrol_m2);
	else
		m = R_REG(sii->osh, &cc->clockcontrol_sb);

	
	rate = si_clock_rate(pll_type, n, m);

	if (pll_type == PLL_TYPE3)
		rate = rate / 2;

	
	si_setcoreidx(sih, idx);
exit:
	INTR_RESTORE(sii, intr_val);

	return rate;
}

uint32
BCMINITFN(si_alp_clock)(si_t *sih)
{
	if (PMUCTL_ENAB(sih))
		return si_pmu_alp_clock(sih, si_osh(sih));

	return ALP_CLOCK;
}

uint32
BCMINITFN(si_ilp_clock)(si_t *sih)
{
	if (PMUCTL_ENAB(sih))
		return si_pmu_ilp_clock(sih, si_osh(sih));

	return ILP_CLOCK;
}

void
si_clock_pmu_spuravoid(si_t *sih, bool spuravoid)
{
	if (PMUCTL_ENAB(sih)) {
		si_info_t *sii;
		uint intr_val;

		sii = SI_INFO(sih);
		intr_val = 0;
		INTR_OFF(sii, intr_val);
		si_pmu_spuravoid(sih, sii->osh, spuravoid);
		INTR_RESTORE(sii, intr_val);
	}
}
#endif 


void
si_watchdog(si_t *sih, uint ticks)
{
	if (PMUCTL_ENAB(sih)) {
		if (ticks == 1)
			ticks = 2;
		si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, pmuwatchdog), ~0, ticks);
	} else {
#if !defined(BCMDONGLEHOST)
		
		si_clkctl_cc(sih, ticks ? CLK_FAST : CLK_DYNAMIC);
#endif 
		
		si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, watchdog), ~0, ticks);
	}
}

#if !defined(BCMBUSTYPE) || (BCMBUSTYPE == SI_BUS)

void
si_watchdog_ms(si_t *sih, uint32 ms)
{
	si_info_t *sii;

	sii = SI_INFO(sih);

	si_watchdog(sih, wd_msticks * ms);
}
#endif

#if defined(BCMDBG_ERR) || defined(BCMDBG_ASSERT) || defined(BCMDBG_DUMP)
bool
si_taclear(si_t *sih, bool details)
{
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		return sb_taclear(sih, details);
	else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		return FALSE;
	else {
		ASSERT(0);
		return FALSE;
	}
}
#endif 
#if !defined(BCMDONGLEHOST)

uint16
BCMINITFN(si_d11_devid)(si_t *sih)
{
	si_info_t *sii = SI_INFO(sih);
	uint16 device;

	
	if (CHIPID(sih->chip) == BCM4328_CHIP_ID &&
	    (sih->chippkg == BCM4328USBDUAL_PKG_ID || sih->chippkg == BCM4328SDIODUAL_PKG_ID))
		device = BCM4328_D11DUAL_ID;
	else {
		
		if ((device = (uint16)si_getdevpathintvar(sih, "devid")) != 0)
			;
		
		else if ((device = (uint16)getintvar(sii->vars, "devid")) != 0)
			;
		
		else if ((device = (uint16)getintvar(sii->vars, "wl0id")) != 0)
			;
		else if (CHIPID(sih->chip) == BCM4712_CHIP_ID) {
			
			if (sih->chippkg == BCM4712SMALL_PKG_ID)
				device = BCM4306_D11G_ID;
			else
				device = BCM4306_D11DUAL_ID;
		} else {
			
			device = 0xffff;
		}
	}
	return device;
}

int
BCMINITFN(si_corepciid)(si_t *sih, uint func, uint16 *pcivendor, uint16 *pcidevice,
                        uint8 *pciclass, uint8 *pcisubclass, uint8 *pciprogif,
                        uint8 *pciheader)
{
	uint16 vendor = 0xffff, device = 0xffff;
	uint8 class, subclass, progif = 0;
	uint8 header = PCI_HEADER_NORMAL;
	uint32 core = si_coreid(sih);

	
	if (func >= (uint)(core == USB20H_CORE_ID ? 2 : 1))
		return BCME_ERROR;

	
	switch (si_corevendor(sih)) {
	case SB_VEND_BCM:
	case MFGID_BRCM:
		vendor = VENDOR_BROADCOM;
		break;
	default:
		return BCME_ERROR;
	}

	
	switch (core) {
	case ILINE20_CORE_ID:
		class = PCI_CLASS_NET;
		subclass = PCI_NET_ETHER;
		device = BCM47XX_ILINE_ID;
		break;
	case ENET_CORE_ID:
		class = PCI_CLASS_NET;
		subclass = PCI_NET_ETHER;
		device = BCM47XX_ENET_ID;
		break;
	case GIGETH_CORE_ID:
		class = PCI_CLASS_NET;
		subclass = PCI_NET_ETHER;
		device = BCM47XX_GIGETH_ID;
		break;
	case SDRAM_CORE_ID:
	case MEMC_CORE_ID:
		class = PCI_CLASS_MEMORY;
		subclass = PCI_MEMORY_RAM;
		device = (uint16)core;
		break;
	case PCI_CORE_ID:
	case PCIE_CORE_ID:
		class = PCI_CLASS_BRIDGE;
		subclass = PCI_BRIDGE_PCI;
		device = (uint16)core;
		header = PCI_HEADER_BRIDGE;
		break;
	case MIPS33_CORE_ID:
	case MIPS74K_CORE_ID:
		class = PCI_CLASS_CPU;
		subclass = PCI_CPU_MIPS;
		device = (uint16)core;
		break;
	case CODEC_CORE_ID:
		class = PCI_CLASS_COMM;
		subclass = PCI_COMM_MODEM;
		device = BCM47XX_V90_ID;
		break;
	case USB_CORE_ID:
	case USB11H_CORE_ID:
		class = PCI_CLASS_SERIAL;
		subclass = PCI_SERIAL_USB;
		progif = 0x10; 
		device = BCM47XX_USBH_ID;
		break;
	case USB20H_CORE_ID:
		class = PCI_CLASS_SERIAL;
		subclass = PCI_SERIAL_USB;
		progif = func == 0 ? 0x10 : 0x20; 
		device = BCM47XX_USB20H_ID;
		header = 0x80; 
		break;
	case IPSEC_CORE_ID:
		class = PCI_CLASS_CRYPT;
		subclass = PCI_CRYPT_NETWORK;
		device = BCM47XX_IPSEC_ID;
		break;
	case ROBO_CORE_ID:
		
		class = PCI_CLASS_COMM;
		subclass = PCI_COMM_OTHER;
		device = BCM47XX_ROBO_ID;
		break;
	case CC_CORE_ID:
		class = PCI_CLASS_MEMORY;
		subclass = PCI_MEMORY_FLASH;
		device = (uint16)core;
		break;
	case SATAXOR_CORE_ID:
		class = PCI_CLASS_XOR;
		subclass = PCI_XOR_QDMA;
		device = BCM47XX_SATAXOR_ID;
		break;
	case ATA100_CORE_ID:
		class = PCI_CLASS_DASDI;
		subclass = PCI_DASDI_IDE;
		device = BCM47XX_ATA100_ID;
		break;
	case USB11D_CORE_ID:
		class = PCI_CLASS_SERIAL;
		subclass = PCI_SERIAL_USB;
		device = BCM47XX_USBD_ID;
		break;
	case USB20D_CORE_ID:
		class = PCI_CLASS_SERIAL;
		subclass = PCI_SERIAL_USB;
		device = BCM47XX_USB20D_ID;
		break;
	case D11_CORE_ID:
		class = PCI_CLASS_NET;
		subclass = PCI_NET_OTHER;
		device = si_d11_devid(sih);
		break;

	default:
		class = subclass = progif = 0xff;
		device = (uint16)core;
		break;
	}

	*pcivendor = vendor;
	*pcidevice = device;
	*pciclass = class;
	*pcisubclass = subclass;
	*pciprogif = progif;
	*pciheader = header;

	return 0;
}

#if defined(BCMDBG_DUMP)
void
si_dump(si_t *sih, struct bcmstrbuf *b)
{
	si_info_t *sii;
	uint i;

	sii = SI_INFO(sih);

	bcm_bprintf(b, "si %p chip 0x%x chiprev 0x%x boardtype 0x%x boardvendor 0x%x bus %d\n",
	            sii, sih->chip, sih->chiprev, sih->boardtype, sih->boardvendor, sih->bustype);
	bcm_bprintf(b, "osh %p curmap %p\n",
	            sii->osh, sii->curmap);
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		bcm_bprintf(b, "sonicsrev %d", sih->socirev);
	bcm_bprintf(b, "ccrev %d buscoretype 0x%x buscorerev %d curidx %d\n",
	            sih->ccrev, sih->buscoretype, sih->buscorerev, sii->curidx);

#ifdef	BCMDBG
	if ((BUSTYPE(sih->bustype) == PCI_BUS) && (sii->pch))
		pcicore_dump(sii->pch, b);
#endif

	bcm_bprintf(b, "cores:  ");
	for (i = 0; i < sii->numcores; i++)
		bcm_bprintf(b, "0x%x ", sii->common_info->coreid[i]);
	bcm_bprintf(b, "\n");
}
#endif	

#if defined(BCMDBG) || defined(BCMDBG_DUMP)

void
si_dumpregs(si_t *sih, struct bcmstrbuf *b)
{
	si_info_t *sii;
	uint origidx, intr_val = 0;

	sii = SI_INFO(sih);
	origidx = sii->curidx;

	INTR_OFF(sii, intr_val);
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		sb_dumpregs(sih, b);
	else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		ai_dumpregs(sih, b);
	else
		ASSERT(0);

	si_setcoreidx(sih, origidx);
	INTR_RESTORE(sii, intr_val);
}
#endif	
#endif 

#ifdef BCMDBG
void
si_view(si_t *sih, bool verbose)
{
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		sb_view(sih, verbose);
	else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		ai_view(sih, verbose);
	else
		ASSERT(0);
}

void
si_viewall(si_t *sih, bool verbose)
{
	si_info_t *sii;
	uint curidx, i;
	uint intr_val = 0;

	sii = SI_INFO(sih);
	curidx = sii->curidx;

	SI_ERROR(("sb_viewall: num_cores %d\n", sii->numcores));
	for (i = 0; i < sii->numcores; i++) {
		INTR_OFF(sii, intr_val);
		si_setcoreidx(sih, i);
		si_view(sih, verbose);
		INTR_RESTORE(sii, intr_val);
	}

	si_setcoreidx(sih, curidx);
}
#endif	
#if !defined(BCMDONGLEHOST)

#if defined(BCMDBG_DUMP)
void
si_ccreg_dump(si_t *sih, struct bcmstrbuf *b)
{
	si_info_t *sii;
	uint origidx;
	uint i, intr_val = 0;
	chipcregs_t *cc;

	sii = SI_INFO(sih);
	origidx = sii->curidx;

	
	if (sih->ccrev != 23)
		return;

	INTR_OFF(sii, intr_val);

	cc = (chipcregs_t *)si_setcore(sih, CC_CORE_ID, 0);

	bcm_bprintf(b, "\n===cc(rev %d) registers(offset val)===\n", sih->ccrev);
	for (i = 0; i <= 0xc4; i += 4) {
		if (i == 0x4c) {
			bcm_bprintf(b, "\n");
			continue;
		}
		bcm_bprintf(b, "0x%x\t0x%x\n", i, *(uint32 *)((uintptr)cc + i));
	}

	bcm_bprintf(b, "\n");

	for (i = 0x1e0; i <= 0x1e4; i += 4) {
		bcm_bprintf(b, "0x%x\t0x%x\n", i, *(uint32 *)((uintptr)cc + i));
	}
	bcm_bprintf(b, "\n");

	if (sih->cccaps & CC_CAP_PMU) {
		for (i = 0x600; i <= 0x660; i += 4) {
			bcm_bprintf(b, "0x%x\t0x%x\n", i, *(uint32 *)((uintptr)cc + i));
		}
	}
	bcm_bprintf(b, "\n");

	si_setcoreidx(sih, origidx);
	INTR_RESTORE(sii, intr_val);
}
#endif	


static uint
si_slowclk_src(si_info_t *sii)
{
	chipcregs_t *cc;

	ASSERT(SI_FAST(sii) || si_coreid(&sii->pub) == CC_CORE_ID);

	if (sii->pub.ccrev < 6) {
		if ((BUSTYPE(sii->pub.bustype) == PCI_BUS) &&
		    (OSL_PCI_READ_CONFIG(sii->osh, PCI_GPIO_OUT, sizeof(uint32)) &
		     PCI_CFG_GPIO_SCS))
			return (SCC_SS_PCI);
		else
			return (SCC_SS_XTAL);
	} else if (sii->pub.ccrev < 10) {
		cc = (chipcregs_t *)si_setcoreidx(&sii->pub, sii->curidx);
		return (R_REG(sii->osh, &cc->slow_clk_ctl) & SCC_SS_MASK);
	} else	
		return (SCC_SS_XTAL);
}


static uint
si_slowclk_freq(si_info_t *sii, bool max_freq, chipcregs_t *cc)
{
	uint32 slowclk;
	uint div;

	ASSERT(SI_FAST(sii) || si_coreid(&sii->pub) == CC_CORE_ID);

	
	ASSERT(R_REG(sii->osh, &cc->capabilities) & CC_CAP_PWR_CTL);

	slowclk = si_slowclk_src(sii);
	if (sii->pub.ccrev < 6) {
		if (slowclk == SCC_SS_PCI)
			return (max_freq ? (PCIMAXFREQ / 64) : (PCIMINFREQ / 64));
		else
			return (max_freq ? (XTALMAXFREQ / 32) : (XTALMINFREQ / 32));
	} else if (sii->pub.ccrev < 10) {
		div = 4 *
		        (((R_REG(sii->osh, &cc->slow_clk_ctl) & SCC_CD_MASK) >> SCC_CD_SHIFT) + 1);
		if (slowclk == SCC_SS_LPO)
			return (max_freq ? LPOMAXFREQ : LPOMINFREQ);
		else if (slowclk == SCC_SS_XTAL)
			return (max_freq ? (XTALMAXFREQ / div) : (XTALMINFREQ / div));
		else if (slowclk == SCC_SS_PCI)
			return (max_freq ? (PCIMAXFREQ / div) : (PCIMINFREQ / div));
		else
			ASSERT(0);
	} else {
		
		div = R_REG(sii->osh, &cc->system_clk_ctl) >> SYCC_CD_SHIFT;
		div = 4 * (div + 1);
		return (max_freq ? XTALMAXFREQ : (XTALMINFREQ / div));
	}
	return (0);
}

static void
BCMINITFN(si_clkctl_setdelay)(si_info_t *sii, void *chipcregs)
{
	chipcregs_t *cc = (chipcregs_t *)chipcregs;
	uint slowmaxfreq, pll_delay, slowclk;
	uint pll_on_delay, fref_sel_delay;

	pll_delay = PLL_DELAY;

	

	slowclk = si_slowclk_src(sii);
	if (slowclk != SCC_SS_XTAL)
		pll_delay += XTAL_ON_DELAY;

	
	slowmaxfreq = si_slowclk_freq(sii, (sii->pub.ccrev >= 10) ? FALSE : TRUE, cc);

	pll_on_delay = ((slowmaxfreq * pll_delay) + 999999) / 1000000;
	fref_sel_delay = ((slowmaxfreq * FREF_DELAY) + 999999) / 1000000;

	W_REG(sii->osh, &cc->pll_on_delay, pll_on_delay);
	W_REG(sii->osh, &cc->fref_sel_delay, fref_sel_delay);
}


void
BCMINITFN(si_clkctl_init)(si_t *sih)
{
	si_info_t *sii;
	uint origidx = 0;
	chipcregs_t *cc;
	bool fast;

	if (!CCCTL_ENAB(sih))
		return;

	sii = SI_INFO(sih);
	fast = SI_FAST(sii);
	if (!fast) {
		origidx = sii->curidx;
		if ((cc = (chipcregs_t *)si_setcore(sih, CC_CORE_ID, 0)) == NULL)
			return;
	} else if ((cc = (chipcregs_t *)CCREGS_FAST(sii)) == NULL)
		return;
	ASSERT(cc != NULL);

	
	if (sih->ccrev >= 10)
		SET_REG(sii->osh, &cc->system_clk_ctl, SYCC_CD_MASK,
		        (ILP_DIV_1MHZ << SYCC_CD_SHIFT));

	si_clkctl_setdelay(sii, (void *)(uintptr)cc);

	if (!fast)
		si_setcoreidx(sih, origidx);
}


uint16
BCMINITFN(si_clkctl_fast_pwrup_delay)(si_t *sih)
{
	si_info_t *sii;
	uint origidx = 0;
	chipcregs_t *cc;
	uint slowminfreq;
	uint16 fpdelay;
	uint intr_val = 0;
	bool fast;

	sii = SI_INFO(sih);
	if (PMUCTL_ENAB(sih)) {
		INTR_OFF(sii, intr_val);
		fpdelay = si_pmu_fast_pwrup_delay(sih, sii->osh);
		INTR_RESTORE(sii, intr_val);
		return fpdelay;
	}

	if (!CCCTL_ENAB(sih))
		return 0;

	fast = SI_FAST(sii);
	fpdelay = 0;
	if (!fast) {
		origidx = sii->curidx;
		INTR_OFF(sii, intr_val);
		if ((cc = (chipcregs_t *)si_setcore(sih, CC_CORE_ID, 0)) == NULL)
			goto done;
	}
	else if ((cc = (chipcregs_t *)CCREGS_FAST(sii)) == NULL)
		goto done;
	ASSERT(cc != NULL);

	slowminfreq = si_slowclk_freq(sii, FALSE, cc);
	fpdelay = (((R_REG(sii->osh, &cc->pll_on_delay) + 2) * 1000000) +
	           (slowminfreq - 1)) / slowminfreq;

done:
	if (!fast) {
		si_setcoreidx(sih, origidx);
		INTR_RESTORE(sii, intr_val);
	}
	return fpdelay;
}


int
si_clkctl_xtal(si_t *sih, uint what, bool on)
{
	si_info_t *sii;
	uint32 in, out, outen;

	sii = SI_INFO(sih);

	switch (BUSTYPE(sih->bustype)) {

	case SDIO_BUS:
		if ((what & XTAL) && (sih->chip == BCM4318_CHIP_ID)) {
			if (on) {
				
				bcmsdh_cfg_write(sii->sdh, SDIO_FUNC_1,
				                 SBSDIO_CHIP_CTRL_DATA, 1, NULL);
				bcmsdh_cfg_write(sii->sdh, SDIO_FUNC_1,
				                 SBSDIO_CHIP_CTRL_EN, 1, NULL);
				OSL_DELAY(XTAL_ON_DELAY);
			} else {
			}
			return (0);
		}
		else
			return (-1);

	case PCMCIA_BUS:
		return (0);


	case PCI_BUS:
		
		if (PCIE(sii))
			return -1;

		in = OSL_PCI_READ_CONFIG(sii->osh, PCI_GPIO_IN, sizeof(uint32));
		out = OSL_PCI_READ_CONFIG(sii->osh, PCI_GPIO_OUT, sizeof(uint32));
		outen = OSL_PCI_READ_CONFIG(sii->osh, PCI_GPIO_OUTEN, sizeof(uint32));

		
		if (on && (in & PCI_CFG_GPIO_XTAL))
			return (0);

		if (what & XTAL)
			outen |= PCI_CFG_GPIO_XTAL;
		if (what & PLL)
			outen |= PCI_CFG_GPIO_PLL;

		if (on) {
			
			if (what & XTAL) {
				out |= PCI_CFG_GPIO_XTAL;
				if (what & PLL)
					out |= PCI_CFG_GPIO_PLL;
				OSL_PCI_WRITE_CONFIG(sii->osh, PCI_GPIO_OUT,
				                     sizeof(uint32), out);
				OSL_PCI_WRITE_CONFIG(sii->osh, PCI_GPIO_OUTEN,
				                     sizeof(uint32), outen);
				OSL_DELAY(XTAL_ON_DELAY);
			}

			
			if (what & PLL) {
				out &= ~PCI_CFG_GPIO_PLL;
				OSL_PCI_WRITE_CONFIG(sii->osh, PCI_GPIO_OUT,
				                     sizeof(uint32), out);
				OSL_DELAY(2000);
			}
		} else {
			if (what & XTAL)
				out &= ~PCI_CFG_GPIO_XTAL;
			if (what & PLL)
				out |= PCI_CFG_GPIO_PLL;
			OSL_PCI_WRITE_CONFIG(sii->osh, PCI_GPIO_OUT, sizeof(uint32), out);
			OSL_PCI_WRITE_CONFIG(sii->osh, PCI_GPIO_OUTEN, sizeof(uint32),
			                     outen);
		}

	default:
		return (-1);
	}

	return (0);
}


bool
si_clkctl_cc(si_t *sih, uint mode)
{
	si_info_t *sii;

	sii = SI_INFO(sih);

	
	if (sih->ccrev < 6)
		return FALSE;

	if (PCI_FORCEHT(sii))
		return (mode == CLK_FAST);

	return _si_clkctl_cc(sii, mode);
}


static bool
_si_clkctl_cc(si_info_t *sii, uint mode)
{
	uint origidx = 0;
	chipcregs_t *cc;
	uint32 scc;
	uint intr_val = 0;
	bool fast = SI_FAST(sii);

	
	if (sii->pub.ccrev < 6)
		return (FALSE);

	
	ASSERT(sii->pub.ccrev != 10);

	if (!fast) {
		INTR_OFF(sii, intr_val);
		origidx = sii->curidx;

		if ((BUSTYPE(sii->pub.bustype) == SI_BUS) &&
		    si_setcore(&sii->pub, MIPS33_CORE_ID, 0) &&
		    (si_corerev(&sii->pub) <= 7) && (sii->pub.ccrev >= 10))
			goto done;

		cc = (chipcregs_t *) si_setcore(&sii->pub, CC_CORE_ID, 0);
	} else if ((cc = (chipcregs_t *) CCREGS_FAST(sii)) == NULL)
		goto done;
	ASSERT(cc != NULL);

	if (!CCCTL_ENAB(&sii->pub) && (sii->pub.ccrev < 20))
		goto done;

	switch (mode) {
	case CLK_FAST:	
		if (sii->pub.ccrev < 10) {
			
			si_clkctl_xtal(&sii->pub, XTAL, ON);
			SET_REG(sii->osh, &cc->slow_clk_ctl, (SCC_XC | SCC_FS | SCC_IP), SCC_IP);
		} else if (sii->pub.ccrev < 20) {
			OR_REG(sii->osh, &cc->system_clk_ctl, SYCC_HR);
		} else {
			OR_REG(sii->osh, &cc->clk_ctl_st, CCS_FORCEHT);
		}

		
		if (PMUCTL_ENAB(&sii->pub)) {
			uint32 htavail = CCS_HTAVAIL;
			if (CHIPID(sii->pub.chip) == BCM4328_CHIP_ID)
				htavail = CCS0_HTAVAIL;
			SPINWAIT(((R_REG(sii->osh, &cc->clk_ctl_st) & htavail) == 0),
			         PMU_MAX_TRANSITION_DLY);
			ASSERT(R_REG(sii->osh, &cc->clk_ctl_st) & htavail);
		} else {
			OSL_DELAY(PLL_DELAY);
		}
		break;

	case CLK_DYNAMIC:	
		if (sii->pub.ccrev < 10) {
			scc = R_REG(sii->osh, &cc->slow_clk_ctl);
			scc &= ~(SCC_FS | SCC_IP | SCC_XC);
			if ((scc & SCC_SS_MASK) != SCC_SS_XTAL)
				scc |= SCC_XC;
			W_REG(sii->osh, &cc->slow_clk_ctl, scc);

			
			if (scc & SCC_XC)
				si_clkctl_xtal(&sii->pub, XTAL, OFF);
		} else if (sii->pub.ccrev < 20) {
			
			AND_REG(sii->osh, &cc->system_clk_ctl, ~SYCC_HR);
		} else {
			AND_REG(sii->osh, &cc->clk_ctl_st, ~CCS_FORCEHT);
		}
		break;

	default:
		ASSERT(0);
	}

done:
	if (!fast) {
		si_setcoreidx(&sii->pub, origidx);
		INTR_RESTORE(sii, intr_val);
	}
	return (mode == CLK_FAST);
}

#if defined(BCMDBG_DUMP)

void
si_clkctl_dump(si_t *sih, struct bcmstrbuf *b)
{
	si_info_t *sii;
	chipcregs_t *cc;
	uint origidx;
	uint intr_val = 0;

	if (!(sih->cccaps & CC_CAP_PWR_CTL))
		return;

	sii = SI_INFO(sih);
	INTR_OFF(sii, intr_val);
	origidx = sii->curidx;
	if ((cc = (chipcregs_t *)si_setcore(sih, CC_CORE_ID, 0)) == NULL)
		goto done;

	bcm_bprintf(b, "pll_on_delay 0x%x fref_sel_delay 0x%x ",
		cc->pll_on_delay, cc->fref_sel_delay);
	if ((sih->ccrev >= 6) && (sih->ccrev < 10))
		bcm_bprintf(b, "slow_clk_ctl 0x%x ", cc->slow_clk_ctl);
	if (sih->ccrev >= 10) {
		bcm_bprintf(b, "system_clk_ctl 0x%x ", cc->system_clk_ctl);
		bcm_bprintf(b, "clkstatestretch 0x%x ", cc->clkstatestretch);
	}

	if (BUSTYPE(sih->bustype) == PCI_BUS)
		bcm_bprintf(b, "gpioout 0x%x gpioouten 0x%x ",
		            OSL_PCI_READ_CONFIG(sii->osh, PCI_GPIO_OUT, sizeof(uint32)),
		            OSL_PCI_READ_CONFIG(sii->osh, PCI_GPIO_OUTEN, sizeof(uint32)));

	if (sih->cccaps & CC_CAP_PMU) {
		
	}
	bcm_bprintf(b, "\n");

	si_setcoreidx(sih, origidx);
done:
	INTR_RESTORE(sii, intr_val);
}
#endif	


int
BCMINITFN(si_devpath)(si_t *sih, char *path, int size)
{
	int slen;

	ASSERT(path != NULL);
	ASSERT(size >= SI_DEVPATH_BUFSZ);

	if (!path || size <= 0)
		return -1;

	switch (BUSTYPE(sih->bustype)) {
	case SI_BUS:
	case JTAG_BUS:
		slen = snprintf(path, (size_t)size, "sb/%u/", si_coreidx(sih));
		break;
	case PCI_BUS:
		ASSERT((SI_INFO(sih))->osh != NULL);
		slen = snprintf(path, (size_t)size, "pci/%u/%u/",
		                OSL_PCI_BUS((SI_INFO(sih))->osh),
		                OSL_PCI_SLOT((SI_INFO(sih))->osh));
		break;
	case PCMCIA_BUS:
		SI_ERROR(("si_devpath: OSL_PCMCIA_BUS() not implemented, bus 1 assumed\n"));
		SI_ERROR(("si_devpath: OSL_PCMCIA_SLOT() not implemented, slot 1 assumed\n"));
		slen = snprintf(path, (size_t)size, "pc/1/1/");
		break;
	case SDIO_BUS:
		SI_ERROR(("si_devpath: device 0 assumed\n"));
		slen = snprintf(path, (size_t)size, "sd/%u/", si_coreidx(sih));
		break;
	default:
		slen = -1;
		ASSERT(0);
		break;
	}

	if (slen < 0 || slen >= size) {
		path[0] = '\0';
		return -1;
	}

	return 0;
}


char *
BCMINITFN(si_getdevpathvar)(si_t *sih, const char *name)
{
	char varname[SI_DEVPATH_BUFSZ + 32];

	si_devpathvar(sih, varname, sizeof(varname), name);

	return (getvar(NULL, varname));
}


int
BCMINITFN(si_getdevpathintvar)(si_t *sih, const char *name)
{
#if defined(BCMBUSTYPE) && (BCMBUSTYPE == SI_BUS)
	return (getintvar(NULL, name));
#else
	char varname[SI_DEVPATH_BUFSZ + 32];

	si_devpathvar(sih, varname, sizeof(varname), name);

	return (getintvar(NULL, varname));
#endif
}


static char *
BCMINITFN(si_devpathvar)(si_t *sih, char *var, int len, const char *name)
{
	uint path_len;

	if (!var || len <= 0)
		return var;

	if (si_devpath(sih, var, len) == 0) {
		path_len = strlen(var);

		if (strlen(name) + 1 > (uint)(len - path_len))
			var[0] = '\0';
		else
			strncpy(var + path_len, name, len - path_len - 1);
	}

	return var;
}



static bool
si_ispcie(si_info_t *sii)
{
	uint8 cap_ptr;

	if (BUSTYPE(sii->pub.bustype) != PCI_BUS)
		return FALSE;

	cap_ptr = pcicore_find_pci_capability(sii->osh, PCI_CAP_PCIECAP_ID, NULL, NULL);
	if (!cap_ptr)
		return FALSE;

	return TRUE;
}



void
si_pci_pmeen(si_t *sih)
{
	si_info_t *sii;

	sii = SI_INFO(sih);

	pcicore_pmeen(sii->pch);
}


bool
si_pci_pmeclr(si_t *sih)
{
	si_info_t *sii;

	sii = SI_INFO(sih);

	return pcicore_pmeclr(sii->pch);
}


void
si_pcmcia_init(si_t *sih)
{
	si_info_t *sii;
	uint8 cor = 0;

	sii = SI_INFO(sih);

	
	OSL_PCMCIA_READ_ATTR(sii->osh, PCMCIA_FCR0 + PCMCIA_COR, &cor, 1);
	cor |= COR_IRQEN | COR_FUNEN;
	OSL_PCMCIA_WRITE_ATTR(sii->osh, PCMCIA_FCR0 + PCMCIA_COR, &cor, 1);

}
#endif 


void
si_sdio_init(si_t *sih)
{
	si_info_t *sii = SI_INFO(sih);

	if (((sih->buscoretype == PCMCIA_CORE_ID) && (sih->buscorerev >= 8)) || \
	    (sih->buscoretype == SDIOD_CORE_ID)) {
		uint idx;
		sdpcmd_regs_t *sdpregs;

		
		idx = sii->curidx;
		ASSERT(idx == si_findcoreidx(sih, D11_CORE_ID, 0));

		
		if (!(sdpregs = (sdpcmd_regs_t *)si_setcore(sih, PCMCIA_CORE_ID, 0)))
			sdpregs = (sdpcmd_regs_t *)si_setcore(sih, SDIOD_CORE_ID, 0);
		ASSERT(sdpregs);

		SI_MSG(("si_sdio_init: For PCMCIA/SDIO Corerev %d, enable ints from core %d " \
		        "through SD core %d (%p)\n",
		        sih->buscorerev, idx, sii->curidx, sdpregs));

		
		W_REG(sii->osh, &sdpregs->hostintmask, I_SBINT);
		W_REG(sii->osh, &sdpregs->sbintmask, (I_SB_SERR | I_SB_RESPERR | (1 << idx)));

		
		si_setcoreidx(sih, idx);
	}

	
	bcmsdh_intr_enable(sii->sdh);

}

#if !defined(BCMDONGLEHOST)
bool
BCMINITFN(si_pci_war16165)(si_t *sih)
{
	si_info_t *sii;

	sii = SI_INFO(sih);

	return (PCI(sii) && (sih->buscorerev <= 10));
}


void
si_pcie_war_ovr_disable(si_t *sih)
{
	si_info_t *sii;

	sii = SI_INFO(sih);

	if (!PCIE(sii))
		return;

	pcie_war_ovr_aspm_disable(sii->pch);
}

void
BCMINITFN(si_pci_up)(si_t *sih)
{
	si_info_t *sii;

	sii = SI_INFO(sih);

	
	if (BUSTYPE(sih->bustype) != PCI_BUS)
		return;

	if (PCI_FORCEHT(sii))
		_si_clkctl_cc(sii, CLK_FAST);

	if (PCIE(sii)) {
		pcicore_up(sii->pch, SI_PCIUP);
		if (((sih->chip == BCM4311_CHIP_ID) && (sih->chiprev == 2)) ||
		    (sih->chip == BCM4312_CHIP_ID))
			sb_set_initiator_to((void *)sii, 0x3,
			                    si_findcoreidx((void *)sii, D11_CORE_ID, 0));
	}
}


void
BCMUNINITFN(si_pci_sleep)(si_t *sih)
{
	si_info_t *sii;

	sii = SI_INFO(sih);

	pcicore_sleep(sii->pch);
}


void
BCMINITFN(si_pci_down)(si_t *sih)
{
	si_info_t *sii;

	sii = SI_INFO(sih);

	
	if (BUSTYPE(sih->bustype) != PCI_BUS)
		return;

	
	if (PCI_FORCEHT(sii))
		_si_clkctl_cc(sii, CLK_DYNAMIC);

	pcicore_down(sii->pch, SI_PCIDOWN);
}


void
BCMINITFN(si_pci_setup)(si_t *sih, uint coremask)
{
	si_info_t *sii;
	sbpciregs_t *pciregs = NULL;
	uint32 siflag = 0, w;
	uint idx = 0;

	sii = SI_INFO(sih);

	if (BUSTYPE(sii->pub.bustype) != PCI_BUS)
		return;

	ASSERT(PCI(sii) || PCIE(sii));
	ASSERT(sii->pub.buscoreidx != BADIDX);

	if (PCI(sii)) {
		
		idx = sii->curidx;

		
		siflag = si_flag(sih);

		
		pciregs = (sbpciregs_t *)si_setcoreidx(sih, sii->pub.buscoreidx);
	}

	
	if (PCIE(sii) || (PCI(sii) && ((sii->pub.buscorerev) >= 6))) {
		
		w = OSL_PCI_READ_CONFIG(sii->osh, PCI_INT_MASK, sizeof(uint32));
		w |= (coremask << PCI_SBIM_SHIFT);
		OSL_PCI_WRITE_CONFIG(sii->osh, PCI_INT_MASK, sizeof(uint32), w);
	} else {
		
		si_setint(sih, siflag);
	}

	if (PCI(sii)) {
		OR_REG(sii->osh, &pciregs->sbtopci2, (SBTOPCI_PREF | SBTOPCI_BURST));
		if (sii->pub.buscorerev >= 11) {
			OR_REG(sii->osh, &pciregs->sbtopci2, SBTOPCI_RC_READMULTI);
			w = R_REG(sii->osh, &pciregs->clkrun);
			W_REG(sii->osh, &pciregs->clkrun, (w | PCI_CLKRUN_DSBL));
			w = R_REG(sii->osh, &pciregs->clkrun);
		}

		if (sii->pub.buscorerev < 5)
			si_core_tofixup(sih);

		
		si_setcoreidx(sih, idx);
	}
}

uint8
si_pcieclkreq(si_t *sih, uint32 mask, uint32 val)
{
	si_info_t *sii;

	sii = SI_INFO(sih);

	if (!(PCIE(sii)))
		return 0;
	return pcie_clkreq(sii->pch, mask, val);
}

#ifdef BCMDBG
uint32
si_pcielcreg(si_t *sih, uint32 mask, uint32 val)
{
	si_info_t *sii;

	sii = SI_INFO(sih);

	if (!PCIE(sii))
		return 0;

	return pcie_lcreg(sii->pch, mask, val);
}

#endif 


uint
si_pcie_readreg(void *sih, uint addrtype, uint offset)
{
	return pcie_readreg(((si_info_t *)sih)->osh, (sbpcieregs_t *)PCIEREGS(((si_info_t *)sih)),
	                    addrtype, offset);
}



int
si_pci_fixcfg(si_t *sih)
{
	uint origidx, pciidx;
	sbpciregs_t *pciregs = NULL;
	sbpcieregs_t *pcieregs = NULL;
	void *regs = NULL;
	uint16 val16, *reg16 = NULL;
	uint32 w;

	si_info_t *sii = SI_INFO(sih);

	ASSERT(BUSTYPE(sii->pub.bustype) == PCI_BUS);

	if ((sii->pub.chip == BCM4321_CHIP_ID) && (sii->pub.chiprev < 2)) {
		w = (sii->pub.chiprev == 0) ?
		        CHIPCTRL_4321A0_DEFAULT : CHIPCTRL_4321A1_DEFAULT;
		si_corereg(&sii->pub, SI_CC_IDX, OFFSETOF(chipcregs_t, chipcontrol), ~0, w);
	}

	
	
	origidx = si_coreidx(&sii->pub);

	
	if (sii->pub.buscoretype == PCIE_CORE_ID) {
		pcieregs = (sbpcieregs_t *)si_setcore(&sii->pub, PCIE_CORE_ID, 0);
		regs = pcieregs;
		ASSERT(pcieregs != NULL);
		reg16 = &pcieregs->sprom[SRSH_PI_OFFSET];
	} else if (sii->pub.buscoretype == PCI_CORE_ID) {
		pciregs = (sbpciregs_t *)si_setcore(&sii->pub, PCI_CORE_ID, 0);
		regs = pciregs;
		ASSERT(pciregs != NULL);
		reg16 = &pciregs->sprom[SRSH_PI_OFFSET];
	}
	pciidx = si_coreidx(&sii->pub);
	val16 = R_REG(sii->osh, reg16);
	if (((val16 & SRSH_PI_MASK) >> SRSH_PI_SHIFT) != (uint16)pciidx) {
		val16 = (uint16)(pciidx << SRSH_PI_SHIFT) | (val16 & ~SRSH_PI_MASK);
		W_REG(sii->osh, reg16, val16);
	}

	
	si_setcoreidx(&sii->pub, origidx);

	pcicore_hwup(sii->pch);
	return 0;
}


#if defined(BCMDBG_DUMP)
int
si_gpiodump(si_t *sih, struct bcmstrbuf *b)
{
	si_info_t *sii;
	uint origidx;
	uint intr_val = 0;
	chipcregs_t *cc;

	sii = SI_INFO(sih);

	INTR_OFF(sii, intr_val);

	origidx = si_coreidx(sih);

	cc = (chipcregs_t *)si_setcore(sih, CC_CORE_ID, 0);
	ASSERT(cc);

	bcm_bprintf(b, "GPIOregs\t");

	bcm_bprintf(b, "gpioin 0x%x ", R_REG(sii->osh, &cc->gpioin));
	bcm_bprintf(b, "gpioout 0x%x ", R_REG(sii->osh, &cc->gpioout));
	bcm_bprintf(b, "gpioouten 0x%x ", R_REG(sii->osh, &cc->gpioouten));
	bcm_bprintf(b, "gpiocontrol 0x%x ", R_REG(sii->osh, &cc->gpiocontrol));
	bcm_bprintf(b, "gpiointpolarity 0x%x ", R_REG(sii->osh, &cc->gpiointpolarity));
	bcm_bprintf(b, "gpiointmask 0x%x ", R_REG(sii->osh, &cc->gpiointmask));

	if (sii->pub.ccrev >= 16) {
		bcm_bprintf(b, "gpiotimerval 0x%x ", R_REG(sii->osh, &cc->gpiotimerval));
		bcm_bprintf(b, "gpiotimeroutmask 0x%x", R_REG(sii->osh, &cc->gpiotimeroutmask));
	}
	bcm_bprintf(b, "\n");

	
	si_setcoreidx(sih, origidx);

	INTR_RESTORE(sii, intr_val);
	return 0;

}
#endif 
#endif 


void *
si_gpiosetcore(si_t *sih)
{
	return (si_setcoreidx(sih, SI_CC_IDX));
}


uint32
si_gpiocontrol(si_t *sih, uint32 mask, uint32 val, uint8 priority)
{
	uint regoff;

	regoff = 0;

	
	if ((priority != GPIO_HI_PRIORITY) &&
	    (BUSTYPE(sih->bustype) == SI_BUS) && (val || mask)) {
		mask = priority ? (si_gpioreservation & mask) :
			((si_gpioreservation | mask) & ~(si_gpioreservation));
		val &= mask;
	}

	regoff = OFFSETOF(chipcregs_t, gpiocontrol);
	return (si_corereg(sih, SI_CC_IDX, regoff, mask, val));
}


uint32
si_gpioouten(si_t *sih, uint32 mask, uint32 val, uint8 priority)
{
	uint regoff;

	regoff = 0;

	
	if ((priority != GPIO_HI_PRIORITY) &&
	    (BUSTYPE(sih->bustype) == SI_BUS) && (val || mask)) {
		mask = priority ? (si_gpioreservation & mask) :
			((si_gpioreservation | mask) & ~(si_gpioreservation));
		val &= mask;
	}

	regoff = OFFSETOF(chipcregs_t, gpioouten);
	return (si_corereg(sih, SI_CC_IDX, regoff, mask, val));
}


uint32
si_gpioout(si_t *sih, uint32 mask, uint32 val, uint8 priority)
{
	uint regoff;

	regoff = 0;

	
	if ((priority != GPIO_HI_PRIORITY) &&
	    (BUSTYPE(sih->bustype) == SI_BUS) && (val || mask)) {
		mask = priority ? (si_gpioreservation & mask) :
			((si_gpioreservation | mask) & ~(si_gpioreservation));
		val &= mask;
	}

	regoff = OFFSETOF(chipcregs_t, gpioout);
	return (si_corereg(sih, SI_CC_IDX, regoff, mask, val));
}


uint32
si_gpioreserve(si_t *sih, uint32 gpio_bitmask, uint8 priority)
{
	si_info_t *sii;

	sii = SI_INFO(sih);

	
	if ((BUSTYPE(sih->bustype) != SI_BUS) || (!priority)) {
		ASSERT((BUSTYPE(sih->bustype) == SI_BUS) && (priority));
		return -1;
	}
	
	if ((!gpio_bitmask) || ((gpio_bitmask) & (gpio_bitmask - 1))) {
		ASSERT((gpio_bitmask) && !((gpio_bitmask) & (gpio_bitmask - 1)));
		return -1;
	}

	
	if (si_gpioreservation & gpio_bitmask)
		return -1;
	
	si_gpioreservation |= gpio_bitmask;

	return si_gpioreservation;
}




uint32
si_gpiorelease(si_t *sih, uint32 gpio_bitmask, uint8 priority)
{
	si_info_t *sii;

	sii = SI_INFO(sih);

	
	if ((BUSTYPE(sih->bustype) != SI_BUS) || (!priority)) {
		ASSERT((BUSTYPE(sih->bustype) == SI_BUS) && (priority));
		return -1;
	}
	
	if ((!gpio_bitmask) || ((gpio_bitmask) & (gpio_bitmask - 1))) {
		ASSERT((gpio_bitmask) && !((gpio_bitmask) & (gpio_bitmask - 1)));
		return -1;
	}

	
	if (!(si_gpioreservation & gpio_bitmask))
		return -1;

	
	si_gpioreservation &= ~gpio_bitmask;

	return si_gpioreservation;
}


uint32
si_gpioin(si_t *sih)
{
	si_info_t *sii;
	uint regoff;

	sii = SI_INFO(sih);
	regoff = 0;

	regoff = OFFSETOF(chipcregs_t, gpioin);
	return (si_corereg(sih, SI_CC_IDX, regoff, 0, 0));
}


uint32
si_gpiointpolarity(si_t *sih, uint32 mask, uint32 val, uint8 priority)
{
	si_info_t *sii;
	uint regoff;

	sii = SI_INFO(sih);
	regoff = 0;

	
	if ((BUSTYPE(sih->bustype) == SI_BUS) && (val || mask)) {
		mask = priority ? (si_gpioreservation & mask) :
			((si_gpioreservation | mask) & ~(si_gpioreservation));
		val &= mask;
	}

	regoff = OFFSETOF(chipcregs_t, gpiointpolarity);
	return (si_corereg(sih, SI_CC_IDX, regoff, mask, val));
}


uint32
si_gpiointmask(si_t *sih, uint32 mask, uint32 val, uint8 priority)
{
	si_info_t *sii;
	uint regoff;

	sii = SI_INFO(sih);
	regoff = 0;

	
	if ((BUSTYPE(sih->bustype) == SI_BUS) && (val || mask)) {
		mask = priority ? (si_gpioreservation & mask) :
			((si_gpioreservation | mask) & ~(si_gpioreservation));
		val &= mask;
	}

	regoff = OFFSETOF(chipcregs_t, gpiointmask);
	return (si_corereg(sih, SI_CC_IDX, regoff, mask, val));
}


uint32
si_gpioled(si_t *sih, uint32 mask, uint32 val)
{
	si_info_t *sii;

	sii = SI_INFO(sih);
	if (sih->ccrev < 16)
		return -1;

	
	return (si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, gpiotimeroutmask), mask, val));
}


uint32
si_gpiotimerval(si_t *sih, uint32 mask, uint32 gpiotimerval)
{
	si_info_t *sii;

	sii = SI_INFO(sih);

	if (sih->ccrev < 16)
		return -1;

	return (si_corereg(sih, SI_CC_IDX,
		OFFSETOF(chipcregs_t, gpiotimerval), mask, gpiotimerval));
}

uint32
si_gpiopull(si_t *sih, bool updown, uint32 mask, uint32 val)
{
	si_info_t *sii;
	uint offs;

	sii = SI_INFO(sih);
	if (sih->ccrev < 20)
		return -1;

	offs = (updown ? OFFSETOF(chipcregs_t, gpiopulldown) : OFFSETOF(chipcregs_t, gpiopullup));
	return (si_corereg(sih, SI_CC_IDX, offs, mask, val));
}

uint32
si_gpioevent(si_t *sih, uint regtype, uint32 mask, uint32 val)
{
	si_info_t *sii;
	uint offs;

	sii = SI_INFO(sih);
	if (sih->ccrev < 11)
		return -1;

	if (regtype == GPIO_REGEVT)
		offs = OFFSETOF(chipcregs_t, gpioevent);
	else if (regtype == GPIO_REGEVT_INTMSK)
		offs = OFFSETOF(chipcregs_t, gpioeventintmask);
	else if (regtype == GPIO_REGEVT_INTPOL)
		offs = OFFSETOF(chipcregs_t, gpioeventintpolarity);
	else
		return -1;

	return (si_corereg(sih, SI_CC_IDX, offs, mask, val));
}

void *
BCMINITFN(si_gpio_handler_register)(si_t *sih, uint32 event,
	bool level, gpio_handler_t cb, void *arg)
{
	si_info_t *sii;
	gpioh_item_t *gi;

	ASSERT(event);
	ASSERT(cb != NULL);

	sii = SI_INFO(sih);
	if (sih->ccrev < 11)
		return NULL;

	if ((gi = MALLOC(sii->osh, sizeof(gpioh_item_t))) == NULL)
		return NULL;

	bzero(gi, sizeof(gpioh_item_t));
	gi->event = event;
	gi->handler = cb;
	gi->arg = arg;
	gi->level = level;

	gi->next = sii->gpioh_head;
	sii->gpioh_head = gi;

#ifdef BCMDBG_ERR
	{
		gpioh_item_t *h = sii->gpioh_head;
		int cnt = 0;

		for (; h; h = h->next) {
			cnt++;
			SI_ERROR(("gpiohdler=%p cb=%p event=0x%x\n",
				h, h->handler, h->event));
		}
		SI_ERROR(("gpiohdler total=%d\n", cnt));
	}
#endif
	return (void *)(gi);
}

void
BCMINITFN(si_gpio_handler_unregister)(si_t *sih, void *gpioh)
{
	si_info_t *sii;
	gpioh_item_t *p, *n;

	sii = SI_INFO(sih);
	if (sih->ccrev < 11)
		return;

	ASSERT(sii->gpioh_head != NULL);
	if ((void*)sii->gpioh_head == gpioh) {
		sii->gpioh_head = sii->gpioh_head->next;
		MFREE(sii->osh, gpioh, sizeof(gpioh_item_t));
		return;
	} else {
		p = sii->gpioh_head;
		n = p->next;
		while (n) {
			if ((void*)n == gpioh) {
				p->next = n->next;
				MFREE(sii->osh, gpioh, sizeof(gpioh_item_t));
				return;
			}
			p = n;
			n = n->next;
		}
	}

#ifdef BCMDBG_ERR
	{
		gpioh_item_t *h = sii->gpioh_head;
		int cnt = 0;

		for (; h; h = h->next) {
			cnt++;
			SI_ERROR(("gpiohdler=%p cb=%p event=0x%x\n",
				h, h->handler, h->event));
		}
		SI_ERROR(("gpiohdler total=%d\n", cnt));
	}
#endif
	ASSERT(0); 
}

void
si_gpio_handler_process(si_t *sih)
{
	si_info_t *sii;
	gpioh_item_t *h;
	uint32 status;
	uint32 level = si_gpioin(sih);
	uint32 edge = si_gpioevent(sih, GPIO_REGEVT, 0, 0);

	sii = SI_INFO(sih);
	for (h = sii->gpioh_head; h != NULL; h = h->next) {
		if (h->handler) {
			status = (h->level ? level : edge);

			if (status & h->event)
				h->handler(status, h->arg);
		}
	}

	si_gpioevent(sih, GPIO_REGEVT, edge, edge); 
}

uint32
si_gpio_int_enable(si_t *sih, bool enable)
{
	si_info_t *sii;
	uint offs;

	sii = SI_INFO(sih);
	if (sih->ccrev < 11)
		return -1;

	offs = OFFSETOF(chipcregs_t, intmask);
	return (si_corereg(sih, SI_CC_IDX, offs, CI_GPIO, (enable ? CI_GPIO : 0)));
}



uint32
BCMINITFN(si_socram_size)(si_t *sih)
{
	si_info_t *sii;
	uint origidx;
	uint intr_val = 0;

	sbsocramregs_t *regs;
	bool wasup;
	uint corerev;
	uint32 coreinfo;
	uint memsize = 0;

	sii = SI_INFO(sih);

	
	INTR_OFF(sii, intr_val);
	origidx = si_coreidx(sih);

	
	if (!(regs = si_setcore(sih, SOCRAM_CORE_ID, 0)))
		goto done;

	
	if (!(wasup = si_iscoreup(sih)))
		si_core_reset(sih, 0, 0);
	corerev = si_corerev(sih);
	coreinfo = R_REG(sii->osh, &regs->coreinfo);

	
	if (corerev == 0)
		memsize = 1 << (16 + (coreinfo & SRCI_MS0_MASK));
	else if (corerev < 3) {
		memsize = 1 << (SR_BSZ_BASE + (coreinfo & SRCI_SRBSZ_MASK));
		memsize *= (coreinfo & SRCI_SRNB_MASK) >> SRCI_SRNB_SHIFT;
	} else {
		uint nb = (coreinfo & SRCI_SRNB_MASK) >> SRCI_SRNB_SHIFT;
		uint bsz = (coreinfo & SRCI_SRBSZ_MASK);
		uint lss = (coreinfo & SRCI_LSS_MASK) >> SRCI_LSS_SHIFT;
		if (lss != 0)
			nb --;
		memsize = nb * (1 << (bsz + SR_BSZ_BASE));
		if (lss != 0)
			memsize += (1 << ((lss - 1) + SR_BSZ_BASE));
	}

	
	if (!wasup)
		si_core_disable(sih, 0);
	si_setcoreidx(sih, origidx);

done:
	INTR_RESTORE(sii, intr_val);

	return memsize;
}

#ifdef BCMECICOEX
#define NOTIFY_BT_FM_DISABLE(sih, val) \
	si_eci_notify_bt((sih), ECI_OUT_FM_DISABLE_MASK, ((val) << ECI_OUT_FM_DISABLE_SHIFT), FALSE)


static int
BCMINITFN(si_query_FMDisabled_from_OTP)(si_t *sih, uint16 *FMDisabled)
{
	int error = BCME_OK;
	uint bitoff = 0;
	bool wasup;
	void *oh;

	
	switch (CHIPID(sih->chip)) {
	case BCM4325_CHIP_ID:
		if (sih->chiprev >= 6)
			bitoff = OTP4325_FM_DISABLED_OFFSET;
		break;
	default:
		break;
	}

	
	if (bitoff) {
		if (!(wasup = si_is_otp_powered(sih))) {
			si_otp_power(sih, TRUE);
		}

		if ((oh = otp_init(sih)) != NULL)
			*FMDisabled = !otp_read_one_bit(oh, OTP4325_FM_DISABLED_OFFSET);
		else
			error = BCME_NOTFOUND;

		if (!wasup) {
			si_otp_power(sih, FALSE);
		}
	}

	return error;
}


void *
BCMINITFN(si_eci_init)(si_t *sih)
{
	uint32 origidx = 0;
	si_info_t *sii;
	chipcregs_t *cc;
	bool fast;
	uint16 FMDisabled = FALSE;

	
	if (!(sih->cccaps & CC_CAP_ECI))
		return NULL;

	sii = SI_INFO(sih);
	fast = SI_FAST(sii);
	if (!fast) {
		origidx = sii->curidx;
		if ((cc = (chipcregs_t *)si_setcore(sih, CC_CORE_ID, 0)) == NULL)
			return NULL;
	} else if ((cc = (chipcregs_t *)CCREGS_FAST(sii)) == NULL)
		return NULL;
	ASSERT(cc);

	
	W_REG(sii->osh, &cc->eci_intmaskhi, 0x0);
	W_REG(sii->osh, &cc->eci_intmaskmi, 0x0);
	W_REG(sii->osh, &cc->eci_intmasklo, 0x0);

	
	
	W_REG(sii->osh, &cc->eci_control, ECI_WL_BITS);

	
	W_REG(sii->osh, &cc->eci_eventmaskhi, 0x0);
	W_REG(sii->osh, &cc->eci_eventmaskmi, 0x0);
	W_REG(sii->osh, &cc->eci_eventmasklo, 0x0);

	
	if (!fast)
		si_setcoreidx(sih, origidx);

	
	if (!si_query_FMDisabled_from_OTP(sih, &FMDisabled)) {
		if (FMDisabled) {
			NOTIFY_BT_FM_DISABLE(sih, 1);
		}
	}

	return (void *)cc;
}


void
si_eci_notify_bt(si_t *sih, uint32 mask, uint32 val, bool interrupt)
{
	
	if ((sih->cccaps & CC_CAP_ECI) == 0)
		return;

	
	val = val & ~(1<<30);
	mask = mask | (1<<30);
	si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, eci_output), mask, val);
	
	if (interrupt) {
		si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, eci_output),
		           (1 << 30), (1 << 30));
	}
	return;
}
#endif 

void
si_btcgpiowar(si_t *sih)
{
	si_info_t *sii;
	uint origidx;
	uint intr_val = 0;
	chipcregs_t *cc;

	sii = SI_INFO(sih);

	
	if (!(sih->cccaps & CC_CAP_UARTGPIO))
		return;

	
	INTR_OFF(sii, intr_val);

	origidx = si_coreidx(sih);

	cc = (chipcregs_t *)si_setcore(sih, CC_CORE_ID, 0);
	ASSERT(cc != NULL);

	W_REG(sii->osh, &cc->uart0mcr, R_REG(sii->osh, &cc->uart0mcr) | 0x04);

	
	si_setcoreidx(sih, origidx);

	INTR_RESTORE(sii, intr_val);
}


bool
si_deviceremoved(si_t *sih)
{
	uint32 w;
	si_info_t *sii;

	sii = SI_INFO(sih);

	switch (BUSTYPE(sih->bustype)) {
	case PCI_BUS:
		ASSERT(sii->osh != NULL);
		w = OSL_PCI_READ_CONFIG(sii->osh, PCI_CFG_VID, sizeof(uint32));
		if ((w & 0xFFFF) != VENDOR_BROADCOM)
			return TRUE;
		else
			return FALSE;
	default:
		return FALSE;
	}
	return FALSE;
}

#if !defined(BCMDONGLEHOST)
bool
si_is_otp_disabled(si_t *sih)
{
	switch (CHIPID(sih->chip)) {
	case BCM4325_CHIP_ID:
		return (sih->chipst & CST4325_SPROM_OTP_SEL_MASK) == CST4325_OTP_PWRDN;
	case BCM4329_CHIP_ID:
		return (sih->chipst & CST4329_SPROM_OTP_SEL_MASK) == CST4329_OTP_PWRDN;
	case BCM4315_CHIP_ID:
		return (sih->chipst & CST4315_SPROM_OTP_SEL_MASK) == CST4315_OTP_PWRDN;
	default:
		return FALSE;
	}
}

bool
si_is_sprom_available(si_t *sih)
{
	switch (CHIPID(sih->chip)) {
	case BCM4325_CHIP_ID:
		return (sih->chipst & CST4325_SPROM_SEL) != 0;
	case BCM4329_CHIP_ID:
		return (sih->chipst & CST4329_SPROM_SEL) != 0;
	case BCM4315_CHIP_ID:
		return (sih->chipst & CST4315_SPROM_SEL) != 0;
	case BCM4322_CHIP_ID: {
		uint32 spromotp;
		spromotp = (sih->chipst & CST4322_SPROM_OTP_SEL_MASK)
			>>CST4322_SPROM_OTP_SEL_SHIFT;
		return (spromotp & CST4322_SPROM_PRESENT) != 0;
	}
	default:
		return TRUE;
	}
}

bool
si_is_otp_powered(si_t *sih)
{
	if (PMUCTL_ENAB(sih))
		return si_pmu_is_otp_powered(sih, si_osh(sih));
	return TRUE;
}

void
si_otp_power(si_t *sih, bool on)
{
	uint32 otps;
	if (PMUCTL_ENAB(sih))
		si_pmu_otp_power(sih, si_osh(sih), on);
	if (sih->ccrev >= 21) {
		SPINWAIT((((otps = si_corereg(sih, SI_CC_IDX, CC_OTPST, 0, 0))
		           & OTPS_READY) != (on ? OTPS_READY : 0)), 100);
		ASSERT((otps & OTPS_READY) == (on ? OTPS_READY : 0));
		if ((otps & OTPS_READY) != (on ? OTPS_READY : 0))
			SI_ERROR(("OTP ready bit not %s after wait\n", (on ? "ON" : "OFF")));
	}
	OSL_DELAY(1000);
}

bool
#if defined(BCMDBG) || defined(WLTEST)
si_is_sprom_enabled(si_t *sih)
#else
BCMATTACHFN(si_is_sprom_enabled)(si_t *sih)
#endif
{
	if (PMUCTL_ENAB(sih))
		return si_pmu_is_sprom_enabled(sih, si_osh(sih));
	return TRUE;
}

void
#if defined(BCMDBG) || defined(WLTEST)
si_sprom_enable(si_t *sih, bool enable)
#else
BCMATTACHFN(si_sprom_enable)(si_t *sih, bool enable)
#endif
{
	if (PMUCTL_ENAB(sih))
		si_pmu_sprom_enable(sih, si_osh(sih), enable);
}

int
si_cis_source(si_t *sih)
{
	
	static uint cis_sel[] = { CIS_DEFAULT, CIS_SROM, CIS_OTP, CIS_SROM };

	switch (CHIPID(sih->chip)) {
	case BCM4325_CHIP_ID:
		return ((sih->chipst & CST4325_SPROM_OTP_SEL_MASK) >= sizeof(cis_sel)) ?
		        CIS_DEFAULT : cis_sel[(sih->chipst & CST4325_SPROM_OTP_SEL_MASK)];
	case BCM4329_CHIP_ID:
		return ((sih->chipst & CST4329_SPROM_OTP_SEL_MASK) >= sizeof(cis_sel)) ?
		        CIS_DEFAULT : cis_sel[(sih->chipst & CST4329_SPROM_OTP_SEL_MASK)];
	case BCM4315_CHIP_ID:
		return ((sih->chipst & CST4315_SPROM_OTP_SEL_MASK) >= sizeof(cis_sel)) ?
		        CIS_DEFAULT : cis_sel[(sih->chipst & CST4315_SPROM_OTP_SEL_MASK)];
	case BCM4322_CHIP_ID:
		return (sih->chipst & CST4322_SPROM_PRESENT) ? CIS_SROM :
		        (sih->chipst & CST4322_OTP_PRESENT) ? CIS_OTP : CIS_DEFAULT;
	default:
		return CIS_DEFAULT;

	}
}

void
si_4329_tweak(si_t *sih, uint32 mask, uint32 val)
{
	si_info_t *sii;
	chipcregs_t *cc;
	uint origidx;
	uint32 temp;

	sii = SI_INFO(sih);

	origidx = sii->curidx;
	ASSERT(GOODIDX(origidx));

	cc = (chipcregs_t *)si_setcore(sih, CC_CORE_ID, 0);

	W_REG(sii->osh, &cc->chipcontrol_addr, 0);
	temp = R_REG(sii->osh, &cc->chipcontrol_data);
	temp = temp & ~mask;
	temp = temp | val;
	W_REG(sii->osh, &cc->chipcontrol_data, temp);

	si_setcoreidx(sih, origidx);
}
void
si_4329_vbatmeas_on(si_t *sih, uint32 *save_reg0, uint32 *save_reg5)
{
	si_info_t *sii;
	chipcregs_t *cc;
	uint origidx;
	uint32 temp;

	sii = SI_INFO(sih);

	origidx = sii->curidx;
	ASSERT(GOODIDX(origidx));

	cc = (chipcregs_t *)si_setcore(sih, CC_CORE_ID, 0);

	W_REG(sii->osh, &cc->regcontrol_addr, 0);
	temp = R_REG(sii->osh, &cc->regcontrol_data);
	*save_reg0 = temp;
	temp = temp | 0x00000001;
	W_REG(sii->osh, &cc->regcontrol_data, temp);

	W_REG(sii->osh, &cc->regcontrol_addr, 5);
	temp = R_REG(sii->osh, &cc->regcontrol_data);
	*save_reg5 = temp;
	temp = temp | 0x80000000;
	W_REG(sii->osh, &cc->regcontrol_data, temp);

	si_setcoreidx(sih, origidx);
}

void
si_4329_vbatmeas_off(si_t *sih, uint32 save_reg0, uint32 save_reg5)
{
	si_info_t *sii;
	chipcregs_t *cc;
	uint origidx;

	sii = SI_INFO(sih);

	origidx = sii->curidx;
	ASSERT(GOODIDX(origidx));

	cc = (chipcregs_t *)si_setcore(sih, CC_CORE_ID, 0);

	W_REG(sii->osh, &cc->regcontrol_addr, 0);
	W_REG(sii->osh, &cc->regcontrol_data, save_reg0);

	W_REG(sii->osh, &cc->regcontrol_addr, 5);
	W_REG(sii->osh, &cc->regcontrol_data, save_reg5);

	si_setcoreidx(sih, origidx);
}
#endif 
