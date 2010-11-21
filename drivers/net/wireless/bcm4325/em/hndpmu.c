/*
 * Misc utility routines for accessing PMU corerev specific features
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
 * $Id: hndpmu.c,v 1.95.2.17.4.13.4.14 2009/09/14 16:03:39 Exp $
 */

#include <typedefs.h>
#include <bcmdefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <siutils.h>
#include <bcmdevs.h>
#include <hndsoc.h>
#include <sbchipc.h>
#include <hndpmu.h>


#ifdef BCMDBG_ERR
#define	PMU_ERROR(args)	printf args
#else
#define	PMU_ERROR(args)
#endif	

#ifdef BCMDBG
#define	PMU_MSG(args)	printf args
#else
#define	PMU_MSG(args)
#endif	

#if !defined(BCMDONGLEHOST)

static void si_pmu0_pllinit0(si_t *sih, osl_t *osh, chipcregs_t *cc, uint32 xtal);
static void si_pmu1_pllinit0(si_t *sih, osl_t *osh, chipcregs_t *cc, uint32 xtal);
static uint32 si_pmu0_alpclk0(si_t *sih, osl_t *osh, chipcregs_t *cc);
static uint32 si_pmu0_cpuclk0(si_t *sih, osl_t *osh, chipcregs_t *cc);
static uint32 si_pmu1_cpuclk0(si_t *sih, osl_t *osh, chipcregs_t *cc);
static uint32 si_pmu1_alpclk0(si_t *sih, osl_t *osh, chipcregs_t *cc);


static bool si_pmu_res_depfltr_bb(si_t *sih);
static bool si_pmu_res_depfltr_ncb(si_t *sih);
static bool si_pmu_res_depfltr_paldo(si_t *sih);
static bool si_pmu_res_depfltr_npaldo(si_t *sih);
static uint32 si_pmu_res_deps(si_t *sih, osl_t *osh, chipcregs_t *cc, uint32 rsrcs, bool all);
static uint si_pmu_res_uptime(si_t *sih, osl_t *osh, chipcregs_t *cc, uint8 rsrc);
static void si_pmu_res_masks(si_t *sih, uint32 *pmin, uint32 *pmax);


#define FVCO_880	880000	
#define FVCO_1760	1760000	
#define FVCO_1440	1440000	

void
si_pmu_chipcontrol(si_t *sih, uint reg, uint32 mask, uint32 val)
{
	si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, chipcontrol_addr), ~0, reg);
	si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, chipcontrol_data), mask, val);
}


void
BCMINITFN(si_pmu_set_switcher_voltage)(si_t *sih, osl_t *osh,
                                       uint8 bb_voltage, uint8 rf_voltage)
{
	chipcregs_t *cc;
	uint origidx;

	ASSERT(sih->cccaps & CC_CAP_PMU);

	
	origidx = si_coreidx(sih);
	cc = si_setcoreidx(sih, SI_CC_IDX);
	ASSERT(cc != NULL);

	W_REG(osh, &cc->regcontrol_addr, 0x01);
	W_REG(osh, &cc->regcontrol_data, (uint32)(bb_voltage & 0x1f) << 22);

	W_REG(osh, &cc->regcontrol_addr, 0x00);
	W_REG(osh, &cc->regcontrol_data, (uint32)(rf_voltage & 0x1f) << 14);

	
	si_setcoreidx(sih, origidx);
}

void
BCMINITFN(si_pmu_set_ldo_voltage)(si_t *sih, osl_t *osh, uint8 ldo, uint8 voltage)
{
	uint8 sr_cntl_shift = 0, rc_shift = 0, shift = 0, mask = 0;
	uint8 addr = 0;

	ASSERT(sih->cccaps & CC_CAP_PMU);

	switch (CHIPID(sih->chip)) {
	case BCM4328_CHIP_ID:
	case BCM5354_CHIP_ID:
		switch (ldo) {
		case SET_LDO_VOLTAGE_LDO1:
			addr = 2;
			sr_cntl_shift = 8;
			rc_shift = 17;
			mask = 0xf;
			break;
		case SET_LDO_VOLTAGE_LDO2:
			addr = 3;
			rc_shift = 1;
			mask = 0xf;
			break;
		case SET_LDO_VOLTAGE_LDO3:
			addr = 3;
			rc_shift = 9;
			mask = 0xf;
			break;
		case SET_LDO_VOLTAGE_PAREF:
			addr = 3;
			rc_shift = 17;
			mask = 0x3f;
			break;
		default:
			ASSERT(FALSE);
			return;
		}
		break;
	case BCM4312_CHIP_ID:
		switch (ldo) {
		case SET_LDO_VOLTAGE_PAREF:
			addr = 0;
			rc_shift = 21;
			mask = 0x3f;
			break;
		default:
			ASSERT(FALSE);
			return;
		}
		break;
	case BCM4325_CHIP_ID:
		switch (ldo) {
		case SET_LDO_VOLTAGE_CLDO_PWM:
			addr = 5;
			rc_shift = 9;
			mask = 0xf;
			break;
		case SET_LDO_VOLTAGE_CLDO_BURST:
			addr = 5;
			rc_shift = 13;
			mask = 0xf;
			break;
		case SET_LDO_VOLTAGE_CBUCK_PWM:
			addr = 3;
			rc_shift = 20;
			mask = 0x1f;
			
			if (((sih->chipst & CST4325_PMUTOP_2B_MASK) >>
			     CST4325_PMUTOP_2B_SHIFT) == 1)
				voltage ^= 0x9;
			break;
		case SET_LDO_VOLTAGE_CBUCK_BURST:
			addr = 3;
			rc_shift = 25;
			mask = 0x1f;
			
			if (((sih->chipst & CST4325_PMUTOP_2B_MASK) >>
			     CST4325_PMUTOP_2B_SHIFT) == 1)
				voltage ^= 0x9;
			break;
		case SET_LDO_VOLTAGE_LNLDO1:
			addr = 5;
			rc_shift = 17;
			mask = 0x1f;
			break;
		case SET_LDO_VOLTAGE_LNLDO2_SEL:
			addr = 6;
			rc_shift = 0;
			mask = 0x1;
			break;
		default:
			ASSERT(FALSE);
			return;
		}
		break;
	default:
		ASSERT(FALSE);
		return;
	}

	shift = sr_cntl_shift + rc_shift;

	si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, regcontrol_addr),
		~0, addr);
	si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, regcontrol_data),
		mask << shift, (voltage & mask) << shift);
}

void
si_pmu_paref_ldo_enable(si_t *sih, osl_t *osh, bool enable)
{
	uint ldo = 0;

	ASSERT(sih->cccaps & CC_CAP_PMU);

	switch (CHIPID(sih->chip)) {
	case BCM4328_CHIP_ID:
		ldo = RES4328_PA_REF_LDO;
		break;
	case BCM5354_CHIP_ID:
		ldo = RES5354_PA_REF_LDO;
		break;
	case BCM4312_CHIP_ID:
		ldo = RES4312_PA_REF_LDO;
		break;
	default:
		return;
	}

	si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, min_res_mask),
	           PMURES_BIT(ldo), enable ? PMURES_BIT(ldo) : 0);
}


#define D11SCC_SLOW2FAST_TRANSITION	2

uint16
BCMINITFN(si_pmu_fast_pwrup_delay)(si_t *sih, osl_t *osh)
{
	uint delay = PMU_MAX_TRANSITION_DLY;
	chipcregs_t *cc;
	uint origidx;

	ASSERT(sih->cccaps & CC_CAP_PMU);

	
	origidx = si_coreidx(sih);
	cc = si_setcoreidx(sih, SI_CC_IDX);
	ASSERT(cc != NULL);

	switch (CHIPID(sih->chip)) {
	case BCM4328_CHIP_ID:
		delay = 7000;
		break;
	case BCM4325_CHIP_ID:
		if (ISSIM_ENAB(sih))
			delay = 70;
		else {
			uint32 ilp = si_ilp_clock(sih);
			delay = (si_pmu_res_uptime(sih, osh, cc, RES4325_HT_AVAIL) +
			         D11SCC_SLOW2FAST_TRANSITION) * ((1000000 + ilp - 1) / ilp);
			delay = (11 * delay) / 10;
		}
		break;
	case BCM4315_CHIP_ID:
		if (ISSIM_ENAB(sih))
			delay = 70;
		else {
			uint32 ilp = si_ilp_clock(sih);
			delay = (si_pmu_res_uptime(sih, osh, cc, RES4315_HT_AVAIL) +
			         D11SCC_SLOW2FAST_TRANSITION) * ((1000000 + ilp - 1) / ilp);
			delay = (11 * delay) / 10;
		}
		break;
	case BCM4312_CHIP_ID:
		delay = 7000;
		break;
	case BCM4322_CHIP_ID:
		delay = 7000;
		break;

	default:
		break;
	}

	PMU_MSG(("si_pmu_fast_pwrup_delay: chip %x rev %d delay %d\n",
	         sih->chip, sih->chiprev, delay));

	
	si_setcoreidx(sih, origidx);

	return (uint16)delay;
}

uint32
BCMINITFN(si_pmu_force_ilp)(si_t *sih, osl_t *osh, bool force)
{
	chipcregs_t *cc;
	uint origidx;
	uint32 oldpmucontrol;

	ASSERT(sih->cccaps & CC_CAP_PMU);

	
	origidx = si_coreidx(sih);
	cc = si_setcoreidx(sih, SI_CC_IDX);
	ASSERT(cc != NULL);

	oldpmucontrol = R_REG(osh, &cc->pmucontrol);
	if (force)
		W_REG(osh, &cc->pmucontrol, oldpmucontrol &
			~(PCTL_HT_REQ_EN | PCTL_ALP_REQ_EN));
	else
		W_REG(osh, &cc->pmucontrol, oldpmucontrol |
			(PCTL_HT_REQ_EN | PCTL_ALP_REQ_EN));

	
	si_setcoreidx(sih, origidx);

	return oldpmucontrol;
}


typedef struct {
	uint8 resnum;
	uint16 updown;
} pmu_res_updown_t;


typedef struct {
	uint32 res_mask;		
	int8 action;			
	uint32 depend_mask;		
	bool (*filter)(si_t *sih);	
} pmu_res_depend_t;


#define RES_DEPEND_SET		0	
#define RES_DEPEND_ADD		1	
#define RES_DEPEND_REMOVE	-1	

