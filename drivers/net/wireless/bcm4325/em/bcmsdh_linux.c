/*
 * SDIO access interface for drivers - linux specific (pci only)
 *
 * Copyright (C) 2009, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: bcmsdh_linux.c,v 1.42.10.11.4.12 2009/10/15 22:43:50 Exp $
 */



#define __UNDEF_NO_VERSION__

#include <typedefs.h>
#include <linuxver.h>

#include <linux/pci.h>
#include <linux/completion.h>

#include <osl.h>
#include <pcicfg.h>
#include <bcmdefs.h>
#include <bcmdevs.h>

#if defined(OOB_INTR_ONLY)
#include <linux/irq.h>
extern void dhdsdio_isr(void * args);
#include <dngl_stats.h>
#include <dhd.h>
#endif 

#if defined(CONFIG_MACH_SANDGATE2G) || defined(CONFIG_MACH_LOGICPD_PXA270)
#if !defined(BCMPLATFORM_BUS)
#define BCMPLATFORM_BUS
#endif 

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19))
#include <linux/platform_device.h>
#endif 
#endif 


typedef struct bcmsdh_hc bcmsdh_hc_t;

struct bcmsdh_hc {
	bcmsdh_hc_t *next;
#ifdef BCMPLATFORM_BUS
	struct device *dev;			
#else
	struct pci_dev *dev;		
#endif 
	osl_t *osh;
	void *regs;			
	bcmsdh_info_t *sdh;		
	void *ch;
	unsigned int oob_irq;
};
static bcmsdh_hc_t *sdhcinfo = NULL;


static bcmsdh_driver_t drvinfo = {NULL, NULL};


#ifdef BCMDBG_ERR
#define SDLX_MSG(x)	printf x
#else
#define SDLX_MSG(x)
#endif 


bool
bcmsdh_chipmatch(uint16 vendor, uint16 device)
{
	

#ifdef BCMSDIOH_STD
	
	if (vendor == VENDOR_SI_IMAGE) {
		return (TRUE);
	}
	
	if (device == BCM27XX_SDIOH_ID && vendor == VENDOR_BROADCOM) {
		return (TRUE);
	}
	
	if (device == SDIOH_FPGA_ID && vendor == VENDOR_BROADCOM) {
		return (TRUE);
	}
	
	if (device == PCIXX21_SDIOH_ID && vendor == VENDOR_TI) {
		return (TRUE);
	}
	if (device == PCIXX21_SDIOH0_ID && vendor == VENDOR_TI) {
		return (TRUE);
	}
	
	if (device == R5C822_SDIOH_ID && vendor == VENDOR_RICOH) {
		return (TRUE);
	}
	
	if (device == JMICRON_SDIOH_ID && vendor == VENDOR_JMICRON) {
		return (TRUE);
	}

#endif 
#ifdef BCMSDIOH_SPI
	
	if (device == SPIH_FPGA_ID && vendor == VENDOR_BROADCOM) {
		printf("Found PCI SPI Host Controller\n");
		return (TRUE);
	}

#endif 

	return (FALSE);
}

#if defined(BCMPLATFORM_BUS)
#if defined(BCMLXSDMMC)

int bcmsdh_probe_em(struct device *dev);
int bcmsdh_remove_em(struct device *dev);

EXPORT_SYMBOL(bcmsdh_probe_em);
EXPORT_SYMBOL(bcmsdh_remove_em);

#else

static int __devinit bcmsdh_probe_em(struct device *dev);
static int __devexit bcmsdh_remove_em(struct device *dev);
#endif 

#ifndef BCMLXSDMMC
static struct device_driver bcmsdh_driver = {
	.name		= "pxa2xx-mci",
	.bus		= &platform_bus_type,
	.probe		= bcmsdh_probe_em,
	.remove		= bcmsdh_remove_em,
	.suspend	= NULL,
	.resume		= NULL,
	};
#endif 

