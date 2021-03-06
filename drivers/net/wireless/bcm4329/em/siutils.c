/*
 * Misc utility routines for accessing chip-specific features
 * of the SiliconBackplane-based Broadcom chips.
 *
 * Copyright (C) 2010, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: siutils.c,v 1.662.4.4.4.16.4.27 2010/04/19 05:21:23 Exp $
 */

#include <typedefs.h>
#include <bcmdefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <siutils.h>
#include <bcmdevs.h>
#include <hndsoc.h>
#include <sbchipc.h>
#include <pcicfg.h>
#include <sbpcmcia.h>
#include <sbsocram.h>
#include <bcmsdh.h>
#include <sdio.h>
#include <sbsdio.h>
#include <sbhnddma.h>
#include <sbsdpcmdev.h>
#include <bcmsdpcm.h>
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
			wd_msticks = ALP_CLOCK / 1000;
		}

		ksii_attached = TRUE;
		SI_MSG(("si_kattach done. ccrev = %d, wd_msticks = %d\n",
		        ksii.pub.ccrev, wd_msticks));
	}

	return &ksii.pub;
}


static bool
BCMATTACHFN(si_buscore_prep)(si_info_t *sii, uint bustype, uint devid, void *sdh)
{
	
	if (BUSTYPE(bustype) == PCMCIA_BUS)
		sii->memseg = TRUE;


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


	SI_MSG(("Buscore id/type/rev %d/0x%x/%d\n", sii->pub.buscoreidx, sii->pub.buscoretype,
		sii->pub.buscorerev));

	if (BUSTYPE(sii->pub.bustype) == SI_BUS && (CHIPID(sii->pub.chip) == BCM4712_CHIP_ID) &&
	    (sii->pub.chippkg != BCM4712LARGE_PKG_ID) && (sii->pub.chiprev <= 3))
		OR_REG(sii->osh, &cc->slow_clk_ctl, SCC_SS_XTAL);


	
	if ((BUSTYPE(bustype) == SDIO_BUS) || (BUSTYPE(bustype) == SPI_BUS)) {
		if (si_setcore(&sii->pub, ARM7S_CORE_ID, 0) ||
		    si_setcore(&sii->pub, ARMCM3_CORE_ID, 0))
			si_core_disable(&sii->pub, 0);
	}

	
	si_setcoreidx(&sii->pub, *origidx);

	return TRUE;
}


#ifdef CONFIG_XIP
extern uint8 patch_pair;
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

	pvars = NULL;

#ifdef _HNDRTE_
	if (!si_onetimeinit) {
#endif

#ifdef CONFIG_XIP
		
		if (patch_pair) {

			hnd_tcam_bootloader_load(si_setcore(sih, SOCRAM_CORE_ID, 0), pvars);

			si_setcoreidx(sih, origidx);
		}
#endif 

		if (sii->pub.ccrev >= 20) {
			cc = (chipcregs_t *)si_setcore(sih, CC_CORE_ID, 0);
			W_REG(osh, &cc->gpiopullup, 0);
			W_REG(osh, &cc->gpiopulldown, 0);
			si_setcoreidx(sih, origidx);
		}

		
#ifdef _HNDRTE_
		si_onetimeinit = TRUE;
	}
#endif


#ifdef BCMDBG
	
	si_taclear(sih, FALSE);
#endif	

	return (sii);
}


