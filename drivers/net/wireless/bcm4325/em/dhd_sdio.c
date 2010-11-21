/*
 * DHD Bus Module for SDIO
 *
 * Copyright (C) 2009, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: dhd_sdio.c,v 1.157.2.27.2.36.4.71 2009/10/15 00:11:28 Exp $
 */

#include <typedefs.h>
#include <osl.h>
#include <bcmsdh.h>

#ifdef BCMEMBEDIMAGE
#include BCMEMBEDIMAGE
#endif 

#include <bcmdefs.h>
#include <bcmutils.h>
#include <bcmendian.h>
#include <bcmdevs.h>

#include <siutils.h>
#include <hndpmu.h>
#include <hndsoc.h>
#include <sbchipc.h>
#include <sbhnddma.h>

#include <sdio.h>
#ifdef BCMSPI
#include <spid.h>
#endif 
#include <sbsdio.h>
#include <sbsdpcmdev.h>
#include <bcmsdpcm.h>

#include <proto/ethernet.h>
#include <proto/802.1d.h>
#include <proto/802.11.h>

#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_bus.h>
#include <dhd_proto.h>
#include <dhd_dbg.h>
#include <dhdioctl.h>
#include <sdiovar.h>

#define QLEN		128	
#define FCHI		((QLEN) - 10)
#define FCLOW		((FCHI)/2)
#define PRIOMASK	7

#define TXRETRIES	2	

#if defined(CONFIG_MACH_SANDGATE2G)
#define DHD_RXBOUND	250	
#else
#define DHD_RXBOUND	50	
#endif 

#define DHD_TXBOUND	20	

#ifdef UNDER_CE
#define DHD_TXMINMAX	20	
#else
#define DHD_TXMINMAX	1	
#endif 

#define MEMBLOCK    2048 
#define MAX_DATA_BUF (32 * 1024)	


#ifndef DHD_SDALIGN
#define DHD_SDALIGN	32
#endif
#if !ISPOWEROF2(DHD_SDALIGN)
#error DHD_SDALIGN is not a power of 2!
#endif

#ifndef DHD_FIRSTREAD
#define DHD_FIRSTREAD	32
#endif
#if !ISPOWEROF2(DHD_FIRSTREAD)
#error DHD_FIRSTREAD is not a power of 2!
#endif


#define SDPCM_HDRLEN	(SDPCM_FRAMETAG_LEN + SDPCM_SWHEADER_LEN)
#ifdef SDTEST
#define SDPCM_RESERVE	(SDPCM_HDRLEN + SDPCM_TEST_HDRLEN + DHD_SDALIGN)
#else
#define SDPCM_RESERVE	(SDPCM_HDRLEN + DHD_SDALIGN)
#endif


#ifndef MAX_HDR_READ
#define MAX_HDR_READ	32
#endif
#if !ISPOWEROF2(MAX_HDR_READ)
#error MAX_HDR_READ is not a power of 2!
#endif

#define MAX_RX_DATASZ	2048


#define DHD_WAIT_F2RDY	4000


#if (PMU_MAX_TRANSITION_DLY <= 500000)
#undef PMU_MAX_TRANSITION_DLY
#define PMU_MAX_TRANSITION_DLY 500000
#endif


#define DHD_INIT_CLKCTL1	(SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_ALP_AVAIL_REQ)
#define DHD_INIT_CLKCTL2	(SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_FORCE_ALP)


#define F2SYNC	(SDIO_REQ_4BYTE | SDIO_REQ_FIXED)


#define PKTFREE2()		if ((bus->bus != SPI_BUS) || bus->usebufpool) \
					PKTFREE(bus->dhd->osh, pkt, FALSE);
DHD_SPINWAIT_SLEEP_INIT(sdioh_spinwait_sleep);

int gDK8 = FALSE;			
					

extern int dhdcdc_set_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf, uint len);

typedef struct dhd_bus {
	dhd_pub_t *dhd;

	bcmsdh_info_t	*sdh;	
	si_t		*sih;	
	char		*vars;	
	uint		varsz;	
	uint32		sbaddr; 

	sdpcmd_regs_t	*regs;    
	uint		sdpcmrev; 
	uint		armrev;	  
	uint		ramrev;	  
	uint32		ramsize;  
	uint32		orig_ramsize;  

	uint32		bus;      
	uint32		hostintmask;  
	uint32		intstatus; 
	bool		dpc_sched; 
	bool		fcstate;   

	uint16		cl_devid; 
	char		*fw_path; 
	char		*nv_path; 
	const char      *nvram_params; 

	uint		blocksize; 
	uint		roundup; 

	struct pktq	txq;	
	uint8		flowcontrol;	
	uint8		tx_seq;	
	uint8		tx_max;	

	uint8		hdrbuf[MAX_HDR_READ + DHD_SDALIGN];
	uint8		*rxhdr; 
	uint16		nextlen; 
	uint8		rx_seq;	
	bool		rxskip;	

	void		*glomd;	
	void		*glom;	
	uint		glomerr; 

	uint8		*rxbuf; 
	uint		rxblen;	
	uint8		*rxctl;	
	uint8		*databuf; 
	uint8		*dataptr; 
	uint		rxlen;	

	uint8		sdpcm_ver; 

	bool		intr;	
	bool		poll;	
	bool		ipend;	
	bool		intdis;	
	uint 		intrcount; 
	uint		lastintrs; 
	uint		spurious; 
	uint		pollrate; 
	uint		polltick; 
	uint		pollcnt; 

	uint		regfails; 

	uint		clkstate; 
	bool		activity; 
	int32		idletime; 
	uint32		idlecount; 
	int32		idleclock; 
	uint32		sd_divisor; 
	uint32		sd_mode; 
	uint32		sd_rxchain; 
	bool		use_rxchain; 
	bool		sleeping; 
	bool		rxflow_mode;	
	bool		rxflow;			
	uint		prev_rxlim_hit;	
	bool		alp_only; 
	
	bool		usebufpool;

#ifdef SDTEST
	
	bool	ext_loop;
	uint8	loopid;

	
	uint	pktgen_freq;	
	uint	pktgen_count;	
	uint	pktgen_print;	
	uint	pktgen_total;	
	uint	pktgen_minlen;	
	uint	pktgen_maxlen;	
	uint	pktgen_mode;	
	uint	pktgen_stop;	

	
	uint	pktgen_tick;	
	uint	pktgen_ptick;	
	uint	pktgen_sent;	
	uint	pktgen_rcvd;	
	uint	pktgen_fail;	
	uint16	pktgen_len;	
#endif 

	
	uint	tx_sderrs;	
	uint	fcqueued;	
	uint	rxrtx;		
	uint	rx_toolong;	
	uint	rxc_errors;	
	uint	rx_hdrfail;	
	uint	rx_badhdr;	
	uint	rx_badseq;	
	uint	fc_rcvd;	
	uint	fc_xoff;	
	uint	fc_xon;		
	uint	rxglomfail;	
	uint	rxglomframes;	
	uint	rxglompkts;	
	uint	f2rxhdrs;	
	uint	f2rxdata;	
	uint	f2txdata;	
	uint	f1regdata;	
#ifdef BCMSPI
	void 	*ackpkt;	
	bool	pr61317_war;
#endif  

} dhd_bus_t;


#define CLK_NONE	0
#define CLK_SDONLY	1
#define CLK_PENDING	2	
#define CLK_AVAIL	3

#define DHD_NOPMU(dhd)	(FALSE)

#ifdef DHD_DEBUG
static int qcount[NUMPRIO];
static int tx_packets[NUMPRIO];
#endif 


const uint dhd_deferred_tx = 1;


uint dhd_txbound;
uint dhd_rxbound;
uint dhd_txminmax = DHD_TXMINMAX;


#define DONGLE_MIN_MEMSIZE (128 *1024)
int dhd_dongle_memsize;

static bool dhd_doflow;
static bool dhd_alignctl;

static bool sd1idle;

static bool retrydata;
#define RETRYCHAN(chan) (((chan) == SDPCM_EVENT_CHANNEL) || retrydata)

#ifdef BCMSPI

static const uint watermark = 32;
#else
static const uint watermark = 8;
#endif 
static const uint firstread = DHD_FIRSTREAD;

#define HDATLEN (firstread - (SDPCM_HDRLEN))


static const uint retry_limit = 2;


static bool forcealign;

#define ALIGNMENT  4

#if defined(OOB_INTR_ONLY) && defined(SDIO_ISR_THREAD)
#error OOB_INTR_ONLY is NOT working with SDIO_ISR_THREAD
#endif 

#define PKTALIGN(osh, p, len, align) \
	do {                                                        \
		uint datalign;                                      \
								    \
		datalign = (uintptr)PKTDATA((osh), (p));            \
		datalign = ROUNDUP(datalign, (align)) - datalign;   \
		ASSERT(datalign < (align));                         \
		ASSERT(PKTLEN((osh), (p)) >= ((len) + datalign));   \
		if (datalign)                                       \
			PKTPULL((osh), (p), datalign);              \
		PKTSETLEN((osh), (p), (len));                       \
	} while (0)


static const uint max_roundup = 512;


static bool dhd_readahead;



#define DATAOK(bus) \
	(((uint8)(bus->tx_max - bus->tx_seq) != 0) && \
	(((uint8)(bus->tx_max - bus->tx_seq) & 0x80) == 0))



#define R_SDREG(regvar, regaddr, retryvar) \
do { \
	retryvar = 0; \
	do { \
		regvar = R_REG(bus->dhd->osh, regaddr); \
	} while (bcmsdh_regfail(bus->sdh) && (++retryvar <= retry_limit)); \
	if (retryvar) { \
		bus->regfails += (retryvar-1); \
		if (retryvar > retry_limit) { \
			DHD_ERROR(("%s: FAILED" #regvar "READ, LINE %d\n", \
			           __FUNCTION__, __LINE__)); \
			regvar = 0; \
		} \
	} \
} while (0)

#define W_SDREG(regval, regaddr, retryvar) \
do { \
	retryvar = 0; \
	do { \
		W_REG(bus->dhd->osh, regaddr, regval); \
	} while (bcmsdh_regfail(bus->sdh) && (++retryvar <= retry_limit)); \
	if (retryvar) { \
		bus->regfails += (retryvar-1); \
		if (retryvar > retry_limit) \
			DHD_ERROR(("%s: FAILED REGISTER WRITE, LINE %d\n", \
			           __FUNCTION__, __LINE__)); \
	} \
} while (0)

#ifdef BCMSPI

#define DHD_BUS			SPI_BUS


#define PKT_AVAILABLE()			(bcmsdh_get_dstatus(bus->sdh) & STATUS_F2_PKT_AVAILABLE)

#define HOSTINTMASK		(I_HMB_FC_CHANGE | I_HMB_HOST_INT)

#define GSPI_PR55150_BAILOUT									\
do {												\
	uint32 dstatussw = bcmsdh_get_dstatus((void *)bus->sdh);				\
	uint32 dstatushw = bcmsdh_cfg_read_word(bus->sdh, SDIO_FUNC_0, SPID_STATUS_REG, NULL);	\
	uint32 intstatuserr = 0;								\
	uint retries = 0;									\
												\
	R_SDREG(intstatuserr, &bus->regs->intstatus, retries);					\
	printf("dstatussw = 0x%x, dstatushw = 0x%x, intstatus = 0x%x\n",			\
	        dstatussw, dstatushw, intstatuserr); 						\
												\
	bus->nextlen = 0;									\
	*finished = TRUE;									\
} while (0)

#else 

#define DHD_BUS			SDIO_BUS

#define PKT_AVAILABLE()		(intstatus & I_HMB_FRAME_IND)

#define HOSTINTMASK		(I_TOHOSTMAIL | I_CHIPACTIVE)

#define GSPI_PR55150_BAILOUT

#endif 

#ifdef SDTEST
static void dhdsdio_testrcv(dhd_bus_t *bus, void *pkt, uint seq);
static void dhdsdio_sdtest_set(dhd_bus_t *bus, bool start);
#endif

static int dhdsdio_download_state(dhd_bus_t *bus, bool enter);

static void dhdsdio_release(dhd_bus_t *bus, osl_t *osh);
static void dhdsdio_release_malloc(dhd_bus_t *bus, osl_t *osh);
static void dhdsdio_disconnect(void *ptr);
static bool dhdsdio_chipmatch(uint16 chipid);
static bool dhdsdio_probe_attach(dhd_bus_t *bus, osl_t *osh, void *sdh,
                                 void * regsva, uint16  devid);
static bool dhdsdio_probe_malloc(dhd_bus_t *bus, osl_t *osh, void *sdh);
static bool dhdsdio_probe_init(dhd_bus_t *bus, osl_t *osh, void *sdh);
static void dhdsdio_release_dongle(dhd_bus_t *bus, osl_t *osh);

static uint process_nvram_vars(char *varbuf, uint len);

static void dhd_dongle_setmemsize(struct dhd_bus *bus, int mem_size);
static int dhd_bcmsdh_recv_buf(dhd_bus_t *bus, uint32 addr, uint fn, uint flags,
	uint8 *buf, uint nbytes,
	void *pkt, bcmsdh_cmplt_fn_t complete, void *handle);
static int dhd_bcmsdh_send_buf(dhd_bus_t *bus, uint32 addr, uint fn, uint flags,
	uint8 *buf, uint nbytes,
	void *pkt, bcmsdh_cmplt_fn_t complete, void *handle);

static bool dhdsdio_download_firmware(struct dhd_bus *bus, osl_t *osh, void *sdh);
static int _dhdsdio_download_firmware(struct dhd_bus *bus);

static int dhdsdio_download_code_file(struct dhd_bus *bus, char *image_path);
static int dhdsdio_download_nvram(struct dhd_bus *bus);
#ifdef BCMEMBEDIMAGE
static int dhdsdio_download_code_array(struct dhd_bus *bus);
#endif

static void
dhd_dongle_setmemsize(struct dhd_bus *bus, int mem_size)
{
	int32 min_size =  DONGLE_MIN_MEMSIZE;
	
	DHD_ERROR(("user: Restrict the dongle ram size to %d, min accepted %d\n",
		dhd_dongle_memsize, min_size));
	if ((dhd_dongle_memsize > min_size) &&
		(dhd_dongle_memsize < (int32)bus->orig_ramsize))
		bus->ramsize = dhd_dongle_memsize;
}

static int
dhdsdio_set_siaddr_window(dhd_bus_t *bus, uint32 address)
{
	int err = 0;
	bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRLOW,
	                 (address >> 8) & SBSDIO_SBADDRLOW_MASK, &err);
	if (!err)
		bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRMID,
		                 (address >> 16) & SBSDIO_SBADDRMID_MASK, &err);
	if (!err)
		bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRHIGH,
		                 (address >> 24) & SBSDIO_SBADDRHIGH_MASK, &err);
	return err;
}

#ifdef BCMSPI
static void
dhdsdio_wkwlan(dhd_bus_t *bus, bool on)
{
	int err;
	uint32 regdata;
	bcmsdh_info_t *sdh = bus->sdh;

	if ((bus->sih->chip == BCM4329_CHIP_ID) && (bus->sih->chiprev == 0)) {
		
		regdata = bcmsdh_cfg_read_word(sdh, SDIO_FUNC_0, SPID_CONFIG, NULL);
		DHD_INFO(("F0 REG0 rd = 0x%x\n", regdata));

		if (on == TRUE)
			regdata |= WAKE_UP;
		else
			regdata &= ~WAKE_UP;

		bcmsdh_cfg_write_word(sdh, SDIO_FUNC_0, SPID_CONFIG, regdata, &err);
	}
}
#endif 


static int
dhdsdio_htclk(dhd_bus_t *bus, bool on, bool pendok)
{
	int err;
	uint8 clkctl, clkreq, devctl;
	bcmsdh_info_t *sdh;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

#if defined(OOB_INTR_ONLY)
	pendok = FALSE;
#endif
	clkctl = 0;
	sdh = bus->sdh;


	if (on) {
		
		clkreq = bus->alp_only ? SBSDIO_ALP_AVAIL_REQ : SBSDIO_HT_AVAIL_REQ;

		if ((bus->sih->chip == BCM4329_CHIP_ID) && (bus->sih->chiprev == 0))
			clkreq |= SBSDIO_FORCE_ALP;


#ifdef BCMSPI
			dhdsdio_wkwlan(bus, TRUE);
#endif 


		bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, clkreq, &err);
		if (err) {
			DHD_ERROR(("%s: HT Avail request error: %d\n", __FUNCTION__, err));
			return BCME_ERROR;
		}

		if (pendok &&
		    ((bus->sih->buscoretype == PCMCIA_CORE_ID) && (bus->sih->buscorerev == 9))) {
			uint32 dummy, retries;
			R_SDREG(dummy, &bus->regs->clockctlstatus, retries);
		}

		
		clkctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);
		if (err) {
			DHD_ERROR(("%s: HT Avail read error: %d\n", __FUNCTION__, err));
			return BCME_ERROR;
		}

		
		if (!SBSDIO_CLKAV(clkctl, bus->alp_only) && pendok) {
			DHD_INFO(("CLKCTL: set PENDING\n"));
			bus->clkstate = CLK_PENDING;

			
			devctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, &err);
			if (err) {
				DHD_ERROR(("%s: Devctl access error setting CA: %d\n",
				           __FUNCTION__, err));
				return BCME_ERROR;
			}

			devctl |= SBSDIO_DEVCTL_CA_INT_ONLY;
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, devctl, &err);
			return BCME_OK;
		} else if (bus->clkstate == CLK_PENDING) {
			
			devctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, &err);
			devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, devctl, &err);
		}

		
		if (!SBSDIO_CLKAV(clkctl, bus->alp_only)) {
			SPINWAIT_SLEEP(sdioh_spinwait_sleep,
				((clkctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1,
			                                    SBSDIO_FUNC1_CHIPCLKCSR, &err)),
			          !SBSDIO_CLKAV(clkctl, bus->alp_only)), PMU_MAX_TRANSITION_DLY);
		}
		if (err) {
			DHD_ERROR(("%s: HT Avail request error: %d\n", __FUNCTION__, err));
			return BCME_ERROR;
		}
		if (!SBSDIO_CLKAV(clkctl, bus->alp_only)) {
			DHD_ERROR(("%s: HT Avail timeout (%d): clkctl 0x%02x\n",
			           __FUNCTION__, PMU_MAX_TRANSITION_DLY, clkctl));
			return BCME_ERROR;
		}


		
		bus->clkstate = CLK_AVAIL;
		DHD_INFO(("CLKCTL: turned ON\n"));

#if defined(DHD_DEBUG)
		if (bus->alp_only == TRUE) {
#if !defined(BCMLXSDMMC)
			if (!SBSDIO_ALPONLY(clkctl)) {
				DHD_ERROR(("%s: HT Clock, when ALP Only\n", __FUNCTION__));
			}
#endif 
		} else {
			if (SBSDIO_ALPONLY(clkctl)) {
				DHD_ERROR(("%s: HT Clock should be on.\n", __FUNCTION__));
			}
		}
#endif 

		bus->activity = TRUE;
	} else {
		clkreq = 0;

		if (bus->clkstate == CLK_PENDING) {
			
			devctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, &err);
			devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, devctl, &err);
		}

		bus->clkstate = CLK_SDONLY;
		bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, clkreq, &err);
		DHD_INFO(("CLKCTL: turned OFF\n"));
		if (err) {
			DHD_ERROR(("%s: Failed access turning clock off: %d\n",
			           __FUNCTION__, err));
			return BCME_ERROR;
		}
#ifdef BCMSPI
			dhdsdio_wkwlan(bus, FALSE);
#endif 
	}
	return BCME_OK;
}


static int
dhdsdio_sdclk(dhd_bus_t *bus, bool on)
{
#ifndef BCMSPI
	int err;
	int32 iovalue;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (on) {
		if (bus->idleclock == DHD_IDLE_STOP) {
			
			iovalue = 1;
			err = bcmsdh_iovar_op(bus->sdh, "sd_clock", NULL, 0,
			                      &iovalue, sizeof(iovalue), TRUE);
			if (err) {
				DHD_ERROR(("%s: error enabling sd_clock: %d\n",
				           __FUNCTION__, err));
				return BCME_ERROR;
			}

			iovalue = bus->sd_mode;
			err = bcmsdh_iovar_op(bus->sdh, "sd_mode", NULL, 0,
			                      &iovalue, sizeof(iovalue), TRUE);
			if (err) {
				DHD_ERROR(("%s: error changing sd_mode: %d\n",
				           __FUNCTION__, err));
				return BCME_ERROR;
			}
		} else if (bus->idleclock != DHD_IDLE_ACTIVE) {
			
			iovalue = bus->sd_divisor;
			err = bcmsdh_iovar_op(bus->sdh, "sd_divisor", NULL, 0,
			                      &iovalue, sizeof(iovalue), TRUE);
			if (err) {
				DHD_ERROR(("%s: error restoring sd_divisor: %d\n",
				           __FUNCTION__, err));
				return BCME_ERROR;
			}
		}
		bus->clkstate = CLK_SDONLY;
	} else {
		
		if ((bus->sd_divisor == -1) || (bus->sd_mode == -1)) {
			DHD_TRACE(("%s: can't idle clock, divisor %d mode %d\n",
			           __FUNCTION__, bus->sd_divisor, bus->sd_mode));
			return BCME_ERROR;
		}
		if (bus->idleclock == DHD_IDLE_STOP) {
			if (sd1idle) {
				
				iovalue = 1;
				err = bcmsdh_iovar_op(bus->sdh, "sd_mode", NULL, 0,
				                      &iovalue, sizeof(iovalue), TRUE);
				if (err) {
					DHD_ERROR(("%s: error changing sd_clock: %d\n",
					           __FUNCTION__, err));
					return BCME_ERROR;
				}
			}

			iovalue = 0;
			err = bcmsdh_iovar_op(bus->sdh, "sd_clock", NULL, 0,
			                      &iovalue, sizeof(iovalue), TRUE);
			if (err) {
				DHD_ERROR(("%s: error disabling sd_clock: %d\n",
				           __FUNCTION__, err));
				return BCME_ERROR;
			}
		} else if (bus->idleclock != DHD_IDLE_ACTIVE) {
			
			iovalue = bus->idleclock;
			err = bcmsdh_iovar_op(bus->sdh, "sd_divisor", NULL, 0,
			                      &iovalue, sizeof(iovalue), TRUE);
			if (err) {
				DHD_ERROR(("%s: error changing sd_divisor: %d\n",
				           __FUNCTION__, err));
				return BCME_ERROR;
			}
		}
		if (!gDK8)
			bus->clkstate = CLK_NONE;
	}
#endif 

	return BCME_OK;
}


static int
dhdsdio_clkctl(dhd_bus_t *bus, uint target, bool pendok)
{
#ifdef DHD_DEBUG
	uint oldstate = bus->clkstate;
#endif 

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	
	if (bus->clkstate == target) {
		if (target == CLK_AVAIL)
			bus->activity = TRUE;
		return BCME_OK;
	}

	switch (target) {
	case CLK_AVAIL:
		
		if (bus->clkstate == CLK_NONE)
			dhdsdio_sdclk(bus, TRUE);
		
		dhdsdio_htclk(bus, TRUE, pendok);
		bus->activity = TRUE;
		break;

	case CLK_SDONLY:
		
		if (bus->clkstate == CLK_NONE)
			dhdsdio_sdclk(bus, TRUE);
		else if (bus->clkstate == CLK_AVAIL)
			dhdsdio_htclk(bus, FALSE, FALSE);
		else
			DHD_ERROR(("dhdsdio_clkctl: request for %d -> %d\n",
			           bus->clkstate, target));
		break;

	case CLK_NONE:
		
		if (!gDK8) {
			if (bus->clkstate == CLK_AVAIL)
				dhdsdio_htclk(bus, FALSE, FALSE);
			
			dhdsdio_sdclk(bus, FALSE);
		}
		break;
	}
#ifdef DHD_DEBUG
	DHD_INFO(("dhdsdio_clkctl: %d -> %d\n", oldstate, bus->clkstate));
#endif 

	return BCME_OK;
}