static const pmu_res_updown_t BCMINITDATA(bcm4328a0_res_updown)[] = {
	{ RES4328_EXT_SWITCHER_PWM, 0x0101 },
	{ RES4328_BB_SWITCHER_PWM, 0x1f01 },
	{ RES4328_BB_SWITCHER_BURST, 0x010f },
	{ RES4328_BB_EXT_SWITCHER_BURST, 0x0101 },
	{ RES4328_ILP_REQUEST, 0x0202 },
	{ RES4328_RADIO_SWITCHER_PWM, 0x0f01 },
	{ RES4328_RADIO_SWITCHER_BURST, 0x0f01 },
	{ RES4328_ROM_SWITCH, 0x0101 },
	{ RES4328_PA_REF_LDO, 0x0f01 },
	{ RES4328_RADIO_LDO, 0x0f01 },
	{ RES4328_AFE_LDO, 0x0f01 },
	{ RES4328_PLL_LDO, 0x0f01 },
	{ RES4328_BG_FILTBYP, 0x0101 },
	{ RES4328_TX_FILTBYP, 0x0101 },
	{ RES4328_RX_FILTBYP, 0x0101 },
	{ RES4328_XTAL_PU, 0x0101 },
	{ RES4328_XTAL_EN, 0xa001 },
	{ RES4328_BB_PLL_FILTBYP, 0x0101 },
	{ RES4328_RF_PLL_FILTBYP, 0x0101 },
	{ RES4328_BB_PLL_PU, 0x0701 }
};

static const pmu_res_depend_t BCMINITDATA(bcm4328a0_res_depend)[] = {
	
	{
		PMURES_BIT(RES4328_ILP_REQUEST),
		RES_DEPEND_SET,
		PMURES_BIT(RES4328_EXT_SWITCHER_PWM) | PMURES_BIT(RES4328_BB_SWITCHER_PWM),
		NULL
	}
};

static const pmu_res_updown_t BCMINITDATA(bcm4325a0_res_updown_qt)[] = {
	{ RES4325_HT_AVAIL, 0x0300 },
	{ RES4325_BBPLL_PWRSW_PU, 0x0101 },
	{ RES4325_RFPLL_PWRSW_PU, 0x0101 },
	{ RES4325_ALP_AVAIL, 0x0100 },
	{ RES4325_XTAL_PU, 0x1000 },
	{ RES4325_LNLDO1_PU, 0x0800 },
	{ RES4325_CLDO_CBUCK_PWM, 0x0101 },
	{ RES4325_CBUCK_PWM, 0x0803 }
};

static const pmu_res_updown_t BCMINITDATA(bcm4325a0_res_updown)[] = {
	{ RES4325_XTAL_PU, 0x1501 }
};

static const pmu_res_depend_t BCMINITDATA(bcm4325a0_res_depend)[] = {
	
	{
		PMURES_BIT(RES4325_OTP_PU),
		RES_DEPEND_REMOVE,
		PMURES_BIT(RES4325_BUCK_BOOST_BURST),
		NULL
	},
	
	{
		PMURES_BIT(RES4325_ALP_AVAIL) | PMURES_BIT(RES4325_HT_AVAIL),
		RES_DEPEND_ADD,
		PMURES_BIT(RES4325_BUCK_BOOST_BURST) | PMURES_BIT(RES4325_BUCK_BOOST_PWM),
		si_pmu_res_depfltr_bb
	},
	
	{
		PMURES_BIT(RES4325_HT_AVAIL),
		RES_DEPEND_ADD,
		PMURES_BIT(RES4325_RX_PWRSW_PU) | PMURES_BIT(RES4325_TX_PWRSW_PU) |
		PMURES_BIT(RES4325_LOGEN_PWRSW_PU) | PMURES_BIT(RES4325_AFE_PWRSW_PU),
		NULL
	},
	
	{
		PMURES_BIT(RES4325_ILP_REQUEST) | PMURES_BIT(RES4325_ABUCK_BURST) |
		PMURES_BIT(RES4325_ABUCK_PWM) | PMURES_BIT(RES4325_LNLDO1_PU) |
		PMURES_BIT(RES4325C1_LNLDO2_PU) | PMURES_BIT(RES4325_XTAL_PU) |
		PMURES_BIT(RES4325_ALP_AVAIL) | PMURES_BIT(RES4325_RX_PWRSW_PU) |
		PMURES_BIT(RES4325_TX_PWRSW_PU) | PMURES_BIT(RES4325_RFPLL_PWRSW_PU) |
		PMURES_BIT(RES4325_LOGEN_PWRSW_PU) | PMURES_BIT(RES4325_AFE_PWRSW_PU) |
		PMURES_BIT(RES4325_BBPLL_PWRSW_PU) | PMURES_BIT(RES4325_HT_AVAIL),
		RES_DEPEND_REMOVE,
		PMURES_BIT(RES4325B0_CBUCK_LPOM) | PMURES_BIT(RES4325B0_CBUCK_BURST) |
		PMURES_BIT(RES4325B0_CBUCK_PWM),
		si_pmu_res_depfltr_ncb
	}
};

static const pmu_res_updown_t BCMINITDATA(bcm4315a0_res_updown_qt)[] = {
	{ RES4315_HT_AVAIL, 0x0101 },
	{ RES4315_XTAL_PU, 0x0100 },
	{ RES4315_LNLDO1_PU, 0x0100 },
	{ RES4315_PALDO_PU, 0x0100 },
	{ RES4315_CLDO_PU, 0x0100 },
	{ RES4315_CBUCK_PWM, 0x0100 },
	{ RES4315_CBUCK_BURST, 0x0100 },
	{ RES4315_CBUCK_LPOM, 0x0100 }
};

static const pmu_res_updown_t BCMINITDATA(bcm4315a0_res_updown)[] = {
	{ RES4315_XTAL_PU, 0x2701 }
};

static const pmu_res_depend_t BCMINITDATA(bcm4315a0_res_depend)[] = {
	
	{
		PMURES_BIT(RES4315_OTP_PU),
		RES_DEPEND_REMOVE,
		PMURES_BIT(RES4315_PALDO_PU),
		si_pmu_res_depfltr_npaldo
	},
	
	{
		PMURES_BIT(RES4315_ALP_AVAIL) | PMURES_BIT(RES4315_HT_AVAIL),
		RES_DEPEND_ADD,
		PMURES_BIT(RES4315_PALDO_PU),
		si_pmu_res_depfltr_paldo
	},
	
	{
		PMURES_BIT(RES4315_HT_AVAIL),
		RES_DEPEND_ADD,
		PMURES_BIT(RES4315_RX_PWRSW_PU) | PMURES_BIT(RES4315_TX_PWRSW_PU) |
		PMURES_BIT(RES4315_LOGEN_PWRSW_PU) | PMURES_BIT(RES4315_AFE_PWRSW_PU),
		NULL
	},
	
	{
		PMURES_BIT(RES4315_CLDO_PU) | PMURES_BIT(RES4315_ILP_REQUEST) |
		PMURES_BIT(RES4315_LNLDO1_PU) | PMURES_BIT(RES4315_OTP_PU) |
		PMURES_BIT(RES4315_LNLDO2_PU) | PMURES_BIT(RES4315_XTAL_PU) |
		PMURES_BIT(RES4315_ALP_AVAIL) | PMURES_BIT(RES4315_RX_PWRSW_PU) |
		PMURES_BIT(RES4315_TX_PWRSW_PU) | PMURES_BIT(RES4315_RFPLL_PWRSW_PU) |
		PMURES_BIT(RES4315_LOGEN_PWRSW_PU) | PMURES_BIT(RES4315_AFE_PWRSW_PU) |
		PMURES_BIT(RES4315_BBPLL_PWRSW_PU) | PMURES_BIT(RES4315_HT_AVAIL),
		RES_DEPEND_REMOVE,
		PMURES_BIT(RES4315_CBUCK_LPOM) | PMURES_BIT(RES4315_CBUCK_BURST) |
		PMURES_BIT(RES4315_CBUCK_PWM),
		si_pmu_res_depfltr_ncb
	}
};

static const pmu_res_updown_t BCMINITDATA(bcm4329a0_res_updown)[] = {
	{ RES4329_XTAL_PU, 0xF501 }
};

static const pmu_res_depend_t BCMINITDATA(bcm4329a0_res_depend)[] = {
	
	{
		PMURES_BIT(RES4329_HT_AVAIL),
		RES_DEPEND_ADD,
		PMURES_BIT(RES4329_CBUCK_LPOM) | PMURES_BIT(RES4329_CBUCK_BURST) |
		PMURES_BIT(RES4329_CBUCK_PWM) | PMURES_BIT(RES4329_CLDO_PU) |
		PMURES_BIT(RES4329_PALDO_PU) | PMURES_BIT(RES4329_LNLDO1_PU) |
		PMURES_BIT(RES4329_XTAL_PU) | PMURES_BIT(RES4329_ALP_AVAIL) |
		PMURES_BIT(RES4329_RX_PWRSW_PU) | PMURES_BIT(RES4329_TX_PWRSW_PU) |
		PMURES_BIT(RES4329_RFPLL_PWRSW_PU) | PMURES_BIT(RES4329_LOGEN_PWRSW_PU) |
		PMURES_BIT(RES4329_AFE_PWRSW_PU) | PMURES_BIT(RES4329_BBPLL_PWRSW_PU),
		NULL
	}
};



static bool
BCMINITFN(si_pmu_res_depfltr_bb)(si_t *sih)
{
	return (sih->boardflags & BFL_BUCKBOOST) != 0;
}


static bool
BCMINITFN(si_pmu_res_depfltr_ncb)(si_t *sih)
{
	if (CHIPID(sih->chip) == BCM4325_CHIP_ID)
		return (sih->chiprev >= 2) && ((sih->boardflags & BFL_NOCBUCK) != 0);
	return ((sih->boardflags & BFL_NOCBUCK) != 0);
}


static bool
BCMINITFN(si_pmu_res_depfltr_paldo)(si_t *sih)
{
	return (sih->boardflags & BFL_PALDO) != 0;
}


static bool
BCMINITFN(si_pmu_res_depfltr_npaldo)(si_t *sih)
{
	return (sih->boardflags & BFL_PALDO) == 0;
}

#define BCM94325_BBVDDIOSD_BOARDS(sih) (sih->boardtype == BCM94325DEVBU_BOARD || \
					sih->boardtype == BCM94325BGABU_BOARD)


