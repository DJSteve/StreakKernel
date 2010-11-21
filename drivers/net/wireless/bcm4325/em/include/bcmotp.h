/*
 * OTP support.
 *
 * Copyright (C) 2009, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: bcmotp.h,v 13.10.108.4.4.6 2008/09/10 01:54:45 Exp $
 */



#define REVA4_OTPGU_BASE	12


#define REVB8_OTPGU_BASE	20


#define OTPGU_HSB_OFF		0
#define OTPGU_SFB_OFF		1
#define OTPGU_CI_OFF		2
#define OTPGU_SROM_OFF		4


#define OTPGU_CI_SZ			2


#define OTPGU_HWP_OFF		60
#define OTPGU_SWP_OFF		61
#define OTPGU_CIP_OFF		62
#define OTPGU_FUSEP_OFF		63


#define MAXNUMRDES	9


#define OTP_HW_RGN	1
#define OTP_SW_RGN	2
#define OTP_CI_RGN	4
#define OTP_FUSE_RGN	8


#define OTP4325_FM_DISABLED_OFFSET	188

#if !defined(BCMDONGLEHOST)
extern void *otp_init(si_t *sih);
#endif 

extern int otp_status(void *oh);
extern int otp_size(void *oh);

extern int otp_read_region(void *oh, int region, uint16 *data, uint *wlen);
extern uint16 otpr(void *oh, chipcregs_t *cc, uint wn);
extern uint16 otp_read_one_bit(void *oh, uint offset);
extern int otp_nvread(void *oh, char *data, uint *len);

#ifdef BCMNVRAMW
#if !defined(BCMDONGLEHOST)
extern int otp_write_region(void *oh, int region, uint16 *data, uint wlen);
extern int otp_fix_word16(void *oh, uint wn, uint16 mask, uint16 val);
extern int otp_write_rde(void *oh, int rde, uint bit, uint val);
extern int otpw(void *oh, int wn, uint16 data);
extern int otp_nvwrite(void *oh, uint16 *data, uint wlen);
#endif 
#endif 

#if (defined(BCMNVRAMR) || defined(BCMNVRAMW))
extern int otp_dump(void *oh, int arg, char *buf, uint size);
#endif