int
dhdsdio_bussleep(dhd_bus_t *bus, bool sleep)
{
	bcmsdh_info_t *sdh = bus->sdh;
	sdpcmd_regs_t *regs = bus->regs;
	uint retries = 0;

	DHD_INFO(("dhdsdio_bussleep: request %s (currently %s)\n",
	          (sleep ? "SLEEP" : "WAKE"),
	          (bus->sleeping ? "SLEEP" : "WAKE")));

	
	if (sleep == bus->sleeping)
		return BCME_OK;

	
	if (sleep) {
		
		if (bus->dpc_sched || bus->rxskip || pktq_len(&bus->txq))
			return BCME_BUSY;


		
		bcmsdh_intr_disable(bus->sdh);

		
		dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);

		
		W_SDREG(SMB_USE_OOB, &regs->tosbmailbox, retries);
		if (retries > retry_limit)
			DHD_ERROR(("CANNOT SIGNAL CHIP, WILL NOT WAKE UP!!\n"));

		
		dhdsdio_clkctl(bus, CLK_SDONLY, FALSE);

		bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
		                 SBSDIO_FORCE_HW_CLKREQ_OFF, NULL);

		
		if (bus->sih->chip != BCM4329_CHIP_ID && bus->sih->chip != BCM4319_CHIP_ID) {
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL,
				SBSDIO_DEVCTL_PADS_ISO, NULL);
		}

		
		bus->sleeping = TRUE;

	} else {
		

		bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
		                 0, NULL);

		
		if ((bus->sih->buscoretype == PCMCIA_CORE_ID) && (bus->sih->buscorerev >= 10))
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, 0, NULL);


		
		dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);

		
		W_SDREG(0, &regs->tosbmailboxdata, retries);
		if (retries <= retry_limit)
			W_SDREG(SMB_DEV_INT, &regs->tosbmailbox, retries);
		if (retries > retry_limit)
			DHD_ERROR(("CANNOT SIGNAL CHIP TO CLEAR OOB!!\n"));

		
		dhdsdio_clkctl(bus, CLK_SDONLY, FALSE);

		
		bus->sleeping = FALSE;

		
		if (bus->intr && (bus->dhd->busstate == DHD_BUS_DATA)) {
			bus->intdis = FALSE;
			bcmsdh_intr_enable(bus->sdh);
		}
	}

	return BCME_OK;
}

#if defined(OOB_INTR_ONLY)
void
dhd_enable_oob_intr(struct dhd_bus *bus, bool enable)
{
	sdpcmd_regs_t *regs = bus->regs;
	uint retries = 0;

	dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);
	if (enable == TRUE) {

		
		W_SDREG(SMB_USE_OOB, &regs->tosbmailbox, retries);
		if (retries > retry_limit)
			DHD_ERROR(("CANNOT SIGNAL CHIP, WILL NOT WAKE UP!!\n"));

	} else {
		
		W_SDREG(0, &regs->tosbmailboxdata, retries);
		if (retries <= retry_limit)
			W_SDREG(SMB_DEV_INT, &regs->tosbmailbox, retries);
	}

	
	dhdsdio_clkctl(bus, CLK_SDONLY, FALSE);
}
#endif 

#define BUS_WAKE(bus) \
	do { \
		if ((bus)->sleeping) \
			dhdsdio_bussleep((bus), FALSE); \
	} while (0);




static int
dhdsdio_txpkt(dhd_bus_t *bus, void *pkt, uint chan, bool free_pkt)
{
	int ret;
	osl_t *osh;
	uint8 *frame;
	uint16 len, pad = 0;
	uint32 swheader;
	uint retries = 0;
	bcmsdh_info_t *sdh;
	void *new;
	int i;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	sdh = bus->sdh;
	osh = bus->dhd->osh;

	if (bus->dhd->dongle_reset) {
		ret = BCME_NOTREADY;
		goto done;
	}

	frame = (uint8*)PKTDATA(osh, pkt);

	
	if ((pad = ((uintptr)frame % DHD_SDALIGN))) {
		if (PKTHEADROOM(osh, pkt) < pad) {
			DHD_INFO(("%s: insufficient headroom %d for %d pad\n",
			          __FUNCTION__, (int)PKTHEADROOM(osh, pkt), pad));
			bus->dhd->tx_realloc++;
			new = PKTGET(osh, (PKTLEN(osh, pkt) + DHD_SDALIGN), TRUE);
			if (!new) {
				DHD_ERROR(("%s: couldn't allocate new %d-byte packet\n",
				           __FUNCTION__, PKTLEN(osh, pkt) + DHD_SDALIGN));
				ret = BCME_NOMEM;
				goto done;
			}

			PKTALIGN(osh, new, PKTLEN(osh, pkt), DHD_SDALIGN);
			bcopy(PKTDATA(osh, pkt), PKTDATA(osh, new), PKTLEN(osh, pkt));
			if (free_pkt)
				PKTFREE(osh, pkt, TRUE);
			
			free_pkt = TRUE;
			pkt = new;
			frame = (uint8*)PKTDATA(osh, pkt);
			ASSERT(((uintptr)frame % DHD_SDALIGN) == 0);
			pad = 0;
		} else {
			PKTPUSH(osh, pkt, pad);
			frame = (uint8*)PKTDATA(osh, pkt);

			ASSERT((pad + SDPCM_HDRLEN) <= (int) PKTLEN(osh, pkt));
			bzero(frame, pad + SDPCM_HDRLEN);
		}
	}
	ASSERT(pad < DHD_SDALIGN);

	
	len = (uint16)PKTLEN(osh, pkt);
	*(uint16*)frame = htol16(len);
	*(((uint16*)frame) + 1) = htol16(~len);

	
	swheader = ((chan << SDPCM_CHANNEL_SHIFT) & SDPCM_CHANNEL_MASK) | bus->tx_seq |
	        (((pad + SDPCM_HDRLEN) << SDPCM_DOFFSET_SHIFT) & SDPCM_DOFFSET_MASK);
	htol32_ua_store(swheader, frame + SDPCM_FRAMETAG_LEN);
	htol32_ua_store(0, frame + SDPCM_FRAMETAG_LEN + sizeof(swheader));
	bus->tx_seq = (bus->tx_seq + 1) % SDPCM_SEQUENCE_WRAP;

#ifdef DHD_DEBUG
	tx_packets[PKTPRIO(pkt)]++;
	if (DHD_BYTES_ON() &&
	    (((DHD_CTL_ON() && (chan == SDPCM_CONTROL_CHANNEL)) ||
	      (DHD_DATA_ON() && (chan != SDPCM_CONTROL_CHANNEL))))) {
		prhex("Tx Frame", frame, len);
	} else if (DHD_HDRS_ON()) {
		prhex("TxHdr", frame, MIN(len, 16));
	}
#endif

#ifndef BCMSPI
	
	if (bus->roundup && bus->blocksize && (len > bus->blocksize)) {
		uint16 pad = bus->blocksize - (len % bus->blocksize);
		if ((pad <= bus->roundup) && (pad < bus->blocksize))
#ifdef NOTUSED
			if (pad <= PKTTAILROOM(osh, pkt))
#endif 
				len += pad;
	} else if (len % DHD_SDALIGN) {
		len += DHD_SDALIGN - (len % DHD_SDALIGN);
	}
#endif  

	
	if (forcealign && (len & (ALIGNMENT - 1))) {
#ifdef NOTUSED
		if (PKTTAILROOM(osh, pkt))
#endif
			len = ROUNDUP(len, ALIGNMENT);
#ifdef NOTUSED
		else
			DHD_ERROR(("%s: sending unrounded %d-byte packet\n", __FUNCTION__, len));
#endif
	}

	do {
		ret = dhd_bcmsdh_send_buf(bus, bcmsdh_cur_sbwad(sdh), SDIO_FUNC_2, F2SYNC,
		                      frame, len, pkt, NULL, NULL);
		bus->f2txdata++;
		ASSERT(ret != BCME_PENDING);

		if (ret < 0) {
			
			DHD_INFO(("%s: sdio error %d, abort command and terminate frame.\n",
			          __FUNCTION__, ret));
			bus->tx_sderrs++;

			bcmsdh_abort(sdh, SDIO_FUNC_2);
#ifdef BCMSPI
			DHD_ERROR(("%s: gSPI transmit error.  Check Overflow or F2-fifo-not-ready"
			           " counters.\n", __FUNCTION__));
#endif 
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_FRAMECTRL,
			                 SFC_WF_TERM, NULL);
			bus->f1regdata++;

			for (i = 0; i < 3; i++) {
				uint8 hi, lo;
				hi = bcmsdh_cfg_read(sdh, SDIO_FUNC_1,
				                     SBSDIO_FUNC1_WFRAMEBCHI, NULL);
				lo = bcmsdh_cfg_read(sdh, SDIO_FUNC_1,
				                     SBSDIO_FUNC1_WFRAMEBCLO, NULL);
				bus->f1regdata += 2;
				if ((hi == 0) && (lo == 0))
					break;
			}

		}
	} while ((ret < 0) && retrydata && retries++ < TXRETRIES);

done:
	
	PKTPULL(osh, pkt, SDPCM_HDRLEN + pad);
#ifndef UNDER_CE
	dhd_os_sdunlock(bus->dhd);
#endif
	dhd_txcomplete(bus->dhd, pkt, ret != 0);
#ifndef UNDER_CE
	dhd_os_sdlock(bus->dhd);
#endif

	if (free_pkt)
		PKTFREE(osh, pkt, TRUE);

	return ret;
}

static bool
dhd_prec_enq(struct dhd_bus *bus, struct pktq *q, void *pkt, int prec)
{
	void *p;
	int eprec = -1;		
	bool discard_oldest;


	
	if (!pktq_pfull(q, prec) && !pktq_full(q)) {
		pktq_penq(q, prec, pkt);
		return TRUE;
	}

	
	if (pktq_pfull(q, prec))
		eprec = prec;
	else if (pktq_full(q)) {
		p = pktq_peek_tail(q, &eprec);
		ASSERT(p);
		if (eprec > prec)
			return FALSE;
	}

	
	if (eprec >= 0) {
		
		ASSERT(!pktq_pempty(q, eprec));
		discard_oldest = AC_BITMAP_TST(bus->dhd->wme_dp, eprec);
		if (eprec == prec && !discard_oldest)
			return FALSE;		
		
		p = discard_oldest ? pktq_pdeq(q, eprec) : pktq_pdeq_tail(q, eprec);
		if (p == NULL) {
			DHD_ERROR(("%s: pktq_penq() failed, oldest %d.",
				__FUNCTION__, discard_oldest));
			ASSERT(p);
		}

		PKTFREE(bus->dhd->osh, p, TRUE);
	}

	
	p = pktq_penq(q, prec, pkt);
	if (p == NULL) {
		DHD_ERROR(("%s: pktq_penq() failed.", __FUNCTION__));
		ASSERT(p);
	}

	return TRUE;
}

int
dhd_bus_txdata(struct dhd_bus *bus, void *pkt)
{
	int ret = BCME_ERROR;
	osl_t *osh;
	uint datalen, prec;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	osh = bus->dhd->osh;
	datalen = PKTLEN(osh, pkt);

#ifdef SDTEST
	
	if (bus->ext_loop) {
		uint8* data;
		PKTPUSH(osh, pkt, SDPCM_TEST_HDRLEN);
		data = PKTDATA(osh, pkt);
		*data++ = SDPCM_TEST_ECHOREQ;
		*data++ = (uint8)bus->loopid++;
		*data++ = (datalen >> 0);
		*data++ = (datalen >> 8);
		datalen += SDPCM_TEST_HDRLEN;
	}
#endif 

	
	PKTPUSH(osh, pkt, SDPCM_HDRLEN);
	ASSERT(ISALIGNED((uintptr)PKTDATA(osh, pkt), 2));

	prec = PRIO2PREC((PKTPRIO(pkt) & PRIOMASK));


	
	if (dhd_deferred_tx || bus->fcstate || pktq_len(&bus->txq) || bus->dpc_sched ||
	    (!DATAOK(bus)) || (bus->flowcontrol & NBITVAL(prec)) ||
	    (bus->clkstate == CLK_PENDING)) {
		bus->fcqueued++;

		
		dhd_os_sdlock_txq(bus->dhd);
		if (dhd_prec_enq(bus, &bus->txq, pkt, prec) == FALSE) {
			PKTPULL(osh, pkt, SDPCM_HDRLEN);
			dhd_txcomplete(bus->dhd, pkt, FALSE);
			PKTFREE(osh, pkt, TRUE);
			ret = BCME_NORESOURCE;
		}
		else
			ret = BCME_OK;
		dhd_os_sdunlock_txq(bus->dhd);

		if ((pktq_len(&bus->txq) >= FCHI) && dhd_doflow)
			dhd_txflowcontrol(bus->dhd, 0, ON);

#ifdef DHD_DEBUG
		if (pktq_plen(&bus->txq, prec) > qcount[prec])
			qcount[prec] = pktq_plen(&bus->txq, prec);
#endif
		
		if (dhd_deferred_tx && !bus->dpc_sched) {
			bus->dpc_sched = TRUE;
			dhd_sched_dpc(bus->dhd);
		}
	} else {
		
		dhd_os_sdlock(bus->dhd);

		
		BUS_WAKE(bus);
		dhdsdio_clkctl(bus, CLK_AVAIL, TRUE);

#ifndef SDTEST
		ret = dhdsdio_txpkt(bus, pkt, SDPCM_DATA_CHANNEL, TRUE);
#else
		ret = dhdsdio_txpkt(bus, pkt,
		        (bus->ext_loop ? SDPCM_TEST_CHANNEL : SDPCM_DATA_CHANNEL), TRUE);
#endif
		if (ret)
			bus->dhd->tx_errors++;
		else
			bus->dhd->dstats.tx_bytes += datalen;

		if ((bus->idletime == DHD_IDLE_IMMEDIATE) && !bus->dpc_sched) {
			bus->activity = FALSE;
			dhdsdio_clkctl(bus, CLK_NONE, TRUE);
		}

		dhd_os_sdunlock(bus->dhd);
	}


	return ret;
}

static uint
dhdsdio_sendfromq(dhd_bus_t *bus, uint maxframes)
{
	void *pkt;
	uint32 intstatus = 0;
	uint retries = 0;
	int ret = 0, prec_out;
	uint cnt = 0;
	uint datalen;
	uint8 tx_prec_map;

	dhd_pub_t *dhd = bus->dhd;
	sdpcmd_regs_t *regs = bus->regs;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	tx_prec_map = ~bus->flowcontrol;

	
	for (cnt = 0; (cnt < maxframes) && DATAOK(bus); cnt++) {
		dhd_os_sdlock_txq(bus->dhd);
		if ((pkt = pktq_mdeq(&bus->txq, tx_prec_map, &prec_out)) == NULL) {
			dhd_os_sdunlock_txq(bus->dhd);
			break;
		}
		dhd_os_sdunlock_txq(bus->dhd);
		datalen = PKTLEN(bus->dhd->osh, pkt) - SDPCM_HDRLEN;

#ifndef SDTEST
		ret = dhdsdio_txpkt(bus, pkt, SDPCM_DATA_CHANNEL, TRUE);
#else
		ret = dhdsdio_txpkt(bus, pkt,
		        (bus->ext_loop ? SDPCM_TEST_CHANNEL : SDPCM_DATA_CHANNEL), TRUE);
#endif
		if (ret)
			bus->dhd->tx_errors++;
		else
			bus->dhd->dstats.tx_bytes += datalen;

		
		if (!bus->intr && cnt)
		{
			
			R_SDREG(intstatus, &regs->intstatus, retries);
			bus->f2txdata++;
			if (bcmsdh_regfail(bus->sdh))
				break;
			if (intstatus & bus->hostintmask)
				bus->ipend = TRUE;
		}
	}

	
	if (dhd_doflow && dhd->up && (dhd->busstate == DHD_BUS_DATA) &&
	    dhd->txoff && (pktq_len(&bus->txq) < FCLOW))
		dhd_txflowcontrol(dhd, 0, OFF);

	return cnt;
}

int
dhd_bus_txctl(struct dhd_bus *bus, uchar *msg, uint msglen)
{
	uint8 *frame;
	uint16 len;
	uint32 swheader;
	uint retries = 0;
	bcmsdh_info_t *sdh = bus->sdh;
	uint8 doff = 0;
	int ret = -1;
	int i;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (bus->dhd->dongle_reset)
		return -EIO;

	
	frame = msg - SDPCM_HDRLEN;
	len = (msglen += SDPCM_HDRLEN);

	
	if (dhd_alignctl) {
		if ((doff = ((uintptr)frame % DHD_SDALIGN))) {
			frame -= doff;
			len += doff;
			msglen += doff;
			bzero(frame, doff + SDPCM_HDRLEN);
		}
		ASSERT(doff < DHD_SDALIGN);
	}
	doff += SDPCM_HDRLEN;

#ifndef BCMSPI
	
	if (bus->roundup && bus->blocksize && (len > bus->blocksize)) {
		uint16 pad = bus->blocksize - (len % bus->blocksize);
		if ((pad <= bus->roundup) && (pad < bus->blocksize))
			len += pad;
	} else if (len % DHD_SDALIGN) {
		len += DHD_SDALIGN - (len % DHD_SDALIGN);
	}
#endif 

	
	if (forcealign && (len & (ALIGNMENT - 1)))
		len = ROUNDUP(len, ALIGNMENT);

	ASSERT(ISALIGNED((uintptr)frame, 2));


	
	dhd_os_sdlock(bus->dhd);

	BUS_WAKE(bus);

	
	dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);

	
	*(uint16*)frame = htol16((uint16)msglen);
	*(((uint16*)frame) + 1) = htol16(~msglen);

	
	swheader = ((SDPCM_CONTROL_CHANNEL << SDPCM_CHANNEL_SHIFT) & SDPCM_CHANNEL_MASK)
	        | bus->tx_seq | ((doff << SDPCM_DOFFSET_SHIFT) & SDPCM_DOFFSET_MASK);
	htol32_ua_store(swheader, frame + SDPCM_FRAMETAG_LEN);
	htol32_ua_store(0, frame + SDPCM_FRAMETAG_LEN + sizeof(swheader));
	bus->tx_seq = (bus->tx_seq + 1) % SDPCM_SEQUENCE_WRAP;

#ifdef DHD_DEBUG
	if (DHD_BYTES_ON() && DHD_CTL_ON()) {
		prhex("Tx Frame", frame, len);
	} else if (DHD_HDRS_ON()) {
		prhex("TxHdr", frame, MIN(len, 16));
	}
#endif

	do {
		ret = dhd_bcmsdh_send_buf(bus, bcmsdh_cur_sbwad(sdh), SDIO_FUNC_2, F2SYNC,
		                      frame, len, NULL, NULL, NULL);
		ASSERT(ret != BCME_PENDING);

		if (ret < 0) {
			
			DHD_INFO(("%s: sdio error %d, abort command and terminate frame.\n",
			          __FUNCTION__, ret));
			bus->tx_sderrs++;

			bcmsdh_abort(sdh, SDIO_FUNC_2);

#ifdef BCMSPI
			DHD_ERROR(("%s: Check Overflow or F2-fifo-not-ready counters."
			           " gSPI transmit error on control channel.\n", __FUNCTION__));
#endif 
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_FRAMECTRL,
			                 SFC_WF_TERM, NULL);
			bus->f1regdata++;

			for (i = 0; i < 3; i++) {
				uint8 hi, lo;
				hi = bcmsdh_cfg_read(sdh, SDIO_FUNC_1,
				                     SBSDIO_FUNC1_WFRAMEBCHI, NULL);
				lo = bcmsdh_cfg_read(sdh, SDIO_FUNC_1,
				                     SBSDIO_FUNC1_WFRAMEBCLO, NULL);
				bus->f1regdata += 2;
				if ((hi == 0) && (lo == 0))
					break;
			}

		}
	} while ((ret < 0) && retries++ < TXRETRIES);

	if ((bus->idletime == DHD_IDLE_IMMEDIATE) && !bus->dpc_sched) {
		bus->activity = FALSE;
		dhdsdio_clkctl(bus, CLK_NONE, TRUE);
	}

	dhd_os_sdunlock(bus->dhd);

	if (ret)
		bus->dhd->tx_ctlerrs++;
	else
		bus->dhd->tx_ctlpkts++;

	return ret ? -EIO : 0;
}

int
dhd_bus_rxctl(struct dhd_bus *bus, uchar *msg, uint msglen)
{
	int timeleft;
	uint rxlen = 0;
	bool pending;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (bus->dhd->dongle_reset)
		return -EIO;

	
	timeleft = dhd_os_ioctl_resp_wait(bus->dhd, &bus->rxlen, &pending);

	dhd_os_sdlock(bus->dhd);
	rxlen = bus->rxlen;
	bcopy(bus->rxctl, msg, MIN(msglen, rxlen));
	bus->rxlen = 0;
	dhd_os_sdunlock(bus->dhd);

	if (rxlen) {
		DHD_CTL(("%s: resumed on rxctl frame, got %d expected %d\n",
		         __FUNCTION__, rxlen, msglen));
	} else if (timeleft == 0) {
		DHD_ERROR(("%s: resumed on timeout\n", __FUNCTION__));
	} else if (pending == TRUE) {
		DHD_CTL(("%s: cancelled\n", __FUNCTION__));
		return -ERESTARTSYS;
	} else {
		DHD_CTL(("%s: resumed for unknown reason?\n", __FUNCTION__));
	}

	if (rxlen)
		bus->dhd->rx_ctlpkts++;
	else
		bus->dhd->rx_ctlerrs++;

	return rxlen ? rxlen : -ETIMEDOUT;
}


enum {
	IOV_INTR = 1,
	IOV_POLLRATE,
	IOV_SDREG,
	IOV_SBREG,
	IOV_SDCIS,
	IOV_MEMBYTES,
	IOV_MEMSIZE,
	IOV_DOWNLOAD,
	IOV_FORCEEVEN,
	IOV_SDIOD_DRIVE,
	IOV_READAHEAD,
	IOV_SDRXCHAIN,
	IOV_ALIGNCTL,
	IOV_SDALIGN,
	IOV_DEVRESET,
	IOV_CPU,
#ifdef SDTEST
	IOV_PKTGEN,
	IOV_EXTLOOP,
#endif 
	IOV_SPROM,
	IOV_TXBOUND,
	IOV_RXBOUND,
	IOV_TXMINMAX,
#ifdef UNDER_CE
	IOV_RXFLOWMODE,
	IOV_RXFLOWHI,
	IOV_RXFLOWLOW,
	IOV_DPCPRIORITY,
	IOV_RXDPCPRIORITY,
	IOV_WATCHDOGPRIORITY,
	IOV_SENDUPPRIORITY,
	IOV_DPCTQ,
	IOV_SENDUPTQ,
	IOV_DBGOUTPUT,
#endif 
	IOV_IDLETIME,
	IOV_IDLECLOCK,
	IOV_SD1IDLE,
	IOV_SLEEP,
	IOV_VARS
};

