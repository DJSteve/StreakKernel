/*
 * SPROM format definitions for the Broadcom 47xx and 43xx chip family.
 *
 * $Id: sbsprom.h,v 1.6 2007/08/16 22:58:33 Exp $
 * Copyright(c) 2002 Broadcom Corporation
 */

#ifndef	_SBSPROM_H
#define	_SBSPROM_H

#include "typedefs.h"
#include "bcmdevs.h"


#define SRW		2


#define CFG_SROM_WRITABLE_OFFSET	0x88
#define SROM_WRITEABLE			0x10


#define SBCORES_BASE	0x18000000
#define SBCORES_EACH	0x1000


#define SROM_BASE	4096


#define SROM_SIZE	64

#define SROM_BYTES	(SROM_SIZE * SRW)

#define MAX_FN		4


#define SROM_HWCTL	0
#define HW_FUNMSK	0x000f
#define HW_FCLK		0x0200
#define HW_CBM		0x0400
#define HW_PIMSK	0xf000
#define HW_PISHIFT	12
#define HW_4301PISHIFT 13
#define HW_PI4402	0x2
#define HW_FUN4401	0x0001
#define HW_FCLK4402	0x0000


#define SROM_COMMPW		1

#define BR_PRESSHIFT	8

#define BR_SIZESHIFT	9


#define SROM_SSID	2


#define SROM_VID	3


#define SROM_FN0	4
#define SROM_FNSZ	8



#define SRFN_DID	0


#define SRFN_CCL	1

#define SRFN_CCHD0	2


#define SRFN_PMED123	3

#define PME_IL		0
#define PME_ENET0	1
#define PME_ENET1	2
#define PME_CODEC	3

#define PME_4402_ENET	0
#define PME_4402_CODEC	1
#define PME_4301_WL	2
#define PMEREP_4402_ENET	(PMERD3CV | PMERD3CA | PMERD3H | PMERD2 | PMERD1 | PMERD0 | PME)


#define SRFN_B1PMER	4
#define B1E		1
#define B1SZMSK	0xe
#define B1SZSH		1
#define PMERMSK	0x0ff0
#define PME		0x0010
#define PMERD0		0x0020
#define PMERD1		0x0040
#define PMERD2		0x0080
#define PMERD3H	0x0100
#define PMERD3CA	0x0200
#define PMERD3CV	0x0400
#define IGNCLKRR	0x0800
#define B0LMSK		0xf000


#define SRFN_B0H	5

#define SRFN_CISL	6
#define SRFN_CISH	7


#define SROM_I_MACHI	36
#define SROM_I_MACMID	37
#define SROM_I_MACLO	38


#define SROM_W0_MACHI	36
#define SROM_W0_MACMID	37
#define SROM_W0_MACLO	38


#define SROM_E0_MACHI	39
#define SROM_E0_MACMID	40
#define SROM_E0_MACLO	41


#define SROM_E1_MACHI	42
#define SROM_E1_MACMID	43
#define SROM_E1_MACLO	44


#define SROM_W1_MACHI	42
#define SROM_W1_MACMID	43
#define SROM_W1_MACLO	44

#define SROM_EPHY	45


#define SROM_REV_AA_LOCK	46


#define SROM_WL0_PAB0	47
#define SROM_WL0_PAB1	48
#define SROM_WL0_PAB2	49
#define SROM_WL0_PAB3	50
#define SROM_WL0_PAB4	51


#define SROM_WL_MAXPWR	52


#define SROM_WL1_PAB0	53
#define SROM_WL1_PAB1	54
#define SROM_WL1_PAB2	55


#define SROM_ITT        56


#define SROM_WL_OEM	59
#define SROM_OEM_SIZE	4



#define BU4710_SSID	0x0400
#define VSIM4710_SSID	0x0401
#define QT4710_SSID	0x0402

#define BU4610_SSID	0x0403
#define VSIM4610_SSID	0x0404

#define BU4307_SSID	0x0405
#define BCM94301CB_SSID	0x0406
#define BCM94301MP_SSID	0x0407
#define BCM94307MP_SSID	0x0408
#define AP4307_SSID	0x0409

#define BU4309_SSID	0x040a
#define BCM94309CB_SSID	0x040b
#define BCM94309MP_SSID	0x040c
#define AP4309_SSID	0x040d

#define BU4312_SSID	0x048a

#define BU4402_SSID	0x4402

#define CLASS_OTHER	0x8000
#define CLASS_ETHER	0x0000
#define CLASS_NET	0x0002
#define CLASS_COMM	0x0007
#define CLASS_MODEM	0x0300
#define CLASS_MIPS	0x3000
#define CLASS_PROC	0x000b
#define CLASS_FLASH	0x0100
#define CLASS_MEM	0x0005
#define CLASS_SERIALBUS 0x000c
#define CLASS_OHCI	0x0310


#define MACHI			0x90

#define MACMID_BU4710I		0x4c17
#define MACMID_BU4710E0		0x4c18
#define MACMID_BU4710E1		0x4c19

#define MACMID_94710R1I		0x4c1a
#define MACMID_94710R1E0	0x4c1b
#define MACMID_94710R1E1	0x4c1c

#define MACMID_94710R4I		0x4c1d
#define MACMID_94710R4E0	0x4c1e
#define MACMID_94710R4E1	0x4c1f

#define MACMID_94710DEVI	0x4c20
#define MACMID_94710DEVE0	0x4c21
#define MACMID_94710DEVE1	0x4c22

#define MACMID_BU4402		0x4c23

#define MACMID_BU4610I		0x4c24
#define MACMID_BU4610E0		0x4c25
#define MACMID_BU4610E1		0x4c26

#define MACMID_BU4307W		0x4c27
#define MACMID_BU4307E		0x4c28

#define MACMID_94301CB		0x4c29

#define MACMID_94301MP		0x4c2a

#define MACMID_94307MPW		0x4c2b
#define MACMID_94307MPE		0x4c2c

#define MACMID_AP4307W		0x4c2d
#define MACMID_AP4307E		0x4c2e

#define MACMID_BU4309W0		0x4c2f
#define MACMID_BU4309W1		0x4c30
#define MACMID_BU4309E		0x4c31

#define MACMID_94309CBW0	0x4c32
#define MACMID_94309CBW1	0x4c33

#define MACMID_94309MPW0	0x4c34
#define MACMID_94309MPW1	0x4c35
#define MACMID_94309MPE		0x4c36

#define MACMID_BU4401		0x4c37








#define SROM_EPHY_ONE	0x80ff



#define SROM_EPHY_TWO	0x80e6


#define SROM_EPHY_DUAL	0x0001


#define SROM_EPHY_R1	0x0010



#define SROM_EPHY_R4	0x83e5


#define SROM_EPHY_INTERNAL 0x0001


#define SROM_EPHY_ZERO	0x0000

#define SROM_VERS	0x0001


#endif	