static void
si_pmu_res_masks(si_t *sih, uint32 *pmin, uint32 *pmax)
{
	uint32 min_mask = 0, max_mask = 0;
	uint rsrcs;
	char *val;

	
	rsrcs = (sih->pmucaps & PCAP_RC_MASK) >> PCAP_RC_SHIFT;

	
	switch (CHIPID(sih->chip)) {
	case BCM4328_CHIP_ID:
		
		min_mask = PMURES_BIT(RES4328_EXT_SWITCHER_PWM) |
		        PMURES_BIT(RES4328_BB_SWITCHER_PWM) |
		        PMURES_BIT(RES4328_XTAL_EN);
#if defined(BCMROMOFFLOAD) || defined(BCMROMBUILD)
		min_mask |= PMURES_BIT(RES4328_ROM_SWITCH);
#endif
		
		max_mask = 0xfffff;
		break;
	case BCM5354_CHIP_ID:
		
		max_mask = 0xfffff;
		break;
	case BCM4325_CHIP_ID:
		ASSERT(sih->chiprev >= 2);
		
		if (!(sih->boardflags & BFL_NOCBUCK))
			min_mask |= PMURES_BIT(RES4325B0_CBUCK_LPOM);
		if (((sih->chipst & CST4325_PMUTOP_2B_MASK) >>
		     CST4325_PMUTOP_2B_SHIFT) == 1)
			min_mask |= PMURES_BIT(RES4325B0_CLDO_PU);
		if (!si_is_otp_disabled(sih))
			min_mask |= PMURES_BIT(RES4325_OTP_PU);
		
		if ((sih->boardflags & BFL_BUCKBOOST) && (BCM94325_BBVDDIOSD_BOARDS(sih)))
			min_mask |= PMURES_BIT(RES4325_BUCK_BOOST_BURST);
		
		max_mask = ~(~0 << rsrcs);
		break;
	case BCM4329_CHIP_ID:
		
		min_mask = PMURES_BIT(RES4329_CBUCK_LPOM) | PMURES_BIT(RES4329_CLDO_PU);
		
		max_mask = 0x3ff63e;
		break;
	case BCM4315_CHIP_ID:
		
		if (!(sih->boardflags & BFL_NOCBUCK))
			min_mask = PMURES_BIT(RES4315_CBUCK_LPOM);
		min_mask |= PMURES_BIT(RES4315_CLDO_PU);
		
		max_mask = ~(~0 << rsrcs);
		break;
	case BCM4312_CHIP_ID:
		
		min_mask = 0xcbb;
		
		break;
	case BCM4322_CHIP_ID:
		if (sih->chiprev < 2) {
			
			min_mask = PMURES_BIT(RES4322_RF_LDO) |	PMURES_BIT(RES4322_XTAL_PU)
				| PMURES_BIT(RES4322_ALP_AVAIL);
			max_mask = 0xff;
		}
		break;

	default:
		break;
	}

	
	if ((val = getvar(NULL, "rmin")) != NULL) {
		PMU_MSG(("Applying rmin=%s to min_mask\n", val));
		min_mask = (uint32)bcm_strtoul(val, NULL, 0);
	}
	
	if ((val = getvar(NULL, "rmax")) != NULL) {
		PMU_MSG(("Applying rmax=%s to max_mask\n", val));
		max_mask = (uint32)bcm_strtoul(val, NULL, 0);
	}

	*pmin = min_mask;
	*pmax = max_mask;
}


void
BCMINITFN(si_pmu_res_init)(si_t *sih, osl_t *osh)
{
	chipcregs_t *cc;
	uint origidx;
	const pmu_res_updown_t *pmu_res_updown_table = NULL;
	uint pmu_res_updown_table_sz = 0;
	const pmu_res_depend_t *pmu_res_depend_table = NULL;
	uint pmu_res_depend_table_sz = 0;
	uint32 min_mask = 0, max_mask = 0;
	char name[8], *val;
	uint i, rsrcs;

	ASSERT(sih->cccaps & CC_CAP_PMU);

	
	origidx = si_coreidx(sih);
	cc = si_setcoreidx(sih, SI_CC_IDX);
	ASSERT(cc != NULL);

	switch (CHIPID(sih->chip)) {
	case BCM4328_CHIP_ID:
		pmu_res_updown_table = bcm4328a0_res_updown;
		pmu_res_updown_table_sz = ARRAYSIZE(bcm4328a0_res_updown);
		pmu_res_depend_table = bcm4328a0_res_depend;
		pmu_res_depend_table_sz = ARRAYSIZE(bcm4328a0_res_depend);
		break;
	case BCM4325_CHIP_ID:
		
		if (ISSIM_ENAB(sih)) {
			pmu_res_updown_table = bcm4325a0_res_updown_qt;
			pmu_res_updown_table_sz = ARRAYSIZE(bcm4325a0_res_updown_qt);
		} else {
			pmu_res_updown_table = bcm4325a0_res_updown;
			pmu_res_updown_table_sz = ARRAYSIZE(bcm4325a0_res_updown);
		}
		
		pmu_res_depend_table = bcm4325a0_res_depend;
		pmu_res_depend_table_sz = ARRAYSIZE(bcm4325a0_res_depend);
		break;
	case BCM4315_CHIP_ID:
		
		if (ISSIM_ENAB(sih)) {
			pmu_res_updown_table = bcm4315a0_res_updown_qt;
			pmu_res_updown_table_sz = ARRAYSIZE(bcm4315a0_res_updown_qt);
		}
		else {
			pmu_res_updown_table = bcm4315a0_res_updown;
			pmu_res_updown_table_sz = ARRAYSIZE(bcm4315a0_res_updown);
		}
		
		pmu_res_depend_table = bcm4315a0_res_depend;
		pmu_res_depend_table_sz = ARRAYSIZE(bcm4315a0_res_depend);
		break;
	case BCM4329_CHIP_ID:
		
		if (ISSIM_ENAB(sih)) {
			pmu_res_updown_table = NULL;
			pmu_res_updown_table_sz = 0;
		} else {
			pmu_res_updown_table = bcm4329a0_res_updown;
			pmu_res_updown_table_sz = ARRAYSIZE(bcm4329a0_res_updown);
		}
		
		pmu_res_depend_table = bcm4329a0_res_depend;
		pmu_res_depend_table_sz = ARRAYSIZE(bcm4329a0_res_depend);
		break;


	default:
		break;
	}

	
	rsrcs = (sih->pmucaps & PCAP_RC_MASK) >> PCAP_RC_SHIFT;

	
	while (pmu_res_updown_table_sz--) {
		ASSERT(pmu_res_updown_table != NULL);
		PMU_MSG(("Changing rsrc %d res_updn_timer to 0x%x\n",
		         pmu_res_updown_table[pmu_res_updown_table_sz].resnum,
		         pmu_res_updown_table[pmu_res_updown_table_sz].updown));
		W_REG(osh, &cc->res_table_sel,
		      pmu_res_updown_table[pmu_res_updown_table_sz].resnum);
		W_REG(osh, &cc->res_updn_timer,
		      pmu_res_updown_table[pmu_res_updown_table_sz].updown);
	}
	
	for (i = 0; i < rsrcs; i ++) {
		snprintf(name, sizeof(name), "r%dt", i);
		if ((val = getvar(NULL, name)) == NULL)
			continue;
		PMU_MSG(("Applying %s=%s to rsrc %d res_updn_timer\n", name, val, i));
		W_REG(osh, &cc->res_table_sel, (uint32)i);
		W_REG(osh, &cc->res_updn_timer, (uint32)bcm_strtoul(val, NULL, 0));
	}

	
	while (pmu_res_depend_table_sz--) {
		ASSERT(pmu_res_depend_table != NULL);
		if (pmu_res_depend_table[pmu_res_depend_table_sz].filter != NULL &&
		    !(pmu_res_depend_table[pmu_res_depend_table_sz].filter)(sih))
			continue;
		for (i = 0; i < rsrcs; i ++) {
			if ((pmu_res_depend_table[pmu_res_depend_table_sz].res_mask &
			     PMURES_BIT(i)) == 0)
				continue;
			W_REG(osh, &cc->res_table_sel, i);
			switch (pmu_res_depend_table[pmu_res_depend_table_sz].action) {
			case RES_DEPEND_SET:
				PMU_MSG(("Changing rsrc %d res_dep_mask to 0x%x\n", i,
				    pmu_res_depend_table[pmu_res_depend_table_sz].depend_mask));
				W_REG(osh, &cc->res_dep_mask,
				      pmu_res_depend_table[pmu_res_depend_table_sz].depend_mask);
				break;
			case RES_DEPEND_ADD:
				PMU_MSG(("Adding 0x%x to rsrc %d res_dep_mask\n",
				    pmu_res_depend_table[pmu_res_depend_table_sz].depend_mask, i));
				OR_REG(osh, &cc->res_dep_mask,
				       pmu_res_depend_table[pmu_res_depend_table_sz].depend_mask);
				break;
			case RES_DEPEND_REMOVE:
				PMU_MSG(("Removing 0x%x from rsrc %d res_dep_mask\n",
				    pmu_res_depend_table[pmu_res_depend_table_sz].depend_mask, i));
				AND_REG(osh, &cc->res_dep_mask,
				        ~pmu_res_depend_table[pmu_res_depend_table_sz].depend_mask);
				break;
			default:
				ASSERT(0);
				break;
			}
		}
	}
	
	for (i = 0; i < rsrcs; i ++) {
		snprintf(name, sizeof(name), "r%dd", i);
		if ((val = getvar(NULL, name)) == NULL)
			continue;
		PMU_MSG(("Applying %s=%s to rsrc %d res_dep_mask\n", name, val, i));
		W_REG(osh, &cc->res_table_sel, (uint32)i);
		W_REG(osh, &cc->res_dep_mask, (uint32)bcm_strtoul(val, NULL, 0));
	}

	
	si_pmu_res_masks(sih, &min_mask, &max_mask);

	
	if (min_mask) {
		PMU_MSG(("Changing min_res_mask to 0x%x\n", min_mask));
		W_REG(osh, &cc->min_res_mask, min_mask);
	}
	
	if (max_mask) {
		PMU_MSG(("Changing max_res_mask to 0x%x\n", max_mask));
		W_REG(osh, &cc->max_res_mask, max_mask);
	}

	
	si_setcoreidx(sih, origidx);
}


typedef struct {
	uint16	freq;
	uint8	xf;
	uint8	wbint;
	uint32	wbfrac;
} pmu0_xtaltab0_t;


static const pmu0_xtaltab0_t BCMINITDATA(pmu0_xtaltab0)[] = {
	{ 12000,	1,	73,	349525 },
	{ 13000,	2,	67,	725937 },
	{ 14400,	3,	61,	116508 },
	{ 15360,	4,	57,	305834 },
	{ 16200,	5,	54,	336579 },
	{ 16800,	6,	52,	399457 },
	{ 19200,	7,	45,	873813 },
	{ 19800,	8,	44,	466033 },
	{ 20000,	9,	44,	0 },
	{ 25000,	10,	70,	419430 },
	{ 26000,	11,	67,	725937 },
	{ 30000,	12,	58,	699050 },
	{ 38400,	13,	45,	873813 },
	{ 40000,	14,	45,	0 },
	{ 0,		0,	0,	0 }
};

#define PMU0_XTAL0_DEFAULT	8



