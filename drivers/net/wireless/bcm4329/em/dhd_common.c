/*
 * Broadcom Dongle Host Driver (DHD), common DHD core.
 *
 * Copyright (C) 2010, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: dhd_common.c,v 1.5.6.8.2.6.6.60 2010/05/26 03:36:55 Exp $
 */
#include <typedefs.h>
#include <osl.h>

#include <epivers.h>
#include <bcmutils.h>

#include <bcmendian.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_bus.h>
#include <dhd_proto.h>
#include <dhd_dbg.h>
#include <msgtrace.h>

#ifdef WLBTAMP
#include <proto/bt_amp_hci.h>
#include <dhd_bta.h>
#endif

#ifdef NDIS
#include <wl_ndis.h>
#endif 
#include <wlioctl.h>

int dhd_msg_level;

#ifdef NDIS
extern uint wl_msg_level;
#endif

char fw_path[MOD_PARAM_PATHLEN];
char nv_path[MOD_PARAM_PATHLEN];


uint32 dhd_conn_event;
uint32 dhd_conn_status;
uint32 dhd_conn_reason;

#define htod32(i) i
#define htod16(i) i
#define dtoh32(i) i
#define dtoh16(i) i

extern int dhdcdc_set_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf, uint len);
extern void dhd_ind_scan_confirm(void *h, bool status);
#ifdef NDIS
extern
void dhd_init_timer(void *h, PNDIS_TIMER_FUNCTION fn, void* arg, NDIS_MINIPORT_TIMER *pndis_timer);
extern void dhd_release_timer(void *h, NDIS_MINIPORT_TIMER *pndis_timer);
extern int dhd_del_timer(void * dhdp, NDIS_MINIPORT_TIMER *pndis_timer);
extern int dhd_add_timer(void * dhdp, NDIS_MINIPORT_TIMER *pndis_timer, int delay);
#endif
extern int dhd_wl_ioctl(dhd_pub_t *dhd, uint cmd, char *buf, uint buflen);
void dhd_iscan_lock(void);
void dhd_iscan_unlock(void);


#ifndef DHD_SDALIGN
#define DHD_SDALIGN	32
#endif
#if !ISPOWEROF2(DHD_SDALIGN)
#error DHD_SDALIGN is not a power of 2!
#endif

#ifdef DHD_DEBUG
const char dhd_version[] = "Dongle Host Driver, version " EPI_VERSION_STR "\nCompiled on "
	__DATE__ " at " __TIME__;
#else
const char dhd_version[] = "Dongle Host Driver, version " EPI_VERSION_STR;
#endif

void dhd_set_timer(void *bus, uint wdtick);


enum {
	IOV_VERSION = 1,
	IOV_MSGLEVEL,
#ifdef NDIS
	IOV_WLMSGLEVEL,
#endif 
	IOV_BCMERRORSTR,
	IOV_BCMERROR,
	IOV_WDTICK,
	IOV_DUMP,
#ifdef DHD_DEBUG
	IOV_CONS,
	IOV_DCONSOLE_POLL,
#endif
	IOV_CLEARCOUNTS,
	IOV_LOGDUMP,
	IOV_LOGCAL,
	IOV_LOGSTAMP,
	IOV_GPIOOB,
	IOV_IOCTLTIMEOUT,
#ifdef WLBTAMP
	IOV_HCI_CMD,		
	IOV_HCI_ACL_DATA,	
#endif
	IOV_LAST
};

const bcm_iovar_t dhd_iovars[] = {
	{"version", 	IOV_VERSION,	0,	IOVT_BUFFER,	sizeof(dhd_version) },
#ifdef DHD_DEBUG
	{"msglevel",	IOV_MSGLEVEL,	0,	IOVT_UINT32,	0 },
#ifdef NDIS
	{"wlmsglevel",	IOV_WLMSGLEVEL,	0,	IOVT_UINT32,	0 },
#endif 
#endif 
	{"bcmerrorstr", IOV_BCMERRORSTR, 0, IOVT_BUFFER,	BCME_STRLEN },
	{"bcmerror",	IOV_BCMERROR,	0,	IOVT_INT8,	0 },
	{"wdtick",	IOV_WDTICK, 0,	IOVT_UINT32,	0 },
	{"dump",	IOV_DUMP,	0,	IOVT_BUFFER,	DHD_IOCTL_MAXLEN },
#ifdef DHD_DEBUG
	{"dconpoll",	IOV_DCONSOLE_POLL, 0,	IOVT_UINT32,	0 },
	{"cons",	IOV_CONS,	0,	IOVT_BUFFER,	0 },
#endif
	{"clearcounts", IOV_CLEARCOUNTS, 0, IOVT_VOID,	0 },
#ifdef BCMPERFSTATS
	{"logdump",	IOV_LOGDUMP,	0,	IOVT_BUFFER,	DHD_IOCTL_MAXLEN },
	{"logcal",	IOV_LOGCAL, 0,	IOVT_UINT32,	0 },
	{"logstamp",	IOV_LOGSTAMP,	0,	IOVT_BUFFER,	0 },
#endif
	{"gpioob",	IOV_GPIOOB,	0,	IOVT_UINT32,	0 },
	{"ioctl_timeout",	IOV_IOCTLTIMEOUT,	0,	IOVT_UINT32,	0 },
#ifdef WLBTAMP
	{"HCI_cmd",	IOV_HCI_CMD,	0,	IOVT_BUFFER,	0},
	{"HCI_ACL_data", IOV_HCI_ACL_DATA, 0,	IOVT_BUFFER,	0},
#endif
	{NULL, 0, 0, 0, 0 }
};

void
dhd_common_init(void)
{
	
	dhd_msg_level = DHD_ERROR_VAL;
	fw_path[0] = '\0';
	nv_path[0] = '\0';
}

static int
dhd_dump(dhd_pub_t *dhdp, char *buf, int buflen)
{
	char eabuf[ETHER_ADDR_STR_LEN];

	struct bcmstrbuf b;
	struct bcmstrbuf *strbuf = &b;

	bcm_binit(strbuf, buf, buflen);

	
	bcm_bprintf(strbuf, "%s\n", dhd_version);
	bcm_bprintf(strbuf, "\n");
	bcm_bprintf(strbuf, "pub.up %d pub.txoff %d pub.busstate %d\n",
	            dhdp->up, dhdp->txoff, dhdp->busstate);
	bcm_bprintf(strbuf, "pub.hdrlen %d pub.maxctl %d pub.rxsz %d\n",
	            dhdp->hdrlen, dhdp->maxctl, dhdp->rxsz);
	bcm_bprintf(strbuf, "pub.iswl %d pub.drv_version %ld pub.mac %s\n",
	            dhdp->iswl, dhdp->drv_version, bcm_ether_ntoa(&dhdp->mac, eabuf));
	bcm_bprintf(strbuf, "pub.bcmerror %d tickcnt %d\n", dhdp->bcmerror, dhdp->tickcnt);

	bcm_bprintf(strbuf, "dongle stats:\n");
	bcm_bprintf(strbuf, "tx_packets %ld tx_bytes %ld tx_errors %ld tx_dropped %ld\n",
	            dhdp->dstats.tx_packets, dhdp->dstats.tx_bytes,
	            dhdp->dstats.tx_errors, dhdp->dstats.tx_dropped);
	bcm_bprintf(strbuf, "rx_packets %ld rx_bytes %ld rx_errors %ld rx_dropped %ld\n",
	            dhdp->dstats.rx_packets, dhdp->dstats.rx_bytes,
	            dhdp->dstats.rx_errors, dhdp->dstats.rx_dropped);
	bcm_bprintf(strbuf, "multicast %ld\n", dhdp->dstats.multicast);

	bcm_bprintf(strbuf, "bus stats:\n");
	bcm_bprintf(strbuf, "tx_packets %ld tx_multicast %ld tx_errors %ld\n",
	            dhdp->tx_packets, dhdp->tx_multicast, dhdp->tx_errors);
	bcm_bprintf(strbuf, "tx_ctlpkts %ld tx_ctlerrs %ld\n",
	            dhdp->tx_ctlpkts, dhdp->tx_ctlerrs);
	bcm_bprintf(strbuf, "rx_packets %ld rx_multicast %ld rx_errors %ld \n",
	            dhdp->rx_packets, dhdp->rx_multicast, dhdp->rx_errors);
	bcm_bprintf(strbuf, "rx_ctlpkts %ld rx_ctlerrs %ld rx_dropped %ld rx_flushed %ld\n",
	            dhdp->rx_ctlpkts, dhdp->rx_ctlerrs, dhdp->rx_dropped, dhdp->rx_flushed);
	bcm_bprintf(strbuf, "rx_readahead_cnt %ld tx_realloc %ld fc_packets %ld\n",
	            dhdp->rx_readahead_cnt, dhdp->tx_realloc, dhdp->fc_packets);
	bcm_bprintf(strbuf, "wd_dpc_sched %ld\n", dhdp->wd_dpc_sched);
	bcm_bprintf(strbuf, "\n");

	
	dhd_prot_dump(dhdp, strbuf);
	bcm_bprintf(strbuf, "\n");

	
	dhd_bus_dump(dhdp, strbuf);

	return (!strbuf->size ? BCME_BUFTOOSHORT : 0);
}