#ifndef BCMLXSDMMC
static
#endif 
int bcmsdh_probe_em(struct device *dev)
{
	osl_t *osh = NULL;
	bcmsdh_hc_t *sdhc = NULL;
	ulong regs = 0;
	bcmsdh_info_t *sdh = NULL;
#if !defined(BCMLXSDMMC) && defined(BCMPLATFORM_BUS)
	struct platform_device *pdev;
	struct resource *r;
#endif 
	int irq = 0;
	uint32 vendevid;

#if !defined(BCMLXSDMMC) && defined(BCMPLATFORM_BUS)
	pdev = to_platform_device(dev);
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!r || irq == NO_IRQ)
		return -ENXIO;
#endif 

#if defined(OOB_INTR_ONLY)
	irq = dhd_customer_oob_irq_map();
	if  (irq < 0) {
		SDLX_MSG(("%s: Host irq is not defined\n", __FUNCTION__));
		return 1;
	}
#endif 

	
	if (!(osh = osl_attach(dev, PCI_BUS, FALSE))) {
		SDLX_MSG(("%s: osl_attach failed\n", __FUNCTION__));
		goto err;
	}
	if (!(sdhc = MALLOC(osh, sizeof(bcmsdh_hc_t)))) {
		SDLX_MSG(("%s: out of memory, allocated %d bytes\n",
			__FUNCTION__,
			MALLOCED(osh)));
		goto err;
	}
	bzero(sdhc, sizeof(bcmsdh_hc_t));
	sdhc->osh = osh;

	sdhc->dev = (void *)dev;

#ifdef BCMLXSDMMC
	if (!(sdh = bcmsdh_attach(osh, (void *)0,
	                          (void **)&regs, irq))) {
		SDLX_MSG(("%s: bcmsdh_attach failed\n", __FUNCTION__));
		goto err;
	}
#else
	if (!(sdh = bcmsdh_attach(osh, (void *)r->start,
	                          (void **)&regs, irq))) {
		SDLX_MSG(("%s: bcmsdh_attach failed\n", __FUNCTION__));
		goto err;
	}
#endif 
	sdhc->sdh = sdh;
	sdhc->oob_irq = irq;

	
	sdhc->next = sdhcinfo;
	sdhcinfo = sdhc;
	
	vendevid = bcmsdh_query_device(sdh);

	
	if (!(sdhc->ch = drvinfo.attach((vendevid >> 16),
	                                 (vendevid & 0xFFFF), 0, 0, 0, 0,
	                                (void *)regs, NULL, sdh))) {
		SDLX_MSG(("%s: device attach failed\n", __FUNCTION__));
		goto err;
	}

	return 0;

	
err:
	if (sdhc) {
		if (sdhc->sdh)
			bcmsdh_detach(sdhc->osh, sdhc->sdh);
		MFREE(osh, sdhc, sizeof(bcmsdh_hc_t));
	}
	if (osh)
		osl_detach(osh);
	return -ENODEV;
}

#ifndef BCMLXSDMMC
static
#endif 
int bcmsdh_remove_em(struct device *dev)
{
	bcmsdh_hc_t *sdhc, *prev;
	osl_t *osh;

	sdhc = sdhcinfo;
	drvinfo.detach(sdhc->ch);
	bcmsdh_detach(sdhc->osh, sdhc->sdh);
	
	for (sdhc = sdhcinfo, prev = NULL; sdhc; sdhc = sdhc->next) {
		if (sdhc->dev == (void *)dev) {
			if (prev)
				prev->next = sdhc->next;
			else
				sdhcinfo = NULL;
			break;
		}
		prev = sdhc;
	}
	if (!sdhc) {
		SDLX_MSG(("%s: failed\n", __FUNCTION__));
		return 0;
	}


	
	osh = sdhc->osh;
	MFREE(osh, sdhc, sizeof(bcmsdh_hc_t));
	osl_detach(osh);

#if !defined(BCMLXSDMMC)
	dev_set_drvdata(dev, NULL);
#endif 

	return 0;
}

#else 

#if !defined(BCMLXSDMMC)

static int __devinit bcmsdh_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void __devexit bcmsdh_pci_remove(struct pci_dev *pdev);