static void
BCMINITFN(si_pmu0_pllinit0)(si_t *sih, osl_t *osh, chipcregs_t *cc, uint32 xtal)
{
	uint32 tmp;
	const pmu0_xtaltab0_t *xt;

	
	for (xt = pmu0_xtaltab0; xt->freq; xt ++)
		if (xt->freq == xtal)
			break;
	if (xt->freq == 0)
		xt = &pmu0_xtaltab0[PMU0_XTAL0_DEFAULT];

	PMU_MSG(("XTAL %d.%d MHz (%d)\n", xtal / 1000, xtal % 1000, xt->xf));

	
	tmp = (R_REG(osh, &cc->pmucontrol) & PCTL_XTALFREQ_MASK) >>
	        PCTL_XTALFREQ_SHIFT;
	if (tmp == xt->xf) {
		PMU_MSG(("PLL already programmed for %d.%d MHz\n",
		         xt->freq / 1000, xt->freq % 1000));
		return;
	}

	if (tmp) {
		PMU_MSG(("Reprogramming PLL for %d.%d MHz (was %d.%dMHz)\n",
		         xt->freq / 1000, xt->freq % 1000,
		         pmu0_xtaltab0[tmp-1].freq / 1000, pmu0_xtaltab0[tmp-1].freq % 1000));
	} else {
		PMU_MSG(("Programming PLL for %d.%d MHz\n",
		         xt->freq / 1000, xt->freq % 1000));
	}

	
	switch (CHIPID(sih->chip)) {
	case BCM4328_CHIP_ID:
		AND_REG(osh, &cc->min_res_mask, ~PMURES_BIT(RES4328_BB_PLL_PU));
		AND_REG(osh, &cc->max_res_mask, ~PMURES_BIT(RES4328_BB_PLL_PU));
		break;
	case BCM5354_CHIP_ID:
		AND_REG(osh, &cc->min_res_mask, ~PMURES_BIT(RES5354_BB_PLL_PU));
		AND_REG(osh, &cc->max_res_mask, ~PMURES_BIT(RES5354_BB_PLL_PU));
		break;
	default:
		ASSERT(0);
	}
	SPINWAIT(R_REG(osh, &cc->clk_ctl_st) & CCS0_HTAVAIL, PMU_MAX_TRANSITION_DLY);
	ASSERT(!(R_REG(osh, &cc->clk_ctl_st) & CCS0_HTAVAIL));

	PMU_MSG(("Done masking\n"));

	
	W_REG(osh, &cc->pllcontrol_addr, PMU0_PLL0_PLLCTL0);
	tmp = R_REG(osh, &cc->pllcontrol_data);
	if (xt->freq >= PMU0_PLL0_PC0_PDIV_FREQ)
		tmp |= PMU0_PLL0_PC0_PDIV_MASK;
	else
		tmp &= ~PMU0_PLL0_PC0_PDIV_MASK;
	W_REG(osh, &cc->pllcontrol_data, tmp);

	
	W_REG(osh, &cc->pllcontrol_addr, PMU0_PLL0_PLLCTL1);
	tmp = R_REG(osh, &cc->pllcontrol_data);
	tmp = ((tmp & ~(PMU0_PLL0_PC1_WILD_INT_MASK | PMU0_PLL0_PC1_WILD_FRAC_MASK)) |
	       (((xt->wbint << PMU0_PLL0_PC1_WILD_INT_SHIFT) &
	         PMU0_PLL0_PC1_WILD_INT_MASK) |
	        ((xt->wbfrac << PMU0_PLL0_PC1_WILD_FRAC_SHIFT) &
	         PMU0_PLL0_PC1_WILD_FRAC_MASK)));
	if (xt->wbfrac == 0)
		tmp |= PMU0_PLL0_PC1_STOP_MOD;
	else
		tmp &= ~PMU0_PLL0_PC1_STOP_MOD;
	W_REG(osh, &cc->pllcontrol_data, tmp);

	
	W_REG(osh, &cc->pllcontrol_addr, PMU0_PLL0_PLLCTL2);
	tmp = R_REG(osh, &cc->pllcontrol_data);
	tmp = ((tmp & ~PMU0_PLL0_PC2_WILD_INT_MASK) |
	       ((xt->wbint >> PMU0_PLL0_PC2_WILD_INT_SHIFT) &
	        PMU0_PLL0_PC2_WILD_INT_MASK));
	W_REG(osh, &cc->pllcontrol_data, tmp);

	PMU_MSG(("Done pll\n"));

	
	tmp = R_REG(osh, &cc->pmucontrol);
	tmp = ((tmp & ~PCTL_ILP_DIV_MASK) |
	       (((((xt->freq + 127) / 128) - 1) << PCTL_ILP_DIV_SHIFT) & PCTL_ILP_DIV_MASK));
	tmp = ((tmp & ~PCTL_XTALFREQ_MASK) |
	       ((xt->xf << PCTL_XTALFREQ_SHIFT) & PCTL_XTALFREQ_MASK));
	W_REG(osh, &cc->pmucontrol, tmp);
}


static uint32
BCMINITFN(si_pmu0_alpclk0)(si_t *sih, osl_t *osh, chipcregs_t *cc)
{
	const pmu0_xtaltab0_t *xt;
	uint32 xf;

	
	xf = (R_REG(osh, &cc->pmucontrol) & PCTL_XTALFREQ_MASK) >>
	        PCTL_XTALFREQ_SHIFT;
	for (xt = pmu0_xtaltab0; xt->freq; xt++)
		if (xt->xf == xf)
			break;
	
	ASSERT(xt->freq);

	return xt->freq * 1000;
}


static uint32
BCMINITFN(si_pmu0_cpuclk0)(si_t *sih, osl_t *osh, chipcregs_t *cc)
{
	uint32 tmp, divarm;
#ifdef BCMDBG
	uint32 pdiv, wbint, wbfrac, fvco;
	uint32 freq;
#endif
	uint32 FVCO = FVCO_880;

	
	W_REG(osh, &cc->pllcontrol_addr, PMU0_PLL0_PLLCTL0);
	tmp = R_REG(osh, &cc->pllcontrol_data);
	divarm = (tmp & PMU0_PLL0_PC0_DIV_ARM_MASK) >> PMU0_PLL0_PC0_DIV_ARM_SHIFT;

#ifdef BCMDBG
	
	pdiv = tmp & PMU0_PLL0_PC0_PDIV_MASK;

	W_REG(osh, &cc->pllcontrol_addr, PMU0_PLL0_PLLCTL1);
	tmp = R_REG(osh, &cc->pllcontrol_data);
	wbfrac = (tmp & PMU0_PLL0_PC1_WILD_FRAC_MASK) >> PMU0_PLL0_PC1_WILD_FRAC_SHIFT;
	wbint = (tmp & PMU0_PLL0_PC1_WILD_INT_MASK) >> PMU0_PLL0_PC1_WILD_INT_SHIFT;

	W_REG(osh, &cc->pllcontrol_addr, PMU0_PLL0_PLLCTL2);
	tmp = R_REG(osh, &cc->pllcontrol_data);
	wbint += (tmp & PMU0_PLL0_PC2_WILD_INT_MASK) << PMU0_PLL0_PC2_WILD_INT_SHIFT;

	freq = si_pmu0_alpclk0(sih, osh, cc) / 1000;

	fvco = (freq * wbint) << 8;
	fvco += (freq * (wbfrac >> 10)) >> 2;
	fvco += (freq * (wbfrac & 0x3ff)) >> 10;
	fvco >>= 8;
	fvco >>= pdiv;
	fvco /= 1000;
	fvco *= 1000;

	PMU_MSG(("si_pmu0_cpuclk0: wbint %u wbfrac %u fvco %u\n",
	         wbint, wbfrac, fvco));

	FVCO = fvco;
#endif	

	
	return FVCO / (divarm + PMU0_PLL0_PC0_DIV_ARM_BASE) * 1000;
}


typedef struct {
	uint16	fref;
	uint8	xf;
	uint8	p1div;
	uint8	p2div;
	uint8	ndiv_int;
	uint32	ndiv_frac;
} pmu1_xtaltab0_t;


static const pmu1_xtaltab0_t BCMINITDATA(pmu1_xtaltab0_880)[] = {
	{12000,	1,	3,	22,	0x9,	0xFFFFEF},
	{13000,	2,	1,	6,	0xb,	0x483483},
	{14400,	3,	1,	10,	0xa,	0x1C71C7},
	{15360,	4,	1,	5,	0xb,	0x755555},
	{16200,	5,	1,	10,	0x5,	0x6E9E06},
	{16800,	6,	1,	10,	0x5,	0x3Cf3Cf},
	{19200,	7,	1,	4,	0xb,	0x755555},
	{19800,	8,	1,	11,	0x4,	0xA57EB},
	{20000,	9,	1,	11,	0x4,	0x0},
	{24000,	10,	3,	11,	0xa,	0x0},
	{25000,	11,	5,	16,	0xb,	0x0},
	{26000,	12,	1,	2,	0x10,	0xEC4EC4},
	{30000,	13,	3,	8,	0xb,	0x0},
	{33600,	14,	1,	2,	0xd,	0x186186},
	{38400,	15,	1,	2,	0xb,	0x755555},
	{40000,	16,	1,	2,	0xb,	0},
	{0,	0,	0,	0,	0,	0}
};

#define PMU1_XTALTAB0_880_12000K	0
#define PMU1_XTALTAB0_880_13000K	1
#define PMU1_XTALTAB0_880_14400K	2
#define PMU1_XTALTAB0_880_15360K	3
#define PMU1_XTALTAB0_880_16200K	4
#define PMU1_XTALTAB0_880_16800K	5
#define PMU1_XTALTAB0_880_19200K	6
#define PMU1_XTALTAB0_880_19800K	7
#define PMU1_XTALTAB0_880_20000K	8
#define PMU1_XTALTAB0_880_24000K	9
#define PMU1_XTALTAB0_880_25000K	10
#define PMU1_XTALTAB0_880_26000K	11
#define PMU1_XTALTAB0_880_30000K	12
#define PMU1_XTALTAB0_880_38400K	13
#define PMU1_XTALTAB0_880_40000K	14


static const pmu1_xtaltab0_t BCMINITDATA(pmu1_xtaltab0_1760)[] = {
	{12000,	1,	3,	44,	0x9,	0xFFFFEF},
	{13000,	2,	1,	12,	0xb,	0x483483},
	{14400,	3,	1,	20,	0xa,	0x1C71C7},
	{15360,	4,	1,	10,	0xb,	0x755555},
	{16200,	5,	1,	20,	0x5,	0x6E9E06},
	{16800,	6,	1,	20,	0x5,	0x3Cf3Cf},
	{19200,	7,	1,	18,	0x5,	0x17B425},
	{19800,	8,	1,	22,	0x4,	0xA57EB},
	{20000,	9,	1,	22,	0x4,	0x0},
	{24000,	10,	3,	22,	0xa,	0x0},
	{25000,	11,	5,	32,	0xb,	0x0},
	{26000,	12,	1,	4,	0x10,	0xEC4EC4},
	{30000,	13,	3,	16,	0xb,	0x0},
	{38400,	14,	1,	10,	0x4,	0x955555},
	{40000,	15,	1,	4,	0xb,	0},
	{0,	0,	0,	0,	0,	0}
};