static int
dhd_doiovar(dhd_pub_t *dhd_pub, const bcm_iovar_t *vi, uint32 actionid, const char *name,
            void *params, int plen, void *arg, int len, int val_size)
{
	int bcmerror = 0;
	int32 int_val = 0;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if ((bcmerror = bcm_iovar_lencheck(vi, arg, len, IOV_ISSET(actionid))) != 0)
		goto exit;

	if (plen >= (int)sizeof(int_val))
		bcopy(params, &int_val, sizeof(int_val));

	switch (actionid) {
	case IOV_GVAL(IOV_VERSION):
		
		strncpy((char*)arg, dhd_version, len);
		break;

	case IOV_GVAL(IOV_MSGLEVEL):
		int_val = (int32)dhd_msg_level;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_MSGLEVEL):
		dhd_msg_level = int_val;
		break;

#if defined(NDIS)
	case IOV_GVAL(IOV_WLMSGLEVEL):
		int_val = (int32)wl_msg_level;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_WLMSGLEVEL):
		wl_msg_level = int_val;
		break;
#endif 

	case IOV_GVAL(IOV_BCMERRORSTR):
		strncpy((char *)arg, bcmerrorstr(dhd_pub->bcmerror), BCME_STRLEN);
		((char *)arg)[BCME_STRLEN - 1] = 0x00;
		break;

	case IOV_GVAL(IOV_BCMERROR):
		int_val = (int32)dhd_pub->bcmerror;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_GVAL(IOV_WDTICK):
		int_val = (int32)dhd_watchdog_ms;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_WDTICK):
		if (!dhd_pub->up) {
			bcmerror = BCME_NOTUP;
			break;
		}
		dhd_os_wd_timer(dhd_pub, (uint)int_val);
		break;

	case IOV_GVAL(IOV_DUMP):
		bcmerror = dhd_dump(dhd_pub, arg, len);
		break;

#ifdef DHD_DEBUG
	case IOV_GVAL(IOV_DCONSOLE_POLL):
		int_val = (int32)dhd_console_ms;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_DCONSOLE_POLL):
		dhd_console_ms = (uint)int_val;
		break;

	case IOV_SVAL(IOV_CONS):
		if (len > 0)
			bcmerror = dhd_bus_console_in(dhd_pub, arg, len - 1);
		break;
#endif

	case IOV_SVAL(IOV_CLEARCOUNTS):
		dhd_pub->tx_packets = dhd_pub->rx_packets = 0;
		dhd_pub->tx_errors = dhd_pub->rx_errors = 0;
		dhd_pub->tx_ctlpkts = dhd_pub->rx_ctlpkts = 0;
		dhd_pub->tx_ctlerrs = dhd_pub->rx_ctlerrs = 0;
		dhd_pub->rx_dropped = 0;
		dhd_pub->rx_readahead_cnt = 0;
		dhd_pub->tx_realloc = 0;
		dhd_pub->wd_dpc_sched = 0;
		memset(&dhd_pub->dstats, 0, sizeof(dhd_pub->dstats));
		dhd_bus_clearcounts(dhd_pub);
		break;

#ifdef BCMPERFSTATS
	case IOV_GVAL(IOV_LOGDUMP): {
		bcmdumplog((char*)arg, len);
		break;
	}

	case IOV_SVAL(IOV_LOGCAL): {
		bcmlog("Starting OSL_DELAY (%d usecs)", (uint)int_val, 0);
		OSL_DELAY((uint)int_val);
		bcmlog("Finished OSL_DELAY (%d usecs)", (uint)int_val, 0);
		break;
	}

	case IOV_SVAL(IOV_LOGSTAMP): {
		int int_val2;

		if (plen >= 2 * sizeof(int)) {
			bcopy((char *)params + sizeof(int_val), &int_val2, sizeof(int_val2));
			bcmlog("User message %d %d", (uint)int_val, (uint)int_val2);
		} else if (plen >= sizeof(int)) {
			bcmlog("User message %d", (uint)int_val, 0);
		} else {
			bcmlog("User message", 0, 0);
		}
		break;
	}
#endif 

	case IOV_GVAL(IOV_IOCTLTIMEOUT): {
		int_val = (int32)dhd_os_get_ioctl_resp_timeout();
		bcopy(&int_val, arg, sizeof(int_val));
		break;
	}

	case IOV_SVAL(IOV_IOCTLTIMEOUT): {
		if (int_val <= 0)
			bcmerror = BCME_BADARG;
		else
			dhd_os_set_ioctl_resp_timeout((unsigned int)int_val);
		break;
	}

#ifdef WLBTAMP
	case IOV_SVAL(IOV_HCI_CMD): {
		amp_hci_cmd_t *cmd = (amp_hci_cmd_t *)arg;

		
		if (len < HCI_CMD_PREAMBLE_SIZE)
			return BCME_BUFTOOSHORT;

		
		if (len < (int)(HCI_CMD_PREAMBLE_SIZE + cmd->plen))
			return BCME_BUFTOOSHORT;

		dhd_bta_docmd(dhd_pub, cmd, len);
		break;
	}

	case IOV_SVAL(IOV_HCI_ACL_DATA): {
		amp_hci_ACL_data_t *ACL_data = (amp_hci_ACL_data_t *)arg;

		
		if (len < HCI_ACL_DATA_PREAMBLE_SIZE)
			return BCME_BUFTOOSHORT;

		
		if (len < (int)(HCI_ACL_DATA_PREAMBLE_SIZE + ACL_data->dlen))
			return BCME_BUFTOOSHORT;

		dhd_bta_tx_hcidata(dhd_pub, ACL_data, len);
		break;
	}
#endif 

	default:
		bcmerror = BCME_UNSUPPORTED;
		break;
	}

exit:
	return bcmerror;
}


void
dhd_store_conn_status(uint32 event, uint32 status, uint32 reason)
{
	
	if (!(event == WLC_E_SET_SSID && status == WLC_E_STATUS_FAIL &&
	      dhd_conn_event == WLC_E_PRUNE)) {
		dhd_conn_event = event;
		dhd_conn_status = status;
		dhd_conn_reason = reason;
	}
}

bool
dhd_prec_enq(dhd_pub_t *dhdp, struct pktq *q, void *pkt, int prec)
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
		discard_oldest = AC_BITMAP_TST(dhdp->wme_dp, eprec);
		if (eprec == prec && !discard_oldest)
			return FALSE;		
		
		p = discard_oldest ? pktq_pdeq(q, eprec) : pktq_pdeq_tail(q, eprec);
		if (p == NULL) {
			DHD_ERROR(("%s: pktq_penq() failed, oldest %d.",
				__FUNCTION__, discard_oldest));
			ASSERT(p);
		}

		PKTFREE(dhdp->osh, p, TRUE);
	}

	
	p = pktq_penq(q, prec, pkt);
	if (p == NULL) {
		DHD_ERROR(("%s: pktq_penq() failed.", __FUNCTION__));
		ASSERT(p);
	}

	return TRUE;
}