static struct pci_device_id bcmsdh_pci_devid[] __devinitdata = {
	{ vendor: PCI_ANY_ID,
	device: PCI_ANY_ID,
	subvendor: PCI_ANY_ID,
	subdevice: PCI_ANY_ID,
	class: 0,
	class_mask: 0,
	driver_data: 0,
	},
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, bcmsdh_pci_devid);


static struct pci_driver bcmsdh_pci_driver = {
	node:		{},
	name:		"bcmsdh",
	id_table:	bcmsdh_pci_devid,
	probe:		bcmsdh_pci_probe,
	remove:		bcmsdh_pci_remove,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0))
	save_state:	NULL,
#endif
	suspend:	NULL,
	resume:		NULL,
	};


extern uint sd_pci_slot;	





module_param(sd_pci_slot, uint, 0);



static int __devinit
bcmsdh_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	osl_t *osh = NULL;
	bcmsdh_hc_t *sdhc = NULL;
	ulong regs;
	bcmsdh_info_t *sdh = NULL;
	int rc;

	if(sd_pci_slot!=0xFFFFffff) {
		if(pdev->bus->number!=(sd_pci_slot>>16) || PCI_SLOT(pdev->devfn)!=(sd_pci_slot&0xffff)) {
			SDLX_MSG(("%s: %s: bus %X, slot %X, vendor %X, device %X (but want bus %X: slot %X)\n",
				__FUNCTION__,bcmsdh_chipmatch(pdev->vendor, pdev->device)?"Found compatible SDIOHC":"Probing unknown device",
				pdev->bus->number, PCI_SLOT(pdev->devfn),pdev->vendor,pdev->device, sd_pci_slot>>16,sd_pci_slot&0xffff));
			return -ENODEV;
		}
		SDLX_MSG(("%s: %s: bus %X, slot %X, vendor %X, device %X (good PCI location)\n",
			__FUNCTION__,bcmsdh_chipmatch(pdev->vendor, pdev->device)?"Using compatible SDIOHC":"WARNING, forced use of unkown device",
			pdev->bus->number, PCI_SLOT(pdev->devfn),pdev->vendor,pdev->device));
	}

	if ((pdev->vendor == VENDOR_TI) && ((pdev->device == PCIXX21_FLASHMEDIA_ID) ||
	    (pdev->device == PCIXX21_FLASHMEDIA0_ID))) {
		uint32 config_reg;

		SDLX_MSG(("%s: Disabling TI FlashMedia Controller.\n", __FUNCTION__));
		if (!(osh = osl_attach(pdev, PCI_BUS, FALSE))) {
			SDLX_MSG(("%s: osl_attach failed\n", __FUNCTION__));
			goto err;
		}

		config_reg = OSL_PCI_READ_CONFIG(osh, 0x4c, 4);

		
		config_reg |= 0x02;
		OSL_PCI_WRITE_CONFIG(osh, 0x4c, 4, config_reg);
		osl_detach(osh);
	}
	
	
	if (!bcmsdh_chipmatch(pdev->vendor, pdev->device)) {
		return -ENODEV;
	}

	
	SDLX_MSG(("%s: Found possible SDIO Host Controller: bus %d slot %d func %d irq %d\n",
		__FUNCTION__,
		pdev->bus->number, PCI_SLOT(pdev->devfn),
		PCI_FUNC(pdev->devfn), pdev->irq));

	

	
	if (!(osh = osl_attach(pdev, PCI_BUS, FALSE))) {
		SDLX_MSG(("%s: osl_attach failed\n", __FUNCTION__));
		goto err;
	}
	if (!(sdhc = MALLOC(osh, sizeof(bcmsdh_hc_t)))) {
		SDLX_MSG(("%s: out of memory, allocated %d bytes\n",
			__FUNCTION__,
			MALLOCED(osh)));
		goto err;
	}
	bzero(sdhc, sizeof(bcmsdh_hc_t));
	sdhc->osh = osh;

	sdhc->dev = pdev;

	
	pci_set_master(pdev);
	rc = pci_enable_device(pdev);
	if (rc) {
		SDLX_MSG(("%s: Cannot enble PCI device\n", __FUNCTION__));
		goto err;
	}
	if (!(sdh = bcmsdh_attach(osh, (void *)(uintptr)pci_resource_start(pdev, 0),
	                          (void **)&regs, pdev->irq))) {
		SDLX_MSG(("%s: bcmsdh_attach failed\n", __FUNCTION__));
		goto err;
	}

	sdhc->sdh = sdh;

	
	if (!(sdhc->ch = drvinfo.attach(VENDOR_BROADCOM, 
	                                bcmsdh_query_device(sdh) & 0xFFFF, 0, 0, 0, 0,
	                                (void *)regs, NULL, sdh))) {
		SDLX_MSG(("%s: device attach failed\n", __FUNCTION__));
		goto err;
	}

	
	sdhc->next = sdhcinfo;
	sdhcinfo = sdhc;

	return 0;

	