#define PMU1_XTALTAB0_1760_12000K	0
#define PMU1_XTALTAB0_1760_13000K	1
#define PMU1_XTALTAB0_1760_14400K	2
#define PMU1_XTALTAB0_1760_15360K	3
#define PMU1_XTALTAB0_1760_16200K	4
#define PMU1_XTALTAB0_1760_16800K	5
#define PMU1_XTALTAB0_1760_19200K	6
#define PMU1_XTALTAB0_1760_19800K	7
#define PMU1_XTALTAB0_1760_20000K	8
#define PMU1_XTALTAB0_1760_24000K	9
#define PMU1_XTALTAB0_1760_25000K	10
#define PMU1_XTALTAB0_1760_26000K	11
#define PMU1_XTALTAB0_1760_30000K	12
#define PMU1_XTALTAB0_1760_38400K	13
#define PMU1_XTALTAB0_1760_40000K	14


static const pmu1_xtaltab0_t BCMINITDATA(pmu1_xtaltab0_1440)[] = {
	{12000,	1,	3,	44,	0x9,	0xFFFFEF},
	{13000,	2,	1,	12,	0xb,	0x483483},
	{14400,	3,	1,	20,	0xa,	0x1C71C7},
	{15360,	4,	1,	10,	0xb,	0x755555},
	{16200,	5,	1,	20,	0x5,	0x6E9E06},
	{16800,	6,	1,	20,	0x5,	0x3Cf3Cf},
	{19200,	7,	1,	18,	0x5,	0x17B425},
	{19800,	8,	1,	22,	0x4,	0xA57EB},
	{20000,	9,	1,	22,	0x4,	0x0},
	{24000,	10,	1,	1,	0x3c,	0x0},
	{25000,	11,	5,	32,	0xb,	0x0},
	{26000,	12,	1,	4,	0x10,	0xEC4EC4},
	{30000,	13,	1,	1,	0x30,	0x0},
	{38400,	14,	1,	10,	0x4,	0x955555},
	{40000,	15,	1,	4,	0xb,	0},
	{48000,	16,	2,	1,	0x3c,	0x0},
	{0,	0,	0,	0,	0,	0}
};


#define PMU1_XTALTAB0_1440_12000K	0
#define PMU1_XTALTAB0_1440_13000K	1
#define PMU1_XTALTAB0_1440_14400K	2
#define PMU1_XTALTAB0_1440_15360K	3
#define PMU1_XTALTAB0_1440_16200K	4
#define PMU1_XTALTAB0_1440_16800K	5
#define PMU1_XTALTAB0_1440_19200K	6
#define PMU1_XTALTAB0_1440_19800K	7
#define PMU1_XTALTAB0_1440_20000K	8
#define PMU1_XTALTAB0_1440_24000K	9
#define PMU1_XTALTAB0_1440_25000K	10
#define PMU1_XTALTAB0_1440_26000K	11
#define PMU1_XTALTAB0_1440_30000K	12
#define PMU1_XTALTAB0_1440_38400K	13
#define PMU1_XTALTAB0_1440_40000K	14


static const pmu1_xtaltab0_t *
BCMINITFN(si_pmu1_xtaltab0)(si_t *sih)
{
	switch (CHIPID(sih->chip)) {
	case BCM4325_CHIP_ID:
		return pmu1_xtaltab0_880;
	case BCM4329_CHIP_ID:
		return pmu1_xtaltab0_880;
	case BCM4315_CHIP_ID:
		return pmu1_xtaltab0_1760;
	default:
		PMU_MSG(("si_pmu1_xtaltab0: Unknown chipid %04X\n", sih->chip));
		break;
	}
	ASSERT(0);
	return NULL;
}


static const pmu1_xtaltab0_t *
BCMINITFN(si_pmu1_xtaldef0)(si_t *sih)
{
	switch (CHIPID(sih->chip)) {
	case BCM4325_CHIP_ID:
		
		return &pmu1_xtaltab0_880[PMU1_XTALTAB0_880_26000K];
	case BCM4329_CHIP_ID:
		
		return &pmu1_xtaltab0_880[PMU1_XTALTAB0_1760_38400K];
	case BCM4315_CHIP_ID:
		
		return &pmu1_xtaltab0_1760[PMU1_XTALTAB0_1760_26000K];
	default:
		PMU_MSG(("si_pmu1_xtaldef0: Unknown chipid %04X\n", sih->chip));
		break;
	}
	ASSERT(0);
	return NULL;
}


static uint32
BCMINITFN(si_pmu1_pllfvco0)(si_t *sih)
{
	switch (CHIPID(sih->chip)) {
	case BCM4325_CHIP_ID:
		return FVCO_880;
	case BCM4329_CHIP_ID:
		return FVCO_880;
	case BCM4315_CHIP_ID:
		return FVCO_1760;
	default:
		PMU_MSG(("si_pmu1_pllfvco0: Unknown chipid %04X\n", sih->chip));
		break;
	}
	ASSERT(0);
	return 0;
}


static uint32
BCMINITFN(si_pmu1_alpclk0)(si_t *sih, osl_t *osh, chipcregs_t *cc)
{
	const pmu1_xtaltab0_t *xt;
	uint32 xf;

	
	xf = (R_REG(osh, &cc->pmucontrol) & PCTL_XTALFREQ_MASK) >>
	        PCTL_XTALFREQ_SHIFT;
	for (xt = si_pmu1_xtaltab0(sih); xt != NULL && xt->fref != 0; xt ++)
		if (xt->xf == xf)
			break;
	
	if (xt == NULL || xt->fref == 0)
		xt = si_pmu1_xtaldef0(sih);
	ASSERT(xt != NULL && xt->fref != 0);

	return xt->fref * 1000;
}


static void
BCMINITFN(si_pmu1_pllinit0)(si_t *sih, osl_t *osh, chipcregs_t *cc, uint32 xtal)
{
	const pmu1_xtaltab0_t *xt;
	uint32 tmp;
	uint32 buf_strength = 0;

	
	if (xtal == 0) {
		PMU_MSG(("Unspecified xtal frequency, skip PLL configuration\n"));
		return;
	}

	
	for (xt = si_pmu1_xtaltab0(sih); xt != NULL && xt->fref != 0; xt ++)
		if (xt->fref == xtal)
			break;

	
	if (xt == NULL || xt->fref == 0) {
		PMU_MSG(("Unsupported xtal frequency %d.%d MHz, skip PLL configuration\n",
		         xtal / 1000, xtal % 1000));
		return;
	}
	if (((R_REG(osh, &cc->pmucontrol) & PCTL_XTALFREQ_MASK) >>
	     PCTL_XTALFREQ_SHIFT) == xt->xf) {
		PMU_MSG(("PLL already programmed for %d.%d MHz\n",
			xt->fref / 1000, xt->fref % 1000));
		return;
	}

	PMU_MSG(("XTAL %d.%d MHz (%d)\n", xtal / 1000, xtal % 1000, xt->xf));
	PMU_MSG(("Programming PLL for %d.%d MHz\n", xt->fref / 1000, xt->fref % 1000));

	switch (CHIPID(sih->chip)) {
	case BCM4325_CHIP_ID:
		
		buf_strength = 0x222222;
		
		AND_REG(osh, &cc->min_res_mask,
		        ~(PMURES_BIT(RES4325_BBPLL_PWRSW_PU) | PMURES_BIT(RES4325_HT_AVAIL)));
		AND_REG(osh, &cc->max_res_mask,
		        ~(PMURES_BIT(RES4325_BBPLL_PWRSW_PU) | PMURES_BIT(RES4325_HT_AVAIL)));
		SPINWAIT(R_REG(osh, &cc->clk_ctl_st) & CCS_HTAVAIL, PMU_MAX_TRANSITION_DLY);
		ASSERT(!(R_REG(osh, &cc->clk_ctl_st) & CCS_HTAVAIL));
		break;
	case BCM4329_CHIP_ID:
		
		buf_strength = 0x888888;
		AND_REG(osh, &cc->min_res_mask,
		        ~(PMURES_BIT(RES4329_BBPLL_PWRSW_PU) | PMURES_BIT(RES4329_HT_AVAIL)));
		AND_REG(osh, &cc->max_res_mask,
		        ~(PMURES_BIT(RES4329_BBPLL_PWRSW_PU) | PMURES_BIT(RES4329_HT_AVAIL)));
		SPINWAIT(R_REG(osh, &cc->clk_ctl_st) & CCS_HTAVAIL, PMU_MAX_TRANSITION_DLY);
		ASSERT(!(R_REG(osh, &cc->clk_ctl_st) & CCS_HTAVAIL));
		break;
	case BCM4315_CHIP_ID:
		
		buf_strength = 0x222222;
		
		AND_REG(osh, &cc->min_res_mask, ~(PMURES_BIT(RES4315_HT_AVAIL)));
		AND_REG(osh, &cc->max_res_mask, ~(PMURES_BIT(RES4315_HT_AVAIL)));
		OSL_DELAY(100);

		AND_REG(osh, &cc->min_res_mask, ~(PMURES_BIT(RES4315_BBPLL_PWRSW_PU)));
		AND_REG(osh, &cc->max_res_mask, ~(PMURES_BIT(RES4315_BBPLL_PWRSW_PU)));
		OSL_DELAY(100);

		SPINWAIT(R_REG(osh, &cc->clk_ctl_st) & CCS_HTAVAIL, PMU_MAX_TRANSITION_DLY);
		ASSERT(!(R_REG(osh, &cc->clk_ctl_st) & CCS_HTAVAIL));
		break;
	default:
		ASSERT(0);
	}

	PMU_MSG(("Done masking\n"));

	
	W_REG(osh, &cc->pllcontrol_addr, PMU1_PLL0_PLLCTL0);
	tmp = R_REG(osh, &cc->pllcontrol_data) &
	        ~(PMU1_PLL0_PC0_P1DIV_MASK | PMU1_PLL0_PC0_P2DIV_MASK);
	tmp |= ((xt->p1div << PMU1_PLL0_PC0_P1DIV_SHIFT) & PMU1_PLL0_PC0_P1DIV_MASK) |
	        ((xt->p2div << PMU1_PLL0_PC0_P2DIV_SHIFT) & PMU1_PLL0_PC0_P2DIV_MASK);
	W_REG(osh, &cc->pllcontrol_data, tmp);


	if ((CHIPID(sih->chip) == BCM4329_CHIP_ID) && (sih->chiprev == 0)) {

		W_REG(osh, &cc->pllcontrol_addr, PMU1_PLL0_PLLCTL1);
		tmp = R_REG(osh, &cc->pllcontrol_data);
		tmp = tmp & (~DOT11MAC_880MHZ_CLK_DIVISOR_MASK);
		tmp = tmp | DOT11MAC_880MHZ_CLK_DIVISOR_VAL;
		W_REG(osh, &cc->pllcontrol_data, tmp);
	}

	
	W_REG(osh, &cc->pllcontrol_addr, PMU1_PLL0_PLLCTL2);
	tmp = R_REG(osh, &cc->pllcontrol_data) &
	        ~(PMU1_PLL0_PC2_NDIV_INT_MASK | PMU1_PLL0_PC2_NDIV_MODE_MASK);
	tmp |= ((xt->ndiv_int << PMU1_PLL0_PC2_NDIV_INT_SHIFT) & PMU1_PLL0_PC2_NDIV_INT_MASK) |
	        ((1 << PMU1_PLL0_PC2_NDIV_MODE_SHIFT) & PMU1_PLL0_PC2_NDIV_MODE_MASK);
	W_REG(osh, &cc->pllcontrol_data, tmp);

	
	W_REG(osh, &cc->pllcontrol_addr, PMU1_PLL0_PLLCTL3);
	tmp = R_REG(osh, &cc->pllcontrol_data) & ~PMU1_PLL0_PC3_NDIV_FRAC_MASK;
	tmp |= ((xt->ndiv_frac << PMU1_PLL0_PC3_NDIV_FRAC_SHIFT) &
	        PMU1_PLL0_PC3_NDIV_FRAC_MASK);
	W_REG(osh, &cc->pllcontrol_data, tmp);

	
	if (buf_strength) {
		PMU_MSG(("Adjusting PLL buffer drive strength: %x\n", buf_strength));

		W_REG(osh, &cc->pllcontrol_addr, PMU1_PLL0_PLLCTL5);
		tmp = R_REG(osh, &cc->pllcontrol_data) & ~PMU1_PLL0_PC5_CLK_DRV_MASK;
		tmp |= (buf_strength << PMU1_PLL0_PC5_CLK_DRV_SHIFT);
		W_REG(osh, &cc->pllcontrol_data, tmp);
	}

	PMU_MSG(("Done pll\n"));

	
	if (sih->pmurev >= 2)
		OR_REG(osh, &cc->pmucontrol, PCTL_PLL_PLLCTL_UPD);

	
	tmp = R_REG(osh, &cc->pmucontrol) &
	        ~(PCTL_ILP_DIV_MASK | PCTL_XTALFREQ_MASK);
	tmp |= (((((xt->fref + 127) / 128) - 1) << PCTL_ILP_DIV_SHIFT) &
	        PCTL_ILP_DIV_MASK) |
	       ((xt->xf << PCTL_XTALFREQ_SHIFT) & PCTL_XTALFREQ_MASK);

	if ((CHIPID(sih->chip) == BCM4329_CHIP_ID) && sih->chiprev == 0) {
		
		AND_REG(osh, &cc->clkstretch, ~CSTRETCH_HT);
		tmp &= ~PCTL_HT_REQ_EN;
	}

	W_REG(osh, &cc->pmucontrol, tmp);
}