const bcm_iovar_t dhdsdio_iovars[] = {
	{"intr",	IOV_INTR,	0,	IOVT_BOOL,	0 },
	{"sleep",	IOV_SLEEP,	0,	IOVT_BOOL,	0 },
	{"pollrate",	IOV_POLLRATE,	0,	IOVT_UINT32,	0 },
	{"idletime",	IOV_IDLETIME,	0,	IOVT_INT32,	0 },
	{"idleclock",	IOV_IDLECLOCK,	0,	IOVT_INT32,	0 },
	{"sd1idle",	IOV_SD1IDLE,	0,	IOVT_BOOL,	0 },
	{"membytes",	IOV_MEMBYTES,	0,	IOVT_BUFFER,	2 * sizeof(int) },
	{"memsize",	IOV_MEMSIZE,	0,	IOVT_UINT32,	0 },
	{"download",	IOV_DOWNLOAD,	0,	IOVT_BOOL,	0 },
	{"vars",	IOV_VARS,	0,	IOVT_BUFFER,	0 },
	{"sdiod_drive",	IOV_SDIOD_DRIVE, 0,	IOVT_UINT32,	0 },
	{"readahead",	IOV_READAHEAD,	0,	IOVT_BOOL,	0 },
	{"sdrxchain",	IOV_SDRXCHAIN,	0,	IOVT_BOOL,	0 },
	{"alignctl",	IOV_ALIGNCTL,	0,	IOVT_BOOL,	0 },
	{"sdalign",	IOV_SDALIGN,	0,	IOVT_BOOL,	0 },
	{"devreset",	IOV_DEVRESET,	0,	IOVT_BOOL,	0 },
	{"txbound",	IOV_TXBOUND,	0,	IOVT_UINT32,	0 },
	{"rxbound",	IOV_RXBOUND,	0,	IOVT_UINT32,	0 },
	{"txminmax",	IOV_TXMINMAX,	0,	IOVT_UINT32,	0 },
#ifdef UNDER_CE
	{"rxflowmode", IOV_RXFLOWMODE, 0,	IOVT_UINT32, 0 },
	{"rxflowhi",	IOV_RXFLOWHI,	0,	IOVT_UINT32, 0 },
	{"rxflowlow",	IOV_RXFLOWLOW,	0,	IOVT_UINT32, 0 },
	{"dpcprio",	IOV_DPCPRIORITY,	0,	IOVT_UINT32,	0 },
	{"rxdpcprio",	IOV_RXDPCPRIORITY,	0,	IOVT_UINT32,	0 },
	{"sendupprio",	IOV_SENDUPPRIORITY,	0,	IOVT_UINT32,	0 },
	{"watchdogprio",	IOV_WATCHDOGPRIORITY,	0,	IOVT_UINT32,	0 },
	{"dpctq",		IOV_DPCTQ,		0,	IOVT_UINT32,	0 },
	{"senduptq",	IOV_SENDUPTQ,	0,	IOVT_UINT32,	0 },
	{"dbgoutput", IOV_DBGOUTPUT,	0,	IOVT_UINT32, 0 },
#endif 
#ifdef DHD_DEBUG
	{"sdreg",	IOV_SDREG,	0,	IOVT_BUFFER,	sizeof(sdreg_t) },
	{"sbreg",	IOV_SBREG,	0,	IOVT_BUFFER,	sizeof(sdreg_t) },
	{"sd_cis",	IOV_SDCIS,	0,	IOVT_BUFFER,	DHD_IOCTL_MAXLEN },
	{"forcealign",	IOV_FORCEEVEN,	0,	IOVT_BOOL,	0 },
	{"cpu",		IOV_CPU,	0,	IOVT_BOOL,	0 },
#endif 
#if defined(BCMDBG)
	{"sprom",	IOV_SPROM,	0,	IOVT_BUFFER,	2 * sizeof(int) },
#endif 
#ifdef SDTEST
	{"extloop",	IOV_EXTLOOP,	0,	IOVT_BOOL,	0 },
	{"pktgen",	IOV_PKTGEN,	0,	IOVT_BUFFER,	sizeof(dhd_pktgen_t) },
#endif 

	{NULL, 0, 0, 0, 0 }
};

static void
dhd_dump_pct(struct bcmstrbuf *strbuf, char *desc, uint num, uint div)
{
	uint q1, q2;

	if (!div) {
		bcm_bprintf(strbuf, "%s N/A", desc);
	} else {
		q1 = num / div;
		q2 = (100 * (num - (q1 * div))) / div;
		bcm_bprintf(strbuf, "%s %d.%02d", desc, q1, q2);
	}
}

void
dhd_bus_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf)
{
	dhd_bus_t *bus = dhdp->bus;

	bcm_bprintf(strbuf, "Bus SDIO structure:\n");
	bcm_bprintf(strbuf, "hostintmask 0x%08x intstatus 0x%08x sdpcm_ver %d\n",
	            bus->hostintmask, bus->intstatus, bus->sdpcm_ver);
	bcm_bprintf(strbuf, "fcstate %d qlen %d tx_seq %d rxskip %d rxlen %d rx_seq %d\n",
	            bus->fcstate, pktq_len(&bus->txq), bus->tx_seq, bus->rxskip,
	            bus->rxlen, bus->rx_seq);
	bcm_bprintf(strbuf, "intr %d intrcount %d lastintrs %d spurious %d\n",
	            bus->intr, bus->intrcount, bus->lastintrs, bus->spurious);
	bcm_bprintf(strbuf, "pollrate %d pollcnt %d regfails %d\n",
	            bus->pollrate, bus->pollcnt, bus->regfails);

	bcm_bprintf(strbuf, "\nAdditional counters:\n");
	bcm_bprintf(strbuf, "tx_sderrs %d fcqueued %d rxrtx %d rx_toolong %d rxc_errors %d\n",
	            bus->tx_sderrs, bus->fcqueued, bus->rxrtx, bus->rx_toolong,
	            bus->rxc_errors);
	bcm_bprintf(strbuf, "rx_hdrfail %d badhdr %d badseq %d\n",
	            bus->rx_hdrfail, bus->rx_badhdr, bus->rx_badseq);
	bcm_bprintf(strbuf, "fc_rcvd %d, fc_xoff %d, fc_xon %d\n",
	            bus->fc_rcvd, bus->fc_xoff, bus->fc_xon);
	bcm_bprintf(strbuf, "rxglomfail %d, rxglomframes %d, rxglompkts %d\n",
	            bus->rxglomfail, bus->rxglomframes, bus->rxglompkts);
	bcm_bprintf(strbuf, "f2rx (hdrs/data) %d (%d/%d), f2tx %d f1regs %d\n",
	            (bus->f2rxhdrs + bus->f2rxdata), bus->f2rxhdrs, bus->f2rxdata,
	            bus->f2txdata, bus->f1regdata);
	{
		dhd_dump_pct(strbuf, "\nRx: pkts/f2rd", bus->dhd->rx_packets,
		             (bus->f2rxhdrs + bus->f2rxdata));
		dhd_dump_pct(strbuf, ", pkts/f1sd", bus->dhd->rx_packets, bus->f1regdata);
		dhd_dump_pct(strbuf, ", pkts/sd", bus->dhd->rx_packets,
		             (bus->f2rxhdrs + bus->f2rxdata + bus->f1regdata));
		dhd_dump_pct(strbuf, ", pkts/int", bus->dhd->rx_packets, bus->intrcount);
		bcm_bprintf(strbuf, "\n");

		dhd_dump_pct(strbuf, "Rx: glom pct", (100 * bus->rxglompkts),
		             bus->dhd->rx_packets);
		dhd_dump_pct(strbuf, ", pkts/glom", bus->rxglompkts, bus->rxglomframes);
		bcm_bprintf(strbuf, "\n");

		dhd_dump_pct(strbuf, "Tx: pkts/f2wr", bus->dhd->tx_packets, bus->f2txdata);
		dhd_dump_pct(strbuf, ", pkts/f1sd", bus->dhd->tx_packets, bus->f1regdata);
		dhd_dump_pct(strbuf, ", pkts/sd", bus->dhd->tx_packets,
		             (bus->f2txdata + bus->f1regdata));
		dhd_dump_pct(strbuf, ", pkts/int", bus->dhd->tx_packets, bus->intrcount);
		bcm_bprintf(strbuf, "\n");

		dhd_dump_pct(strbuf, "Total: pkts/f2rw",
		             (bus->dhd->tx_packets + bus->dhd->rx_packets),
		             (bus->f2txdata + bus->f2rxhdrs + bus->f2rxdata));
		dhd_dump_pct(strbuf, ", pkts/f1sd",
		             (bus->dhd->tx_packets + bus->dhd->rx_packets), bus->f1regdata);
		dhd_dump_pct(strbuf, ", pkts/sd",
		             (bus->dhd->tx_packets + bus->dhd->rx_packets),
		             (bus->f2txdata + bus->f2rxhdrs + bus->f2rxdata + bus->f1regdata));
		dhd_dump_pct(strbuf, ", pkts/int",
		             (bus->dhd->tx_packets + bus->dhd->rx_packets), bus->intrcount);
		bcm_bprintf(strbuf, "\n\n");
	}

#ifdef SDTEST
	if (bus->pktgen_count) {
		bcm_bprintf(strbuf, "pktgen config and count:\n");
		bcm_bprintf(strbuf, "freq %d count %d print %d total %d min %d len %d\n",
		            bus->pktgen_freq, bus->pktgen_count, bus->pktgen_print,
		            bus->pktgen_total, bus->pktgen_minlen, bus->pktgen_maxlen);
		bcm_bprintf(strbuf, "send attempts %d rcvd %d fail %d\n",
		            bus->pktgen_sent, bus->pktgen_rcvd, bus->pktgen_fail);
	}
#endif 
#ifdef DHD_DEBUG
	bcm_bprintf(strbuf, "dpc_sched %d host interrupt%spending\n",
	            bus->dpc_sched, (bcmsdh_intr_pending(bus->sdh) ? " " : " not "));
	bcm_bprintf(strbuf, "blocksize %d roundup %d\n", bus->blocksize, bus->roundup);
#endif 
	bcm_bprintf(strbuf, "clkstate %d activity %d idletime %d idlecount %d sleeping %d\n",
	            bus->clkstate, bus->activity, bus->idletime, bus->idlecount, bus->sleeping);
}

void
dhd_bus_clearcounts(dhd_pub_t *dhdp)
{
	dhd_bus_t *bus = (dhd_bus_t *)dhdp->bus;

	bus->intrcount = bus->lastintrs = bus->spurious = bus->regfails = 0;
	bus->rxrtx = bus->rx_toolong = bus->rxc_errors = 0;
	bus->rx_hdrfail = bus->rx_badhdr = bus->rx_badseq = 0;
	bus->tx_sderrs = bus->fc_rcvd = bus->fc_xoff = bus->fc_xon = 0;
	bus->rxglomfail = bus->rxglomframes = bus->rxglompkts = 0;
	bus->f2rxhdrs = bus->f2rxdata = bus->f2txdata = bus->f1regdata = 0;
}

#ifdef SDTEST
static int
dhdsdio_pktgen_get(dhd_bus_t *bus, uint8 *arg)
{
	dhd_pktgen_t pktgen;

	pktgen.version = DHD_PKTGEN_VERSION;
	pktgen.freq = bus->pktgen_freq;
	pktgen.count = bus->pktgen_count;
	pktgen.print = bus->pktgen_print;
	pktgen.total = bus->pktgen_total;
	pktgen.minlen = bus->pktgen_minlen;
	pktgen.maxlen = bus->pktgen_maxlen;
	pktgen.numsent = bus->pktgen_sent;
	pktgen.numrcvd = bus->pktgen_rcvd;
	pktgen.numfail = bus->pktgen_fail;
	pktgen.mode = bus->pktgen_mode;
	pktgen.stop = bus->pktgen_stop;

	bcopy(&pktgen, arg, sizeof(pktgen));

	return 0;
}

static int
dhdsdio_pktgen_set(dhd_bus_t *bus, uint8 *arg)
{
	dhd_pktgen_t pktgen;
	uint oldcnt, oldmode;

	bcopy(arg, &pktgen, sizeof(pktgen));
	if (pktgen.version != DHD_PKTGEN_VERSION)
		return BCME_BADARG;

	oldcnt = bus->pktgen_count;
	oldmode = bus->pktgen_mode;

	bus->pktgen_freq = pktgen.freq;
	bus->pktgen_count = pktgen.count;
	bus->pktgen_print = pktgen.print;
	bus->pktgen_total = pktgen.total;
	bus->pktgen_minlen = pktgen.minlen;
	bus->pktgen_maxlen = pktgen.maxlen;
	bus->pktgen_mode = pktgen.mode;
	bus->pktgen_stop = pktgen.stop;

	bus->pktgen_tick = bus->pktgen_ptick = 0;
	bus->pktgen_len = MAX(bus->pktgen_len, bus->pktgen_minlen);
	bus->pktgen_len = MIN(bus->pktgen_len, bus->pktgen_maxlen);

	
	if (bus->pktgen_count && (!oldcnt || oldmode != bus->pktgen_mode))
		bus->pktgen_sent = bus->pktgen_rcvd = bus->pktgen_fail = 0;

	return 0;
}
#endif 

static int
dhdsdio_membytes(dhd_bus_t *bus, bool write, uint32 address, uint8 *data, uint size)
{
	int bcmerror = 0;
	uint32 sdaddr;
	uint dsize;

	
	sdaddr = address & SBSDIO_SB_OFT_ADDR_MASK;
	if ((sdaddr + size) & SBSDIO_SBWINDOW_MASK)
		dsize = (SBSDIO_SB_OFT_ADDR_LIMIT - sdaddr);
	else
		dsize = size;

	
	if ((bcmerror = dhdsdio_set_siaddr_window(bus, address))) {
		DHD_ERROR(("%s: window change failed\n", __FUNCTION__));
		goto xfer_done;
	}

	
	while (size) {
		DHD_INFO(("%s: %s %d bytes at offset 0x%08x in window 0x%08x\n",
		          __FUNCTION__, (write ? "write" : "read"), dsize, sdaddr,
		          (address & SBSDIO_SBWINDOW_MASK)));
		if ((bcmerror = bcmsdh_rwdata(bus->sdh, write, sdaddr, data, dsize))) {
			DHD_ERROR(("%s: membytes transfer failed\n", __FUNCTION__));
			break;
		}

		
		if ((size -= dsize)) {
			data += dsize;
			address += dsize;
			if ((bcmerror = dhdsdio_set_siaddr_window(bus, address))) {
				DHD_ERROR(("%s: window change failed\n", __FUNCTION__));
				break;
			}
			sdaddr = 0;
			dsize = MIN(SBSDIO_SB_OFT_ADDR_LIMIT, size);
		}
	}

xfer_done:
	
	if (dhdsdio_set_siaddr_window(bus, bcmsdh_cur_sbwad(bus->sdh))) {
		DHD_ERROR(("%s: FAILED to return to 0x%x\n", __FUNCTION__,
			bcmsdh_cur_sbwad(bus->sdh)));
	}

	return bcmerror;
}

int
dhdsdio_downloadvars(dhd_bus_t *bus, void *arg, int len)
{
	int bcmerror = BCME_OK;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	
	if (bus->dhd->up) {
		bcmerror = BCME_NOTDOWN;
		goto err;
	}
	if (!len) {
		bcmerror = BCME_BUFTOOSHORT;
		goto err;
	}

	
	if (bus->vars)
		MFREE(bus->dhd->osh, bus->vars, bus->varsz);

	bus->vars = MALLOC(bus->dhd->osh, len);
	bus->varsz = bus->vars ? len : 0;
	if (bus->vars == NULL) {
		bcmerror = BCME_NOMEM;
		goto err;
	}

	
	bcopy(arg, bus->vars, bus->varsz);
err:
	return bcmerror;
}

static int
dhdsdio_doiovar(dhd_bus_t *bus, const bcm_iovar_t *vi, uint32 actionid, const char *name,
                void *params, int plen, void *arg, int len, int val_size)
{
	int bcmerror = 0;
	int32 int_val = 0;
	bool bool_val = 0;

	DHD_TRACE(("%s: Enter, action %d name %s params %p plen %d arg %p len %d val_size %d\n",
	           __FUNCTION__, actionid, name, params, plen, arg, len, val_size));

	if ((bcmerror = bcm_iovar_lencheck(vi, arg, len, IOV_ISSET(actionid))) != 0)
		goto exit;

	if (plen >= (int)sizeof(int_val))
		bcopy(params, &int_val, sizeof(int_val));

	bool_val = (int_val != 0) ? TRUE : FALSE;


	
	dhd_os_sdlock(bus->dhd);

	
	if (bus->dhd->dongle_reset && !(actionid == IOV_SVAL(IOV_DEVRESET) ||
	                                actionid == IOV_GVAL(IOV_DEVRESET))) {
		bcmerror = BCME_NOTREADY;
		goto exit;
	}

	
	if (vi->varid == IOV_SLEEP) {
		if (IOV_ISSET(actionid)) {
			bcmerror = dhdsdio_bussleep(bus, bool_val);
		} else {
			int_val = (int32)bus->sleeping;
			bcopy(&int_val, arg, val_size);
		}
		goto exit;
	}

	
	if (!bus->dhd->dongle_reset) {
		BUS_WAKE(bus);
		dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);
	}

	switch (actionid) {
	case IOV_GVAL(IOV_INTR):
		int_val = (int32)bus->intr;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_INTR):
		bus->intr = bool_val;
		bus->intdis = FALSE;
		if (bus->dhd->up) {
			if (bus->intr) {
				DHD_INTR(("%s: enable SDIO device interrupts\n", __FUNCTION__));
				bcmsdh_intr_enable(bus->sdh);
			} else {
				DHD_INTR(("%s: disable SDIO interrupts\n", __FUNCTION__));
				bcmsdh_intr_disable(bus->sdh);
			}
		}
		break;

	case IOV_GVAL(IOV_POLLRATE):
		int_val = (int32)bus->pollrate;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_POLLRATE):
		bus->pollrate = (uint)int_val;
		bus->poll = (bus->pollrate != 0);
		break;

	case IOV_GVAL(IOV_IDLETIME):
		int_val = bus->idletime;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_IDLETIME):
		if ((int_val < 0) && (int_val != DHD_IDLE_IMMEDIATE)) {
			bcmerror = BCME_BADARG;
		} else {
			bus->idletime = int_val;
		}
		break;

	case IOV_GVAL(IOV_IDLECLOCK):
		int_val = (int32)bus->idleclock;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_IDLECLOCK):
		bus->idleclock = int_val;
		break;

	case IOV_GVAL(IOV_SD1IDLE):
		int_val = (int32)sd1idle;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_SD1IDLE):
		sd1idle = bool_val;
		break;


	case IOV_SVAL(IOV_MEMBYTES):
	case IOV_GVAL(IOV_MEMBYTES):
	{
		uint32 address;
		uint size, dsize;
		uint8 *data;

		bool set = (actionid == IOV_SVAL(IOV_MEMBYTES));

		ASSERT(plen >= 2*sizeof(int));

		address = (uint32)int_val;
		bcopy((char *)params + sizeof(int_val), &int_val, sizeof(int_val));
		size = (uint)int_val;

		
		dsize = set ? plen - (2 * sizeof(int)) : len;
		if (dsize < size) {
			DHD_ERROR(("%s: error on %s membytes, addr 0x%08x size %d dsize %d\n",
			           __FUNCTION__, (set ? "set" : "get"), address, size, dsize));
			bcmerror = BCME_BADARG;
			break;
		}

		DHD_INFO(("%s: Request to %s %d bytes at address 0x%08x\n", __FUNCTION__,
		          (set ? "write" : "read"), size, address));

		
		if ((bus->orig_ramsize) &&
		    ((address > bus->orig_ramsize) || (address + size > bus->orig_ramsize))) {
			DHD_ERROR(("%s: ramsize 0x%08x doesn't have %d bytes at 0x%08x\n",
			           __FUNCTION__, bus->orig_ramsize, size, address));
			bcmerror = BCME_BADARG;
			break;
		}

		
		data = set ? (uint8*)params + 2 * sizeof(int): (uint8*)arg;

		
		bcmerror = dhdsdio_membytes(bus, set, address, data, size);

		break;
	}

	case IOV_GVAL(IOV_MEMSIZE):
		int_val = (int32)bus->ramsize;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_GVAL(IOV_SDIOD_DRIVE):
		int_val = (int32)dhd_sdiod_drive_strength;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_SDIOD_DRIVE):
		dhd_sdiod_drive_strength = int_val;
		si_sdiod_drive_strength_init(bus->sih, bus->dhd->osh, dhd_sdiod_drive_strength);
		break;

	case IOV_SVAL(IOV_DOWNLOAD):
		bcmerror = dhdsdio_download_state(bus, bool_val);
		break;

	case IOV_SVAL(IOV_VARS):
		bcmerror = dhdsdio_downloadvars(bus, arg, len);
		break;

	case IOV_GVAL(IOV_READAHEAD):
		int_val = (int32)dhd_readahead;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_READAHEAD):
		if (bool_val && !dhd_readahead)
			bus->nextlen = 0;
		dhd_readahead = bool_val;
		break;

	case IOV_GVAL(IOV_SDRXCHAIN):
		int_val = (int32)bus->use_rxchain;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_SDRXCHAIN):
		if (bool_val && !bus->sd_rxchain)
			bcmerror = BCME_UNSUPPORTED;
		else
			bus->use_rxchain = bool_val;
		break;
#ifndef BCMSPI
	case IOV_GVAL(IOV_ALIGNCTL):
		int_val = (int32)dhd_alignctl;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_ALIGNCTL):
		dhd_alignctl = bool_val;
		break;
#endif 

	case IOV_GVAL(IOV_SDALIGN):
		int_val = DHD_SDALIGN;
		bcopy(&int_val, arg, val_size);
		break;

#ifdef DHD_DEBUG
	case IOV_GVAL(IOV_VARS):
		if (bus->varsz < (uint)len)
			bcopy(bus->vars, arg, bus->varsz);
		else
			bcmerror = BCME_BUFTOOSHORT;
		break;
#endif 


	case IOV_GVAL(IOV_SDREG):
	{
		sdreg_t *sd_ptr;
		uint32 addr, size;

		sd_ptr = (sdreg_t *)params;

		addr = (uintptr)bus->regs + sd_ptr->offset;
		size = sd_ptr->func;
		int_val = (int32)bcmsdh_reg_read(bus->sdh, addr, size);
		if (bcmsdh_regfail(bus->sdh))
			bcmerror = BCME_SDIO_ERROR;
		bcopy(&int_val, arg, sizeof(int32));
		break;
	}

	case IOV_SVAL(IOV_SDREG):
	{
		sdreg_t *sd_ptr;
		uint32 addr, size;

		sd_ptr = (sdreg_t *)params;

		addr = (uintptr)bus->regs + sd_ptr->offset;
		size = sd_ptr->func;
		bcmsdh_reg_write(bus->sdh, addr, size, sd_ptr->value);
		if (bcmsdh_regfail(bus->sdh))
			bcmerror = BCME_SDIO_ERROR;
		break;
	}

	
	case IOV_GVAL(IOV_SBREG):
	{
		sdreg_t sdreg;
		uint32 addr, size;

		bcopy(params, &sdreg, sizeof(sdreg));

		addr = SI_ENUM_BASE + sdreg.offset;
		size = sdreg.func;
		int_val = (int32)bcmsdh_reg_read(bus->sdh, addr, size);
		if (bcmsdh_regfail(bus->sdh))
			bcmerror = BCME_SDIO_ERROR;
		bcopy(&int_val, arg, sizeof(int32));
		break;
	}

	case IOV_SVAL(IOV_SBREG):
	{
		sdreg_t sdreg;
		uint32 addr, size;

		bcopy(params, &sdreg, sizeof(sdreg));

		addr = SI_ENUM_BASE + sdreg.offset;
		size = sdreg.func;
		bcmsdh_reg_write(bus->sdh, addr, size, sdreg.value);
		if (bcmsdh_regfail(bus->sdh))
			bcmerror = BCME_SDIO_ERROR;
		break;
	}

	case IOV_GVAL(IOV_SDCIS):
	{
		*(char *)arg = 0;

		bcmstrcat(arg, "\nFunc 0\n");
		bcmsdh_cis_read(bus->sdh, 0x10, (uint8 *)arg + strlen(arg), 49 * 32);
		bcmstrcat(arg, "\nFunc 1\n");
		bcmsdh_cis_read(bus->sdh, 0x11, (uint8 *)arg + strlen(arg), 49 * 32);
		bcmstrcat(arg, "\nFunc 2\n");
		bcmsdh_cis_read(bus->sdh, 0x12, (uint8 *)arg + strlen(arg), 49 * 32);
		break;
	}

	case IOV_GVAL(IOV_FORCEEVEN):
		int_val = (int32)forcealign;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_FORCEEVEN):
		forcealign = bool_val;
		break;

	case IOV_GVAL(IOV_TXBOUND):
		int_val = (int32)dhd_txbound;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_TXBOUND):
		dhd_txbound = (uint)int_val;
		break;

	case IOV_GVAL(IOV_RXBOUND):
		int_val = (int32)dhd_rxbound;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_RXBOUND):
		dhd_rxbound = (uint)int_val;
		break;

	case IOV_GVAL(IOV_TXMINMAX):
		int_val = (int32)dhd_txminmax;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_TXMINMAX):
		dhd_txminmax = (uint)int_val;
		break;
