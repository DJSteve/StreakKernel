/*
 * Header file describing the internal (inter-module) DHD interfaces.
 *
 * Provides type definitions and function prototypes used to link the
 * DHD OS, bus, and protocol modules.
 *
 * Copyright (C) 2009, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: dhd.h,v 1.32.4.7.2.4.28.37 2009/10/30 22:43:52 Exp $
 */



#ifndef _dhd_h_
#define _dhd_h_

#if defined(LINUX)
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_HAS_WAKELOCK)
#include <linux/wakelock.h>
#endif 


#else 
#define ENOMEM		1
#define EFAULT      2
#define EINVAL		3
#define EIO			4
#define ETIMEDOUT	5
#define ERESTARTSYS 6
#endif 

#include <wlioctl.h>

#ifdef NDIS60
#include <wdf.h>
#include <WdfMiniport.h>
#endif 


struct dhd_bus;
struct dhd_prot;
struct dhd_info;


enum dhd_bus_state {
	DHD_BUS_DOWN,		
	DHD_BUS_LOAD,		
	DHD_BUS_DATA		
};

enum dhd_bus_wake_state {
	WAKE_LOCK_OFF,
	WAKE_LOCK_PRIV,
	WAKE_LOCK_DPC,
	WAKE_LOCK_IOCTL,
	WAKE_LOCK_DOWNLOAD,
	WAKE_LOCK_TMOUT,
	WAKE_LOCK_WATCHDOG,
	WAKE_LOCK_LINK_DOWN_TMOUT,
	WAKE_LOCK_MAX
};


typedef struct dhd_pub {
	
	osl_t *osh;		
	struct dhd_bus *bus;	
	struct dhd_prot *prot;	
	struct dhd_info  *info; 

	
	bool up;		
	bool txoff;		
	bool dongle_reset;  
	enum dhd_bus_state busstate;
	uint hdrlen;		
	uint maxctl;		
	uint rxsz;		
	uint8 wme_dp;	

	
	bool iswl;		
	ulong drv_version;	
	struct ether_addr mac;	
	dngl_stats_t dstats;	

	
	ulong tx_packets;	
	ulong tx_multicast;	
	ulong tx_errors;	
	ulong tx_ctlpkts;	
	ulong tx_ctlerrs;	
	ulong rx_packets;	
	ulong rx_multicast;	
	ulong rx_errors;	
	ulong rx_ctlpkts;	
	ulong rx_ctlerrs;	
	ulong rx_dropped;	
	ulong rx_flushed;  

	ulong rx_readahead_cnt;	
	ulong tx_realloc;	

	
	int bcmerror;
	uint tickcnt;

	
	int dongle_error;

	uint8 country_code[WLC_CNTRY_BUF_SZ];

#if defined(LINUX) || defined(linux)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_HAS_WAKELOCK)
	struct wake_lock wakelock[WAKE_LOCK_MAX];
#endif 
#endif 

#ifdef NDIS
	PDEVICE_OBJECT pdo;
	PDEVICE_OBJECT fdo;
	PDEVICE_OBJECT nextDeviceObj;
#ifdef NDIS60
	WDFDEVICE wdfDevice;
#endif 
#endif 
#ifdef WLBTAMP
	uint16	maxdatablks;
#endif 
} dhd_pub_t;

#ifdef NDIS60

typedef struct _wdf_device_info {
	dhd_pub_t *dhd;
} wdf_device_info_t;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(wdf_device_info_t, dhd_get_wdf_device_info)


#endif 