static int
dhd_iovar_op(dhd_pub_t *dhd_pub, const char *name,
             void *params, int plen, void *arg, int len, bool set)
{
	int bcmerror = 0;
	int val_size;
	const bcm_iovar_t *vi = NULL;
	uint32 actionid;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(name);
	ASSERT(len >= 0);

	
	ASSERT(set || (arg && len));

	
	ASSERT(!set || (!params && !plen));

	if ((vi = bcm_iovar_lookup(dhd_iovars, name)) == NULL) {
		bcmerror = BCME_UNSUPPORTED;
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
	bcmerror = dhd_doiovar(dhd_pub, vi, actionid, name, params, plen, arg, len, val_size);

exit:
	return bcmerror;
}

int
dhd_ioctl(dhd_pub_t *dhd_pub, dhd_ioctl_t *ioc, void *buf, uint buflen)
{
	int bcmerror = 0;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (!buf) return BCME_BADARG;

	switch (ioc->cmd) {
	case DHD_GET_MAGIC:
		if (buflen < sizeof(int))
			bcmerror = BCME_BUFTOOSHORT;
		else
			*(int*)buf = DHD_IOCTL_MAGIC;
		break;

	case DHD_GET_VERSION:
		if (buflen < sizeof(int))
			bcmerror = -BCME_BUFTOOSHORT;
		else
			*(int*)buf = DHD_IOCTL_VERSION;
		break;

	case DHD_GET_VAR:
	case DHD_SET_VAR: {
		char *arg;
		uint arglen;

		
		for (arg = buf, arglen = buflen; *arg && arglen; arg++, arglen--);

		if (*arg) {
			bcmerror = BCME_BUFTOOSHORT;
			break;
		}

		
		arg++, arglen--;

		
		if (ioc->cmd == DHD_GET_VAR)
			bcmerror = dhd_iovar_op(dhd_pub, buf, arg, arglen,
			buf, buflen, IOV_GET);
		else
			bcmerror = dhd_iovar_op(dhd_pub, buf, NULL, 0, arg, arglen, IOV_SET);
		if (bcmerror != BCME_UNSUPPORTED)
			break;

		
		if (ioc->cmd == DHD_GET_VAR)
			bcmerror = dhd_prot_iovar_op(dhd_pub, buf, arg,
			                             arglen, buf, buflen, IOV_GET);
		else
			bcmerror = dhd_prot_iovar_op(dhd_pub, buf,
			                             NULL, 0, arg, arglen, IOV_SET);
		if (bcmerror != BCME_UNSUPPORTED)
			break;

		
		if (ioc->cmd == DHD_GET_VAR)
			bcmerror = dhd_bus_iovar_op(dhd_pub, buf,
			                            arg, arglen, buf, buflen, IOV_GET);
		else
			bcmerror = dhd_bus_iovar_op(dhd_pub, buf,
			                            NULL, 0, arg, arglen, IOV_SET);

		break;
	}

	default:
		bcmerror = BCME_UNSUPPORTED;
	}

	return bcmerror;
}

#ifdef APSTA_PINGTEST
struct ether_addr guest_eas[MAX_GUEST];
#endif

#ifdef SHOW_EVENTS
static void
wl_show_host_event(wl_event_msg_t *event, void *event_data)
{
	uint i, status, reason;
	bool group = FALSE, flush_txq = FALSE, link = FALSE;
	char *auth_str, *event_name;
	uchar *buf;
	char err_msg[256], eabuf[ETHER_ADDR_STR_LEN];
	static struct {uint event; char *event_name;} event_names[] = {
		{WLC_E_SET_SSID, "SET_SSID"},
		{WLC_E_JOIN, "JOIN"},
		{WLC_E_START, "START"},
		{WLC_E_AUTH, "AUTH"},
		{WLC_E_AUTH_IND, "AUTH_IND"},
		{WLC_E_DEAUTH, "DEAUTH"},
		{WLC_E_DEAUTH_IND, "DEAUTH_IND"},
		{WLC_E_ASSOC, "ASSOC"},
		{WLC_E_ASSOC_IND, "ASSOC_IND"},
		{WLC_E_REASSOC, "REASSOC"},
		{WLC_E_REASSOC_IND, "REASSOC_IND"},
		{WLC_E_DISASSOC, "DISASSOC"},
		{WLC_E_DISASSOC_IND, "DISASSOC_IND"},
		{WLC_E_QUIET_START, "START_QUIET"},
		{WLC_E_QUIET_END, "END_QUIET"},
		{WLC_E_BEACON_RX, "BEACON_RX"},
		{WLC_E_LINK, "LINK"},
		{WLC_E_MIC_ERROR, "MIC_ERROR"},
		{WLC_E_NDIS_LINK, "NDIS_LINK"},
		{WLC_E_ROAM, "ROAM"},
		{WLC_E_TXFAIL, "TXFAIL"},
		{WLC_E_PMKID_CACHE, "PMKID_CACHE"},
		{WLC_E_RETROGRADE_TSF, "RETROGRADE_TSF"},
		{WLC_E_PRUNE, "PRUNE"},
		{WLC_E_AUTOAUTH, "AUTOAUTH"},
		{WLC_E_EAPOL_MSG, "EAPOL_MSG"},
		{WLC_E_SCAN_COMPLETE, "SCAN_COMPLETE"},
		{WLC_E_ADDTS_IND, "ADDTS_IND"},
		{WLC_E_DELTS_IND, "DELTS_IND"},
		{WLC_E_BCNSENT_IND, "BCNSENT_IND"},
		{WLC_E_BCNRX_MSG, "BCNRX_MSG"},
		{WLC_E_BCNLOST_MSG, "BCNLOST_MSG"},
		{WLC_E_ROAM_PREP, "ROAM_PREP"},
		{WLC_E_PFN_NET_FOUND, "PNO_NET_FOUND"},
		{WLC_E_PFN_NET_LOST, "PNO_NET_LOST"},
		{WLC_E_RESET_COMPLETE, "RESET_COMPLETE"},
		{WLC_E_JOIN_START, "JOIN_START"},
		{WLC_E_ROAM_START, "ROAM_START"},
		{WLC_E_ASSOC_START, "ASSOC_START"},
		{WLC_E_IBSS_ASSOC, "IBSS_ASSOC"},
		{WLC_E_RADIO, "RADIO"},
		{WLC_E_PSM_WATCHDOG, "PSM_WATCHDOG"},
		{WLC_E_PROBREQ_MSG, "PROBREQ_MSG"},
		{WLC_E_SCAN_CONFIRM_IND, "SCAN_CONFIRM_IND"},
		{WLC_E_PSK_SUP, "PSK_SUP"},
		{WLC_E_COUNTRY_CODE_CHANGED, "COUNTRY_CODE_CHANGED"},
		{WLC_E_EXCEEDED_MEDIUM_TIME, "EXCEEDED_MEDIUM_TIME"},
		{WLC_E_ICV_ERROR, "ICV_ERROR"},
		{WLC_E_UNICAST_DECODE_ERROR, "UNICAST_DECODE_ERROR"},
		{WLC_E_MULTICAST_DECODE_ERROR, "MULTICAST_DECODE_ERROR"},
		{WLC_E_TRACE, "TRACE"},
#ifdef WLBTAMP
		{WLC_E_BTA_HCI_EVENT, "BTA_HCI_EVENT"},
#endif
		{WLC_E_ACTION_FRAME, "ACTION FRAME"},
		{WLC_E_ACTION_FRAME_COMPLETE, "ACTION FRAME TX COMPLETE"},
		{WLC_E_IF, "IF"},
		{WLC_E_RSSI, "RSSI"},
		{WLC_E_PFN_SCAN_COMPLETE, "SCAN_COMPLETE"}
	};
	uint event_type, flags, auth_type, datalen;
	event_type = ntoh32(event->event_type);
	flags = ntoh16(event->flags);
	status = ntoh32(event->status);
	reason = ntoh32(event->reason);
	auth_type = ntoh32(event->auth_type);
	datalen = ntoh32(event->datalen);
	
	sprintf(eabuf, "%02x:%02x:%02x:%02x:%02x:%02x",
	        (uchar)event->addr.octet[0]&0xff,
	        (uchar)event->addr.octet[1]&0xff,
	        (uchar)event->addr.octet[2]&0xff,
	        (uchar)event->addr.octet[3]&0xff,
	        (uchar)event->addr.octet[4]&0xff,
	        (uchar)event->addr.octet[5]&0xff);

	event_name = "UNKNOWN";
	for (i = 0; i < ARRAYSIZE(event_names); i++) {
		if (event_names[i].event == event_type)
			event_name = event_names[i].event_name;
	}

	DHD_EVENT(("EVENT: %s, event ID = %d\n", event_name, event_type));

	if (flags & WLC_EVENT_MSG_LINK)
		link = TRUE;
	if (flags & WLC_EVENT_MSG_GROUP)
		group = TRUE;
	if (flags & WLC_EVENT_MSG_FLUSHTXQ)
		flush_txq = TRUE;

	switch (event_type) {
	case WLC_E_START:
	case WLC_E_DEAUTH:
	case WLC_E_DISASSOC:
		DHD_EVENT(("MACEVENT: %s, MAC %s\n", event_name, eabuf));
		break;

	case WLC_E_ASSOC_IND:
	case WLC_E_REASSOC_IND:
#ifdef APSTA_PINGTEST
		{
			int i;
			for (i = 0; i < MAX_GUEST; ++i)
				if (ETHER_ISNULLADDR(&guest_eas[i]))
					break;
			if (i < MAX_GUEST)
				bcopy(event->addr.octet, guest_eas[i].octet, ETHER_ADDR_LEN);
		}
#endif 
		DHD_EVENT(("MACEVENT: %s, MAC %s\n", event_name, eabuf));
		break;

	case WLC_E_ASSOC:
	case WLC_E_REASSOC:
		if (status == WLC_E_STATUS_SUCCESS) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, SUCCESS\n", event_name, eabuf));
		} else if (status == WLC_E_STATUS_TIMEOUT) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, TIMEOUT\n", event_name, eabuf));
		} else if (status == WLC_E_STATUS_FAIL) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, FAILURE, reason %d\n",
			       event_name, eabuf, (int)reason));
		} else {
			DHD_EVENT(("MACEVENT: %s, MAC %s, unexpected status %d\n",
			       event_name, eabuf, (int)status));
		}
		break;

	case WLC_E_DEAUTH_IND:
	case WLC_E_DISASSOC_IND:
#ifdef APSTA_PINGTEST
		{
			int i;
			for (i = 0; i < MAX_GUEST; ++i) {
				if (bcmp(guest_eas[i].octet, event->addr.octet,
				         ETHER_ADDR_LEN) == 0) {
					bzero(guest_eas[i].octet, ETHER_ADDR_LEN);
					break;
				}
			}
		}
#endif 
		DHD_EVENT(("MACEVENT: %s, MAC %s, reason %d\n", event_name, eabuf, (int)reason));
		break;

	case WLC_E_AUTH:
	case WLC_E_AUTH_IND:
		if (auth_type == DOT11_OPEN_SYSTEM)
			auth_str = "Open System";
		else if (auth_type == DOT11_SHARED_KEY)
			auth_str = "Shared Key";
		else {
			sprintf(err_msg, "AUTH unknown: %d", (int)auth_type);
			auth_str = err_msg;
		}
		if (event_type == WLC_E_AUTH_IND) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, %s\n", event_name, eabuf, auth_str));
		} else if (status == WLC_E_STATUS_SUCCESS) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, %s, SUCCESS\n",
				event_name, eabuf, auth_str));
		} else if (status == WLC_E_STATUS_TIMEOUT) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, %s, TIMEOUT\n",
				event_name, eabuf, auth_str));
		} else if (status == WLC_E_STATUS_FAIL) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, %s, FAILURE, reason %d\n",
			       event_name, eabuf, auth_str, (int)reason));
		}

		break;

	case WLC_E_JOIN:
	case WLC_E_ROAM:
	case WLC_E_SET_SSID:
		if (status == WLC_E_STATUS_SUCCESS) {
			DHD_EVENT(("MACEVENT: %s, MAC %s\n", event_name, eabuf));
		} else if (status == WLC_E_STATUS_FAIL) {
			DHD_EVENT(("MACEVENT: %s, failed\n", event_name));
		} else if (status == WLC_E_STATUS_NO_NETWORKS) {
			DHD_EVENT(("MACEVENT: %s, no networks found\n", event_name));
		} else {
			DHD_EVENT(("MACEVENT: %s, unexpected status %d\n",
				event_name, (int)status));
		}
		break;

	case WLC_E_BEACON_RX:
		if (status == WLC_E_STATUS_SUCCESS) {
			DHD_EVENT(("MACEVENT: %s, SUCCESS\n", event_name));
		} else if (status == WLC_E_STATUS_FAIL) {
			DHD_EVENT(("MACEVENT: %s, FAIL\n", event_name));
		} else {
			DHD_EVENT(("MACEVENT: %s, status %d\n", event_name, status));
		}
		break;

	case WLC_E_LINK:
		DHD_EVENT(("MACEVENT: %s %s\n", event_name, link?"UP":"DOWN"));
		break;

	case WLC_E_MIC_ERROR:
		DHD_EVENT(("MACEVENT: %s, MAC %s, Group %d, Flush %d\n",
		       event_name, eabuf, group, flush_txq));
		break;

	case WLC_E_ICV_ERROR:
	case WLC_E_UNICAST_DECODE_ERROR:
	case WLC_E_MULTICAST_DECODE_ERROR:
		DHD_EVENT(("MACEVENT: %s, MAC %s\n",
		       event_name, eabuf));
		break;

	case WLC_E_TXFAIL:
		DHD_EVENT(("MACEVENT: %s, RA %s\n", event_name, eabuf));
		break;

	case WLC_E_SCAN_COMPLETE:
	case WLC_E_PMKID_CACHE:
		DHD_EVENT(("MACEVENT: %s\n", event_name));
		break;

	case WLC_E_PFN_NET_FOUND:
	case WLC_E_PFN_NET_LOST:
	case WLC_E_PFN_SCAN_COMPLETE:
		DHD_EVENT(("PNOEVENT: %s\n", event_name));
		break;

	case WLC_E_PSK_SUP:
	case WLC_E_PRUNE:
		DHD_EVENT(("MACEVENT: %s, status %d, reason %d\n",
		           event_name, (int)status, (int)reason));
		break;

	case WLC_E_TRACE:
		{
			static uint32 seqnum_prev = 0;
			msgtrace_hdr_t hdr;
			uint32 nblost;
			char *s, *p;

			buf = (uchar *) event_data;
			memcpy(&hdr, buf, MSGTRACE_HDRLEN);

			if (hdr.version != MSGTRACE_VERSION) {
				printf("\nMACEVENT: %s [unsupported version --> "
				       "dhd version:%d dongle version:%d]\n",
				       event_name, MSGTRACE_VERSION, hdr.version);
				
				datalen = 0;
				break;
			}

			
			buf[MSGTRACE_HDRLEN + ntoh16(hdr.len)] = '\0';

			if (ntoh32(hdr.discarded_bytes) || ntoh32(hdr.discarded_printf)) {
				printf("\nWLC_E_TRACE: [Discarded traces in dongle -->"
				       "discarded_bytes %d discarded_printf %d]\n",
				       ntoh32(hdr.discarded_bytes), ntoh32(hdr.discarded_printf));
			}

			nblost = ntoh32(hdr.seqnum) - seqnum_prev - 1;
			if (nblost > 0) {
				printf("\nWLC_E_TRACE: [Event lost --> seqnum %d nblost %d\n",
				        ntoh32(hdr.seqnum), nblost);
			}
			seqnum_prev = ntoh32(hdr.seqnum);

			
			p = (char *)&buf[MSGTRACE_HDRLEN];
			while ((s = strstr(p, "\n")) != NULL) {
				*s = '\0';
				printf("%s\n", p);
				p = s + 1;
			}
			printf("%s\n", p);

			
			datalen = 0;
		}
		break;

#ifdef WLBTAMP
	case WLC_E_BTA_HCI_EVENT:
		DHD_EVENT(("MACEVENT: %s %d\n", event_name, ntoh32(*((int *)event_data))));
		break;
#endif

	case WLC_E_RSSI:
		DHD_EVENT(("MACEVENT: %s %d\n", event_name, ntoh32(*((int *)event_data))));
		break;

	default:
		DHD_EVENT(("MACEVENT: %s %d, MAC %s, status %d, reason %d, auth %d\n",
		       event_name, event_type, eabuf, (int)status, (int)reason,
		       (int)auth_type));
		break;
	}

	
	if (datalen) {
		buf = (uchar *) event_data;
		DHD_EVENT((" data (%d) : ", datalen));
		for (i = 0; i < datalen; i++)
			DHD_EVENT((" 0x%02x ", *buf++));
		DHD_EVENT(("\n"));
	}
}
#endif 