err:
	if (sdhc->sdh)
		bcmsdh_detach(sdhc->osh, sdhc->sdh);
	if (sdhc)
		MFREE(osh, sdhc, sizeof(bcmsdh_hc_t));
	if (osh)
		osl_detach(osh);
	return -ENODEV;
}



static void __devexit
bcmsdh_pci_remove(struct pci_dev *pdev)
{
	bcmsdh_hc_t *sdhc, *prev;
	osl_t *osh;

	
	for (sdhc = sdhcinfo, prev = NULL; sdhc; sdhc = sdhc->next) {
		if (sdhc->dev == pdev) {
			if (prev)
				prev->next = sdhc->next;
			else
				sdhcinfo = NULL;
			break;
		}
		prev = sdhc;
	}
	if (!sdhc)
		return;

	drvinfo.detach(sdhc->ch);

	bcmsdh_detach(sdhc->osh, sdhc->sdh);

	
	osh = sdhc->osh;
	MFREE(osh, sdhc, sizeof(bcmsdh_hc_t));
	osl_detach(osh);
}
#endif 
#endif 

extern int sdio_function_init(void);

int
bcmsdh_register(bcmsdh_driver_t *driver)
{
	int error = 0;

	drvinfo = *driver;

#if defined(BCMPLATFORM_BUS)
#if defined(BCMLXSDMMC)
	SDLX_MSG(("Linux Kernel SDIO/MMC Driver\n"));
	error = sdio_function_init();
#else
	SDLX_MSG(("Intel PXA270 SDIO Driver\n"));
	error = driver_register(&bcmsdh_driver);
#endif 
	return error;
#endif 

#if !defined(BCMPLATFORM_BUS) && !defined(BCMLXSDMMC)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0))
	if (!(error = pci_module_init(&bcmsdh_pci_driver)))
		return 0;
#else
	if (!(error = pci_register_driver(&bcmsdh_pci_driver)))
		return 0;
#endif

	SDLX_MSG(("%s: pci_module_init failed 0x%x\n", __FUNCTION__, error));
#endif 

	return error;
}

extern void sdio_function_cleanup(void);

void
bcmsdh_unregister(void)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0))
	if (bcmsdh_pci_driver.node.next)
#endif

#if defined(BCMPLATFORM_BUS) && !defined(BCMLXSDMMC)
		driver_unregister(&bcmsdh_driver);
#endif
#if defined(BCMLXSDMMC)
	sdio_function_cleanup();
#endif 
#if !defined(BCMPLATFORM_BUS) && !defined(BCMLXSDMMC)
		pci_unregister_driver(&bcmsdh_pci_driver);
#endif 
}

#if defined(OOB_INTR_ONLY)
static irqreturn_t wlan_oob_irq(int irq, void *dev_id)
{
	dhd_pub_t *dhdp;

	dhdp = (dhd_pub_t *)sdhcinfo->dev->driver_data;

	if (dhdp == NULL) {
		disable_irq(sdhcinfo->oob_irq);
		printk("Out of band GPIO interrupt fired way too early\n");
		return IRQ_HANDLED;
	}

	WAKE_LOCK_TIMEOUT(dhdp, WAKE_LOCK_TMOUT, 25);

	dhdsdio_isr((void *)dhdp->bus);

	return IRQ_HANDLED;
}