#if defined(LINUX) || defined(linux)
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_PM_SLEEP)

	#define DHD_PM_RESUME_WAIT_INIT(a) DECLARE_WAIT_QUEUE_HEAD(a);
	#define _DHD_PM_RESUME_WAIT(a, b) do {\
			int retry = 0; \
			while (dhd_mmc_suspend && retry++ != b) { \
				wait_event_timeout(a, FALSE, HZ/100); \
			} \
		} 	while (0)
	#define DHD_PM_RESUME_WAIT(a) 			_DHD_PM_RESUME_WAIT(a, 30)
	#define DHD_PM_RESUME_WAIT_FOREVER(a) 	_DHD_PM_RESUME_WAIT(a, ~0)
	#define DHD_PM_RESUME_RETURN_ERROR(a)	do { if (dhd_mmc_suspend) return a; } while (0)
	#define DHD_PM_RESUME_RETURN		do { if (dhd_mmc_suspend) return; } while (0)

	#define DHD_SPINWAIT_SLEEP_INIT(a) DECLARE_WAIT_QUEUE_HEAD(a);
	#define SPINWAIT_SLEEP(a, exp, us) do { \
		uint countdown = (us) + 9; \
		while ((exp) && (countdown >= 10)) { \
			wait_event_timeout(a, FALSE, HZ/100); \
			countdown -= 10; \
		} \
	} while (0)

	#else

	#define DHD_PM_RESUME_WAIT_INIT(a)
	#define DHD_PM_RESUME_WAIT(a)
	#define DHD_PM_RESUME_WAIT_FOREVER(a)
	#define DHD_PM_RESUME_RETURN_ERROR(a)
	#define DHD_PM_RESUME_RETURN

	#define DHD_SPINWAIT_SLEEP_INIT(a)
	#define SPINWAIT_SLEEP(a, exp, us)  do { \
		uint countdown = (us) + 9; \
		while ((exp) && (countdown >= 10)) { \
			OSL_DELAY(10);  \
			countdown -= 10;  \
		} \
	} while (0)

	#endif 
#else
	#define DHD_SPINWAIT_SLEEP_INIT(a)
	#define SPINWAIT_SLEEP(a, exp, us)  do { \
		uint countdown = (us) + 9; \
		while ((exp) && (countdown >= 10)) { \
			OSL_DELAY(10);  \
			countdown -= 10;  \
		} \
	} while (0)
#endif 

#if defined(LINUX) || defined(linux)

inline static void WAKE_LOCK_INIT(dhd_pub_t * dhdp, int index, char * y)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_HAS_WAKELOCK)
	wake_lock_init(&dhdp->wakelock[index], WAKE_LOCK_SUSPEND, y);
#endif 
}

inline static void WAKE_LOCK(dhd_pub_t * dhdp, int index)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_HAS_WAKELOCK)
	wake_lock(&dhdp->wakelock[index]);
#endif 
}

inline static void WAKE_UNLOCK(dhd_pub_t * dhdp, int index)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_HAS_WAKELOCK)
	wake_unlock(&dhdp->wakelock[index]);
#endif 
}

inline static void WAKE_LOCK_TIMEOUT(dhd_pub_t * dhdp, int index, long time)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_HAS_WAKELOCK)
	wake_lock_timeout(&dhdp->wakelock[index], time);
#endif 
}

inline static void WAKE_LOCK_DESTROY(dhd_pub_t * dhdp, int index)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_HAS_WAKELOCK)
	wake_lock_destroy(&dhdp->wakelock[index]);
#endif 
}

#endif 
typedef struct dhd_if_event {
	uint8 ifidx;
	uint8 action;
} dhd_if_event_t;




osl_t *dhd_osl_attach(void *pdev, uint bustype);
void dhd_osl_detach(osl_t *osh);


extern dhd_pub_t *dhd_attach(osl_t *osh, struct dhd_bus *bus, uint bus_hdrlen);
extern int dhd_net_attach(dhd_pub_t *dhdp, int idx);


extern void dhd_detach(dhd_pub_t *dhdp);


extern void dhd_txflowcontrol(dhd_pub_t *dhdp, int ifidx, bool on);


extern void dhd_rx_frame(dhd_pub_t *dhdp, int ifidx, void *rxp, int numpkt);


extern char *dhd_ifname(dhd_pub_t *dhdp, int idx);


extern void dhd_sched_dpc(dhd_pub_t *dhdp);


extern void dhd_txcomplete(dhd_pub_t *dhdp, void *txp, bool success);


extern int  dhdcdc_query_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf, uint len);