int
wl_host_event(struct dhd_info *dhd, int *ifidx, void *pktdata,
              wl_event_msg_t *event, void **data_ptr)
{
	
	bcm_event_t *pvt_data = (bcm_event_t *)pktdata;
	char *event_data;
	uint32 type, status;
	uint16 flags;
	int evlen;

	if (bcmp(BRCM_OUI, &pvt_data->bcm_hdr.oui[0], DOT11_OUI_LEN)) {
		DHD_ERROR(("%s: mismatched OUI, bailing\n", __FUNCTION__));
		return (BCME_ERROR);
	}

	
	if (ntoh16_ua((void *)&pvt_data->bcm_hdr.usr_subtype) != BCMILCP_BCM_SUBTYPE_EVENT) {
		DHD_ERROR(("%s: mismatched subtype, bailing\n", __FUNCTION__));
		return (BCME_ERROR);
	}

	*data_ptr = &pvt_data[1];
	event_data = *data_ptr;

	
	memcpy(event, &pvt_data->event, sizeof(wl_event_msg_t));

	type = ntoh32_ua((void *)&event->event_type);
	flags = ntoh16_ua((void *)&event->flags);
	status = ntoh32_ua((void *)&event->status);
	evlen = ntoh32_ua((void *)&event->datalen) + sizeof(bcm_event_t);

	switch (type) {
		case WLC_E_IF:
			{
				dhd_if_event_t *ifevent = (dhd_if_event_t *)event_data;
				DHD_TRACE(("%s: if event\n", __FUNCTION__));

				if (ifevent->ifidx > 0 && ifevent->ifidx < DHD_MAX_IFS)
				{
					if (ifevent->action == WLC_E_IF_ADD)
						dhd_add_if(dhd, ifevent->ifidx,
							NULL, event->ifname,
							pvt_data->eth.ether_dhost,
							ifevent->flags, ifevent->bssidx);
					else
						dhd_del_if(dhd, ifevent->ifidx);
				} else {
					DHD_ERROR(("%s: Invalid ifidx %d for %s\n",
						__FUNCTION__, ifevent->ifidx, event->ifname));
				}
			}
			
			*ifidx = dhd_ifname2idx(dhd, event->ifname);
			
			dhd_event(dhd, (char *)pvt_data, evlen, *ifidx);
			break;

#if defined(UNDER_CE) && defined(NDIS60)
	case WLC_E_NDIS_LINK:
		break;
#endif

#ifdef P2P
		case WLC_E_NDIS_LINK:
			break;
#endif
		
		
		case WLC_E_LINK:
		case WLC_E_ASSOC_IND:
		case WLC_E_REASSOC_IND:
		case WLC_E_DISASSOC_IND:
		case WLC_E_MIC_ERROR:
		default:
		

			*ifidx = dhd_ifname2idx(dhd, event->ifname);
			
			dhd_event(dhd, (char *)pvt_data, evlen, *ifidx);
			DHD_TRACE(("%s: MAC event %d, flags %x, status %x\n",
			           __FUNCTION__, type, flags, status));

			
			if (type == WLC_E_NDIS_LINK) {
				uint32 temp;

				temp = ntoh32_ua((void *)&event->event_type);
				DHD_TRACE(("Converted to WLC_E_LINK type %d\n", temp));

				temp = ntoh32(WLC_E_NDIS_LINK);
				memcpy((void *)(&pvt_data->event.event_type), &temp,
					sizeof(pvt_data->event.event_type));
			}
			break;
	}

#ifdef SHOW_EVENTS
	wl_show_host_event(event, event_data);
#endif 

	return (BCME_OK);
}


void
wl_event_to_host_order(wl_event_msg_t *evt)
{
	
	evt->event_type = ntoh32(evt->event_type);
	evt->flags = ntoh16(evt->flags);
	evt->status = ntoh32(evt->status);
	evt->reason = ntoh32(evt->reason);
	evt->auth_type = ntoh32(evt->auth_type);
	evt->datalen = ntoh32(evt->datalen);
	evt->version = ntoh16(evt->version);
}

void print_buf(void *pbuf, int len, int bytes_per_line)
{
	int i, j = 0;
	unsigned char *buf = pbuf;

	if (bytes_per_line == 0) {
		bytes_per_line = len;
	}

	for (i = 0; i < len; i++) {
		printf("%2.2x", *buf++);
		j++;
		if (j == bytes_per_line) {
			printf("\n");
			j = 0;
		} else {
			printf(":");
		}
	}
	printf("\n");
}

#define strtoul(nptr, endptr, base) bcm_strtoul((nptr), (endptr), (base))


static int
wl_pattern_atoh(char *src, char *dst)
{
	int i;
	if (strncmp(src, "0x", 2) != 0 &&
	    strncmp(src, "0X", 2) != 0) {
		DHD_ERROR(("Mask invalid format. Needs to start with 0x\n"));
		return -1;
	}
	src = src + 2; 
	if (strlen(src) % 2 != 0) {
		DHD_ERROR(("Mask invalid format. Needs to be of even length\n"));
		return -1;
	}
	for (i = 0; *src != '\0'; i++) {
		char num[3];
		strncpy(num, src, 2);
		num[2] = '\0';
		dst[i] = (uint8)strtoul(num, NULL, 16);
		src += 2;
	}
	return i;
}

void
dhd_pktfilter_offload_enable(dhd_pub_t * dhd, char *arg, int enable, int master_mode)
{
	char				*argv[8];
	int					i = 0;
	const char 			*str;
	int					buf_len;
	int					str_len;
	char				*arg_save = 0, *arg_org = 0;
	int					rc;
	char				buf[128];
	wl_pkt_filter_enable_t	enable_parm;
	wl_pkt_filter_enable_t	* pkt_filterp;

	if (!(arg_save = MALLOC(dhd->osh, strlen(arg) + 1))) {
		DHD_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
		goto fail;
	}
	arg_org = arg_save;
	memcpy(arg_save, arg, strlen(arg) + 1);

	argv[i] = bcmstrtok(&arg_save, " ", 0);

	i = 0;
	if (NULL == argv[i]) {
		DHD_ERROR(("No args provided\n"));
		goto fail;
	}

	str = "pkt_filter_enable";
	str_len = strlen(str);
	strncpy(buf, str, str_len);
	buf[str_len] = '\0';
	buf_len = str_len + 1;

	pkt_filterp = (wl_pkt_filter_enable_t *)(buf + str_len + 1);

	
	enable_parm.id = htod32(strtoul(argv[i], NULL, 0));

	
	enable_parm.enable = htod32(enable);

	buf_len += sizeof(enable_parm);
	memcpy((char *)pkt_filterp,
	       &enable_parm,
	       sizeof(enable_parm));

	
	rc = dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, buf, buf_len);
	rc = rc >= 0 ? 0 : rc;
	if (rc)
		DHD_TRACE(("%s: failed to add pktfilter %s, retcode = %d\n",
		__FUNCTION__, arg, rc));
	else
		DHD_TRACE(("%s: successfully added pktfilter %s\n",
		__FUNCTION__, arg));

	
	bcm_mkiovar("pkt_filter_mode", (char *)&master_mode, 4, buf, sizeof(buf));
	rc = dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, buf, sizeof(buf));
	rc = rc >= 0 ? 0 : rc;
	if (rc)
		DHD_TRACE(("%s: failed to add pktfilter %s, retcode = %d\n",
		__FUNCTION__, arg, rc));

fail:
	if (arg_org)
		MFREE(dhd->osh, arg_org, strlen(arg) + 1);
}