#ifdef UNDER_CE
	case IOV_GVAL(IOV_RXFLOWMODE):
		int_val = (uint32)bus->rxflow_mode;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_RXFLOWMODE):
		
		if ((int_val == 0) && bus->rxflow_mode && bus->rxflow)
			dhd_rx_flow((void *)bus->dhd, FALSE);

		bus->rxflow_mode = dhd_rxflow_mode(bus->dhd, (uint)int_val, TRUE);
		break;

	case IOV_GVAL(IOV_RXFLOWHI):
		
		int_val = dhd_rx_flow_hilimit(bus->dhd, 0);
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_RXFLOWHI):
		
		dhd_rx_flow_hilimit(bus->dhd, (uint32)int_val);
		break;

	case IOV_GVAL(IOV_RXFLOWLOW):
		
		int_val = dhd_rx_flow_lowlimit(bus->dhd, 0);
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_RXFLOWLOW):
		
		dhd_rx_flow_lowlimit(bus->dhd, (uint32)int_val);
		break;

	case IOV_GVAL(IOV_DPCPRIORITY):
		dhd_thread_priority(bus->dhd, DHD_DPC, (uint *) &int_val, FALSE);
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_DPCPRIORITY):
		dhd_thread_priority(bus->dhd, DHD_DPC, (uint *) &int_val, TRUE);
		break;

	case IOV_GVAL(IOV_RXDPCPRIORITY):
		dhd_thread_priority(bus->dhd, DHD_RXDPC, (uint *) &int_val, FALSE);
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_RXDPCPRIORITY):
		dhd_thread_priority(bus->dhd, DHD_RXDPC, (uint *) &int_val, TRUE);
		break;

	case IOV_GVAL(IOV_SENDUPPRIORITY):
		dhd_thread_priority(bus->dhd, DHD_SENDUP, (uint *) &int_val, FALSE);
		bcopy(&int_val, arg, val_size);
		break;
	case IOV_SVAL(IOV_SENDUPPRIORITY):
		dhd_thread_priority(bus->dhd, DHD_SENDUP, (uint *) &int_val, TRUE);
		break;

	case IOV_GVAL(IOV_WATCHDOGPRIORITY):
		dhd_thread_priority(bus->dhd, DHD_WATCHDOG, (uint *) &int_val, FALSE);
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_WATCHDOGPRIORITY):
		dhd_thread_priority(bus->dhd, DHD_WATCHDOG, (uint *) &int_val, TRUE);
		break;

	case IOV_GVAL(IOV_DPCTQ):
		dhd_thread_quantum(bus->dhd, DHD_DPC, (uint *) &int_val, FALSE);
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_DPCTQ):
		dhd_thread_quantum(bus->dhd, DHD_DPC, (uint *) &int_val, TRUE);
		break;

	case IOV_GVAL(IOV_SENDUPTQ):
		dhd_thread_quantum(bus->dhd, DHD_SENDUP, (uint *) &int_val, FALSE);
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_SENDUPTQ):
		dhd_thread_quantum(bus->dhd, DHD_SENDUP, (uint *) &int_val, TRUE);
		break;

	case IOV_GVAL(IOV_DBGOUTPUT):
		bcopy(&g_dbgOutputIndex, arg, val_size);
		break;

	case IOV_SVAL(IOV_DBGOUTPUT):
		g_dbgOutputIndex = int_val;
		break;
#endif 
#ifdef DHD_DEBUG

#endif 


#ifdef SDTEST
	case IOV_GVAL(IOV_EXTLOOP):
		int_val = (int32)bus->ext_loop;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_EXTLOOP):
		bus->ext_loop = bool_val;
		break;

	case IOV_GVAL(IOV_PKTGEN):
		bcmerror = dhdsdio_pktgen_get(bus, arg);
		break;

	case IOV_SVAL(IOV_PKTGEN):
		bcmerror = dhdsdio_pktgen_set(bus, arg);
		break;
#endif 


	case IOV_SVAL(IOV_DEVRESET):
		DHD_TRACE(("%s: Called set IOV_DEVRESET=%d dongle_reset=%d busstate=%d\n",
		           __FUNCTION__, bool_val, bus->dhd->dongle_reset,
		           bus->dhd->busstate));

		ASSERT(bus->dhd->osh);
		

		dhd_bus_devreset(bus->dhd, (uint8)bool_val);

		break;

	case IOV_GVAL(IOV_DEVRESET):
		DHD_TRACE(("%s: Called get IOV_DEVRESET\n", __FUNCTION__));

		
		int_val = (bool) bus->dhd->dongle_reset;
		bcopy(&int_val, arg, val_size);

		break;

	default:
		bcmerror = BCME_UNSUPPORTED;
		break;
	}

exit:
	if ((bus->idletime == DHD_IDLE_IMMEDIATE) && !bus->dpc_sched) {
		bus->activity = FALSE;
		dhdsdio_clkctl(bus, CLK_NONE, TRUE);
	}

	dhd_os_sdunlock(bus->dhd);

	return bcmerror;
}

static int
dhdsdio_write_vars(dhd_bus_t *bus)
{
	int bcmerror = 0;
	uint32 varsize;
	uint32 varaddr;
	char *vbuffer;
	uint32 varsizew;

	
	varsize = bus->varsz ? ROUNDUP(bus->varsz, 4) : 0;
	varaddr = (bus->ramsize - 4) - varsize;

	if (bus->vars) {
		vbuffer = (char*)MALLOC(bus->dhd->osh, varsize);
		if (!vbuffer)
			return BCME_NOMEM;

		bzero(vbuffer, varsize);
		bcopy(bus->vars, vbuffer, bus->varsz);

		
		bcmerror = dhdsdio_membytes(bus, TRUE, varaddr, vbuffer, varsize);

		MFREE(bus->dhd->osh, vbuffer, varsize);
	}

	
	DHD_INFO(("Physical memory size: %d, usable memory size: %d\n",
		bus->orig_ramsize, bus->ramsize));
	DHD_INFO(("Vars are at %d, orig varsize is %d\n",
		varaddr, varsize));
	varsize = ((bus->orig_ramsize - 4) - varaddr);

	
	if (bcmerror) {
		varsizew = 0;
	} else {
		varsizew = varsize / 4;
		varsizew = (~varsizew << 16) | (varsizew & 0x0000FFFF);
		varsizew = htol32(varsizew);
	}

	DHD_INFO(("New varsize is %d, length token=0x%08x\n", varsize, varsizew));

	
	bcmerror = dhdsdio_membytes(bus, TRUE, (bus->orig_ramsize - 4),
		(uint8*)&varsizew, 4);

	return bcmerror;
}

static int
dhdsdio_download_state(dhd_bus_t *bus, bool enter)
{
	uint retries;
	int bcmerror = 0;

	
	if (enter) {

		bus->alp_only = TRUE;

		if (!(si_setcore(bus->sih, ARM7S_CORE_ID, 0)) &&
		    !(si_setcore(bus->sih, ARMCM3_CORE_ID, 0))) {
			DHD_ERROR(("%s: Failed to find ARM core!\n", __FUNCTION__));
			bcmerror = BCME_ERROR;
			goto fail;
		}

		si_core_disable(bus->sih, 0);
		if (bcmsdh_regfail(bus->sdh)) {
			bcmerror = BCME_SDIO_ERROR;
			goto fail;
		}

		if (!(si_setcore(bus->sih, SOCRAM_CORE_ID, 0))) {
			DHD_ERROR(("%s: Failed to find SOCRAM core!\n", __FUNCTION__));
			bcmerror = BCME_ERROR;
			goto fail;
		}

		si_core_reset(bus->sih, 0, 0);
		if (bcmsdh_regfail(bus->sdh)) {
			DHD_ERROR(("%s: Failure trying reset SOCRAM core?\n", __FUNCTION__));
			bcmerror = BCME_SDIO_ERROR;
			goto fail;
		}

		
		if (bus->ramsize) {
			uint32 zeros = 0;
			dhdsdio_membytes(bus, TRUE, bus->ramsize - 4, (uint8*)&zeros, 4);
		}
	} else {
		if (!(si_setcore(bus->sih, SOCRAM_CORE_ID, 0))) {
			DHD_ERROR(("%s: Failed to find SOCRAM core!\n", __FUNCTION__));
			bcmerror = BCME_ERROR;
			goto fail;
		}

		if (!si_iscoreup(bus->sih)) {
			DHD_ERROR(("%s: SOCRAM core is down after reset?\n", __FUNCTION__));
			bcmerror = BCME_ERROR;
			goto fail;
		}

		if ((bcmerror = dhdsdio_write_vars(bus))) {
			DHD_ERROR(("%s: no vars written to RAM\n", __FUNCTION__));
			bcmerror = 0;
		}

		if (!si_setcore(bus->sih, PCMCIA_CORE_ID, 0) &&
		    !si_setcore(bus->sih, SDIOD_CORE_ID, 0)) {
			DHD_ERROR(("%s: Can't change back to SDIO core?\n", __FUNCTION__));
			bcmerror = BCME_ERROR;
			goto fail;
		}
		W_SDREG(0xFFFFFFFF, &bus->regs->intstatus, retries);


		if (!(si_setcore(bus->sih, ARM7S_CORE_ID, 0)) &&
		    !(si_setcore(bus->sih, ARMCM3_CORE_ID, 0))) {
			DHD_ERROR(("%s: Failed to find ARM core!\n", __FUNCTION__));
			bcmerror = BCME_ERROR;
			goto fail;
		}

		si_core_reset(bus->sih, 0, 0);
		if (bcmsdh_regfail(bus->sdh)) {
			DHD_ERROR(("%s: Failure trying to reset ARM core?\n", __FUNCTION__));
			bcmerror = BCME_SDIO_ERROR;
			goto fail;
		}

		
		bus->alp_only = FALSE;

		bus->dhd->busstate = DHD_BUS_LOAD;
	}
fail:
	
	if (!si_setcore(bus->sih, PCMCIA_CORE_ID, 0))
		si_setcore(bus->sih, SDIOD_CORE_ID, 0);

	return bcmerror;
}

int
dhd_bus_iovar_op(dhd_pub_t *dhdp, const char *name,
                 void *params, int plen, void *arg, int len, bool set)
{
	dhd_bus_t *bus = dhdp->bus;
	const bcm_iovar_t *vi = NULL;
	int bcmerror = 0;
	int val_size;
	uint32 actionid;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(name);
	ASSERT(len >= 0);

	
	ASSERT(set || (arg && len));

	
	ASSERT(!set || (!params && !plen));

	
	if ((vi = bcm_iovar_lookup(dhdsdio_iovars, name)) == NULL) {
		dhd_os_sdlock(bus->dhd);

		BUS_WAKE(bus);

		
		dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);

		bcmerror = bcmsdh_iovar_op(bus->sdh, name, params, plen, arg, len, set);

		

		
		if (set && strcmp(name, "sd_divisor") == 0) {
			if (bcmsdh_iovar_op(bus->sdh, "sd_divisor", NULL, 0,
			                    &bus->sd_divisor, sizeof(int32), FALSE) != BCME_OK) {
				bus->sd_divisor = -1;
				DHD_ERROR(("%s: fail on %s get\n", __FUNCTION__, name));
			} else {
				DHD_INFO(("%s: noted %s update, value now %d\n",
				          __FUNCTION__, name, bus->sd_divisor));
			}
		}
		
		if (set && strcmp(name, "sd_mode") == 0) {
			if (bcmsdh_iovar_op(bus->sdh, "sd_mode", NULL, 0,
			                    &bus->sd_mode, sizeof(int32), FALSE) != BCME_OK) {
				bus->sd_mode = -1;
				DHD_ERROR(("%s: fail on %s get\n", __FUNCTION__, name));
			} else {
				DHD_INFO(("%s: noted %s update, value now %d\n",
				          __FUNCTION__, name, bus->sd_mode));
			}
		}
		
		if (set && strcmp(name, "sd_blocksize") == 0) {
			int32 fnum = 2;
			if (bcmsdh_iovar_op(bus->sdh, "sd_blocksize", &fnum, sizeof(int32),
			                    &bus->blocksize, sizeof(int32), FALSE) != BCME_OK) {
				bus->blocksize = 0;
				DHD_ERROR(("%s: fail on %s get\n", __FUNCTION__, "sd_blocksize"));
			} else {
				DHD_INFO(("%s: noted %s update, value now %d\n",
				          __FUNCTION__, "sd_blocksize", bus->blocksize));
			}
		}
		bus->roundup = MIN(max_roundup, bus->blocksize);

		if ((bus->idletime == DHD_IDLE_IMMEDIATE) && !bus->dpc_sched) {
			bus->activity = FALSE;
			dhdsdio_clkctl(bus, CLK_NONE, TRUE);
		}

		dhd_os_sdunlock(bus->dhd);
		goto exit;
	}

	DHD_CTL(("%s: %s %s, len %d plen %d\n", __FUNCTION__,
	         name, (set ? "set" : "get"), len, plen));

	
	if (params == NULL) {
		params = arg;
		plen = len;
	}

	if (vi->type == IOVT_VOID)
		val_size = 0;
	else if (vi->type == IOVT_BUFFER)
		val_size = len;
	else
		
		val_size = sizeof(int);

	actionid = set ? IOV_SVAL(vi->varid) : IOV_GVAL(vi->varid);
	bcmerror = dhdsdio_doiovar(bus, vi, actionid, name, params, plen, arg, len, val_size);

exit:
	return bcmerror;
}

void
dhd_bus_stop(struct dhd_bus *bus, bool enforce_mutex)
{
	osl_t *osh = bus->dhd->osh;
	uint32 local_hostintmask;
	uint8 saveclk;
	uint retries;
	int err;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (enforce_mutex)
		dhd_os_sdlock(bus->dhd);

	BUS_WAKE(bus);

	
	dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);

	
	W_SDREG(0, &bus->regs->hostintmask, retries);
	local_hostintmask = bus->hostintmask;
	bus->hostintmask = 0;

	
	bus->dhd->busstate = DHD_BUS_DOWN;

	
	saveclk = bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);
	if (!err) {
		bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
		                 (saveclk | SBSDIO_FORCE_HT), &err);
	}
	if (err) {
		DHD_ERROR(("%s: Failed to force clock for F2: err %d\n", __FUNCTION__, err));
	}

	
	DHD_INTR(("%s: disable SDIO interrupts\n", __FUNCTION__));
	bcmsdh_intr_disable(bus->sdh);
#ifndef BCMSPI
	bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_0, SDIOD_CCCR_IOEN, SDIO_FUNC_ENABLE_1, NULL);
#endif 

	
	W_SDREG(local_hostintmask, &bus->regs->intstatus, retries);

	
	dhdsdio_clkctl(bus, CLK_SDONLY, FALSE);

	if (enforce_mutex)
		dhd_os_sdunlock(bus->dhd);

	
	pktq_flush(osh, &bus->txq, TRUE);

	
	if (bus->glomd)
		PKTFREE(osh, bus->glomd, FALSE);

	if (bus->glom)
		PKTFREE(osh, bus->glom, FALSE);

	bus->glom = bus->glomd = NULL;

	
	bus->rxlen = 0;
	dhd_os_ioctl_resp_wake(bus->dhd);

	
	bus->rxskip = FALSE;
	bus->tx_seq = bus->rx_seq = 0;
}

int
dhd_bus_init(dhd_pub_t *dhdp, bool enforce_mutex)
{
	dhd_bus_t *bus = dhdp->bus;
	uint retries = 0;

	uint8 ready, enable;
	int err, ret = 0;
#ifdef BCMSPI
	uint32 dstatus = 0;	
#else 
	uint8 saveclk;
#endif 

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(bus->dhd);
	if (!bus->dhd)
		return 0;

	if (enforce_mutex)
		dhd_os_sdlock(bus->dhd);

	
	dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);
	if (bus->clkstate != CLK_AVAIL)
		goto exit;

#ifdef BCMSPI
	
	ready = (SDIO_FUNC_ENABLE_1 | SDIO_FUNC_ENABLE_2);

	
	retries = WAIT_F2RXFIFORDY;
	enable = 0;
	while (retries-- && !enable) {
		OSL_DELAY(WAIT_F2RXFIFORDY_DELAY * 1000);
		dstatus = bcmsdh_cfg_read_word(bus->sdh, SDIO_FUNC_0, SPID_STATUS_REG, NULL);
		if (dstatus & STATUS_F2_RX_READY)
			enable = TRUE;
	}
	if (enable) {
		DHD_ERROR(("Took %d retries before dongle is ready with delay %d(ms) in between\n",
			WAIT_F2RXFIFORDY - retries, WAIT_F2RXFIFORDY_DELAY));
		enable = ready;
	}
	else {
		DHD_ERROR(("dstatus when timed out on f2-fifo not ready = 0x%x\n", dstatus));
		DHD_ERROR(("Waited %d retries, dongle is not ready with delay %d(ms) in between\n",
			WAIT_F2RXFIFORDY, WAIT_F2RXFIFORDY_DELAY));
		ret = -1;
		goto exit;
	}
#else 
	
	saveclk = bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);
	if (!err) {
		bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
		                 (saveclk | SBSDIO_FORCE_HT), &err);
	}
	if (err) {
		DHD_ERROR(("%s: Failed to force clock for F2: err %d\n", __FUNCTION__, err));
		goto exit;
	}

	
	W_SDREG((SDPCM_PROT_VERSION << SMB_DATA_VERSION_SHIFT),
	        &bus->regs->tosbmailboxdata, retries);
	enable = (SDIO_FUNC_ENABLE_1 | SDIO_FUNC_ENABLE_2);

	bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_0, SDIOD_CCCR_IOEN, enable, NULL);

	
	retries = DHD_WAIT_F2RDY;
	while ((enable !=
	        ((ready = bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_0, SDIOD_CCCR_IORDY, NULL)))) &&
	       retries--) {
		OSL_DELAY(1000);
	}

#endif 

	retries = 0;

	DHD_INFO(("%s: enable 0x%02x, ready 0x%02x\n", __FUNCTION__, enable, ready));

#ifdef UNDER_CE
	bus->rxflow_mode = dhd_rxflow_mode(bus->dhd, 0, FALSE);
#endif 
	
	if (ready == enable) {
		
		if (!(bus->regs = si_setcore(bus->sih, PCMCIA_CORE_ID, 0)))
			bus->regs = si_setcore(bus->sih, SDIOD_CORE_ID, 0);

		
		bus->hostintmask = HOSTINTMASK;
		W_SDREG(bus->hostintmask, &bus->regs->hostintmask, retries);

		bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_WATERMARK, (uint8)watermark, &err);

		
		dhdp->busstate = DHD_BUS_DATA;

		

		bus->intdis = FALSE;
		if (bus->intr) {
			DHD_INTR(("%s: enable SDIO device interrupts\n", __FUNCTION__));
			bcmsdh_intr_enable(bus->sdh);
		} else {
			DHD_INTR(("%s: disable SDIO interrupts\n", __FUNCTION__));
			bcmsdh_intr_disable(bus->sdh);
	}

	}

#ifndef BCMSPI

	else {
		
		enable = SDIO_FUNC_ENABLE_1;
		bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_0, SDIOD_CCCR_IOEN, enable, NULL);
	}

	
	bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, saveclk, &err);

#endif 

	
	if (dhdp->busstate != DHD_BUS_DATA)
		dhdsdio_clkctl(bus, CLK_NONE, FALSE);
exit:
	if (enforce_mutex)
		dhd_os_sdunlock(bus->dhd);

	return ret;
}

static void
dhdsdio_rxfail(dhd_bus_t *bus, bool abort, bool rtx)
{
	bcmsdh_info_t *sdh = bus->sdh;
	sdpcmd_regs_t *regs = bus->regs;
	uint retries = 0;
	uint16 lastrbc;
	uint8 hi, lo;
	int err;

	DHD_ERROR(("%s: %sterminate frame%s\n", __FUNCTION__,
	           (abort ? "abort command, " : ""), (rtx ? ", send NAK" : "")));

	if (abort) {
		bcmsdh_abort(sdh, SDIO_FUNC_2);
	}

	bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_FRAMECTRL, SFC_RF_TERM, &err);
	bus->f1regdata++;

	
	for (lastrbc = retries = 0xffff; retries > 0; retries--) {
		hi = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_RFRAMEBCHI, NULL);
		lo = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_RFRAMEBCLO, NULL);
		bus->f1regdata += 2;

		if ((hi == 0) && (lo == 0))
			break;

		if ((hi > (lastrbc >> 8)) && (lo > (lastrbc & 0x00ff))) {
			DHD_ERROR(("%s: count growing: last 0x%04x now 0x%04x\n",
			           __FUNCTION__, lastrbc, ((hi << 8) + lo)));
		}
		lastrbc = (hi << 8) + lo;
	}

	if (!retries) {
		DHD_ERROR(("%s: count never zeroed: last 0x%04x\n", __FUNCTION__, lastrbc));
	} else {
		DHD_INFO(("%s: flush took %d iterations\n", __FUNCTION__, (0xffff - retries)));
	}

	if (rtx) {
		bus->rxrtx++;
		W_SDREG(SMB_NAK, &regs->tosbmailbox, retries);
		bus->f1regdata++;
		if (retries <= retry_limit) {
			bus->rxskip = TRUE;
		}
	}

	
	bus->nextlen = 0;

	
	if (err || bcmsdh_regfail(sdh))
		bus->dhd->busstate = DHD_BUS_DOWN;
}