extern int dhd_os_proto_block(dhd_pub_t * pub);
extern int dhd_os_proto_unblock(dhd_pub_t * pub);
extern int dhd_os_ioctl_resp_wait(dhd_pub_t * pub, uint * condition, bool * pending);
extern int dhd_os_ioctl_resp_wake(dhd_pub_t * pub);
extern unsigned int dhd_os_get_ioctl_resp_timeout(void);
extern void dhd_os_set_ioctl_resp_timeout(unsigned int timeout_msec);
extern void * dhd_os_open_image(char * filename);
extern int dhd_os_get_image_block(char * buf, int len, void * image);
extern void dhd_os_close_image(void * image);
extern void dhd_os_wd_timer(void *bus, uint wdtick);
extern void dhd_os_sdlock(dhd_pub_t * pub);
extern void dhd_os_sdunlock(dhd_pub_t * pub);
extern void dhd_os_sdlock_txq(dhd_pub_t * pub);
extern void dhd_os_sdunlock_txq(dhd_pub_t * pub);
extern void dhd_os_sdlock_rxq(dhd_pub_t * pub);
extern void dhd_os_sdunlock_rxq(dhd_pub_t * pub);
extern void dhd_os_sdlock_sndup_rxq(dhd_pub_t * pub);
extern void dhd_customer_gpio_wlan_ctrl(int onoff);
extern void dhd_os_sdunlock_sndup_rxq(dhd_pub_t * pub);
#if defined(OOB_INTR_ONLY)
extern int dhd_customer_oob_irq_map(void);
#endif 


typedef struct {
	uint32 limit;		
	uint32 increment;	
	uint32 elapsed;		
	uint32 tick;		
} dhd_timeout_t;

extern void dhd_timeout_start(dhd_timeout_t *tmo, uint usec);
extern int dhd_timeout_expired(dhd_timeout_t *tmo);

extern int dhd_ifname2idx(struct dhd_info *dhd, char *name);
extern int wl_host_event(struct dhd_info *dhd, int *idx, void *pktdata,
wl_event_msg_t *, void **data_ptr);
extern void wl_event_to_host_order(wl_event_msg_t * evt);

extern void dhd_common_init(void);

extern int dhd_add_if(struct dhd_info *dhd, int ifidx, void *handle, char *name, uint8 *mac_addr);
extern void dhd_del_if(struct dhd_info *dhd, int ifidx);


extern int dhd_sendpkt(dhd_pub_t *dhdp, int ifidx, void *pkt);



extern void dhd_sendup_event_common(dhd_pub_t *dhdp, wl_event_msg_t *event, void *data);
extern void dhd_sendup_event(dhd_pub_t *dhdp, wl_event_msg_t *event, void *data);
extern int dhd_bus_devreset(dhd_pub_t *dhdp, uint8 flag);
extern uint dhd_bus_status(dhd_pub_t *dhdp);
extern int  dhd_bus_start(dhd_pub_t *dhdp);


#ifdef UNDER_CE
extern uint dhd_rxflow_mode(dhd_pub_t *dhdp, uint mode, bool set);

extern void dhd_rx_flow(void *dhdp, bool enable);

extern void dhd_sendup_indicate(dhd_pub_t *dhdp);

extern void dhd_thread_priority(dhd_pub_t *dhdp, uint thread, uint *prio, bool set);

extern void dhd_thread_quantum(dhd_pub_t *dhdp, uint thread, uint *tq, bool set);

extern uint32 dhd_rx_flow_hilimit(dhd_pub_t *dhdp, uint32 val);

extern uint32 dhd_rx_flow_lowlimit(dhd_pub_t *dhdp, uint32 val);

enum dhd_thread {
	DHD_DPC,			
	DHD_RXDPC,			
	DHD_SENDUP,		
	DHD_WATCHDOG		
};
#endif 


typedef enum cust_gpio_modes {
	WLAN_RESET_ON,
	WLAN_RESET_OFF,
	WLAN_POWER_ON,
	WLAN_POWER_OFF
} cust_gpio_modes_t;





extern uint dhd_watchdog_ms;


extern uint dhd_intr;


extern uint dhd_poll;


extern int dhd_idletime;
#define DHD_IDLETIME_TICKS 1


extern uint dhd_sdiod_drive_strength;


extern uint dhd_force_tx_queueing;

#ifdef SDTEST

extern uint dhd_pktgen;


extern uint dhd_pktgen_len;
#define MAX_PKTGEN_LEN 1800
#endif



#define MOD_PARAM_PATHLEN	2048
extern char fw_path[MOD_PARAM_PATHLEN];
extern char nv_path[MOD_PARAM_PATHLEN];


#define DHD_MAX_IFS	16
#define DHD_DEL_IF	-0xe
#define DHD_BAD_IF	-0xf

#ifdef APSTA_PINGTEST
#define MAX_GUEST 8
#endif

#endif 