void
BCMATTACHFN(si_detach)(si_t *sih)
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
BCMROMFN(si_osh)(si_t *sih)
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
BCMROMFN(si_register_intr_callback)(si_t *sih, void *intrsoff_fn, void *intrsrestore_fn,
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
BCMROMFN(si_intflag)(si_t *sih)
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
BCMROMFN(si_flag)(si_t *sih)
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
BCMROMFN(si_coreidx)(si_t *sih)
{
	si_info_t *sii;

	sii = SI_INFO(sih);
	return sii->curidx;
}


uint
BCMROMFN(si_coreunit)(si_t *sih)
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
BCMROMFN(si_backplane64)(si_t *sih)
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
BCMROMFN(si_findcoreidx)(si_t *sih, uint coreid, uint coreunit)
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
BCMROMFN(si_core_cflags)(si_t *sih, uint32 mask, uint32 val)
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
BCMROMFN(si_iscoreup)(si_t *sih)
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

void
si_write_wrapperreg(si_t *sih, uint32 offset, uint32 val)
{
	
	if (CHIPTYPE(sih->socitype) == SOCI_AI) {
		ai_write_wrap_reg(sih, offset, val);
	}
	else
		return;

	return;
}

uint
BCMROMFN(si_corereg)(si_t *sih, uint coreidx, uint regoff, uint mask, uint val)
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
BCMROMFN(si_core_disable)(si_t *sih, uint32 bits)
{
	if (CHIPTYPE(sih->socitype) == SOCI_SB)
		sb_core_disable(sih, bits);
	else if (CHIPTYPE(sih->socitype) == SOCI_AI)
		ai_core_disable(sih, bits);
}

void
BCMROMFN(si_core_reset)(si_t *sih, uint32 bits, uint32 resetbits)
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



void
si_watchdog(si_t *sih, uint ticks)
{
	if (PMUCTL_ENAB(sih)) {

		if ((sih->chip == BCM4319_CHIP_ID) && (sih->chiprev == 0) && (ticks != 0)) {
			si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, clk_ctl_st), ~0, 0x2);
			si_setcore(sih, USB20D_CORE_ID, 0);
			si_core_disable(sih, 1);
			si_setcore(sih, CC_CORE_ID, 0);
		}

		if (ticks == 1)
			ticks = 2;
		si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, pmuwatchdog), ~0, ticks);
	} else {
		
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

#if defined(BCMDBG_ERR) || defined(BCMDBG_ASSERT) || defined(BCMDBG_DUMP) || \
	defined(BCMDBG)
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


void
si_sdio_init(si_t *sih)
{
	si_info_t *sii = SI_INFO(sih);

	if (((sih->buscoretype == PCMCIA_CORE_ID) && (sih->buscorerev >= 8)) ||
	    (sih->buscoretype == SDIOD_CORE_ID)) {
		uint idx;
		sdpcmd_regs_t *sdpregs;

		
		idx = sii->curidx;
		ASSERT(idx == si_findcoreidx(sih, D11_CORE_ID, 0));

		
		if (!(sdpregs = (sdpcmd_regs_t *)si_setcore(sih, PCMCIA_CORE_ID, 0)))
			sdpregs = (sdpcmd_regs_t *)si_setcore(sih, SDIOD_CORE_ID, 0);
		ASSERT(sdpregs);

		SI_MSG(("si_sdio_init: For PCMCIA/SDIO Corerev %d, enable ints from core %d "
		        "through SD core %d (%p)\n",
		        sih->buscorerev, idx, sii->curidx, sdpregs));

		
		W_REG(sii->osh, &sdpregs->hostintmask, I_SBINT);
		W_REG(sii->osh, &sdpregs->sbintmask, (I_SB_SERR | I_SB_RESPERR | (1 << idx)));

		
		si_setcoreidx(sih, idx);
	}

	
	bcmsdh_intr_enable(sii->sdh);

}



void *
si_gpiosetcore(si_t *sih)
{
	return (si_setcoreidx(sih, SI_CC_IDX));
}


uint32
BCMROMFN(si_gpiocontrol)(si_t *sih, uint32 mask, uint32 val, uint8 priority)
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
BCMROMFN(si_gpioouten)(si_t *sih, uint32 mask, uint32 val, uint8 priority)
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
BCMROMFN(si_gpioout)(si_t *sih, uint32 mask, uint32 val, uint8 priority)
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
BCMATTACHFN(si_query_FMDisabled_from_OTP)(si_t *sih, uint16 *FMDisabled)
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
BCMATTACHFN(si_eci_init)(si_t *sih)
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