static uint32
BCMINITFN(si_pmu1_cpuclk0)(si_t *sih, osl_t *osh, chipcregs_t *cc)
{
	uint32 tmp, m1div;
#ifdef BCMDBG
	uint32 ndiv_int, ndiv_frac, p2div, p1div, fvco;
	uint32 fref;
#endif
	uint32 FVCO = si_pmu1_pllfvco0(sih);

	
	W_REG(osh, &cc->pllcontrol_addr, PMU1_PLL0_PLLCTL1);
	tmp = R_REG(osh, &cc->pllcontrol_data);
	m1div = (tmp & PMU1_PLL0_PC1_M1DIV_MASK) >> PMU1_PLL0_PC1_M1DIV_SHIFT;

#ifdef BCMDBG
	
	W_REG(osh, &cc->pllcontrol_addr, PMU1_PLL0_PLLCTL0);
	tmp = R_REG(osh, &cc->pllcontrol_data);
	p2div = (tmp & PMU1_PLL0_PC0_P2DIV_MASK) >> PMU1_PLL0_PC0_P2DIV_SHIFT;
	p1div = (tmp & PMU1_PLL0_PC0_P1DIV_MASK) >> PMU1_PLL0_PC0_P1DIV_SHIFT;

	
	W_REG(osh, &cc->pllcontrol_addr, PMU1_PLL0_PLLCTL2);
	tmp = R_REG(osh, &cc->pllcontrol_data);
	ndiv_int = (tmp & PMU1_PLL0_PC2_NDIV_INT_MASK) >> PMU1_PLL0_PC2_NDIV_INT_SHIFT;

	W_REG(osh, &cc->pllcontrol_addr, PMU1_PLL0_PLLCTL3);
	tmp = R_REG(osh, &cc->pllcontrol_data);
	ndiv_frac = (tmp & PMU1_PLL0_PC3_NDIV_FRAC_MASK) >> PMU1_PLL0_PC3_NDIV_FRAC_SHIFT;

	fref = si_pmu1_alpclk0(sih, osh, cc) / 1000;

	fvco = (fref * ndiv_int) << 8;
	fvco += (fref * (ndiv_frac >> 12)) >> 4;
	fvco += (fref * (ndiv_frac & 0xfff)) >> 12;
	fvco >>= 8;
	fvco *= p2div;
	fvco /= p1div;
	fvco /= 1000;
	fvco *= 1000;

	PMU_MSG(("si_pmu1_cpuclk0: ndiv_int %u ndiv_frac %u p2div %u p1div %u fvco %u\n",
	         ndiv_int, ndiv_frac, p2div, p1div, fvco));

	FVCO = fvco;
#endif	

	
	return FVCO / m1div * 1000;
}


void
BCMINITFN(si_pmu_pll_init)(si_t *sih, osl_t *osh, uint xtalfreq)
{
	chipcregs_t *cc;
	uint origidx;

	ASSERT(sih->cccaps & CC_CAP_PMU);

	
	origidx = si_coreidx(sih);
	cc = si_setcoreidx(sih, SI_CC_IDX);
	ASSERT(cc != NULL);

	switch (CHIPID(sih->chip)) {
	case BCM4328_CHIP_ID:
		si_pmu0_pllinit0(sih, osh, cc, xtalfreq);
		break;
	case BCM5354_CHIP_ID:
		if (xtalfreq == 0)
			xtalfreq = 25000;
		si_pmu0_pllinit0(sih, osh, cc, xtalfreq);
		break;
	case BCM4325_CHIP_ID:
		si_pmu1_pllinit0(sih, osh, cc, xtalfreq);
		break;
	case BCM4329_CHIP_ID:
		si_pmu1_pllinit0(sih, osh, cc, xtalfreq);
		break;
	case BCM4315_CHIP_ID:
		si_pmu1_pllinit0(sih, osh, cc, xtalfreq);
		break;
	case BCM4312_CHIP_ID:
		
		break;
	case BCM4322_CHIP_ID:
		break;

	default:
		PMU_MSG(("No PLL init done for chip %x rev %d pmurev %d\n",
		         sih->chip, sih->chiprev, sih->pmurev));
		break;
	}

	
	si_setcoreidx(sih, origidx);
}


uint32
BCMINITFN(si_pmu_alp_clock)(si_t *sih, osl_t *osh)
{
	chipcregs_t *cc;
	uint origidx;
	uint32 clock = ALP_CLOCK;

	ASSERT(sih->cccaps & CC_CAP_PMU);

	
	origidx = si_coreidx(sih);
	cc = si_setcoreidx(sih, SI_CC_IDX);
	ASSERT(cc != NULL);

	switch (CHIPID(sih->chip)) {
	case BCM4328_CHIP_ID:
		clock = si_pmu0_alpclk0(sih, osh, cc);
		break;
	case BCM5354_CHIP_ID:
		clock = si_pmu0_alpclk0(sih, osh, cc);
		break;
	case BCM4325_CHIP_ID:
		clock = si_pmu1_alpclk0(sih, osh, cc);
		break;
	case BCM4329_CHIP_ID:
		clock = si_pmu1_alpclk0(sih, osh, cc);
		break;
	case BCM4315_CHIP_ID:
		clock = si_pmu1_alpclk0(sih, osh, cc);
		break;
	case BCM4312_CHIP_ID:
		
		clock = 20000 * 1000;
		break;
	case BCM4322_CHIP_ID:
		
		clock = 20000 * 1000;
		break;

	default:
		PMU_MSG(("No ALP clock specified "
			"for chip %x rev %d pmurev %d, using default %d Hz\n",
			sih->chip, sih->chiprev, sih->pmurev, clock));
		break;
	}

	
	si_setcoreidx(sih, origidx);
	return clock;
}



uint32
BCMINITFN(si_pmu_si_clock)(si_t *sih, osl_t *osh)
{
	chipcregs_t *cc;
	uint origidx;
	uint32 clock = HT_CLOCK;

	ASSERT(sih->cccaps & CC_CAP_PMU);

	
	origidx = si_coreidx(sih);
	cc = si_setcoreidx(sih, SI_CC_IDX);
	ASSERT(cc != NULL);

	switch (CHIPID(sih->chip)) {
	case BCM4328_CHIP_ID:
		clock = si_pmu0_cpuclk0(sih, osh, cc);
		break;
	case BCM5354_CHIP_ID:
		clock = 120000000;
		break;
	case BCM4325_CHIP_ID:
		clock = si_pmu1_cpuclk0(sih, osh, cc);
		break;
	case BCM4329_CHIP_ID:
		if (sih->chiprev == 0)
			clock = 38400 * 1000;
		else
			clock = si_pmu1_cpuclk0(sih, osh, cc);
		break;
	case BCM4315_CHIP_ID:
		clock = si_pmu1_cpuclk0(sih, osh, cc);
		break;
	case BCM4322_CHIP_ID:
		
		clock = 96000 * 1000;
		break;

	default:
		PMU_MSG(("No backplane clock specified "
			"for chip %x rev %d pmurev %d, using default %d Hz\n",
			sih->chip, sih->chiprev, sih->pmurev, clock));
		break;
	}

	
	si_setcoreidx(sih, origidx);
	return clock;
}


uint32
BCMINITFN(si_pmu_cpu_clock)(si_t *sih, osl_t *osh)
{
	return si_pmu_si_clock(sih, osh);
}


#define ILP_CALC_DUR	10	
uint32
BCMINITFN(si_pmu_ilp_clock)(si_t *sih, osl_t *osh)
{
	static uint32 ilpcycles_per_sec = 0;

	if (ISSIM_ENAB(sih))
		return ILP_CLOCK;

	if (ilpcycles_per_sec == 0) {
		uint32 start, end, delta;
		uint32 origidx = si_coreidx(sih);
		chipcregs_t *cc = si_setcoreidx(sih, SI_CC_IDX);
		ASSERT(cc != NULL);
		start = R_REG(osh, &cc->pmutimer);
		OSL_DELAY(ILP_CALC_DUR * 1000);
		end = R_REG(osh, &cc->pmutimer);
		delta = end >= start ? end - start : ~0 - start + 1 + end;
		ilpcycles_per_sec = delta * (1000 / ILP_CALC_DUR);
		si_setcoreidx(sih, origidx);
	}

	return ilpcycles_per_sec;
}
#endif 