static void
dhdsdio_read_control(dhd_bus_t *bus, uint8 *hdr, uint len, uint doff)
{
	bcmsdh_info_t *sdh = bus->sdh;
	uint rdlen, pad;

	int sdret;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	
	if ((bus->bus == SPI_BUS) && (!bus->usebufpool))
		goto gotpkt;

	ASSERT(bus->rxbuf);
	
	bus->rxctl = bus->rxbuf;
	if (dhd_alignctl) {
		bus->rxctl += firstread;
		if ((pad = ((uintptr)bus->rxctl % DHD_SDALIGN)))
			bus->rxctl += (DHD_SDALIGN - pad);
		bus->rxctl -= firstread;
	}
	ASSERT(bus->rxctl >= bus->rxbuf);

	
	bcopy(hdr, bus->rxctl, firstread);
	if (len <= firstread)
		goto gotpkt;

	
	if (bus->bus == SPI_BUS) {
		bcopy(hdr, bus->rxctl, len);
		goto gotpkt;
	}

	
	rdlen = len - firstread;
	if (bus->roundup && bus->blocksize && (rdlen > bus->blocksize)) {
		pad = bus->blocksize - (rdlen % bus->blocksize);
		if ((pad <= bus->roundup) && (pad < bus->blocksize) &&
		    ((len + pad) < bus->dhd->maxctl))
			rdlen += pad;
	} else if (rdlen % DHD_SDALIGN) {
		rdlen += DHD_SDALIGN - (rdlen % DHD_SDALIGN);
	}

	
	if (forcealign && (rdlen & (ALIGNMENT - 1)))
		rdlen = ROUNDUP(rdlen, ALIGNMENT);

	
	if ((rdlen + firstread) > bus->dhd->maxctl) {
		DHD_ERROR(("%s: %d-byte control read exceeds %d-byte buffer\n",
		           __FUNCTION__, rdlen, bus->dhd->maxctl));
		bus->dhd->rx_errors++;
		dhdsdio_rxfail(bus, FALSE, FALSE);
		goto done;
	}

	if ((len - doff) > bus->dhd->maxctl) {
		DHD_ERROR(("%s: %d-byte ctl frame (%d-byte ctl data) exceeds %d-byte limit\n",
		           __FUNCTION__, len, (len - doff), bus->dhd->maxctl));
		bus->dhd->rx_errors++; bus->rx_toolong++;
		dhdsdio_rxfail(bus, FALSE, FALSE);
		goto done;
	}


	
	sdret = dhd_bcmsdh_recv_buf(bus, bcmsdh_cur_sbwad(sdh), SDIO_FUNC_2, F2SYNC,
	                        (bus->rxctl + firstread), rdlen, NULL, NULL, NULL);
	bus->f2rxdata++;
	ASSERT(sdret != BCME_PENDING);

	
	if (sdret < 0) {
		DHD_ERROR(("%s: read %d control bytes failed: %d\n", __FUNCTION__, rdlen, sdret));
		bus->rxc_errors++; 
		dhdsdio_rxfail(bus, TRUE, TRUE);
		goto done;
	}

gotpkt:

#ifdef DHD_DEBUG
	if (DHD_BYTES_ON() && DHD_CTL_ON()) {
		prhex("RxCtrl", bus->rxctl, len);
	}
#endif

	
	bus->rxctl += doff;
	bus->rxlen = len - doff;

done:
	
	dhd_os_ioctl_resp_wake(bus->dhd);
}

static uint8
dhdsdio_rxglom(dhd_bus_t *bus, uint8 rxseq)
{
	uint16 dlen, totlen;
	uint8 *dptr, num = 0;

	uint16 sublen, check;
	void *pfirst, *plast, *pnext, *save_pfirst;
	osl_t *osh = bus->dhd->osh;

	int errcode;
	uint8 chan, seq, doff, sfdoff;
	uint8 txmax;

	int ifidx = 0;
	bool usechain = bus->use_rxchain;

	
	

	DHD_TRACE(("dhdsdio_rxglom: start: glomd %p glom %p\n", bus->glomd, bus->glom));

	
	if (bus->glomd) {
		dhd_os_sdlock_rxq(bus->dhd);

		pfirst = plast = pnext = NULL;
		dlen = (uint16)PKTLEN(osh, bus->glomd);
		dptr = PKTDATA(osh, bus->glomd);
		if (!dlen || (dlen & 1)) {
			DHD_ERROR(("%s: bad glomd len (%d), ignore descriptor\n",
			           __FUNCTION__, dlen));
			dlen = 0;
		}

		for (totlen = num = 0; dlen; num++) {
			
			sublen = ltoh16_ua(dptr);
			dlen -= sizeof(uint16);
			dptr += sizeof(uint16);
			if ((sublen < SDPCM_HDRLEN) ||
			    ((num == 0) && (sublen < (2 * SDPCM_HDRLEN)))) {
				DHD_ERROR(("%s: desciptor len %d bad: %d\n",
				           __FUNCTION__, num, sublen));
				pnext = NULL;
				break;
			}
			if (sublen % DHD_SDALIGN) {
				DHD_ERROR(("%s: sublen %d not a multiple of %d\n",
				           __FUNCTION__, sublen, DHD_SDALIGN));
				usechain = FALSE;
			}
			totlen += sublen;

			
			if (!dlen) {
				sublen += (ROUNDUP(totlen, bus->blocksize) - totlen);
				totlen = ROUNDUP(totlen, bus->blocksize);
			}

			
			if ((pnext = PKTGET(osh, sublen + DHD_SDALIGN, FALSE)) == NULL) {
				DHD_ERROR(("%s: PKTGET failed, num %d len %d\n",
				           __FUNCTION__, num, sublen));
				break;
			}
			ASSERT(!PKTLINK(pnext));
			if (!pfirst) {
				ASSERT(!plast);
				pfirst = plast = pnext;
			} else {
				ASSERT(plast);
				PKTSETNEXT(osh, plast, pnext);
				plast = pnext;
			}

			
			PKTALIGN(osh, pnext, sublen, DHD_SDALIGN);
		}

		
		if (pnext) {
			DHD_GLOM(("%s: allocated %d-byte packet chain for %d subframes\n",
			          __FUNCTION__, totlen, num));
			if (DHD_GLOM_ON() && bus->nextlen) {
				if (totlen != bus->nextlen) {
					DHD_GLOM(("%s: glomdesc mismatch: nextlen %d glomdesc %d "
					          "rxseq %d\n", __FUNCTION__, bus->nextlen,
					          totlen, rxseq));
				}
			}
			bus->glom = pfirst;
			pfirst = pnext = NULL;
		} else {
			if (pfirst)
				PKTFREE(osh, pfirst, FALSE);
			bus->glom = NULL;
			num = 0;
		}

		
		PKTFREE(osh, bus->glomd, FALSE);
		bus->glomd = NULL;
		bus->nextlen = 0;

		dhd_os_sdunlock_rxq(bus->dhd);
	}

	
	if (bus->glom) {
		if (DHD_GLOM_ON()) {
			DHD_GLOM(("%s: attempt superframe read, packet chain:\n", __FUNCTION__));
			for (pnext = bus->glom; pnext; pnext = PKTNEXT(osh, pnext)) {
				DHD_GLOM(("    %p: %p len 0x%04x (%d)\n",
				          pnext, (uint8*)PKTDATA(osh, pnext),
				          PKTLEN(osh, pnext), PKTLEN(osh, pnext)));
			}
		}

		pfirst = bus->glom;
		dlen = (uint16)pkttotlen(osh, pfirst);

		
		if (usechain) {
			errcode = dhd_bcmsdh_recv_buf(bus,
			bcmsdh_cur_sbwad(bus->sdh), SDIO_FUNC_2,
			                          F2SYNC, (uint8*)PKTDATA(osh, pfirst),
			                          dlen, pfirst, NULL, NULL);
		} else if (bus->dataptr) {
			errcode = dhd_bcmsdh_recv_buf(bus,
			bcmsdh_cur_sbwad(bus->sdh), SDIO_FUNC_2,
			                          F2SYNC, bus->dataptr,
			                          dlen, NULL, NULL, NULL);
			sublen = (uint16)pktfrombuf(osh, pfirst, 0, dlen, bus->dataptr);
			if (sublen != dlen) {
				DHD_ERROR(("%s: FAILED TO COPY, dlen %d sublen %d\n",
				           __FUNCTION__, dlen, sublen));
				errcode = -1;
			}
			pnext = NULL;
		} else {
			DHD_ERROR(("COULDN'T ALLOC %d-BYTE GLOM, FORCE FAILURE\n", dlen));
			errcode = -1;
		}
		bus->f2rxdata++;
		ASSERT(errcode != BCME_PENDING);

		
		if (errcode < 0) {
			DHD_ERROR(("%s: glom read of %d bytes failed: %d\n",
			           __FUNCTION__, dlen, errcode));
			bus->dhd->rx_errors++;

			if (bus->glomerr++ < 3) {
				dhdsdio_rxfail(bus, TRUE, TRUE);
			} else {
				bus->glomerr = 0;
				dhdsdio_rxfail(bus, TRUE, FALSE);
				dhd_os_sdlock_rxq(bus->dhd);
				PKTFREE(osh, bus->glom, FALSE);
				dhd_os_sdunlock_rxq(bus->dhd);
				bus->rxglomfail++;
				bus->glom = NULL;
			}
			return 0;
		}

#ifdef DHD_DEBUG
		if (DHD_GLOM_ON()) {
			prhex("SUPERFRAME", PKTDATA(osh, pfirst),
			      MIN(PKTLEN(osh, pfirst), 48));
		}
#endif


		
		dptr = (uint8 *)PKTDATA(osh, pfirst);
		sublen = ltoh16_ua(dptr);
		check = ltoh16_ua(dptr + sizeof(uint16));

		chan = SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN]);
		seq = SDPCM_PACKET_SEQUENCE(&dptr[SDPCM_FRAMETAG_LEN]);
		bus->nextlen = dptr[SDPCM_FRAMETAG_LEN + SDPCM_NEXTLEN_OFFSET];
		if ((bus->nextlen << 4) > MAX_RX_DATASZ) {
			DHD_INFO(("%s: got frame w/nextlen too large (%d) seq %d\n",
			          __FUNCTION__, bus->nextlen, seq));
			bus->nextlen = 0;
		}
		doff = SDPCM_DOFFSET_VALUE(&dptr[SDPCM_FRAMETAG_LEN]);
		txmax = SDPCM_WINDOW_VALUE(&dptr[SDPCM_FRAMETAG_LEN]);

		errcode = 0;
		if ((uint16)~(sublen^check)) {
			DHD_ERROR(("%s (superframe): HW hdr error: len/check 0x%04x/0x%04x\n",
			           __FUNCTION__, sublen, check));
			errcode = -1;
		} else if (ROUNDUP(sublen, bus->blocksize) != dlen) {
			DHD_ERROR(("%s (superframe): len 0x%04x, rounded 0x%04x, expect 0x%04x\n",
			           __FUNCTION__, sublen, ROUNDUP(sublen, bus->blocksize), dlen));
			errcode = -1;
		} else if (SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN]) != SDPCM_GLOM_CHANNEL) {
			DHD_ERROR(("%s (superframe): bad channel %d\n", __FUNCTION__,
			           SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN])));
			errcode = -1;
		} else if (SDPCM_GLOMDESC(&dptr[SDPCM_FRAMETAG_LEN])) {
			DHD_ERROR(("%s (superframe): got second descriptor?\n", __FUNCTION__));
			errcode = -1;
		} else if ((doff < SDPCM_HDRLEN) ||
		           (doff > (PKTLEN(osh, pfirst) - SDPCM_HDRLEN))) {
			DHD_ERROR(("%s (superframe): Bad data offset %d: HW %d pkt %d min %d\n",
			           __FUNCTION__, doff, sublen, PKTLEN(osh, pfirst), SDPCM_HDRLEN));
			errcode = -1;
		}

		
		if (rxseq != seq) {
			DHD_INFO(("%s: (superframe) rx_seq %d, expected %d\n",
			          __FUNCTION__, seq, rxseq));
			bus->rx_badseq++;
			rxseq = seq;
		}

		
		if ((uint8)(txmax - bus->tx_seq) > 0x40) {
			DHD_ERROR(("%s: got unlikely tx max %d with tx_seq %d\n",
			           __FUNCTION__, txmax, bus->tx_seq));
			txmax = bus->tx_seq + 2;
		}
		bus->tx_max = txmax;

		
		PKTPULL(osh, pfirst, doff);
		sfdoff = doff;

		
		for (num = 0, pnext = pfirst; pnext && !errcode;
		     num++, pnext = PKTNEXT(osh, pnext)) {
			dptr = (uint8 *)PKTDATA(osh, pnext);
			dlen = (uint16)PKTLEN(osh, pnext);
			sublen = ltoh16_ua(dptr);
			check = ltoh16_ua(dptr + sizeof(uint16));
			chan = SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN]);
			doff = SDPCM_DOFFSET_VALUE(&dptr[SDPCM_FRAMETAG_LEN]);
#ifdef DHD_DEBUG
			if (DHD_GLOM_ON()) {
				prhex("subframe", dptr, 32);
			}
#endif

			if ((uint16)~(sublen^check)) {
				DHD_ERROR(("%s (subframe %d): HW hdr error: "
				           "len/check 0x%04x/0x%04x\n",
				           __FUNCTION__, num, sublen, check));
				errcode = -1;
			} else if ((sublen > dlen) || (sublen < SDPCM_HDRLEN)) {
				DHD_ERROR(("%s (subframe %d): length mismatch: "
				           "len 0x%04x, expect 0x%04x\n",
				           __FUNCTION__, num, sublen, dlen));
				errcode = -1;
			} else if ((chan != SDPCM_DATA_CHANNEL) &&
			           (chan != SDPCM_EVENT_CHANNEL)) {
				DHD_ERROR(("%s (subframe %d): bad channel %d\n",
				           __FUNCTION__, num, chan));
				errcode = -1;
			} else if ((doff < SDPCM_HDRLEN) || (doff > sublen)) {
				DHD_ERROR(("%s (subframe %d): Bad data offset %d: HW %d min %d\n",
				           __FUNCTION__, num, doff, sublen, SDPCM_HDRLEN));
				errcode = -1;
			}
		}

		if (errcode) {
			
			if (bus->glomerr++ < 3) {
				
				PKTPUSH(osh, pfirst, sfdoff);
				dhdsdio_rxfail(bus, TRUE, TRUE);
			} else {
				bus->glomerr = 0;
				dhdsdio_rxfail(bus, TRUE, FALSE);
				dhd_os_sdlock_rxq(bus->dhd);
				PKTFREE(osh, bus->glom, FALSE);
				dhd_os_sdunlock_rxq(bus->dhd);
				bus->rxglomfail++;
				bus->glom = NULL;
			}
			bus->nextlen = 0;
			return 0;
		}

		
		save_pfirst = pfirst;
		bus->glom = NULL;
		plast = NULL;

		dhd_os_sdlock_rxq(bus->dhd);
		for (num = 0; pfirst; rxseq++, pfirst = pnext) {
			pnext = PKTNEXT(osh, pfirst);
			PKTSETNEXT(osh, pfirst, NULL);

			dptr = (uint8 *)PKTDATA(osh, pfirst);
			sublen = ltoh16_ua(dptr);
			chan = SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN]);
			seq = SDPCM_PACKET_SEQUENCE(&dptr[SDPCM_FRAMETAG_LEN]);
			doff = SDPCM_DOFFSET_VALUE(&dptr[SDPCM_FRAMETAG_LEN]);

			DHD_GLOM(("%s: Get subframe %d, %p(%p/%d), sublen %d chan %d seq %d\n",
			          __FUNCTION__, num, pfirst, PKTDATA(osh, pfirst),
			          PKTLEN(osh, pfirst), sublen, chan, seq));

			ASSERT((chan == SDPCM_DATA_CHANNEL) || (chan == SDPCM_EVENT_CHANNEL));

			if (rxseq != seq) {
				DHD_GLOM(("%s: rx_seq %d, expected %d\n",
				          __FUNCTION__, seq, rxseq));
				bus->rx_badseq++;
				rxseq = seq;
			}

#ifdef DHD_DEBUG
			if (DHD_BYTES_ON() && DHD_DATA_ON()) {
				prhex("Rx Subframe Data", dptr, dlen);
			}
#endif

			PKTSETLEN(osh, pfirst, sublen);
			PKTPULL(osh, pfirst, doff);

			if (PKTLEN(osh, pfirst) == 0) {
				PKTFREE(bus->dhd->osh, pfirst, FALSE);
				if (plast) {
					PKTSETNEXT(osh, plast, pnext);
				} else {
					ASSERT(save_pfirst == pfirst);
					save_pfirst = pnext;
				}
				continue;
			} else if (dhd_prot_hdrpull(bus->dhd, &ifidx, pfirst) != 0) {
				DHD_ERROR(("%s: rx protocol error\n", dhd_ifname(bus->dhd, ifidx)));
				bus->dhd->rx_errors++;
				PKTFREE(osh, pfirst, FALSE);
				if (plast) {
					PKTSETNEXT(osh, plast, pnext);
				} else {
					ASSERT(save_pfirst == pfirst);
					save_pfirst = pnext;
				}
				continue;
			}

			
			PKTSETNEXT(osh, pfirst, pnext);
			plast = pfirst;
			num++;

#ifdef DHD_DEBUG
			if (DHD_GLOM_ON()) {
				DHD_GLOM(("%s subframe %d to stack, %p(%p/%d) nxt/lnk %p/%p\n",
				          __FUNCTION__, num, pfirst,
				          PKTDATA(osh, pfirst), PKTLEN(osh, pfirst),
				          PKTNEXT(osh, pfirst), PKTLINK(pfirst)));
				prhex("", (uint8 *)PKTDATA(osh, pfirst),
				      MIN(PKTLEN(osh, pfirst), 32));
			}
#endif 
		}
		dhd_os_sdunlock_rxq(bus->dhd);
#ifdef UNDER_CE
		if (num) {
			dhd_rx_frame(bus->dhd, ifidx, save_pfirst, num);
			dhd_sendup_indicate(bus->dhd);
		}
#else
		if (num) {
			dhd_os_sdunlock(bus->dhd);
			dhd_rx_frame(bus->dhd, ifidx, save_pfirst, num);
			dhd_os_sdlock(bus->dhd);
		}
#endif 
		bus->rxglomframes++;
		bus->rxglompkts += num;
	}
	return num;
}