void
dhd_pktfilter_offload_set(dhd_pub_t * dhd, char *arg)
{
	const char 			*str;
	wl_pkt_filter_t		pkt_filter;
	wl_pkt_filter_t		*pkt_filterp;
	int					buf_len;
	int					str_len;
	int 				rc;
	uint32				mask_size;
	uint32				pattern_size;
	char				*argv[8], * buf = 0;
	int					i = 0;
	char				*arg_save = 0, *arg_org = 0;
#define BUF_SIZE		2048

	if (!(arg_save = MALLOC(dhd->osh, strlen(arg) + 1))) {
		DHD_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
		goto fail;
	}

	arg_org = arg_save;

	if (!(buf = MALLOC(dhd->osh, BUF_SIZE))) {
		DHD_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
		goto fail;
	}

	memcpy(arg_save, arg, strlen(arg) + 1);

	if (strlen(arg) > BUF_SIZE) {
		DHD_ERROR(("Not enough buffer %d < %d\n", (int)strlen(arg), (int)sizeof(buf)));
		goto fail;
	}

	argv[i] = bcmstrtok(&arg_save, " ", 0);
	while (argv[i++])
		argv[i] = bcmstrtok(&arg_save, " ", 0);

	i = 0;
	if (NULL == argv[i]) {
		DHD_ERROR(("No args provided\n"));
		goto fail;
	}

	str = "pkt_filter_add";
	str_len = strlen(str);
	strncpy(buf, str, str_len);
	buf[ str_len ] = '\0';
	buf_len = str_len + 1;

	pkt_filterp = (wl_pkt_filter_t *) (buf + str_len + 1);

	
	pkt_filter.id = htod32(strtoul(argv[i], NULL, 0));

	if (NULL == argv[++i]) {
		DHD_ERROR(("Polarity not provided\n"));
		goto fail;
	}

	
	pkt_filter.negate_match = htod32(strtoul(argv[i], NULL, 0));

	if (NULL == argv[++i]) {
		DHD_ERROR(("Filter type not provided\n"));
		goto fail;
	}

	
	pkt_filter.type = htod32(strtoul(argv[i], NULL, 0));

	if (NULL == argv[++i]) {
		DHD_ERROR(("Offset not provided\n"));
		goto fail;
	}

	
	pkt_filter.u.pattern.offset = htod32(strtoul(argv[i], NULL, 0));

	if (NULL == argv[++i]) {
		DHD_ERROR(("Bitmask not provided\n"));
		goto fail;
	}

	
	mask_size =
		htod32(wl_pattern_atoh(argv[i], (char *) pkt_filterp->u.pattern.mask_and_pattern));

	if (NULL == argv[++i]) {
		DHD_ERROR(("Pattern not provided\n"));
		goto fail;
	}

	
	pattern_size =
		htod32(wl_pattern_atoh(argv[i],
	         (char *) &pkt_filterp->u.pattern.mask_and_pattern[mask_size]));

	if (mask_size != pattern_size) {
		DHD_ERROR(("Mask and pattern not the same size\n"));
		goto fail;
	}

	pkt_filter.u.pattern.size_bytes = mask_size;
	buf_len += WL_PKT_FILTER_FIXED_LEN;
	buf_len += (WL_PKT_FILTER_PATTERN_FIXED_LEN + 2 * mask_size);

	
	memcpy((char *)pkt_filterp,
	       &pkt_filter,
	       WL_PKT_FILTER_FIXED_LEN + WL_PKT_FILTER_PATTERN_FIXED_LEN);

	rc = dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, buf, buf_len);
	rc = rc >= 0 ? 0 : rc;

	if (rc)
		DHD_TRACE(("%s: failed to add pktfilter %s, retcode = %d\n",
		__FUNCTION__, arg, rc));
	else
		DHD_TRACE(("%s: successfully added pktfilter %s\n",
		__FUNCTION__, arg));

fail:
	if (arg_org)
		MFREE(dhd->osh, arg_org, strlen(arg) + 1);

	if (buf)
		MFREE(dhd->osh, buf, BUF_SIZE);
}

void
dhd_arp_offload_set(dhd_pub_t * dhd, int arp_mode)
{
	char iovbuf[32];
	int retcode;

	bcm_mkiovar("arp_ol", (char *)&arp_mode, 4, iovbuf, sizeof(iovbuf));
	retcode = dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
	retcode = retcode >= 0 ? 0 : retcode;
	if (retcode)
		DHD_TRACE(("%s: failed to set ARP offload mode to 0x%x, retcode = %d\n",
		__FUNCTION__, arp_mode, retcode));
	else
		DHD_TRACE(("%s: successfully set ARP offload mode to 0x%x\n",
		__FUNCTION__, arp_mode));
}

void
dhd_arp_offload_enable(dhd_pub_t * dhd, int arp_enable)
{
	char iovbuf[32];
	int retcode;

	bcm_mkiovar("arpoe", (char *)&arp_enable, 4, iovbuf, sizeof(iovbuf));
	retcode = dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
	retcode = retcode >= 0 ? 0 : retcode;
	if (retcode)
		DHD_TRACE(("%s: failed to enabe ARP offload to %d, retcode = %d\n",
		__FUNCTION__, arp_enable, retcode));
	else
		DHD_TRACE(("%s: successfully enabed ARP offload to %d\n",
		__FUNCTION__, arp_enable));
}

int
dhd_preinit_ioctls(dhd_pub_t *dhd)
{
	char iovbuf[WL_EVENTING_MASK_LEN + 12];	
	uint up = 0;
	char buf[128], *ptr;
	uint power_mode = PM_FAST;
	uint32 dongle_align = DHD_SDALIGN;
	uint32 glom = 0;
	uint bcn_timeout = 3;
	int scan_assoc_time = 40;
	int scan_unassoc_time = 80;

	
	if (dhd->country_code[0] != 0) {
		if (dhdcdc_set_ioctl(dhd, 0, WLC_SET_COUNTRY,
			dhd->country_code, sizeof(dhd->country_code)) < 0) {
			DHD_ERROR(("%s: country code setting failed\n", __FUNCTION__));
		}
	}

	
	memset(buf, 0, sizeof(buf));
	ptr = buf;
	bcm_mkiovar("ver", 0, 0, buf, sizeof(buf));
	dhdcdc_query_ioctl(dhd, 0, WLC_GET_VAR, buf, sizeof(buf));
	bcmstrtok(&ptr, "\n", 0);
	
	DHD_ERROR(("Firmware version = %s\n", buf));

	
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_PM, (char *)&power_mode, sizeof(power_mode));

	
	bcm_mkiovar("bus:txglomalign", (char *)&dongle_align, 4, iovbuf, sizeof(iovbuf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));

	
	bcm_mkiovar("bus:txglom", (char *)&glom, 4, iovbuf, sizeof(iovbuf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));

	
	bcm_mkiovar("bcn_timeout", (char *)&bcn_timeout, 4, iovbuf, sizeof(iovbuf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));

	
	bcm_mkiovar("roam_off", (char *)&dhd_roam, 4, iovbuf, sizeof(iovbuf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));

	
	if (dhd_radio_up)
		dhdcdc_set_ioctl(dhd, 0, WLC_UP, (char *)&up, sizeof(up));

	
	bcm_mkiovar("event_msgs", dhd->eventmask, WL_EVENTING_MASK_LEN, iovbuf, sizeof(iovbuf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));

	dhdcdc_set_ioctl(dhd, 0, WLC_SET_SCAN_CHANNEL_TIME, (char *)&scan_assoc_time,
		sizeof(scan_assoc_time));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_SCAN_UNASSOC_TIME, (char *)&scan_unassoc_time,
		sizeof(scan_unassoc_time));

#ifdef ARP_OFFLOAD_SUPPORT
	
	if (dhd_arp_enable)
		dhd_arp_offload_set(dhd, dhd_arp_mode);
	dhd_arp_offload_enable(dhd, dhd_arp_enable);
#endif 

#ifdef PKT_FILTER_SUPPORT
	{
		int i;
		
		if (dhd_pkt_filter_enable) {
			for (i = 0; i < dhd->pktfilter_count; i++) {
				dhd_pktfilter_offload_set(dhd, dhd->pktfilter[i]);
				dhd_pktfilter_offload_enable(dhd, dhd->pktfilter[i],
					dhd_pkt_filter_init, dhd_master_mode);
			}
		}
	}
#endif 

	return 0;
}

#ifdef SIMPLE_ISCAN

uint iscan_thread_id;
iscan_buf_t * iscan_chain = 0;

iscan_buf_t *
dhd_iscan_allocate_buf(dhd_pub_t *dhd, iscan_buf_t **iscanbuf)
{
	iscan_buf_t *iscanbuf_alloc = 0;
	iscan_buf_t *iscanbuf_head;

	dhd_iscan_lock();

	iscanbuf_alloc = (iscan_buf_t*)MALLOC(dhd->osh, sizeof(iscan_buf_t));
	if (iscanbuf_alloc == NULL)
		goto fail;

#if defined(UNDER_CE) && defined(EXT_STA)
	iscanbuf_alloc->time = osl_sys_time();
#endif
	iscanbuf_alloc->next = NULL;
	iscanbuf_head = *iscanbuf;

	DHD_ISCAN(("%s: addr of allocated node = 0x%X, addr of iscanbuf_head \
		= 0x%X dhd = 0x%X\n", __FUNCTION__, iscanbuf_alloc,
		iscanbuf_head, dhd));

	if (iscanbuf_head == NULL) {
		*iscanbuf = iscanbuf_alloc;
		DHD_ISCAN(("%s: Head is allocated\n", __FUNCTION__));
		goto fail;
	}

	while (iscanbuf_head->next)
		iscanbuf_head = iscanbuf_head->next;

	iscanbuf_head->next = iscanbuf_alloc;

fail:
	dhd_iscan_unlock();
	return iscanbuf_alloc;
}