typedef struct {
	uint8 strength;			
	uint8 sel;			
} sdiod_drive_str_t;


static const sdiod_drive_str_t BCMINITDATA(sdiod_drive_strength_tab1)[] = {
	{4, 0x2},
	{2, 0x3},
	{1, 0x0},
	{0, 0x0} };


static const sdiod_drive_str_t BCMINITDATA(sdiod_drive_strength_tab2)[] = {
	{12, 0x7},
	{10, 0x6},
	{8, 0x5},
	{6, 0x4},
	{4, 0x2},
	{2, 0x1},
	{0, 0x0} };

#define SDIOD_DRVSTR_KEY(chip, pmu)	(((chip) << 16) | (pmu))

void
BCMINITFN(si_sdiod_drive_strength_init)(si_t *sih, osl_t *osh, uint32 drivestrength)
{
	chipcregs_t *cc;
	uint origidx, intr_val;
	sdiod_drive_str_t *str_tab = NULL;
	uint32 str_mask = 0;
	uint32 str_shift = 0;

	if (!(sih->cccaps & CC_CAP_PMU)) {
		return;
	}

	
	cc = (chipcregs_t *) si_switch_core(sih, CC_CORE_ID, &origidx, &intr_val);

	switch (SDIOD_DRVSTR_KEY(sih->chip, sih->pmurev)) {
	case SDIOD_DRVSTR_KEY(BCM4325_CHIP_ID, 1):
		str_tab = (sdiod_drive_str_t *)&sdiod_drive_strength_tab1;
		str_mask = 0x30000000;
		str_shift = 28;
		break;
	case SDIOD_DRVSTR_KEY(BCM4325_CHIP_ID, 2):
	case SDIOD_DRVSTR_KEY(BCM4325_CHIP_ID, 3):
	case SDIOD_DRVSTR_KEY(BCM4315_CHIP_ID, 4):
		str_tab = (sdiod_drive_str_t *)&sdiod_drive_strength_tab2;
		str_mask = 0x00003800;
		str_shift = 11;
		break;

	default:
		PMU_MSG(("No SDIO Drive strength init done for chip %x rev %d pmurev %d\n",
		         sih->chip, sih->chiprev, sih->pmurev));

		break;
	}

	if (str_tab != NULL) {
		uint32 drivestrength_sel = 0;
		uint32 cc_data_temp;
		int i;

		for (i = 0; str_tab[i].strength != 0; i ++) {
			if (drivestrength >= str_tab[i].strength) {
				drivestrength_sel = str_tab[i].sel;
				break;
			}
		}

		W_REG(osh, &cc->chipcontrol_addr, 1);
		cc_data_temp = R_REG(osh, &cc->chipcontrol_data);
		cc_data_temp &= ~str_mask;
		drivestrength_sel <<= str_shift;
		cc_data_temp |= drivestrength_sel;
		W_REG(osh, &cc->chipcontrol_data, cc_data_temp);

		PMU_MSG(("SDIO: %dmA drive strength selected, set to 0x%08x\n",
		         drivestrength, cc_data_temp));
	}

	
	si_restore_core(sih, origidx, intr_val);
}

#if !defined(BCMDONGLEHOST)

void
BCMINITFN(si_pmu_init)(si_t *sih, osl_t *osh)
{
	chipcregs_t *cc;
	uint origidx;

	ASSERT(sih->cccaps & CC_CAP_PMU);

	
	origidx = si_coreidx(sih);
	cc = si_setcoreidx(sih, SI_CC_IDX);
	ASSERT(cc != NULL);

	if (sih->pmurev == 1)
		AND_REG(osh, &cc->pmucontrol, ~PCTL_NOILP_ON_WAIT);
	else if (sih->pmurev >= 2)
		OR_REG(osh, &cc->pmucontrol, PCTL_NOILP_ON_WAIT);

	
	si_setcoreidx(sih, origidx);
}


static uint
BCMINITFN(si_pmu_res_uptime)(si_t *sih, osl_t *osh, chipcregs_t *cc, uint8 rsrc)
{
	uint32 deps;
	uint up, i, dup, dmax;
	uint32 min_mask = 0, max_mask = 0;

	
	W_REG(osh, &cc->res_table_sel, rsrc);
	up = (R_REG(osh, &cc->res_updn_timer) >> 8) & 0xff;

	
	deps = si_pmu_res_deps(sih, osh, cc, PMURES_BIT(rsrc), FALSE);
	for (i = 0; i <= PMURES_MAX_RESNUM; i ++) {
		if (!(deps & PMURES_BIT(i)))
			continue;
		deps &= ~si_pmu_res_deps(sih, osh, cc, PMURES_BIT(i), TRUE);
	}
	si_pmu_res_masks(sih, &min_mask, &max_mask);
	deps &= ~min_mask;

	
	dmax = 0;
	for (i = 0; i <= PMURES_MAX_RESNUM; i ++) {
		if (!(deps & PMURES_BIT(i)))
			continue;
		dup = si_pmu_res_uptime(sih, osh, cc, (uint8)i);
		if (dmax < dup)
			dmax = dup;
	}

	PMU_MSG(("si_pmu_res_uptime: rsrc %u uptime %u(deps 0x%08x uptime %u)\n",
	         rsrc, up, deps, dmax));

	return up + dmax + PMURES_UP_TRANSITION;
}


static uint32
si_pmu_res_deps(si_t *sih, osl_t *osh, chipcregs_t *cc, uint32 rsrcs, bool all)
{
	uint32 deps = 0;
	uint32 i;

	for (i = 0; i <= PMURES_MAX_RESNUM; i ++) {
		if (!(rsrcs & PMURES_BIT(i)))
			continue;
		W_REG(osh, &cc->res_table_sel, i);
		deps |= R_REG(osh, &cc->res_dep_mask);
	}

	return !all ? deps : (deps ? (deps | si_pmu_res_deps(sih, osh, cc, deps, TRUE)) : 0);
}


void
si_pmu_otp_power(si_t *sih, osl_t *osh, bool on)
{
	chipcregs_t *cc;
	uint origidx;
	uint32 rsrcs = 0;	

	ASSERT(sih->cccaps & CC_CAP_PMU);

	
	if (si_is_otp_disabled(sih)) {
		PMU_MSG(("si_pmu_otp_power: OTP is disabled\n"));
		return;
	}

	
	origidx = si_coreidx(sih);
	cc = si_setcoreidx(sih, SI_CC_IDX);
	ASSERT(cc != NULL);

	switch (CHIPID(sih->chip)) {
	case BCM4325_CHIP_ID:
		rsrcs = PMURES_BIT(RES4325_OTP_PU);
		break;
	case BCM4315_CHIP_ID:
		rsrcs = PMURES_BIT(RES4315_OTP_PU);
		break;
	default:
		break;
	}

	if (rsrcs != 0) {
		
		uint32 deps = si_pmu_res_deps(sih, osh, cc, rsrcs, TRUE);
		uint32 min_mask = 0, max_mask = 0;
		si_pmu_res_masks(sih, &min_mask, &max_mask);
		deps &= ~min_mask;
		
		if (on) {
			PMU_MSG(("Adding rsrc 0x%x to min_res_mask\n", rsrcs | deps));
			OR_REG(osh, &cc->min_res_mask, (rsrcs | deps));
			OSL_DELAY(1000);
			SPINWAIT(!(R_REG(osh, &cc->res_state) & rsrcs), PMU_MAX_TRANSITION_DLY);
			ASSERT(R_REG(osh, &cc->res_state) & rsrcs);
		}
		else {
			PMU_MSG(("Removing rsrc 0x%x from min_res_mask\n", rsrcs | deps));
			AND_REG(osh, &cc->min_res_mask, ~(rsrcs | deps));
		}
	}

	
	si_setcoreidx(sih, origidx);
}

void
si_pmu_rcal(si_t *sih, osl_t *osh)
{
	chipcregs_t *cc;
	uint origidx;
	uint rcal_done, BT_out_of_reset;

	ASSERT(sih->cccaps & CC_CAP_PMU);

	
	origidx = si_coreidx(sih);
	cc = si_setcoreidx(sih, SI_CC_IDX);
	ASSERT(cc != NULL);

	switch (CHIPID(sih->chip)) {
	case BCM4325_CHIP_ID: {
		uint8 rcal_code;
		uint32 val;

		
		W_REG(osh, &cc->chipcontrol_addr, 1);

		
		AND_REG(osh, &cc->chipcontrol_data, ~0x04);

		
		rcal_done = ((R_REG(osh, &cc->chipstatus)) & 0x8) >> 3;

		
		if (rcal_done == 1) {
			BT_out_of_reset = 1;
		} else {
			BT_out_of_reset = 0;
		}

		
		OR_REG(osh, &cc->chipcontrol_data, 0x04);

		
		SPINWAIT(0 == (R_REG(osh, &cc->chipstatus) & 0x08), 10 * 1000 * 1000);
		ASSERT(R_REG(osh, &cc->chipstatus) & 0x08);

		if (BT_out_of_reset) {
			rcal_code = 0x6;
		} else {
			
			rcal_code =  (uint8)(R_REG(osh, &cc->chipstatus) >> 5) & 0x0f;
		}

		PMU_MSG(("RCal completed, status 0x%x, code 0x%x\n",
			R_REG(osh, &cc->chipstatus), rcal_code));

		
		W_REG(osh, &cc->regcontrol_addr, 0);
		val = R_REG(osh, &cc->regcontrol_data) & ~((uint32)0x07 << 29);
		val |= (uint32)(rcal_code & 0x07) << 29;
		W_REG(osh, &cc->regcontrol_data, val);
		W_REG(osh, &cc->regcontrol_addr, 1);
		val = R_REG(osh, &cc->regcontrol_data) & ~(uint32)0x01;
		val |= (uint32)((rcal_code >> 3) & 0x01);
		W_REG(osh, &cc->regcontrol_data, val);

		
		W_REG(osh, &cc->chipcontrol_addr, 0);
		val = R_REG(osh, &cc->chipcontrol_data) & ~((uint32)0x03 << 30);
		val |= (uint32)(rcal_code & 0x03) << 30;
		W_REG(osh, &cc->chipcontrol_data, val);
		W_REG(osh, &cc->chipcontrol_addr, 1);
		val = R_REG(osh, &cc->chipcontrol_data) & ~(uint32)0x03;
		val |= (uint32)((rcal_code >> 2) & 0x03);
		W_REG(osh, &cc->chipcontrol_data, val);

		
		W_REG(osh, &cc->chipcontrol_addr, 0);
		OR_REG(osh, &cc->chipcontrol_data, (0x01 << 29));

		
		W_REG(osh, &cc->chipcontrol_addr, 1);
		AND_REG(osh, &cc->chipcontrol_data, ~0x04);

		break;
	}
	case BCM4329_CHIP_ID: {
		uint8 rcal_code;
		uint32 val;

		
		W_REG(osh, &cc->chipcontrol_addr, 1);

		
		AND_REG(osh, &cc->chipcontrol_data, ~0x04);

		
		OR_REG(osh, &cc->chipcontrol_data, 0x04);

		
		SPINWAIT(0 == (R_REG(osh, &cc->chipstatus) & 0x08), 10 * 1000 * 1000);
		ASSERT(R_REG(osh, &cc->chipstatus) & 0x08);

		
		rcal_code =  (uint8)(R_REG(osh, &cc->chipstatus) >> 5) & 0x0f;

		PMU_MSG(("RCal completed, status 0x%x, code 0x%x\n",
			R_REG(osh, &cc->chipstatus), rcal_code));

		
		W_REG(osh, &cc->regcontrol_addr, 0);
		val = R_REG(osh, &cc->regcontrol_data) & ~((uint32)0x07 << 29);
		val |= (uint32)(rcal_code & 0x07) << 29;
		W_REG(osh, &cc->regcontrol_data, val);
		W_REG(osh, &cc->regcontrol_addr, 1);
		val = R_REG(osh, &cc->regcontrol_data) & ~(uint32)0x01;
		val |= (uint32)((rcal_code >> 3) & 0x01);
		W_REG(osh, &cc->regcontrol_data, val);

		
		W_REG(osh, &cc->chipcontrol_addr, 0);
		val = R_REG(osh, &cc->chipcontrol_data) & ~((uint32)0x03 << 30);
		val |= (uint32)(rcal_code & 0x03) << 30;
		W_REG(osh, &cc->chipcontrol_data, val);
		W_REG(osh, &cc->chipcontrol_addr, 1);
		val = R_REG(osh, &cc->chipcontrol_data) & ~(uint32)0x03;
		val |= (uint32)((rcal_code >> 2) & 0x03);
		W_REG(osh, &cc->chipcontrol_data, val);

		
		W_REG(osh, &cc->chipcontrol_addr, 0);
		OR_REG(osh, &cc->chipcontrol_data, (0x01 << 29));

		
		W_REG(osh, &cc->chipcontrol_addr, 1);
		AND_REG(osh, &cc->chipcontrol_data, ~0x04);

		break;
	}
	default:
		break;
	}

	
	si_setcoreidx(sih, origidx);
}