static uint
dhdsdio_readframes(dhd_bus_t *bus, uint maxframes, bool *finished)
{
	osl_t *osh = bus->dhd->osh;
	bcmsdh_info_t *sdh = bus->sdh;

	uint16 len, check;	
	uint8 chan, seq, doff;	
	uint8 fcbits;		
	uint8 delta;

	void *pkt;	
	uint16 pad;	
	uint16 rdlen;	
	uint8 rxseq;	
	uint rxleft = 0;	
	int sdret;	
	uint8 txmax;	
#ifdef BCMSPI
	uint32 dstatus = 0;	
#endif 
	bool len_consistent; 
	uint8 *rxbuf;
	int ifidx = 0;
	uint rxcount = 0; 

#if defined(DHD_DEBUG) || defined(SDTEST)
	bool sdtest = FALSE;	
#endif

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(maxframes);

#ifdef SDTEST
	
	if (bus->pktgen_count && (bus->pktgen_mode == DHD_PKTGEN_RECV)) {
		maxframes = bus->pktgen_count;
		sdtest = TRUE;
	}
#endif

	
	*finished = FALSE;

#ifdef BCMSPI
	
	if (bus->bus == SPI_BUS) {
		
		dstatus = bcmsdh_get_dstatus(bus->sdh);
		if (dstatus == 0)
			DHD_ERROR(("%s:ZERO spi dstatus, a case observed in PR61352 hit !!!\n",
			           __FUNCTION__));

		if ((bus->sih->chip == BCM4329_CHIP_ID) && (bus->sih->chiprev == 0)) {
			uint32 dstatus2 = bcmsdh_cfg_read_word(bus->sdh, SDIO_FUNC_0,
			                  SPID_STATUS_REG, NULL);
			uint16 len =
			          (dstatus & STATUS_F2_PKT_LEN_MASK) >> STATUS_F2_PKT_LEN_SHIFT;
			uint16 len2 =
			          (dstatus2 & STATUS_F2_PKT_LEN_MASK) >> STATUS_F2_PKT_LEN_SHIFT;
			if (len != len2) {
				DHD_ERROR(("2nd piggybacked dstatus = 0x%x and 2nd F0 dstatus"
				           "= 0x%x\n", dstatus, dstatus2));
				DHD_ERROR(("2nd piggybacked len = %d and 2nd F0 len = %d\n",
				          len, len2));
				dstatus = dstatus2;
			}
		}

		DHD_TRACE(("Device status from regread = 0x%x\n", dstatus));
		DHD_TRACE(("Device status from bit-reconstruction = 0x%x\n",
		          bcmsdh_get_dstatus((void *)bus->sdh)));

		if ((dstatus & STATUS_F2_PKT_AVAILABLE) && (((dstatus & STATUS_UNDERFLOW)) == 0)) {
			bus->nextlen = ((dstatus & STATUS_F2_PKT_LEN_MASK) >>
			                STATUS_F2_PKT_LEN_SHIFT);
			
			bus->nextlen = (bus->nextlen == 0) ? SPI_MAX_PKT_LEN : bus->nextlen;
			DHD_INFO(("Entering %s: length to be read from gSPI = %d\n",
			          __FUNCTION__, bus->nextlen));
		} else {
			if (dstatus & STATUS_F2_PKT_AVAILABLE)
				DHD_ERROR(("Underflow during %s.\n", __FUNCTION__));
			else
				DHD_ERROR(("False pkt-available intr.\n"));
			*finished = TRUE;
			return (maxframes - rxleft);
		}
	}
#endif 

	for (rxseq = bus->rx_seq, rxleft = maxframes;
	     !bus->rxskip && rxleft && bus->dhd->busstate != DHD_BUS_DOWN;
	     rxseq++, rxleft--) {

		
		if (bus->glom || bus->glomd) {
			uint8 cnt;
			DHD_GLOM(("%s: calling rxglom: glomd %p, glom %p\n",
			          __FUNCTION__, bus->glomd, bus->glom));
			cnt = dhdsdio_rxglom(bus, rxseq);
			DHD_GLOM(("%s: rxglom returned %d\n", __FUNCTION__, cnt));
			rxseq += cnt - 1;
			rxleft = (rxleft > cnt) ? (rxleft - cnt) : 1;
			continue;
		}

		
		if (dhd_readahead && bus->nextlen) {
			uint16 nextlen = bus->nextlen;
			bus->nextlen = 0;

			if (bus->bus == SPI_BUS) {
				rdlen = len = nextlen;
			}
			else {
				rdlen = len = nextlen << 4;

				
				if (bus->roundup && bus->blocksize && (rdlen > bus->blocksize)) {
					pad = bus->blocksize - (rdlen % bus->blocksize);
					if ((pad <= bus->roundup) && (pad < bus->blocksize) &&
						((rdlen + pad + firstread) < MAX_RX_DATASZ))
						rdlen += pad;

				} else if (rdlen % DHD_SDALIGN) {
					rdlen += DHD_SDALIGN - (rdlen % DHD_SDALIGN);
				}
			}

			
			
			dhd_os_sdlock_rxq(bus->dhd);
			if (!(pkt = PKTGET(osh, rdlen + DHD_SDALIGN, FALSE))) {
				if (bus->bus == SPI_BUS) {
					bus->usebufpool = FALSE;
					bus->rxctl = bus->rxbuf;
					if (dhd_alignctl) {
						bus->rxctl += firstread;
						if ((pad = ((uintptr)bus->rxctl % DHD_SDALIGN)))
							bus->rxctl += (DHD_SDALIGN - pad);
						bus->rxctl -= firstread;
					}
					ASSERT(bus->rxctl >= bus->rxbuf);
					rxbuf = bus->rxctl;
					
					sdret = dhd_bcmsdh_recv_buf(bus,
					bcmsdh_cur_sbwad(sdh), SDIO_FUNC_2,
					           F2SYNC, rxbuf, rdlen, NULL, NULL, NULL);
					bus->f2rxdata++;
					ASSERT(sdret != BCME_PENDING);

#ifdef BCMSPI
					if (bcmsdh_get_dstatus((void *)bus->sdh) &
					                STATUS_UNDERFLOW) {
						bus->nextlen = 0;
						*finished = TRUE;
						DHD_ERROR(("%s: read %d control bytes failed "
						           "due to spi underflow\n",
						           __FUNCTION__, rdlen));
						
						bus->rxc_errors++;
						dhd_os_sdunlock_rxq(bus->dhd);
						continue;
					}
#endif 

					
					if (sdret < 0) {
						DHD_ERROR(("%s: read %d control bytes failed: %d\n",
						   __FUNCTION__, rdlen, sdret));
						
						bus->rxc_errors++;
						dhd_os_sdunlock_rxq(bus->dhd);
						dhdsdio_rxfail(bus, TRUE,
						    (bus->bus == SPI_BUS) ? FALSE : TRUE);
						continue;
					}
				} else {
					
					DHD_ERROR(("%s (nextlen): PKTGET failed: len %d rdlen %d "
					           "expected rxseq %d\n",
					           __FUNCTION__, len, rdlen, rxseq));
					
					dhd_os_sdunlock_rxq(bus->dhd);
					continue;
				}
			} else {
				if (bus->bus == SPI_BUS)
					bus->usebufpool = TRUE;

				ASSERT(!PKTLINK(pkt));
				PKTALIGN(osh, pkt, rdlen, DHD_SDALIGN);
				rxbuf = (uint8 *)PKTDATA(osh, pkt);
				
				sdret = dhd_bcmsdh_recv_buf(bus, bcmsdh_cur_sbwad(sdh), SDIO_FUNC_2,
					F2SYNC, rxbuf, rdlen, pkt, NULL, NULL);
				bus->f2rxdata++;
				ASSERT(sdret != BCME_PENDING);
#ifdef BCMSPI
				if (bcmsdh_get_dstatus((void *)bus->sdh) & STATUS_UNDERFLOW) {
					bus->nextlen = 0;
					*finished = TRUE;
					DHD_ERROR(("%s (nextlen): read %d bytes failed due "
					           "to spi underflow\n",
					           __FUNCTION__, rdlen));
					PKTFREE(bus->dhd->osh, pkt, FALSE);
					bus->dhd->rx_errors++;
					dhd_os_sdunlock_rxq(bus->dhd);
					continue;
				}
#endif 

				if (sdret < 0) {
					DHD_ERROR(("%s (nextlen): read %d bytes failed: %d\n",
					   __FUNCTION__, rdlen, sdret));
					PKTFREE(bus->dhd->osh, pkt, FALSE);
					bus->dhd->rx_errors++;
					dhd_os_sdunlock_rxq(bus->dhd);
					
					dhdsdio_rxfail(bus, TRUE,
					      (bus->bus == SPI_BUS) ? FALSE : TRUE);
					continue;
				}
			}
			dhd_os_sdunlock_rxq(bus->dhd);

			
			bcopy(rxbuf, bus->rxhdr, SDPCM_HDRLEN);

			
			len = ltoh16_ua(bus->rxhdr);
			check = ltoh16_ua(bus->rxhdr + sizeof(uint16));

			
			if (!(len|check)) {
				DHD_INFO(("%s (nextlen): read zeros in HW header???\n",
				           __FUNCTION__));
				dhd_os_sdlock_rxq(bus->dhd);
				PKTFREE2();
				dhd_os_sdunlock_rxq(bus->dhd);
				GSPI_PR55150_BAILOUT;
				continue;
			}

			
			if ((uint16)~(len^check)) {
				DHD_ERROR(("%s (nextlen): HW hdr error: nextlen/len/check"
				           " 0x%04x/0x%04x/0x%04x\n", __FUNCTION__, nextlen,
				           len, check));
				dhd_os_sdlock_rxq(bus->dhd);
				PKTFREE2();
				dhd_os_sdunlock_rxq(bus->dhd);
				bus->rx_badhdr++;
				dhdsdio_rxfail(bus, FALSE, FALSE);
				GSPI_PR55150_BAILOUT;
				continue;
			}

			
			if (len < SDPCM_HDRLEN) {
				DHD_ERROR(("%s (nextlen): HW hdr length invalid: %d\n",
				           __FUNCTION__, len));
				dhd_os_sdlock_rxq(bus->dhd);
				PKTFREE2();
				dhd_os_sdunlock_rxq(bus->dhd);
				GSPI_PR55150_BAILOUT;
				continue;
			}

			
			if (bus->bus == SPI_BUS)
				len_consistent = (nextlen != len);
			else
				len_consistent = (nextlen != (ROUNDUP(len, 16) >> 4));
			if (len_consistent) {
				
				DHD_ERROR(("%s (nextlen): mismatch, nextlen %d len %d rnd %d; "
				           "expected rxseq %d\n",
				           __FUNCTION__, nextlen, len, ROUNDUP(len, 16), rxseq));
				dhd_os_sdlock_rxq(bus->dhd);
				PKTFREE2();
				dhd_os_sdunlock_rxq(bus->dhd);
				dhdsdio_rxfail(bus, TRUE, (bus->bus == SPI_BUS) ? FALSE : TRUE);
				GSPI_PR55150_BAILOUT;
				continue;
			}


			
			chan = SDPCM_PACKET_CHANNEL(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
			seq = SDPCM_PACKET_SEQUENCE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
			doff = SDPCM_DOFFSET_VALUE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
			txmax = SDPCM_WINDOW_VALUE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);

#ifdef BCMSPI
			
			if (bus->bus == SPI_BUS) {
				
				dstatus = bcmsdh_get_dstatus((void *)bus->sdh);
				DHD_INFO(("Device status from bit-reconstruction = 0x%x\n",
				bcmsdh_get_dstatus((void *)bus->sdh)));
				if (dstatus & STATUS_F2_PKT_AVAILABLE) {
					bus->nextlen = ((dstatus & STATUS_F2_PKT_LEN_MASK) >>
					                STATUS_F2_PKT_LEN_SHIFT);
					bus->nextlen = (bus->nextlen == 0) ?
					           SPI_MAX_PKT_LEN : bus->nextlen;
					DHD_INFO(("readahead len from gSPI = %d \n",
					           bus->nextlen));
					bus->dhd->rx_readahead_cnt ++;
				} else {
					bus->nextlen = 0;
					*finished = TRUE;
				}
			} else {
#endif 
				bus->nextlen =
				         bus->rxhdr[SDPCM_FRAMETAG_LEN + SDPCM_NEXTLEN_OFFSET];
				if ((bus->nextlen << 4) > MAX_RX_DATASZ) {
					DHD_INFO(("%s (nextlen): got frame w/nextlen too large"
					          " (%d), seq %d\n", __FUNCTION__, bus->nextlen,
					          seq));
					bus->nextlen = 0;
				}

				bus->dhd->rx_readahead_cnt ++;
#ifdef BCMSPI
			}
#endif 
			
			fcbits = SDPCM_FCMASK_VALUE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);

			delta = 0;
			if (~bus->flowcontrol & fcbits) {
				bus->fc_xoff++;
				delta = 1;
			}
			if (bus->flowcontrol & ~fcbits) {
				bus->fc_xon++;
				delta = 1;
			}

			if (delta) {
				bus->fc_rcvd++;
				bus->flowcontrol = fcbits;
			}

			
			if (rxseq != seq) {
				DHD_INFO(("%s (nextlen): rx_seq %d, expected %d\n",
				          __FUNCTION__, seq, rxseq));
				bus->rx_badseq++;
				rxseq = seq;
			}

			
			if ((uint8)(txmax - bus->tx_seq) > 0x40) {
#ifdef BCMSPI
				if ((bus->bus == SPI_BUS) && !(dstatus & STATUS_F2_RX_READY)) {
					DHD_ERROR(("%s: got unlikely tx max %d with tx_seq %d\n",
						__FUNCTION__, txmax, bus->tx_seq));
					txmax = bus->tx_seq + 2;
				} else {
#endif 
					DHD_ERROR(("%s: got unlikely tx max %d with tx_seq %d\n",
						__FUNCTION__, txmax, bus->tx_seq));
					txmax = bus->tx_seq + 2;
#ifdef BCMSPI
				}
#endif 
			}
			bus->tx_max = txmax;

#ifdef DHD_DEBUG
			if (DHD_BYTES_ON() && DHD_DATA_ON()) {
				prhex("Rx Data", rxbuf, len);
			} else if (DHD_HDRS_ON()) {
				prhex("RxHdr", bus->rxhdr, SDPCM_HDRLEN);
			}
#endif

			if (chan == SDPCM_CONTROL_CHANNEL) {
				if (bus->bus == SPI_BUS) {
					dhdsdio_read_control(bus, rxbuf, len, doff);
					if (bus->usebufpool) {
						dhd_os_sdlock_rxq(bus->dhd);
						PKTFREE(bus->dhd->osh, pkt, FALSE);
						dhd_os_sdunlock_rxq(bus->dhd);
					}
					continue;
				} else {
					DHD_ERROR(("%s (nextlen): readahead on control"
					           " packet %d?\n", __FUNCTION__, seq));
					
					bus->nextlen = 0;
					dhdsdio_rxfail(bus, FALSE, TRUE);
					dhd_os_sdlock_rxq(bus->dhd);
					PKTFREE2();
					dhd_os_sdunlock_rxq(bus->dhd);
					continue;
				}
			}

			if ((bus->bus == SPI_BUS) && !bus->usebufpool) {
				DHD_ERROR(("Received %d bytes on %d channel. Running out of "
				           "rx pktbuf's or not yet malloced.\n", len, chan));
				continue;
			}

			
			if ((doff < SDPCM_HDRLEN) || (doff > len)) {
				DHD_ERROR(("%s (nextlen): bad data offset %d: HW len %d min %d\n",
				           __FUNCTION__, doff, len, SDPCM_HDRLEN));
				dhd_os_sdlock_rxq(bus->dhd);
				PKTFREE2();
				dhd_os_sdunlock_rxq(bus->dhd);
				ASSERT(0);
				dhdsdio_rxfail(bus, FALSE, FALSE);
				continue;
			}

			
			goto deliver;
		}
		
		if (bus->bus == SPI_BUS) {
			break;
		}

		
		sdret = dhd_bcmsdh_recv_buf(bus, bcmsdh_cur_sbwad(sdh), SDIO_FUNC_2, F2SYNC,
		                        bus->rxhdr, firstread, NULL, NULL, NULL);
		bus->f2rxhdrs++;
		ASSERT(sdret != BCME_PENDING);

		if (sdret < 0) {
			DHD_ERROR(("%s: RXHEADER FAILED: %d\n", __FUNCTION__, sdret));
			bus->rx_hdrfail++;
			dhdsdio_rxfail(bus, TRUE, TRUE);
			continue;
		}

#ifdef DHD_DEBUG
		if (DHD_BYTES_ON() || DHD_HDRS_ON()) {
			prhex("RxHdr", bus->rxhdr, SDPCM_HDRLEN);
		}
#endif

		
		len = ltoh16_ua(bus->rxhdr);
		check = ltoh16_ua(bus->rxhdr + sizeof(uint16));

		
		if (!(len|check)) {
			*finished = TRUE;
			break;
		}

		
		if ((uint16)~(len^check)) {
			DHD_ERROR(("%s: HW hdr error: len/check 0x%04x/0x%04x\n",
			           __FUNCTION__, len, check));
			bus->rx_badhdr++;
			dhdsdio_rxfail(bus, FALSE, FALSE);
			continue;
		}

		
		if (len < SDPCM_HDRLEN) {
			DHD_ERROR(("%s: HW hdr length invalid: %d\n", __FUNCTION__, len));
			continue;
		}

		
		chan = SDPCM_PACKET_CHANNEL(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
		seq = SDPCM_PACKET_SEQUENCE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
		doff = SDPCM_DOFFSET_VALUE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
		txmax = SDPCM_WINDOW_VALUE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);

		
		if ((doff < SDPCM_HDRLEN) || (doff > len)) {
			DHD_ERROR(("%s: Bad data offset %d: HW len %d, min %d seq %d\n",
			           __FUNCTION__, doff, len, SDPCM_HDRLEN, seq));
			bus->rx_badhdr++;
			ASSERT(0);
			dhdsdio_rxfail(bus, FALSE, FALSE);
			continue;
		}

		
		bus->nextlen = bus->rxhdr[SDPCM_FRAMETAG_LEN + SDPCM_NEXTLEN_OFFSET];
		if ((bus->nextlen << 4) > MAX_RX_DATASZ) {
			DHD_INFO(("%s (nextlen): got frame w/nextlen too large (%d), seq %d\n",
			          __FUNCTION__, bus->nextlen, seq));
			bus->nextlen = 0;
		}

		
		fcbits = SDPCM_FCMASK_VALUE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);

		delta = 0;
		if (~bus->flowcontrol & fcbits) {
			bus->fc_xoff++;
			delta = 1;
		}
		if (bus->flowcontrol & ~fcbits) {
			bus->fc_xon++;
			delta = 1;
		}

		if (delta) {
			bus->fc_rcvd++;
			bus->flowcontrol = fcbits;
		}

		
		if (rxseq != seq) {
			DHD_INFO(("%s: rx_seq %d, expected %d\n", __FUNCTION__, seq, rxseq));
			bus->rx_badseq++;
			rxseq = seq;
		}

		
		if ((uint8)(txmax - bus->tx_seq) > 0x40) {
			DHD_ERROR(("%s: got unlikely tx max %d with tx_seq %d\n",
			           __FUNCTION__, txmax, bus->tx_seq));
			txmax = bus->tx_seq + 2;
		}
		bus->tx_max = txmax;

		
		if (chan == SDPCM_CONTROL_CHANNEL) {
			dhdsdio_read_control(bus, bus->rxhdr, len, doff);
			continue;
		}

		ASSERT((chan == SDPCM_DATA_CHANNEL) || (chan == SDPCM_EVENT_CHANNEL) ||
		       (chan == SDPCM_TEST_CHANNEL) || (chan == SDPCM_GLOM_CHANNEL));

		
		rdlen = (len > firstread) ? (len - firstread) : 0;

		
		if (bus->roundup && bus->blocksize && (rdlen > bus->blocksize)) {
			pad = bus->blocksize - (rdlen % bus->blocksize);
			if ((pad <= bus->roundup) && (pad < bus->blocksize) &&
			    ((rdlen + pad + firstread) < MAX_RX_DATASZ))
				rdlen += pad;
		} else if (rdlen % DHD_SDALIGN) {
			rdlen += DHD_SDALIGN - (rdlen % DHD_SDALIGN);
		}

		
		if (forcealign && (rdlen & (ALIGNMENT - 1)))
			rdlen = ROUNDUP(rdlen, ALIGNMENT);

		if ((rdlen + firstread) > MAX_RX_DATASZ) {
			
			DHD_ERROR(("%s: too long: len %d rdlen %d\n", __FUNCTION__, len, rdlen));
			bus->dhd->rx_errors++; bus->rx_toolong++;
			dhdsdio_rxfail(bus, FALSE, FALSE);
			continue;
		}

		dhd_os_sdlock_rxq(bus->dhd);
		if (!(pkt = PKTGET(osh, (rdlen + firstread + DHD_SDALIGN), FALSE))) {
			
			DHD_ERROR(("%s: PKTGET failed: rdlen %d chan %d\n",
			           __FUNCTION__, rdlen, chan));
			bus->dhd->rx_dropped++;
			dhd_os_sdunlock_rxq(bus->dhd);
			dhdsdio_rxfail(bus, FALSE, RETRYCHAN(chan));
			continue;
		}
		dhd_os_sdunlock_rxq(bus->dhd);

		ASSERT(!PKTLINK(pkt));

		
		ASSERT(firstread < (PKTLEN(osh, pkt)));
		PKTPULL(osh, pkt, firstread);
		PKTALIGN(osh, pkt, rdlen, DHD_SDALIGN);

		
		sdret = dhd_bcmsdh_recv_buf(bus, bcmsdh_cur_sbwad(sdh), SDIO_FUNC_2, F2SYNC,
		                        ((uint8 *)PKTDATA(osh, pkt)), rdlen, pkt, NULL, NULL);
		bus->f2rxdata++;
		ASSERT(sdret != BCME_PENDING);

		if (sdret < 0) {
			DHD_ERROR(("%s: read %d %s bytes failed: %d\n", __FUNCTION__, rdlen,
			           ((chan == SDPCM_EVENT_CHANNEL) ? "event" :
			            ((chan == SDPCM_DATA_CHANNEL) ? "data" : "test")), sdret));
			dhd_os_sdlock_rxq(bus->dhd);
			PKTFREE(bus->dhd->osh, pkt, FALSE);
			dhd_os_sdunlock_rxq(bus->dhd);
			bus->dhd->rx_errors++;
			dhdsdio_rxfail(bus, TRUE, RETRYCHAN(chan));
			continue;
		}

		
		PKTPUSH(osh, pkt, firstread);
		bcopy(bus->rxhdr, PKTDATA(osh, pkt), firstread);

#ifdef DHD_DEBUG
		if (DHD_BYTES_ON() && DHD_DATA_ON()) {
			prhex("Rx Data", PKTDATA(osh, pkt), len);
		}
#endif

deliver:
		
		if (chan == SDPCM_GLOM_CHANNEL) {
			if (SDPCM_GLOMDESC(&bus->rxhdr[SDPCM_FRAMETAG_LEN])) {
				DHD_GLOM(("%s: got glom descriptor, %d bytes:\n",
				          __FUNCTION__, len));
#ifdef DHD_DEBUG
				if (DHD_GLOM_ON()) {
					prhex("Glom Data", PKTDATA(osh, pkt), len);
				}
#endif
				PKTSETLEN(osh, pkt, len);
				ASSERT(doff == SDPCM_HDRLEN);
				PKTPULL(osh, pkt, SDPCM_HDRLEN);
				bus->glomd = pkt;
			} else {
				DHD_ERROR(("%s: glom superframe w/o descriptor!\n", __FUNCTION__));
				dhdsdio_rxfail(bus, FALSE, FALSE);
			}
			continue;
		}

		
		PKTSETLEN(osh, pkt, len);
		PKTPULL(osh, pkt, doff);

#ifdef SDTEST
		
		if (chan == SDPCM_TEST_CHANNEL) {
			dhdsdio_testrcv(bus, pkt, seq);
			continue;
		}
#endif 

		if (PKTLEN(osh, pkt) == 0) {
			dhd_os_sdlock_rxq(bus->dhd);
			PKTFREE(bus->dhd->osh, pkt, FALSE);
			dhd_os_sdunlock_rxq(bus->dhd);
			continue;
		} else if (dhd_prot_hdrpull(bus->dhd, &ifidx, pkt) != 0) {
			DHD_ERROR(("%s: rx protocol error\n", dhd_ifname(bus->dhd, ifidx)));
			dhd_os_sdlock_rxq(bus->dhd);
			PKTFREE(bus->dhd->osh, pkt, FALSE);
			dhd_os_sdunlock_rxq(bus->dhd);
			bus->dhd->rx_errors++;
			continue;
		}


		
#if !defined(UNDER_CE)
		dhd_os_sdunlock(bus->dhd);
#endif 
		dhd_rx_frame(bus->dhd, ifidx, pkt, 1);
#if !defined(UNDER_CE)
		dhd_os_sdlock(bus->dhd);
#endif 
	}
	 rxcount = maxframes - rxleft;
#ifdef UNDER_CE
	if (rxcount) {
		dhd_sendup_indicate(bus->dhd);
	}
#endif 
#ifdef DHD_DEBUG
	
	if (!rxleft && !sdtest)
		DHD_DATA(("%s: hit rx limit of %d frames\n", __FUNCTION__, maxframes));
	else
#endif 
	DHD_DATA(("%s: processed %d frames\n", __FUNCTION__, rxcount));
#ifdef UNDER_CE
	if (bus->rxflow_mode == RX_FLOW_CONTROL_RXBOUND) {
		if (rxcount >= maxframes && !bus->rxflow) {
			
			DHD_INFO(("%s: rx flow ON\n", __FUNCTION__));
			bus->rxflow = TRUE;
			dhd_rx_flow((void *)bus->dhd, TRUE);
		} else if (rxcount < maxframes &&
			((bus->prev_rxlim_hit & 0xf) == 0) && bus->rxflow) {
			
			DHD_INFO(("%s: rx flow OFF\n", __FUNCTION__));
			bus->rxflow = FALSE;
			dhd_rx_flow((void *)bus->dhd, FALSE);
		} else {
			
		}
		
		if (rxcount >= maxframes) {
			bus->prev_rxlim_hit = (((bus->prev_rxlim_hit << 1) | 1) & 0xf);
		} else {
			bus->prev_rxlim_hit = ((bus->prev_rxlim_hit << 1) & 0xf);
		}
	}
#endif 
	
	if (bus->rxskip)
		rxseq--;
	bus->rx_seq = rxseq;

	return rxcount;
}

static uint32
dhdsdio_hostmail(dhd_bus_t *bus)
{
	sdpcmd_regs_t *regs = bus->regs;
	uint32 intstatus = 0;
	uint32 hmb_data;
	uint8 fcbits;
	uint retries = 0;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	
	R_SDREG(hmb_data, &regs->tohostmailboxdata, retries);
	if (retries <= retry_limit)
		W_SDREG(SMB_INT_ACK, &regs->tosbmailbox, retries);
	bus->f1regdata += 2;

	
	if (hmb_data & HMB_DATA_NAKHANDLED) {
		DHD_INFO(("Dongle reports NAK handled, expect rtx of %d\n", bus->rx_seq));
		if (!bus->rxskip) {
			DHD_ERROR(("%s: unexpected NAKHANDLED!\n", __FUNCTION__));
		}
		bus->rxskip = FALSE;
		intstatus |= I_HMB_FRAME_IND;
	}

	
	if (hmb_data & (HMB_DATA_DEVREADY | HMB_DATA_FWREADY)) {
		bus->sdpcm_ver = (hmb_data >> HMB_DATA_VERSION_SHIFT) & HMB_DATA_VERSION_MASK;
		if (bus->sdpcm_ver != SDPCM_PROT_VERSION)
			DHD_ERROR(("Version mismatch, dongle reports %d, expecting %d\n",
			           bus->sdpcm_ver, SDPCM_PROT_VERSION));
		else
			DHD_INFO(("Dongle ready, protocol version %d\n", bus->sdpcm_ver));
	}

	
	if (hmb_data & HMB_DATA_FC) {
		fcbits = (hmb_data >> HMB_DATA_FCDATA_SHIFT) & HMB_DATA_FCDATA_MASK;

		if (fcbits & ~bus->flowcontrol)
			bus->fc_xoff++;
		if (bus->flowcontrol & ~fcbits)
			bus->fc_xon++;

		bus->fc_rcvd++;
		bus->flowcontrol = fcbits;
	}

	
	if (hmb_data & ~(HMB_DATA_DEVREADY |
	                 HMB_DATA_NAKHANDLED |
	                 HMB_DATA_FC |
	                 HMB_DATA_FWREADY |
	                 (HMB_DATA_FCDATA_MASK << HMB_DATA_FCDATA_SHIFT) |
	                 (HMB_DATA_VERSION_MASK << HMB_DATA_VERSION_SHIFT))) {
		DHD_ERROR(("Unknown mailbox data content: 0x%02x\n", hmb_data));
	}

	return intstatus;
}