int bcmsdh_register_oob_intr(void * dhdp)
{
	int error = 0;

	SDLX_MSG(("%s Enter\n", __FUNCTION__));

	sdhcinfo->dev->driver_data = dhdp;

	set_irq_wake(sdhcinfo->oob_irq, 1);

	
	error = request_irq(sdhcinfo->oob_irq, wlan_oob_irq, IRQF_TRIGGER_FALLING,
		"bcmsdh_sdmmc", NULL);

	if (error)
		return -ENODEV;

	return 0;
}

void bcmsdh_unregister_oob_intr(void)
{
	SDLX_MSG(("%s: Enter\n", __FUNCTION__));

	set_irq_wake(sdhcinfo->oob_irq, 0);
	disable_irq(sdhcinfo->oob_irq);	
	free_irq(sdhcinfo->oob_irq, NULL);
}

void bcmsdh_oob_intr_set(bool enable)
{
	if (enable)
		enable_irq(sdhcinfo->oob_irq);
	else
		disable_irq(sdhcinfo->oob_irq);
}
#endif 


extern uint sd_msglevel;	
module_param(sd_msglevel, uint, 0);

extern uint sd_power;	
module_param(sd_power, uint, 0);

extern uint sd_clock;	
module_param(sd_clock, uint, 0);

extern uint sd_divisor;	
module_param(sd_divisor, uint, 0);

extern uint sd_sdmode;	
module_param(sd_sdmode, uint, 0);

extern uint sd_hiok;	
module_param(sd_hiok, uint, 0);

extern uint sd_f2_blocksize;
module_param(sd_f2_blocksize, int, 0);


#ifdef BCMSDH_MODULE
EXPORT_SYMBOL(bcmsdh_attach);
EXPORT_SYMBOL(bcmsdh_detach);
EXPORT_SYMBOL(bcmsdh_intr_query);
EXPORT_SYMBOL(bcmsdh_intr_enable);
EXPORT_SYMBOL(bcmsdh_intr_disable);
EXPORT_SYMBOL(bcmsdh_intr_reg);
EXPORT_SYMBOL(bcmsdh_intr_dereg);

#if defined(DHD_DEBUG) || defined(BCMDBG)
EXPORT_SYMBOL(bcmsdh_intr_pending);
#endif

EXPORT_SYMBOL(bcmsdh_devremove_reg);
EXPORT_SYMBOL(bcmsdh_cfg_read);
EXPORT_SYMBOL(bcmsdh_cfg_write);
EXPORT_SYMBOL(bcmsdh_cis_read);
EXPORT_SYMBOL(bcmsdh_reg_read);
EXPORT_SYMBOL(bcmsdh_reg_write);
EXPORT_SYMBOL(bcmsdh_regfail);
EXPORT_SYMBOL(bcmsdh_send_buf);
EXPORT_SYMBOL(bcmsdh_recv_buf);

EXPORT_SYMBOL(bcmsdh_rwdata);
EXPORT_SYMBOL(bcmsdh_abort);
EXPORT_SYMBOL(bcmsdh_query_device);
EXPORT_SYMBOL(bcmsdh_query_iofnum);
EXPORT_SYMBOL(bcmsdh_iovar_op);
EXPORT_SYMBOL(bcmsdh_register);
EXPORT_SYMBOL(bcmsdh_unregister);
EXPORT_SYMBOL(bcmsdh_chipmatch);
EXPORT_SYMBOL(bcmsdh_reset);

EXPORT_SYMBOL(bcmsdh_get_dstatus);
EXPORT_SYMBOL(bcmsdh_cfg_read_word);
EXPORT_SYMBOL(bcmsdh_cfg_write_word);
EXPORT_SYMBOL(bcmsdh_cur_sbwad);
EXPORT_SYMBOL(bcmsdh_chipinfo);

#endif 