void
dhd_iscan_free_buf(void *dhdp, iscan_buf_t *iscan_delete)
{
	iscan_buf_t *iscanbuf_free = 0;
	iscan_buf_t *iscanbuf_prv = 0;
	iscan_buf_t *iscanbuf_cur = iscan_chain;
	dhd_pub_t *dhd = dhd_bus_pub(dhdp);

	dhd_iscan_lock();
	
	if (!iscan_delete) {
		while (iscanbuf_cur) {
			iscanbuf_free = iscanbuf_cur;
			iscanbuf_cur = iscanbuf_cur->next;
			iscanbuf_free->next = 0;
			MFREE(dhd->osh, iscanbuf_free, sizeof(iscan_buf_t));
		}
		iscan_chain = 0;
	} else {
		while (iscanbuf_cur) {
			if (iscanbuf_cur == iscan_delete)
				break;
			iscanbuf_prv = iscanbuf_cur;
			iscanbuf_cur = iscanbuf_cur->next;
		}
		if (iscanbuf_prv)
			iscanbuf_prv->next = iscan_delete->next;

		iscan_delete->next = 0;
		MFREE(dhd->osh, iscan_delete, sizeof(iscan_buf_t));

		if (!iscanbuf_prv)
			iscan_chain = 0;
	}
	dhd_iscan_unlock();
}

iscan_buf_t *
dhd_iscan_result_buf(void)
{
	return iscan_chain;
}

#if defined(UNDER_CE) && defined(EXT_STA)

int
dhd_iscan_prune_aged_results(void *dhdp, long age_limit)
{
	iscan_buf_t *iscan_cur, *iscan_aged;
	long curr_time = 0;

	dhd_iscan_lock();

	iscan_cur = dhd_iscan_result_buf();

	curr_time = osl_sys_time();
	age_limit *=1000; 

	while (iscan_cur) {
		iscan_aged = iscan_cur;
		iscan_cur = iscan_cur->next;
		DHD_ISCAN(("%s: iscan buff %X is %ldms old (limit: %ldms, curr_time: %ldms)\n", \
		__FUNCTION__, iscan_aged, iscan_aged->time, age_limit, curr_time));

		if (iscan_aged->time != 0 && age_limit != 0) {
			if (iscan_aged->time + age_limit < curr_time) {
				
				DHD_ISCAN(("%s: iscan buff %X is %ld old (limit: %ld) -> remove\n",
					__FUNCTION__, iscan_aged,
					(curr_time - iscan_aged->time)/1000, age_limit/1000));

				dhd_iscan_free_buf(dhdp, iscan_aged);
			}
			else {
				DHD_ISCAN(("%s: iscan buff %X is %ld old (limit: %ld) -> keep\n", \
					__FUNCTION__, iscan_aged,
					(curr_time - iscan_aged->time)/1000, age_limit/1000));
			}
		}
	}

done:
	dhd_iscan_unlock();
	return 0;
}
#endif 



int
dhd_iscan_print_cache(iscan_buf_t *iscan_skip)
{
	int i = 0, l = 0;
	iscan_buf_t *iscan_cur;
	wl_iscan_results_t *list;
	wl_scan_results_t *results;
	wl_bss_info_t UNALIGNED *bi;

	dhd_iscan_lock();

	iscan_cur = dhd_iscan_result_buf();

	while (iscan_cur) {
		list = (wl_iscan_results_t *)iscan_cur->iscan_buf;
		if (!list)
			break;

		results = (wl_scan_results_t *)&list->results;
		if (!results)
			break;

		if (results->version != WL_BSS_INFO_VERSION) {
			DHD_ISCAN(("%s: results->version %d != WL_BSS_INFO_VERSION\n",
				__FUNCTION__, results->version));
			goto done;
		}

		bi = results->bss_info;
		for (i = 0; i < results->count; i++) {
			if (!bi)
				break;

			DHD_ISCAN(("%s[%2.2d:%2.2d] %X:%X:%X:%X:%X:%X\n",
				iscan_cur != iscan_skip?"BSS":"bss", l, i,
				bi->BSSID.octet[0], bi->BSSID.octet[1], bi->BSSID.octet[2],
				bi->BSSID.octet[3], bi->BSSID.octet[4], bi->BSSID.octet[5]));

			bi = (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length));
		}
		iscan_cur = iscan_cur->next;
		l++;
	}

done:
	dhd_iscan_unlock();
	return 0;
}


int
dhd_iscan_delete_bss(void *dhdp, void *addr, iscan_buf_t *iscan_skip)
{
	int i = 0, j = 0, l = 0;
	iscan_buf_t *iscan_cur;
	wl_iscan_results_t *list;
	wl_scan_results_t *results;
	wl_bss_info_t UNALIGNED *bi, *bi_new, *bi_next;

	uchar *s_addr = addr;

	dhd_iscan_lock();
	DHD_ISCAN(("%s: BSS to remove %X:%X:%X:%X:%X:%X\n",
		__FUNCTION__, s_addr[0], s_addr[1], s_addr[2],
		s_addr[3], s_addr[4], s_addr[5]));

	iscan_cur = dhd_iscan_result_buf();

	while (iscan_cur) {
		if (iscan_cur != iscan_skip) {
			list = (wl_iscan_results_t *)iscan_cur->iscan_buf;
			if (!list)
				break;

			results = (wl_scan_results_t *)&list->results;
			if (!results)
				break;

			if (results->version != WL_BSS_INFO_VERSION) {
				DHD_ERROR(("%s: results->version %d != WL_BSS_INFO_VERSION\n",
				__FUNCTION__, results->version));
				goto done;
			}

			bi = results->bss_info;
			for (i = 0; i < results->count; i++) {
				if (!bi)
					break;

				if (!memcmp(bi->BSSID.octet, addr, ETHER_ADDR_LEN)) {
					DHD_ISCAN(("%s: Del BSS[%2.2d:%2.2d] %X:%X:%X:%X:%X:%X\n", \
					__FUNCTION__, l, i, bi->BSSID.octet[0], \
					bi->BSSID.octet[1], bi->BSSID.octet[2], \
					bi->BSSID.octet[3], bi->BSSID.octet[4], \
					bi->BSSID.octet[5]));

					bi_new = bi;
					bi = (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length));

					results->buflen -= dtoh32(bi_new->length);
					results->count--;

					for (j = i; j < results->count; j++) {
						if (bi && bi_new) {
							DHD_ISCAN(("%s: Moved up BSS[%2.2d:%2.2d] \
							%X:%X:%X:%X:%X:%X\n",
							__FUNCTION__, l, j, bi->BSSID.octet[0],
							bi->BSSID.octet[1], bi->BSSID.octet[2],
							bi->BSSID.octet[3], bi->BSSID.octet[4],
							bi->BSSID.octet[5]));

							bi_next = (wl_bss_info_t *)((uintptr)bi +
								dtoh32(bi->length));
							bcopy(bi, bi_new, dtoh32(bi->length));
							bi_new = (wl_bss_info_t *)((uintptr)bi_new +
								dtoh32(bi_new->length));
							bi = bi_next;
						}
					}

					if (results->count == 0) {
						
						dhd_iscan_free_buf(dhdp, iscan_cur);
						goto done;
					}
					break;
				}
				bi = (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length));
			}
		}
		iscan_cur = iscan_cur->next;
		l++;
	}

done:
	dhd_iscan_unlock();
	return 0;
}

int
dhd_iscan_remove_duplicates(void * dhdp, iscan_buf_t *iscan_cur)
{
	int i = 0;
	wl_iscan_results_t *list;
	wl_scan_results_t *results;
	wl_bss_info_t UNALIGNED *bi, *bi_new, *bi_next;

	dhd_iscan_lock();

	DHD_ISCAN(("%s: Scan cache before delete\n",
		__FUNCTION__));
	dhd_iscan_print_cache(iscan_cur);

	if (!iscan_cur)
		goto done;

	list = (wl_iscan_results_t *)iscan_cur->iscan_buf;
	if (!list)
		goto done;

	results = (wl_scan_results_t *)&list->results;
	if (!results)
		goto done;

	if (results->version != WL_BSS_INFO_VERSION) {
		DHD_ERROR(("%s: results->version %d != WL_BSS_INFO_VERSION\n",
			__FUNCTION__, results->version));
		goto done;
	}

	bi = results->bss_info;
	for (i = 0; i < results->count; i++) {
		if (!bi)
			break;

		DHD_ISCAN(("%s: Find dups for BSS[%2.2d] %X:%X:%X:%X:%X:%X\n",
			__FUNCTION__, i, bi->BSSID.octet[0], bi->BSSID.octet[1], bi->BSSID.octet[2],
			bi->BSSID.octet[3], bi->BSSID.octet[4], bi->BSSID.octet[5]));

		dhd_iscan_delete_bss(dhdp, bi->BSSID.octet, iscan_cur);

		bi = (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length));
	}