bool
dhdsdio_dpc(dhd_bus_t *bus)
{
	bcmsdh_info_t *sdh = bus->sdh;
	sdpcmd_regs_t *regs = bus->regs;
	uint32 intstatus, newstatus = 0;
	uint retries = 0;
	uint rxlimit = dhd_rxbound; 
	uint txlimit = dhd_txbound; 
	uint framecnt = 0;		  
	bool rxdone = TRUE;		  
	bool resched = FALSE;	  

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	
	intstatus = bus->intstatus;

	dhd_os_sdlock(bus->dhd);

	
	if (bus->clkstate == CLK_PENDING) {
		int err;
		uint8 clkctl, devctl = 0;

#ifdef DHD_DEBUG
		
		devctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, &err);
		if (err) {
			DHD_ERROR(("%s: error reading DEVCTL: %d\n", __FUNCTION__, err));
			bus->dhd->busstate = DHD_BUS_DOWN;
		}
		ASSERT(devctl & SBSDIO_DEVCTL_CA_INT_ONLY);
#endif 

		
		clkctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);
		if (err) {
			DHD_ERROR(("%s: error reading CSR: %d\n", __FUNCTION__, err));
			bus->dhd->busstate = DHD_BUS_DOWN;
		}

		DHD_INFO(("DPC: PENDING, devctl 0x%02x clkctl 0x%02x\n", devctl, clkctl));

		if (SBSDIO_HTAV(clkctl)) {
			devctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, &err);
			if (err) {
				DHD_ERROR(("%s: error reading DEVCTL: %d\n",
				           __FUNCTION__, err));
				bus->dhd->busstate = DHD_BUS_DOWN;
			}
			devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, devctl, &err);
			if (err) {
				DHD_ERROR(("%s: error writing DEVCTL: %d\n",
				           __FUNCTION__, err));
				bus->dhd->busstate = DHD_BUS_DOWN;
			}
			bus->clkstate = CLK_AVAIL;
		} else {
			goto clkwait;
		}
	}

	BUS_WAKE(bus);

	
	dhdsdio_clkctl(bus, CLK_AVAIL, TRUE);
	if (bus->clkstate == CLK_PENDING)
		goto clkwait;

	
	if (bus->ipend) {
		bus->ipend = FALSE;
		R_SDREG(newstatus, &regs->intstatus, retries);
		bus->f1regdata++;
		if (bcmsdh_regfail(bus->sdh))
			newstatus = 0;
		newstatus &= bus->hostintmask;
		bus->fcstate = !!(newstatus & I_HMB_FC_STATE);
		if (newstatus) {
			W_SDREG(newstatus, &regs->intstatus, retries);
			bus->f1regdata++;
		}
	}

	
	intstatus |= newstatus;
	bus->intstatus = 0;

	
	if (intstatus & I_HMB_FC_CHANGE) {
		intstatus &= ~I_HMB_FC_CHANGE;
		W_SDREG(I_HMB_FC_CHANGE, &regs->intstatus, retries);
		R_SDREG(newstatus, &regs->intstatus, retries);
		bus->f1regdata += 2;
		bus->fcstate = !!(newstatus & (I_HMB_FC_STATE | I_HMB_FC_CHANGE));
		intstatus |= (newstatus & bus->hostintmask);
	}

	
	if (intstatus & I_HMB_HOST_INT) {
		intstatus &= ~I_HMB_HOST_INT;
		intstatus |= dhdsdio_hostmail(bus);
	}

	
	if (intstatus & I_WR_OOSYNC) {
		DHD_ERROR(("Dongle reports WR_OOSYNC\n"));
		intstatus &= ~I_WR_OOSYNC;
	}

	if (intstatus & I_RD_OOSYNC) {
		DHD_ERROR(("Dongle reports RD_OOSYNC\n"));
		intstatus &= ~I_RD_OOSYNC;
	}

	if (intstatus & I_SBINT) {
		DHD_ERROR(("Dongle reports SBINT\n"));
		intstatus &= ~I_SBINT;
	}

	
	if (intstatus & I_CHIPACTIVE) {
		DHD_INFO(("Dongle reports CHIPACTIVE\n"));
		intstatus &= ~I_CHIPACTIVE;
	}

	
	if (bus->rxskip)
		intstatus &= ~I_HMB_FRAME_IND;

	
	if (PKT_AVAILABLE()) {
		framecnt = dhdsdio_readframes(bus, rxlimit, &rxdone);
		if (rxdone || bus->rxskip)
			intstatus &= ~I_HMB_FRAME_IND;
		rxlimit -= MIN(framecnt, rxlimit);
	}

	
	bus->intstatus = intstatus;

clkwait:
	
	if (bus->intr && bus->intdis && !bcmsdh_regfail(sdh)) {
		DHD_INTR(("%s: enable SDIO interrupts, rxdone %d framecnt %d\n",
		          __FUNCTION__, rxdone, framecnt));
		bus->intdis = FALSE;
		bcmsdh_intr_enable(sdh);
	}

	
	if ((bus->clkstate != CLK_PENDING) && !bus->fcstate &&
	    pktq_mlen(&bus->txq, ~bus->flowcontrol) && txlimit && DATAOK(bus)) {
		framecnt = rxdone ? txlimit : MIN(txlimit, dhd_txminmax);
		framecnt = dhdsdio_sendfromq(bus, framecnt);
		txlimit -= framecnt;
	}

	
	
	if ((bus->dhd->busstate == DHD_BUS_DOWN) || bcmsdh_regfail(sdh)) {
		DHD_ERROR(("%s: failed backplane access over SDIO, halting operation\n",
		           __FUNCTION__));
		bus->dhd->busstate = DHD_BUS_DOWN;
		bus->intstatus = 0;
	} else if (bus->clkstate == CLK_PENDING) {
		
	} else if (bus->intstatus || bus->ipend ||
	           (!bus->fcstate && pktq_mlen(&bus->txq, ~bus->flowcontrol) && DATAOK(bus)) ||
			PKT_AVAILABLE()) {  
		resched = TRUE;
	}

	bus->dpc_sched = resched;

	
	if ((bus->idletime == DHD_IDLE_IMMEDIATE) && (bus->clkstate != CLK_PENDING)) {
		bus->activity = FALSE;
		dhdsdio_clkctl(bus, CLK_NONE, FALSE);
	}

	dhd_os_sdunlock(bus->dhd);

	return resched;
}

bool
dhd_bus_dpc(struct dhd_bus *bus)
{
	
	DHD_TRACE(("Calling dhdsdio_dpc() from %s\n", __FUNCTION__));
	return dhdsdio_dpc(bus);
}

void
dhdsdio_isr(void *arg)
{
	dhd_bus_t *bus = (dhd_bus_t*)arg;
	bcmsdh_info_t *sdh = bus->sdh;

	if (bus->dhd->busstate == DHD_BUS_DOWN) {
		DHD_ERROR(("%s : bus is down. we have nothing to do\n", __FUNCTION__));
		return;
	}

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	
	bus->intrcount++;
	bus->ipend = TRUE;

	
	if (bus->sleeping) {
		DHD_ERROR(("INTERRUPT WHILE SLEEPING??\n"));
		return;
	}

	
	if (bus->intr) {
		DHD_INTR(("%s: disable SDIO interrupts\n", __FUNCTION__));
	} else {
		DHD_ERROR(("dhdsdio_isr() w/o interrupt configured!\n"));
	}

	bcmsdh_intr_disable(sdh);
	bus->intdis = TRUE;

#if defined(SDIO_ISR_THREAD)
	DHD_TRACE(("Calling dhdsdio_dpc() from %s\n", __FUNCTION__));
	dhdsdio_dpc(bus);
#else
	bus->dpc_sched = TRUE;
	dhd_sched_dpc(bus->dhd);
#endif 

}

#ifdef SDTEST
static void
dhdsdio_pktgen_init(dhd_bus_t *bus)
{
	
	if (dhd_pktgen_len) {
		bus->pktgen_maxlen = MIN(dhd_pktgen_len, MAX_PKTGEN_LEN);
		bus->pktgen_minlen = bus->pktgen_maxlen;
	} else {
		bus->pktgen_maxlen = MAX_PKTGEN_LEN;
		bus->pktgen_minlen = 0;
	}
	bus->pktgen_len = (uint16)bus->pktgen_minlen;

	
	bus->pktgen_freq = 1;
	bus->pktgen_print = 10000/dhd_watchdog_ms;
	bus->pktgen_count = (dhd_pktgen * dhd_watchdog_ms + 999) / 1000;

	
	bus->pktgen_mode = DHD_PKTGEN_ECHO;
	bus->pktgen_stop = 1;
}

static void
dhdsdio_pktgen(dhd_bus_t *bus)
{
	void *pkt;
	uint8 *data;
	uint pktcount;
	uint fillbyte;
	osl_t *osh = bus->dhd->osh;
	uint16 len;

	
	if (bus->pktgen_print && (++bus->pktgen_ptick >= bus->pktgen_print)) {
		bus->pktgen_ptick = 0;
		printf("%s: send attempts %d rcvd %d\n",
		       __FUNCTION__, bus->pktgen_sent, bus->pktgen_rcvd);
	}

	
	if (bus->pktgen_mode == DHD_PKTGEN_RECV) {
		if (!bus->pktgen_rcvd)
			dhdsdio_sdtest_set(bus, TRUE);
		return;
	}

	
	for (pktcount = 0; pktcount < bus->pktgen_count; pktcount++) {
		
		if (bus->pktgen_total && (bus->pktgen_sent >= bus->pktgen_total)) {
			bus->pktgen_count = 0;
			break;
		}

		
		len = bus->pktgen_len;
		if (!(pkt = PKTGET(osh, (len + SDPCM_HDRLEN + SDPCM_TEST_HDRLEN + DHD_SDALIGN),
		                   TRUE))) {;
			DHD_ERROR(("%s: PKTGET failed!\n", __FUNCTION__));
			break;
		}
		PKTALIGN(osh, pkt, (len + SDPCM_HDRLEN + SDPCM_TEST_HDRLEN), DHD_SDALIGN);
		data = (uint8*)PKTDATA(osh, pkt) + SDPCM_HDRLEN;

		
		switch (bus->pktgen_mode) {
		case DHD_PKTGEN_ECHO:
			*data++ = SDPCM_TEST_ECHOREQ;
			*data++ = (uint8)bus->pktgen_sent;
			break;

		case DHD_PKTGEN_SEND:
			*data++ = SDPCM_TEST_DISCARD;
			*data++ = (uint8)bus->pktgen_sent;
			break;

		case DHD_PKTGEN_RXBURST:
			*data++ = SDPCM_TEST_BURST;
			*data++ = (uint8)bus->pktgen_count;
			break;

		default:
			DHD_ERROR(("Unrecognized pktgen mode %d\n", bus->pktgen_mode));
			PKTFREE(osh, pkt, TRUE);
			bus->pktgen_count = 0;
			return;
		}

		
		*data++ = (len >> 0);
		*data++ = (len >> 8);

		
		for (fillbyte = 0; fillbyte < len; fillbyte++)
			*data++ = SDPCM_TEST_FILL(fillbyte, (uint8)bus->pktgen_sent);

#ifdef DHD_DEBUG
		if (DHD_BYTES_ON() && DHD_DATA_ON()) {
			data = (uint8*)PKTDATA(osh, pkt) + SDPCM_HDRLEN;
			prhex("dhdsdio_pktgen: Tx Data", data, PKTLEN(osh, pkt) - SDPCM_HDRLEN);
		}
#endif

		
		if (dhdsdio_txpkt(bus, pkt, SDPCM_TEST_CHANNEL, TRUE)) {
			bus->pktgen_fail++;
			if (bus->pktgen_stop && bus->pktgen_stop == bus->pktgen_fail)
				bus->pktgen_count = 0;
		}
		bus->pktgen_sent++;

		
		if (++bus->pktgen_len > bus->pktgen_maxlen)
			bus->pktgen_len = (uint16)bus->pktgen_minlen;

		
		if (bus->pktgen_mode == DHD_PKTGEN_RXBURST)
			break;
	}
}

static void
dhdsdio_sdtest_set(dhd_bus_t *bus, bool start)
{
	void *pkt;
	uint8 *data;
	osl_t *osh = bus->dhd->osh;

	
	if (!(pkt = PKTGET(osh, SDPCM_HDRLEN + SDPCM_TEST_HDRLEN + DHD_SDALIGN, TRUE))) {
		DHD_ERROR(("%s: PKTGET failed!\n", __FUNCTION__));
		return;
	}
	PKTALIGN(osh, pkt, (SDPCM_HDRLEN + SDPCM_TEST_HDRLEN), DHD_SDALIGN);
	data = (uint8*)PKTDATA(osh, pkt) + SDPCM_HDRLEN;

	
	*data++ = SDPCM_TEST_SEND;
	*data++ = start;
	*data++ = (bus->pktgen_maxlen >> 0);
	*data++ = (bus->pktgen_maxlen >> 8);

	
	if (dhdsdio_txpkt(bus, pkt, SDPCM_TEST_CHANNEL, TRUE))
		bus->pktgen_fail++;
}


static void
dhdsdio_testrcv(dhd_bus_t *bus, void *pkt, uint seq)
{
	osl_t *osh = bus->dhd->osh;
	uint8 *data;
	uint pktlen;

	uint8 cmd;
	uint8 extra;
	uint16 len;
	uint16 offset;

	
	if ((pktlen = PKTLEN(osh, pkt)) < SDPCM_TEST_HDRLEN) {
		DHD_ERROR(("dhdsdio_restrcv: toss runt frame, pktlen %d\n", pktlen));
		PKTFREE(osh, pkt, FALSE);
		return;
	}

	
	data = PKTDATA(osh, pkt);
	cmd = *data++;
	extra = *data++;
	len = *data++; len += *data++ << 8;

	
	if (cmd == SDPCM_TEST_DISCARD || cmd == SDPCM_TEST_ECHOREQ || cmd == SDPCM_TEST_ECHORSP) {
		if (pktlen != len + SDPCM_TEST_HDRLEN) {
			DHD_ERROR(("dhdsdio_testrcv: frame length mismatch, pktlen %d seq %d"
			           " cmd %d extra %d len %d\n", pktlen, seq, cmd, extra, len));
			PKTFREE(osh, pkt, FALSE);
			return;
		}
	}

	
	switch (cmd) {
	case SDPCM_TEST_ECHOREQ:
		
		*(uint8 *)(PKTDATA(osh, pkt)) = SDPCM_TEST_ECHORSP;
		if (dhdsdio_txpkt(bus, pkt, SDPCM_TEST_CHANNEL, TRUE) == 0) {
			bus->pktgen_sent++;
		} else {
			bus->pktgen_fail++;
			PKTFREE(osh, pkt, FALSE);
		}
		bus->pktgen_rcvd++;
		break;

	case SDPCM_TEST_ECHORSP:
		if (bus->ext_loop) {
			PKTFREE(osh, pkt, FALSE);
			bus->pktgen_rcvd++;
			break;
		}

		for (offset = 0; offset < len; offset++, data++) {
			if (*data != SDPCM_TEST_FILL(offset, extra)) {
				DHD_ERROR(("dhdsdio_testrcv: echo data mismatch: "
				           "offset %d (len %d) expect 0x%02x rcvd 0x%02x\n",
				           offset, len, SDPCM_TEST_FILL(offset, extra), *data));
				break;
			}
		}
		PKTFREE(osh, pkt, FALSE);
		bus->pktgen_rcvd++;
		break;

	case SDPCM_TEST_DISCARD:
		PKTFREE(osh, pkt, FALSE);
		bus->pktgen_rcvd++;
		break;

	case SDPCM_TEST_BURST:
	case SDPCM_TEST_SEND:
	default:
		DHD_INFO(("dhdsdio_testrcv: unsupported or unknown command, pktlen %d seq %d"
		          " cmd %d extra %d len %d\n", pktlen, seq, cmd, extra, len));
		PKTFREE(osh, pkt, FALSE);
		break;
	}

	
	if (bus->pktgen_mode == DHD_PKTGEN_RECV) {
		if (bus->pktgen_total && (bus->pktgen_rcvd >= bus->pktgen_total)) {
			bus->pktgen_count = 0;
			dhdsdio_sdtest_set(bus, FALSE);
		}
	}
}
#endif 

extern bool
dhd_bus_watchdog(dhd_pub_t *dhdp)
{
	dhd_bus_t *bus;

	DHD_TIMER(("%s: Enter\n", __FUNCTION__));

	bus = dhdp->bus;

	if (bus->dhd->dongle_reset)
		return FALSE;

	
	if (bus->sleeping)
		return FALSE;

	dhd_os_sdlock(bus->dhd);

	
	if (bus->poll && (++bus->polltick >= bus->pollrate)) {
		uint32 intstatus = 0;

		
		bus->polltick = 0;

		
		if (!bus->intr || (bus->intrcount == bus->lastintrs)) {

#ifndef BCMSPI
			if (!bus->dpc_sched || gDK8)
			{
				uint8 devpend;
				devpend = bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_0,
				                          SDIOD_CCCR_INTPEND, NULL);
				intstatus = devpend & (INTR_STATUS_FUNC1 | INTR_STATUS_FUNC2);
			}
#endif 

			
			if (intstatus) {
				bus->pollcnt++;
				bus->ipend = TRUE;
				if (bus->intr) {
					bcmsdh_intr_disable(bus->sdh);
				}
				bus->dpc_sched = TRUE;
				dhd_sched_dpc(bus->dhd);

			}
		}

		
		bus->lastintrs = bus->intrcount;
	}

#ifdef SDTEST
	
	if (bus->pktgen_count && (++bus->pktgen_tick >= bus->pktgen_freq)) {
		
		dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);
		bus->pktgen_tick = 0;
		dhdsdio_pktgen(bus);
	}
#endif

	
	if ((bus->idletime > 0) && (bus->clkstate == CLK_AVAIL)) {
		if (++bus->idlecount >= (uint32)bus->idletime) {
			bus->idlecount = 0;
			if (bus->activity) {
				bus->activity = FALSE;
			} else {
				dhdsdio_clkctl(bus, CLK_NONE, FALSE);
			}
		}
	}

	dhd_os_sdunlock(bus->dhd);

	return bus->ipend;
}

#ifdef DHD_DEBUG
static void
dhd_dump_cis(uint fn, uint8 *cis)
{
	uint byte, tag, tdata;
	DHD_INFO(("Function %d CIS:\n", fn));

	for (tdata = byte = 0; byte < SBSDIO_CIS_SIZE_LIMIT; byte++) {
		if ((byte % 16) == 0)
			DHD_INFO(("    "));
		DHD_INFO(("%02x ", cis[byte]));
		if ((byte % 16) == 15)
			DHD_INFO(("\n"));
		if (!tdata--) {
			tag = cis[byte];
			if (tag == 0xff)
				break;
			else if (!tag)
				tdata = 0;
			else if ((byte + 1) < SBSDIO_CIS_SIZE_LIMIT)
				tdata = cis[byte + 1] + 1;
			else
				DHD_INFO(("]"));
		}
	}
	if ((byte % 16) != 15)
		DHD_INFO(("\n"));
}

#endif 

static bool
dhdsdio_chipmatch(uint16 chipid)
{
	if (chipid == BCM4325_CHIP_ID)
		return TRUE;
	if (chipid == BCM4329_CHIP_ID)
		return TRUE;
	if (chipid == BCM4315_CHIP_ID)
		return TRUE;
	return FALSE;
}

static void *
dhdsdio_probe(uint16 venid, uint16 devid, uint16 bus_no, uint16 slot,
	uint16 func, uint bustype, void *regsva, osl_t * osh, void *sdh)
{
	int ret;
	dhd_bus_t *bus;

	
	dhd_txbound = DHD_TXBOUND;
	dhd_rxbound = DHD_RXBOUND;
	dhd_alignctl = TRUE;
	sd1idle = TRUE;
	dhd_readahead = TRUE;
	retrydata = FALSE;
	dhd_doflow = FALSE;
	dhd_dongle_memsize = 0;
#ifdef BCMSPI
	forcealign = FALSE;
#else
	forcealign = TRUE;
#endif 


	dhd_common_init();

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));
	DHD_INFO(("%s: venid 0x%04x devid 0x%04x\n", __FUNCTION__, venid, devid));

	
	ASSERT((uintptr)regsva == SI_ENUM_BASE);

	
	
	switch (venid) {
		case 0x0000:
		case VENDOR_BROADCOM:
			break;
		default:
			DHD_ERROR(("%s: unknown vendor: 0x%04x\n",
			           __FUNCTION__, venid));
			return NULL;
			break;
	}

	
	switch (devid) {
		case BCM4325_D11DUAL_ID:		
		case BCM4325_D11G_ID:			
		case BCM4325_D11A_ID:			
			DHD_INFO(("%s: found 4325 Dongle\n", __FUNCTION__));
			break;
		case BCM4329_D11N_ID:		
		case BCM4329_D11N2G_ID:		
		case BCM4329_D11N5G_ID:		
		case 0x4329:
			DHD_INFO(("%s: found 4329 Dongle\n", __FUNCTION__));
			break;
		case BCM4315_D11DUAL_ID:		
		case BCM4315_D11G_ID:			
		case BCM4315_D11A_ID:			
			DHD_INFO(("%s: found 4315 Dongle\n", __FUNCTION__));
			break;
		case 0:
			DHD_INFO(("%s: allow device id 0, will check chip internals\n",
			          __FUNCTION__));
			break;

		default:
			DHD_ERROR(("%s: skipping 0x%04x/0x%04x, not a dongle\n",
			           __FUNCTION__, venid, devid));
			return NULL;
			break;
	}

	if (osh == NULL) {
		
		if (!(osh = dhd_osl_attach(sdh, DHD_BUS))) {
			DHD_ERROR(("%s: osl_attach failed!\n", __FUNCTION__));
			return NULL;
		}
	}

	
	if (!(bus = MALLOC(osh, sizeof(dhd_bus_t)))) {
		DHD_ERROR(("%s: MALLOC of dhd_bus_t failed\n", __FUNCTION__));
		goto fail;
	}
	bzero(bus, sizeof(dhd_bus_t));
	bus->sdh = sdh;
	bus->cl_devid = (uint16)devid;
	bus->bus = DHD_BUS;
	bus->usebufpool = FALSE; 

	
	if (!(dhdsdio_probe_attach(bus, osh, sdh, regsva, devid))) {
		DHD_ERROR(("%s: dhdsdio_probe_attach failed\n", __FUNCTION__));
		goto fail;
	}

	
	if (!(bus->dhd = dhd_attach(osh, bus, SDPCM_RESERVE))) {
		DHD_ERROR(("%s: dhd_attach failed\n", __FUNCTION__));
		goto fail;
	}

	
	if (!(dhdsdio_probe_malloc(bus, osh, sdh))) {
		DHD_ERROR(("%s: dhdsdio_probe_malloc failed\n", __FUNCTION__));
		goto fail;
	}

	if (!(dhdsdio_probe_init(bus, osh, sdh))) {
		DHD_ERROR(("%s: dhdsdio_probe_init failed\n", __FUNCTION__));
		goto fail;
	}

	
	DHD_INTR(("%s: disable SDIO interrupts (not interested yet)\n", __FUNCTION__));
	bcmsdh_intr_disable(sdh);
	if ((ret = bcmsdh_intr_reg(sdh, dhdsdio_isr, bus)) != 0) {
		DHD_ERROR(("%s: FAILED: bcmsdh_intr_reg returned %d\n",
		           __FUNCTION__, ret));
		goto fail;
	}
	DHD_INTR(("%s: registered SDIO interrupt function ok\n", __FUNCTION__));

	DHD_INFO(("%s: completed!!\n", __FUNCTION__));

#ifdef BCMSPI
	
	DHD_INFO(("%s: completed!!\n", __FUNCTION__));
	if ((bus->sih->chip == BCM4329_CHIP_ID) && (bus->sih->chiprev <= 1))
		bus->pr61317_war = TRUE;
	else
		bus->pr61317_war = FALSE;

	if (bus->pr61317_war) {
		bus->ackpkt = PKTGET(bus->dhd->osh, 4 + SDPCM_RESERVE, TRUE);
		if (bus->ackpkt == NULL) {
			DHD_ERROR(("%s: FAILED: Couldn't allocate ack packet for BCMSPI\n",
				__FUNCTION__));
			goto fail;
		}
	}
#endif 

	
	if ((ret = dhd_bus_start(bus->dhd)) == -1) {
		DHD_TRACE(("%s: warning : check if firmware was provided\n", __FUNCTION__));
	}
	else if (ret == BCME_NOTUP)  {
		DHD_ERROR(("%s: dongle is not responding\n", __FUNCTION__));
		goto fail;
	}
	
	if (dhd_net_attach(bus->dhd, 0) != 0) {
		DHD_ERROR(("%s: Net attach failed!!\n", __FUNCTION__));
		goto fail;
	}

	return bus;

fail:
	dhdsdio_release(bus, osh);
	return NULL;
}