void
si_pmu_spuravoid(si_t *sih, osl_t *osh, bool spuravoid)
{
	chipcregs_t *cc;
	uint origidx;
	uint32 tmp;

	if (sih->pmurev == 5) {
		PMU_ERROR(("Avoiding PLL programming on pmu rev 5\n"));
		return;
	}

	
	origidx = si_coreidx(sih);
	cc = si_setcoreidx(sih, SI_CC_IDX);
	ASSERT(cc != NULL);

	W_REG(osh, &cc->pllcontrol_addr, PMU1_PLL0_PLLCTL0);
	W_REG(osh, &cc->pllcontrol_data, 0x11100070);
	W_REG(osh, &cc->pllcontrol_addr, PMU1_PLL0_PLLCTL1);
	W_REG(osh, &cc->pllcontrol_data, 0x1014140a);
	W_REG(osh, &cc->pllcontrol_addr, PMU1_PLL0_PLLCTL5);
	W_REG(osh, &cc->pllcontrol_data, 0x88888854);

	if (spuravoid) { 
		W_REG(osh, &cc->pllcontrol_addr, PMU1_PLL0_PLLCTL2);
		W_REG(osh, &cc->pllcontrol_data, 0x05201828);
	} else { 
		W_REG(osh, &cc->pllcontrol_addr, PMU1_PLL0_PLLCTL2);
		W_REG(osh, &cc->pllcontrol_data, 0x05001828);
	}

	tmp = R_REG(osh, &cc->pmucontrol);
	W_REG(osh, &cc->pmucontrol, tmp | (1 << 10));

	
	si_setcoreidx(sih, origidx);
}

bool
si_pmu_is_otp_powered(si_t *sih, osl_t *osh)
{
	uint idx;
	chipcregs_t *cc;
	bool st;

	
	idx = si_coreidx(sih);
	cc = si_setcoreidx(sih, SI_CC_IDX);
	ASSERT(cc != NULL);

	switch (CHIPID(sih->chip)) {
	case BCM4325_CHIP_ID:
		st = (R_REG(osh, &cc->res_state) & PMURES_BIT(RES4325_OTP_PU)) != 0;
		break;
	case BCM4329_CHIP_ID:
		st = (R_REG(osh, &cc->res_state) & PMURES_BIT(RES4329_OTP_PU)) != 0;
		break;
	case BCM4315_CHIP_ID:
		st = (R_REG(osh, &cc->res_state) & PMURES_BIT(RES4315_OTP_PU)) != 0;
		break;
	default:
		st = TRUE;
		break;
	}

	
	si_setcoreidx(sih, idx);
	return st;
}

void
#if defined(BCMDBG) || defined(WLTEST)
si_pmu_sprom_enable(si_t *sih, osl_t *osh, bool enable)
#else
BCMATTACHFN(si_pmu_sprom_enable)(si_t *sih, osl_t *osh, bool enable)
#endif
{
	chipcregs_t *cc;
	uint origidx;

	
	origidx = si_coreidx(sih);
	cc = si_setcoreidx(sih, SI_CC_IDX);
	ASSERT(cc != NULL);

	switch (CHIPID(sih->chip)) {
	case BCM4315_CHIP_ID:
		if (sih->chiprev < 1)
			break;
		if (sih->chipst & CST4315_SPROM_SEL) {
			uint32 val;
			W_REG(osh, &cc->chipcontrol_addr, 0);
			val = R_REG(osh, &cc->chipcontrol_data);
			if (enable)
				val &= ~0x80000000;
			else
				val |= 0x80000000;
			W_REG(osh, &cc->chipcontrol_data, val);
		}
		break;
	default:
		break;
	}

	
	si_setcoreidx(sih, origidx);
}

bool
#if defined(BCMDBG) || defined(WLTEST)
si_pmu_is_sprom_enabled(si_t *sih, osl_t *osh)
#else
BCMATTACHFN(si_pmu_is_sprom_enabled)(si_t *sih, osl_t *osh)
#endif
{
	chipcregs_t *cc;
	uint origidx;
	bool enable = TRUE;

	
	origidx = si_coreidx(sih);
	cc = si_setcoreidx(sih, SI_CC_IDX);
	ASSERT(cc != NULL);

	switch (CHIPID(sih->chip)) {
	case BCM4315_CHIP_ID:
		if (sih->chiprev < 1)
			break;
		if (!(sih->chipst & CST4315_SPROM_SEL))
			break;
		W_REG(osh, &cc->chipcontrol_addr, 0);
		if (R_REG(osh, &cc->chipcontrol_data) & 0x80000000)
			enable = FALSE;
		break;
	default:
		break;
	}

	
	si_setcoreidx(sih, origidx);
	return enable;
}


void
BCMINITFN(si_pmu_chip_init)(si_t *sih, osl_t *osh)
{
	uint origidx;

	ASSERT(sih->cccaps & CC_CAP_PMU);

#ifdef CHIPC_UART_ALWAYS_ON
	si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, clk_ctl_st), CCS_FORCEALP, CCS_FORCEALP);
#endif 

#ifndef CONFIG_XIP
	
	si_pmu_sprom_enable(sih, osh, FALSE);
#endif

	
	origidx = si_coreidx(sih);

	
	switch (CHIPID(sih->chip)) {
	case BCM4315_CHIP_ID:
		break;
	default:
		break;
	}

	
	si_setcoreidx(sih, origidx);
}


void
BCMINITFN(si_pmu_swreg_init)(si_t *sih, osl_t *osh)
{
	ASSERT(sih->cccaps & CC_CAP_PMU);

	switch (CHIPID(sih->chip)) {
	case BCM4325_CHIP_ID:
		if (sih->chiprev < 3)
			break;
		if (((sih->chipst & CST4325_PMUTOP_2B_MASK) >> CST4325_PMUTOP_2B_SHIFT) == 1) {
			
			si_pmu_set_ldo_voltage(sih, osh, SET_LDO_VOLTAGE_CLDO_PWM, 0xf);
			
			si_pmu_set_ldo_voltage(sih, osh, SET_LDO_VOLTAGE_CLDO_BURST, 0xf);
		}
		
		si_pmu_set_ldo_voltage(sih, osh, SET_LDO_VOLTAGE_CBUCK_PWM, 0xb);
		
		si_pmu_set_ldo_voltage(sih, osh, SET_LDO_VOLTAGE_CBUCK_BURST, 0xb);
		
		si_pmu_set_ldo_voltage(sih, osh, SET_LDO_VOLTAGE_LNLDO1, 0x1);
		
		if (sih->boardflags & BFL_LNLDO2_2P5)
			si_pmu_set_ldo_voltage(sih, osh, SET_LDO_VOLTAGE_LNLDO2_SEL, 0x1);
		break;
	case BCM4315_CHIP_ID:
		{
			uint32 val;
			chipcregs_t *cc;
			uint origidx;

			if (sih->chiprev != 2)
				break;

			
			origidx = si_coreidx(sih);
			cc = si_setcoreidx(sih, SI_CC_IDX);
			ASSERT(cc != NULL);

			W_REG(osh, &cc->regcontrol_addr, 4);
			val = R_REG(osh, &cc->regcontrol_data);
			val |= (uint32)(1 << 16);
			W_REG(osh, &cc->regcontrol_data, val);

			
			si_setcoreidx(sih, origidx);
		}
		break;
	default:
		break;
	}
}

void
si_pmu_radio_enable(si_t *sih, bool enable)
{
	ASSERT(sih->cccaps & CC_CAP_PMU);

	switch (CHIPID(sih->chip)) {
	case BCM4325_CHIP_ID:
		if (sih->boardflags & BFL_FASTPWR)
			break;

		if ((sih->boardflags & BFL_BUCKBOOST)) {
			si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, min_res_mask),
			           PMURES_BIT(RES4325_BUCK_BOOST_BURST),
			           enable ? PMURES_BIT(RES4325_BUCK_BOOST_BURST) : 0);
		}

		if (enable) {
			OSL_DELAY(100 * 1000);
		}
		break;
	}
}


uint32 si_pmu_waitforclk_on_backplane(si_t *sih, osl_t *osh, uint32 clk, uint32 delay)
{
	chipcregs_t *cc;
	uint origidx;

	ASSERT(sih->cccaps & CC_CAP_PMU);

	
	origidx = si_coreidx(sih);
	cc = si_setcoreidx(sih, SI_CC_IDX);
	ASSERT(cc != NULL);

	if (delay)
		SPINWAIT(((R_REG(osh, &cc->pmustatus) & clk) != clk), delay);

	
	si_setcoreidx(sih, origidx);

	return (R_REG(osh, &cc->pmustatus) & clk);
}
#endif 