done:
	DHD_ISCAN(("%s: Scan cache after delete\n", __FUNCTION__));
	dhd_iscan_print_cache(iscan_cur);
	dhd_iscan_unlock();
	return 0;
}

void
dhd_iscan_ind_scan_confirm(void *dhdp, bool status)
{
#ifdef NDIS
	
	dhd_iscan_del_timer(dhdp);
#endif

	dhd_ind_scan_confirm(dhdp, status);
}

int
dhd_iscan_request(void * dhdp, uint16 action)
{
	int rc;
	wl_iscan_params_t params;
	dhd_pub_t *dhd = dhd_bus_pub(dhdp);
	char buf[WLC_IOCTL_SMLEN];

#ifdef NDIS
	
	switch (action) {
		case WL_SCAN_ACTION_START:
			
			dhd_iscan_add_timer(dhdp);
			break;

		case WL_SCAN_ACTION_ABORT:
			
			
			dhd_iscan_ind_scan_confirm(dhdp, FALSE);
			break;
	}
#endif 

	memset(&params, 0, sizeof(wl_iscan_params_t));
	memcpy(&params.params.bssid, &ether_bcast, ETHER_ADDR_LEN);

	params.params.bss_type = DOT11_BSSTYPE_ANY;
	params.params.scan_type = DOT11_SCANTYPE_ACTIVE;

	params.params.nprobes = htod32(-1);
	params.params.active_time = htod32(-1);
	params.params.passive_time = htod32(-1);
	params.params.home_time = htod32(-1);
	params.params.channel_num = htod32(0);

	params.version = htod32(ISCAN_REQ_VERSION);
	params.action = htod16(action);
	params.scan_duration = htod16(0);

	bcm_mkiovar("iscan", (char *)&params, sizeof(wl_iscan_params_t), buf, WLC_IOCTL_SMLEN);
	rc = dhd_wl_ioctl(dhdp, WLC_SET_VAR, buf, WLC_IOCTL_SMLEN);

	return rc;
}

static int
dhd_iscan_get_partial_result(void *dhdp, uint *scan_count)
{
	wl_iscan_results_t *list_buf;
	wl_iscan_results_t list;
	wl_scan_results_t *results;
	iscan_buf_t *iscan_cur;
	int status = -1;
	dhd_pub_t *dhd = dhd_bus_pub(dhdp);
	int rc;

#if defined(UNDER_CE) && defined(EXT_STA)
	if (dhd->max_scan_result_age != 0) {
		
		dhd_iscan_prune_aged_results(dhdp, dhd->max_scan_result_age);
	}
#endif

	iscan_cur = dhd_iscan_allocate_buf(dhd, &iscan_chain);
	if (!iscan_cur) {
		DHD_ERROR(("%s: Failed to allocate node\n", __FUNCTION__));
		dhd_iscan_free_buf(dhdp, 0);
		dhd_iscan_request(dhdp, WL_SCAN_ACTION_ABORT);
		goto fail;
	}

	dhd_iscan_lock();

	memset(iscan_cur->iscan_buf, 0, WLC_IW_ISCAN_MAXLEN);
	list_buf = (wl_iscan_results_t*)iscan_cur->iscan_buf;
	results = &list_buf->results;
	results->buflen = WL_ISCAN_RESULTS_FIXED_SIZE;
	results->version = 0;
	results->count = 0;

	memset(&list, 0, sizeof(list));
	list.results.buflen = htod32(WLC_IW_ISCAN_MAXLEN);
	bcm_mkiovar("iscanresults", (char *)&list, WL_ISCAN_RESULTS_FIXED_SIZE,
		iscan_cur->iscan_buf, WLC_IW_ISCAN_MAXLEN);
	rc = dhd_wl_ioctl(dhdp, WLC_GET_VAR, iscan_cur->iscan_buf, WLC_IW_ISCAN_MAXLEN);

	results->buflen = dtoh32(results->buflen);
	results->version = dtoh32(results->version);
	*scan_count = results->count = dtoh32(results->count);
	status = dtoh32(list_buf->status);

	dhd_iscan_unlock();

	if (!(*scan_count))
		dhd_iscan_free_buf(dhdp, iscan_cur);
	else
		dhd_iscan_remove_duplicates(dhdp, iscan_cur);


fail:
	return status;
}

#ifdef NDIS

#define ISCAN_MAX_DELAY 45
NDIS_EVENT iscan_event;
HANDLE tHandle;
NDIS_SPIN_LOCK	dhd_iscan_queue_lock;
NDIS_MINIPORT_TIMER	ndis_timer;

static void
dhd_iscan_timer_function(
	IN PVOID systemspecific1,
	IN NDIS_HANDLE context,
	IN PVOID systemspecific2,
	IN PVOID systemspecific3
)
{
	DHD_ERROR(("%s() called -> send bad scan conf\n", __FUNCTION__));
	dhd_iscan_ind_scan_confirm((void *) context, FALSE);
}

int
dhd_iscan_del_timer(void * dhdp)
{
	DHD_INFO(("%s()\n", __FUNCTION__));
	return dhd_del_timer(dhdp, &ndis_timer);
}

int
dhd_iscan_add_timer(void * dhdp)
{
	DHD_INFO(("%s()\n", __FUNCTION__));
	
	dhd_iscan_del_timer(dhdp);

	
	return dhd_add_timer(dhdp, &ndis_timer, ISCAN_MAX_DELAY * 1000);
}

void
dhd_iscan_lock(void)
{
	NdisAcquireSpinLock(&dhd_iscan_queue_lock);
}

void
dhd_iscan_unlock(void)
{
	NdisReleaseSpinLock(&dhd_iscan_queue_lock);
}

void
dhd_iscan_notify(void)
{
	DHD_INFO(("%s: Entered\n", __FUNCTION__));
	NdisSetEvent(&iscan_event);
}

static void
dhd_iscan_func(void *h)
{
	int status;
	uint scan_count;
	dhd_pub_t *dhd = dhd_bus_pub(h);

	
	CeSetThreadPriority(GetCurrentThread(), 128);
	DHD_ISCAN(("%s: thread created\n", __FUNCTION__));

	while (TRUE) {
		NdisWaitEvent(&iscan_event, 0);		
		NdisResetEvent(&iscan_event);		
		DHD_ISCAN(("%s: thread scheduled\n", __FUNCTION__));

		status = dhd_iscan_get_partial_result(h, &scan_count);

		if (status == WL_SCAN_RESULTS_PARTIAL) {
			dhd_iscan_request(h, WL_SCAN_ACTION_CONTINUE);
		} else if (status == WL_SCAN_RESULTS_SUCCESS && scan_count) {
			dhd_iscan_ind_scan_confirm(h, TRUE);
		} else if (status == WL_SCAN_RESULTS_ABORTED ||
			status == WL_SCAN_RESULTS_NO_MEM) {
			dhd_iscan_request(h, WL_SCAN_ACTION_ABORT);
		}
	}
}

int
dhd_iscan_attach(void *dhdp)
{
	DHD_ISCAN(("%s: dhdp = 0x%x\n", __FUNCTION__, dhdp));

	NdisInitializeEvent(&iscan_event);
	NdisResetEvent(&iscan_event);
	NdisAllocateSpinLock(&dhd_iscan_queue_lock);

	dhd_init_timer(dhdp, dhd_iscan_timer_function, dhdp, &ndis_timer);

	tHandle = CreateThread(NULL,
		0,
		(LPTHREAD_START_ROUTINE)dhd_iscan_func,
		(void *)dhdp,
		0,
		&iscan_thread_id);

	if (!iscan_thread_id)
		return NDIS_STATUS_FAILURE;

	return NDIS_STATUS_SUCCESS;
}

void
dhd_iscan_deattach(void *dhdp)
{
	dhd_release_timer(dhdp, &ndis_timer);
	NdisFreeEvent(&iscan_event);
	NdisFreeSpinLock(&dhd_iscan_queue_lock);
	CloseHandle(tHandle);
}
#endif 
#endif 