static bool
dhdsdio_probe_attach(struct dhd_bus *bus, osl_t *osh, void *sdh, void *regsva,
                     uint16 devid)
{
#ifndef BCMSPI
	uint8 clkctl = 0;
	int err = 0;
#endif 

	bus->alp_only = TRUE;

	
	if (dhdsdio_set_siaddr_window(bus, SI_ENUM_BASE)) {
		DHD_ERROR(("%s: FAILED to return to SI_ENUM_BASE\n", __FUNCTION__));
	}

#ifdef DHD_DEBUG
	printf("F1 signature read @0x18000000=0x%4x\n",
	   bcmsdh_reg_read(bus->sdh, SI_ENUM_BASE, 4));
#endif 

#ifndef BCMSPI	    

	



	bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, DHD_INIT_CLKCTL1, &err);
	if (!err)
		clkctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);

	if (err || ((clkctl & ~SBSDIO_AVBITS) != DHD_INIT_CLKCTL1)) {
		DHD_ERROR(("dhdsdio_probe: ChipClkCSR access: err %d wrote 0x%02x read 0x%02x\n",
		           err, DHD_INIT_CLKCTL1, clkctl));
		goto fail;
	}

#endif 

#ifdef DHD_DEBUG
	if (DHD_INFO_ON()) {
		uint fn, numfn;
		uint8 *cis[SDIOD_MAX_IOFUNCS];
		int err = 0;

#ifndef BCMSPI
		numfn = bcmsdh_query_iofnum(sdh);
		ASSERT(numfn <= SDIOD_MAX_IOFUNCS);

		
		SPINWAIT(((clkctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1,
		                                    SBSDIO_FUNC1_CHIPCLKCSR, NULL)),
		          !SBSDIO_ALPAV(clkctl)), PMU_MAX_TRANSITION_DLY);

		
		bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
		                 DHD_INIT_CLKCTL2, &err);
		OSL_DELAY(65);
#else
		numfn = 0; 
#endif 

		for (fn = 0; fn <= numfn; fn++) {
			if (!(cis[fn] = MALLOC(osh, SBSDIO_CIS_SIZE_LIMIT))) {
				DHD_INFO(("dhdsdio_probe: fn %d cis malloc failed\n", fn));
				break;
			}
			bzero(cis[fn], SBSDIO_CIS_SIZE_LIMIT);

			if ((err = bcmsdh_cis_read(sdh, fn, cis[fn], SBSDIO_CIS_SIZE_LIMIT))) {
				DHD_INFO(("dhdsdio_probe: fn %d cis read err %d\n", fn, err));
				MFREE(osh, cis[fn], SBSDIO_CIS_SIZE_LIMIT);
				break;
			}
			dhd_dump_cis(fn, cis[fn]);
		}

		while (fn-- > 0) {
			ASSERT(cis[fn]);
			MFREE(osh, cis[fn], SBSDIO_CIS_SIZE_LIMIT);
		}

		if (err) {
			DHD_ERROR(("dhdsdio_probe: failure reading or parsing CIS\n"));
			goto fail;
		}
	}
#endif 

	
	if (!(bus->sih = si_attach((uint)devid, osh, regsva, DHD_BUS, sdh,
	                           &bus->vars, &bus->varsz))) {
		DHD_ERROR(("%s: si_attach failed!\n", __FUNCTION__));
		goto fail;
	}

	bcmsdh_chipinfo(sdh, bus->sih->chip, bus->sih->chiprev);

	if (!dhdsdio_chipmatch((uint16)bus->sih->chip)) {
		DHD_ERROR(("%s: unsupported chip: 0x%04x\n",
		           __FUNCTION__, bus->sih->chip));
		goto fail;
	}

	si_sdiod_drive_strength_init(bus->sih, osh, dhd_sdiod_drive_strength);


	
	if (!DHD_NOPMU(bus)) {
		if ((si_setcore(bus->sih, ARM7S_CORE_ID, 0)) ||
		    (si_setcore(bus->sih, ARMCM3_CORE_ID, 0))) {
			bus->armrev = si_corerev(bus->sih);
		} else {
			DHD_ERROR(("%s: failed to find ARM core!\n", __FUNCTION__));
			goto fail;
		}
		if (!(bus->orig_ramsize = si_socram_size(bus->sih))) {
			DHD_ERROR(("%s: failed to find SOCRAM memory!\n", __FUNCTION__));
			goto fail;
		}
		bus->ramsize = bus->orig_ramsize;
		if (dhd_dongle_memsize)
			dhd_dongle_setmemsize(bus, dhd_dongle_memsize);

		DHD_ERROR(("DHD: dongle ram size is set to %d(orig %d)\n",
			bus->ramsize, bus->orig_ramsize));
	}

	
	if (!(bus->regs = si_setcore(bus->sih, PCMCIA_CORE_ID, 0)) &&
	    !(bus->regs = si_setcore(bus->sih, SDIOD_CORE_ID, 0))) {
		DHD_ERROR(("%s: failed to find SDIODEV core!\n", __FUNCTION__));
		goto fail;
	}
	bus->sdpcmrev = si_corerev(bus->sih);

	
	OR_REG(osh, &bus->regs->corecontrol, CC_BPRESEN);

	pktq_init(&bus->txq, (PRIOMASK+1), QLEN);

	
	bus->rxhdr = (uint8*)ROUNDUP((uintptr)&bus->hdrbuf[0], DHD_SDALIGN);

	if (gDK8) {
		dhd_intr = FALSE;
		dhd_poll = TRUE;
	}

	
	bus->intr = (bool)dhd_intr;
	if ((bus->poll = (bool)dhd_poll))
		bus->pollrate = 1;

	return TRUE;

fail:
	return FALSE;
}


static bool
dhdsdio_probe_malloc(dhd_bus_t *bus, osl_t *osh, void *sdh)
{
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (bus->dhd->maxctl) {
		bus->rxblen = ROUNDUP((bus->dhd->maxctl + SDPCM_HDRLEN), ALIGNMENT) + DHD_SDALIGN;
		if (!(bus->rxbuf = MALLOC(osh, bus->rxblen))) {
			DHD_ERROR(("%s: MALLOC of %d-byte rxbuf failed\n",
			           __FUNCTION__, bus->rxblen));
			goto fail;
		}
	}

	
	if (!(bus->databuf = MALLOC(osh, MAX_DATA_BUF))) {
		DHD_ERROR(("%s: MALLOC of %d-byte databuf failed\n",
			__FUNCTION__, MAX_DATA_BUF));
		
		if (!bus->rxblen) MFREE(osh, bus->rxbuf, bus->rxblen);
		goto fail;
	}

	
	if ((uintptr)bus->databuf % DHD_SDALIGN)
		bus->dataptr = bus->databuf + (DHD_SDALIGN - ((uintptr)bus->databuf % DHD_SDALIGN));
	else
		bus->dataptr = bus->databuf;

	return TRUE;

fail:
	return FALSE;
}


static bool
dhdsdio_probe_init(dhd_bus_t *bus, osl_t *osh, void *sdh)
{
	int32 fnum;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

#ifdef SDTEST
	dhdsdio_pktgen_init(bus);
#endif 

#ifndef BCMSPI
	
	bcmsdh_cfg_write(sdh, SDIO_FUNC_0, SDIOD_CCCR_IOEN, SDIO_FUNC_ENABLE_1, NULL);
#endif 

	bus->dhd->busstate = DHD_BUS_DOWN;
	bus->sleeping = FALSE;
	bus->rxflow = FALSE;
	bus->prev_rxlim_hit = 0;

#ifndef BCMSPI

	
	bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, 0, NULL);
#endif 

	
	bus->clkstate = CLK_SDONLY;
	bus->idletime = (int32)dhd_idletime;
	bus->idleclock = DHD_IDLE_ACTIVE;

	
	if (bcmsdh_iovar_op(sdh, "sd_divisor", NULL, 0,
	                    &bus->sd_divisor, sizeof(int32), FALSE) != BCME_OK) {
		DHD_ERROR(("%s: fail on %s get\n", __FUNCTION__, "sd_divisor"));
		bus->sd_divisor = -1;
	} else {
		DHD_INFO(("%s: Initial value for %s is %d\n",
		          __FUNCTION__, "sd_divisor", bus->sd_divisor));
	}

	
	if (bcmsdh_iovar_op(sdh, "sd_mode", NULL, 0,
	                    &bus->sd_mode, sizeof(int32), FALSE) != BCME_OK) {
		DHD_ERROR(("%s: fail on %s get\n", __FUNCTION__, "sd_mode"));
		bus->sd_mode = -1;
	} else {
		DHD_INFO(("%s: Initial value for %s is %d\n",
		          __FUNCTION__, "sd_mode", bus->sd_mode));
	}

	
	fnum = 2;
	if (bcmsdh_iovar_op(sdh, "sd_blocksize", &fnum, sizeof(int32),
	                    &bus->blocksize, sizeof(int32), FALSE) != BCME_OK) {
		bus->blocksize = 0;
		DHD_ERROR(("%s: fail on %s get\n", __FUNCTION__, "sd_blocksize"));
	} else {
		DHD_INFO(("%s: Initial value for %s is %d\n",
		          __FUNCTION__, "sd_blocksize", bus->blocksize));
	}
	bus->roundup = MIN(max_roundup, bus->blocksize);

	
	if (bcmsdh_iovar_op(sdh, "sd_rxchain", NULL, 0,
	                    &bus->sd_rxchain, sizeof(int32), FALSE) != BCME_OK) {
		bus->sd_rxchain = FALSE;
	} else {
		DHD_INFO(("%s: bus module (through bcmsdh API) %s chaining\n",
		          __FUNCTION__, (bus->sd_rxchain ? "supports" : "does not support")));
	}
	bus->use_rxchain = (bool)bus->sd_rxchain;

	return TRUE;
}

bool
dhd_bus_download_firmware(struct dhd_bus *bus, osl_t *osh,
                          char *fw_path, char *nv_path)
{
	bool ret;
	bus->fw_path = fw_path;
	bus->nv_path = nv_path;

	ret = dhdsdio_download_firmware(bus, osh, bus->sdh);

	return ret;
}

static bool
dhdsdio_download_firmware(struct dhd_bus *bus, osl_t *osh, void *sdh)
{
	bool ret;

	
	dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);

	ret = _dhdsdio_download_firmware(bus) == 0;

	dhdsdio_clkctl(bus, CLK_SDONLY, FALSE);

	return ret;
}

static void
dhdsdio_release(dhd_bus_t *bus, osl_t *osh)
{
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (bus) {
		ASSERT(osh);
#ifdef BCMSPI
		
		if (bus->ackpkt) {
			PKTFREE(bus->dhd->osh, bus->ackpkt, TRUE);
			bus->ackpkt = NULL;
		}
#endif 

		
		if (bus->dhd) {
			dhd_detach(bus->dhd);
			bus->dhd = NULL;
		}

		dhdsdio_release_malloc(bus, osh);

		dhdsdio_release_dongle(bus, osh);
		
		bcmsdh_intr_dereg(bus->sdh);

		MFREE(osh, bus, sizeof(dhd_bus_t));
	}

	if (osh)
		dhd_osl_detach(osh);

	DHD_TRACE(("%s: Disconnected\n", __FUNCTION__));
}

static void
dhdsdio_release_malloc(dhd_bus_t *bus, osl_t *osh)
{
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (bus->dhd && bus->dhd->dongle_reset)
		return;

	if (bus->rxbuf) {
		MFREE(osh, bus->rxbuf, bus->rxblen);
		bus->rxctl = bus->rxbuf = NULL;
		bus->rxlen = 0;
	}

	if (bus->databuf) {
		MFREE(osh, bus->databuf, MAX_DATA_BUF);
		bus->databuf = NULL;
	}
}


static void
dhdsdio_release_dongle(dhd_bus_t *bus, osl_t *osh)
{
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (bus->dhd && bus->dhd->dongle_reset)
		return;

	if (bus->sih) {
		dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);
#if !defined(BCMLXSDMMC)
		si_watchdog(bus->sih, 4);
#endif 
		dhdsdio_clkctl(bus, CLK_NONE, FALSE);
		si_detach(bus->sih);
		if (bus->vars && bus->varsz)
			MFREE(osh, bus->vars, bus->varsz);
		bus->vars = NULL;
	}
	DHD_TRACE(("%s: Disconnected\n", __FUNCTION__));
}

static void
dhdsdio_disconnect(void *ptr)
{
	dhd_bus_t *bus = (dhd_bus_t *)ptr;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (bus) {
		ASSERT(bus->dhd);
		dhdsdio_release(bus, bus->dhd->osh);
	}

	DHD_TRACE(("%s: Disconnected\n", __FUNCTION__));
}




static bcmsdh_driver_t dhd_sdio = {
	dhdsdio_probe,
	dhdsdio_disconnect
};

int
dhd_bus_register(void)
{
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	return bcmsdh_register(&dhd_sdio);
}

void
dhd_bus_unregister(void)
{
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	bcmsdh_unregister();
}

#ifdef BCMEMBEDIMAGE
static int
dhdsdio_download_code_array(struct dhd_bus *bus)
{
	int bcmerror = -1;
	int offset = 0;

	DHD_INFO(("%s: download embedded firmware...\n", __FUNCTION__));

	
	while ((offset + MEMBLOCK) < sizeof(dlarray)) {
		bcmerror = dhdsdio_membytes(bus, TRUE, offset, dlarray + offset, MEMBLOCK);
		if (bcmerror) {
			DHD_ERROR(("%s: error %d on writing %d membytes at 0x%08x\n",
			        __FUNCTION__, bcmerror, MEMBLOCK, offset));
			goto err;
		}

		offset += MEMBLOCK;
	}

	if (offset < sizeof(dlarray)) {
		bcmerror = dhdsdio_membytes(bus, TRUE, offset,
			dlarray + offset, sizeof(dlarray) - offset);
		if (bcmerror) {
			DHD_ERROR(("%s: error %d on writing %d membytes at 0x%08x\n",
			        __FUNCTION__, bcmerror, sizeof(dlarray) - offset, offset));
			goto err;
		}
	}

err:
	return bcmerror;
}
#endif 

static int
dhdsdio_download_code_file(struct dhd_bus *bus, char *fw_path)
{
	int bcmerror = -1;
	int offset = 0;
	uint len;
	void * image = NULL;
	uint8 * memblock = NULL, * memptr;

	DHD_INFO(("%s: download firmware %s\n", __FUNCTION__, fw_path));

	image = dhd_os_open_image(fw_path);
	if (image == NULL)
		goto err;

	memptr = memblock = MALLOC(bus->dhd->osh, MEMBLOCK + DHD_SDALIGN);
	if (memblock == NULL) {
		DHD_ERROR(("%s: Failed to allocate memory %d bytes\n", __FUNCTION__, MEMBLOCK));
		goto err;
	}
	if ((uint32)(uintptr)memblock % DHD_SDALIGN)
		memptr += (DHD_SDALIGN - ((uint32)(uintptr)memblock % DHD_SDALIGN));

	
	while ((len = dhd_os_get_image_block((char*)memptr, MEMBLOCK, image))) {
		bcmerror = dhdsdio_membytes(bus, TRUE, offset, memptr, len);
		if (bcmerror) {
			DHD_ERROR(("%s: error %d on writing %d membytes at 0x%08x\n",
			        __FUNCTION__, bcmerror, MEMBLOCK, offset));
			goto err;
		}

		offset += MEMBLOCK;
	}

err:
	if (memblock)
		MFREE(bus->dhd->osh, memblock, MEMBLOCK + DHD_SDALIGN);

	if (image)
		dhd_os_close_image(image);

	return bcmerror;
}



static uint
process_nvram_vars(char *varbuf, uint len)
{
	char *dp;
	bool findNewline;
	int column;
	uint buf_len, n;

	dp = varbuf;

	findNewline = FALSE;
	column = 0;

	for (n = 0; n < len; n++) {
		if (varbuf[n] == 0)
			break;
		if (varbuf[n] == '\r')
			continue;
		if (findNewline && varbuf[n] != '\n')
			continue;
		findNewline = FALSE;
		if (varbuf[n] == '#') {
			findNewline = TRUE;
			continue;
		}
		if (varbuf[n] == '\n') {
			if (column == 0)
				continue;
			*dp++ = 0;
			column = 0;
			continue;
		}
		*dp++ = varbuf[n];
		column++;
	}
	buf_len = dp - varbuf;

	while (dp < varbuf + n)
		*dp++ = 0;

	return buf_len;
}



void
dhd_bus_set_nvram_params(struct dhd_bus * bus, const char *nvram_params)
{
	bus->nvram_params = nvram_params;
}

static int
dhdsdio_download_nvram(struct dhd_bus *bus)
{
	int bcmerror = -1;
	uint len;
	void * image = NULL;
	char * memblock = NULL;
	char *bufp;
	char *nv_path;
	bool nvram_file_exists;

	nv_path = bus->nv_path;

	nvram_file_exists = ((nv_path != NULL) && (nv_path[0] != '\0'));
	if (!nvram_file_exists && (bus->nvram_params == NULL))
		return (0);

	if (nvram_file_exists) {
		image = dhd_os_open_image(nv_path);
		if (image == NULL)
			goto err;
	}

	memblock = MALLOC(bus->dhd->osh, MEMBLOCK);
	if (memblock == NULL) {
		DHD_ERROR(("%s: Failed to allocate memory %d bytes\n",
		           __FUNCTION__, MEMBLOCK));
		goto err;
	}

	
	if (nvram_file_exists) {
		len = dhd_os_get_image_block(memblock, MEMBLOCK, image);
	}
	else {
		len = strlen(bus->nvram_params);
		ASSERT(len <= MEMBLOCK);
		memcpy(memblock, bus->nvram_params, len);
	}

	if (len > 0 && len < MEMBLOCK) {
		bufp = (char *)memblock;
		bufp[len] = 0;
		len = process_nvram_vars(bufp, len);
		bufp += len;
		*bufp++ = 0;
		if (len)
			bcmerror = dhdsdio_downloadvars(bus, memblock, len + 1);
		if (bcmerror) {
			DHD_ERROR(("%s: error downloading vars: %d\n",
			           __FUNCTION__, bcmerror));
		}
	}
	else {
		DHD_ERROR(("%s: error reading nvram file: %d\n",
		           __FUNCTION__, len));
		bcmerror = BCME_SDIO_ERROR;
	}

err:
	if (memblock)
		MFREE(bus->dhd->osh, memblock, MEMBLOCK);

	if (image)
		dhd_os_close_image(image);

	return bcmerror;
}

static int
_dhdsdio_download_firmware(struct dhd_bus *bus)
{
	int bcmerror = -1;

	bool embed = FALSE;	
	bool dlok = FALSE;	

	
	if ((bus->fw_path == NULL) || (bus->fw_path[0] == '\0')) {
#ifdef BCMEMBEDIMAGE
		embed = TRUE;
#else
		return bcmerror;
#endif
	}

	
	if (dhdsdio_download_state(bus, TRUE)) {
		DHD_ERROR(("%s: error placing ARM core in reset\n", __FUNCTION__));
		goto err;
	}

	
	if ((bus->fw_path != NULL) && (bus->fw_path[0] != '\0')) {
		if (dhdsdio_download_code_file(bus, bus->fw_path)) {
			DHD_ERROR(("%s: dongle image file download failed\n", __FUNCTION__));
#ifdef BCMEMBEDIMAGE
			embed = TRUE;
#else
			goto err;
#endif
		}
		else {
			embed = FALSE;
			dlok = TRUE;
		}
	}
#ifdef BCMEMBEDIMAGE
	if (embed) {
		if (dhdsdio_download_code_array(bus)) {
			DHD_ERROR(("%s: dongle image array download failed\n", __FUNCTION__));
			goto err;
		}
		else {
			dlok = TRUE;
		}
	}
#endif
	if (!dlok) {
		DHD_ERROR(("%s: dongle image download failed\n", __FUNCTION__));
		goto err;
	}

	
	
	

	
	if (dhdsdio_download_nvram(bus)) {
		DHD_ERROR(("%s: dongle nvram file download failed\n", __FUNCTION__));
	}

	
	if (dhdsdio_download_state(bus, FALSE)) {
		DHD_ERROR(("%s: error getting out of ARM core reset\n", __FUNCTION__));
		goto err;
	}

	bcmerror = 0;

err:
	return bcmerror;
}

static int
dhd_bcmsdh_recv_buf(dhd_bus_t *bus, uint32 addr, uint fn, uint flags, uint8 *buf, uint nbytes,
	void *pkt, bcmsdh_cmplt_fn_t complete, void *handle)
{
	int status;

	
	status = bcmsdh_recv_buf(bus->sdh, addr, fn, flags, buf, nbytes, pkt, complete, handle);
#ifdef BCMSPI

	if (PKT_AVAILABLE()) {
		DHD_ERROR(("With PR55150 WAR PKT_AVAILABLE is not expected"
		           " before ackpkt is sent\n"));
	}

	if (bus->pr61317_war) {
		ASSERT(bus->ackpkt);
		dhdsdio_txpkt(bus, bus->ackpkt, SDPCM_EVENT_CHANNEL, FALSE);
	}
#endif 
	return status;
}

static int
dhd_bcmsdh_send_buf(dhd_bus_t *bus, uint32 addr, uint fn, uint flags, uint8 *buf, uint nbytes,
	void *pkt, bcmsdh_cmplt_fn_t complete, void *handle)
{
	return (bcmsdh_send_buf(bus->sdh, addr, fn, flags, buf, nbytes, pkt, complete, handle));
}

uint
dhd_bus_chip(struct dhd_bus *bus)
{
	ASSERT(bus->sih != NULL);
	return bus->sih->chip;
}

void *
dhd_bus_pub(struct dhd_bus *bus)
{
	return bus->dhd;
}

void *
dhd_bus_txq(struct dhd_bus *bus)
{
	return &bus->txq;
}

uint
dhd_bus_hdrlen(struct dhd_bus *bus)
{
	return SDPCM_HDRLEN;
}

int
dhd_bus_devreset(dhd_pub_t *dhdp, uint8 flag)
{
	int bcmerror = 0;
	dhd_bus_t *bus;

	bus = dhdp->bus;

	if (flag == TRUE) {
		if (!bus->dhd->dongle_reset) {

			
			
			dhd_bus_stop(bus, FALSE);

			
			dhdsdio_release_dongle(bus, bus->dhd->osh);

			bus->dhd->dongle_reset = TRUE;
			bus->dhd->up = FALSE;

			DHD_TRACE(("%s:  WLAN OFF DONE\n", __FUNCTION__));
			
		} else
			bcmerror = BCME_SDIO_ERROR;
	} else {
		

		DHD_TRACE(("\n\n%s: == WLAN ON ==\n", __FUNCTION__));

		if (bus->dhd->dongle_reset) {
			
			
			bcmsdh_reset(bus->sdh);

			
			if (dhdsdio_probe_attach(bus, bus->dhd->osh, bus->sdh,
			                        (uint32 *)SI_ENUM_BASE,
			                        bus->cl_devid)) {
				
				if (dhdsdio_probe_init(bus, bus->dhd->osh, bus->sdh) &&
					dhdsdio_download_firmware(bus, bus->dhd->osh, bus->sdh)) {

					
					dhd_bus_init((dhd_pub_t *) bus->dhd, FALSE);

#if defined(OOB_INTR_ONLY)
					dhd_enable_oob_intr(bus, TRUE);
#endif 

					bus->dhd->dongle_reset = FALSE;
					bus->dhd->up = TRUE;


					DHD_TRACE(("%s: WLAN ON DONE\n", __FUNCTION__));
				} else
					bcmerror = BCME_SDIO_ERROR;
			} else
				bcmerror = BCME_SDIO_ERROR;
		} else {
			bcmerror = BCME_NOTDOWN;
			DHD_ERROR(("%s: Set DEVRESET=FALSE invoked when device is on\n",
				__FUNCTION__));
			bcmerror = BCME_SDIO_ERROR;
		}
	}
	return bcmerror;
}
