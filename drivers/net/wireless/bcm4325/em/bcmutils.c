/*
 * Driver O/S-independent utility routines
 *
 * Copyright (C) 2009, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 * $Id: bcmutils.c,v 1.210.4.5.2.4.16.6 2009/11/25 02:44:56 Exp $
 */

#include <typedefs.h>
#include <bcmdefs.h>
#include <stdarg.h>
#include <bcmutils.h>
#ifdef BCMDRIVER
#include <osl.h>
#include <siutils.h>
#if !defined(BCMDONGLEHOST)
#include <bcmnvram.h>
#endif 
#else
#include <stdio.h>
#include <string.h>

#if defined(BCMEXTSUP)
#include <bcm_osl.h>
#endif
#endif 
#if defined(_WIN32) || defined(NDIS) || defined(vxworks) || defined(__vxworks) || \
	defined(_CFE_) || defined(EFI)

#include <bcmstdlib.h>
#endif 
#include <bcmendian.h>
#include <bcmdevs.h>
#include <proto/ethernet.h>
#include <proto/vlan.h>
#include <proto/bcmip.h>
#include <proto/802.1d.h>

#ifdef BCMPERFSTATS
#include <bcmperf.h>
#endif

#ifdef BCMDRIVER


uint
BCMROMFN(pktcopy)(osl_t *osh, void *p, uint offset, int len, uchar *buf)
{
	uint n, ret = 0;

	if (len < 0)
		len = 4096;	

	
	for (; p && offset; p = PKTNEXT(osh, p)) {
		if (offset < (uint)PKTLEN(osh, p))
			break;
		offset -= PKTLEN(osh, p);
	}

	if (!p)
		return 0;

	
	for (; p && len; p = PKTNEXT(osh, p)) {
		n = MIN((uint)PKTLEN(osh, p) - offset, (uint)len);
		bcopy(PKTDATA(osh, p) + offset, buf, n);
		buf += n;
		len -= n;
		ret += n;
		offset = 0;
	}

	return ret;
}


uint
pktfrombuf(osl_t *osh, void *p, uint offset, int len, uchar *buf)
{
	uint n, ret = 0;

	
	for (; p && offset; p = PKTNEXT(osh, p)) {
		if (offset < (uint)PKTLEN(osh, p))
			break;
		offset -= PKTLEN(osh, p);
	}

	if (!p)
		return 0;

	
	for (; p && len; p = PKTNEXT(osh, p)) {
		n = MIN((uint)PKTLEN(osh, p) - offset, (uint)len);
		bcopy(buf, PKTDATA(osh, p) + offset, n);
		buf += n;
		len -= n;
		ret += n;
		offset = 0;
	}

	return ret;
}

#ifdef NOTYET

uint
pkt2pktcopy(osl_t *osh, void *p1, uint offs1, void *p2, uint offs2, int maxlen)
{
	uint8 *dp1, *dp2;
	uint len1, len2, copylen, totallen;

	for (; p1 && offs; p1 = PKTNEXT(osh, p1)) {
		if (offs1 < (uint)PKTLEN(osh, p1))
			break;
		offs1 -= PKTLEN(osh, p1);
	}
	for (; p2 && offs; p2 = PKTNEXT(osh, p2)) {
		if (offs2 < (uint)PKTLEN(osh, p2))
			break;
		offs2 -= PKTLEN(osh, p2);
	}

	
}
#endif 



uint
BCMROMFN(pkttotlen)(osl_t *osh, void *p)
{
	uint total;

	total = 0;
	for (; p; p = PKTNEXT(osh, p))
		total += PKTLEN(osh, p);
	return (total);
}


void *
BCMROMFN(pktlast)(osl_t *osh, void *p)
{
	for (; PKTNEXT(osh, p); p = PKTNEXT(osh, p))
		;

	return (p);
}


uint
BCMROMFN(pktsegcnt)(osl_t *osh, void *p)
{
	uint cnt;

	for (cnt = 0; p; p = PKTNEXT(osh, p))
		cnt++;

	return cnt;
}



void *
BCMROMFN(pktq_penq)(struct pktq *pq, int prec, void *p)
{
	struct pktq_prec *q;

	ASSERT(prec >= 0 && prec < pq->num_prec);
	ASSERT(PKTLINK(p) == NULL);         

	ASSERT(!pktq_full(pq));
	ASSERT(!pktq_pfull(pq, prec));

	q = &pq->q[prec];

	if (q->head)
		PKTSETLINK(q->tail, p);
	else
		q->head = p;

	q->tail = p;
	q->len++;

	pq->len++;

	if (pq->hi_prec < prec)
		pq->hi_prec = (uint8)prec;

	return p;
}

void *
BCMROMFN(pktq_penq_head)(struct pktq *pq, int prec, void *p)
{
	struct pktq_prec *q;

	ASSERT(prec >= 0 && prec < pq->num_prec);
	ASSERT(PKTLINK(p) == NULL);         

	ASSERT(!pktq_full(pq));
	ASSERT(!pktq_pfull(pq, prec));

	q = &pq->q[prec];

	if (q->head == NULL)
		q->tail = p;

	PKTSETLINK(p, q->head);
	q->head = p;
	q->len++;

	pq->len++;

	if (pq->hi_prec < prec)
		pq->hi_prec = (uint8)prec;

	return p;
}

void *
BCMROMFN(pktq_pdeq)(struct pktq *pq, int prec)
{
	struct pktq_prec *q;
	void *p;

	ASSERT(prec >= 0 && prec < pq->num_prec);

	q = &pq->q[prec];

	if ((p = q->head) == NULL)
		return NULL;

	if ((q->head = PKTLINK(p)) == NULL)
		q->tail = NULL;

	q->len--;

	pq->len--;

	PKTSETLINK(p, NULL);

	return p;
}

void *
BCMROMFN(pktq_pdeq_tail)(struct pktq *pq, int prec)
{
	struct pktq_prec *q;
	void *p, *prev;

	ASSERT(prec >= 0 && prec < pq->num_prec);

	q = &pq->q[prec];

	if ((p = q->head) == NULL)
		return NULL;

	for (prev = NULL; p != q->tail; p = PKTLINK(p))
		prev = p;

	if (prev)
		PKTSETLINK(prev, NULL);
	else
		q->head = NULL;

	q->tail = prev;
	q->len--;

	pq->len--;

	return p;
}

void
pktq_pflush(osl_t *osh, struct pktq *pq, int prec, bool dir)
{
	struct pktq_prec *q;
	void *p;

	q = &pq->q[prec];
	p = q->head;
	while (p) {
		q->head = PKTLINK(p);
		PKTSETLINK(p, NULL);
		PKTFREE(osh, p, dir);
		q->len--;
		pq->len--;
		p = q->head;
	}
	ASSERT(q->len == 0);
	q->tail = NULL;
}

bool
BCMROMFN(pktq_pdel)(struct pktq *pq, void *pktbuf, int prec)
{
	struct pktq_prec *q;
	void *p;

	ASSERT(prec >= 0 && prec < pq->num_prec);

	if (!pktbuf)
		return FALSE;

	q = &pq->q[prec];

	if (q->head == pktbuf) {
		if ((q->head = PKTLINK(pktbuf)) == NULL)
			q->tail = NULL;
	} else {
		for (p = q->head; p && PKTLINK(p) != pktbuf; p = PKTLINK(p))
			;
		if (p == NULL)
			return FALSE;

		PKTSETLINK(p, PKTLINK(pktbuf));
		if (q->tail == pktbuf)
			q->tail = p;
	}

	q->len--;
	pq->len--;
	PKTSETLINK(pktbuf, NULL);
	return TRUE;
}

void
BCMROMFN(pktq_init)(struct pktq *pq, int num_prec, int max_len)
{
	int prec;

	ASSERT(num_prec > 0 && num_prec <= PKTQ_MAX_PREC);

	
	bzero(pq, OFFSETOF(struct pktq, q) + (sizeof(struct pktq_prec) * num_prec));

	pq->num_prec = (uint16)num_prec;

	pq->max = (uint16)max_len;

	for (prec = 0; prec < num_prec; prec++)
		pq->q[prec].max = pq->max;
}

void *
BCMROMFN(pktq_deq)(struct pktq *pq, int *prec_out)
{
	struct pktq_prec *q;
	void *p;
	int prec;

	if (pq->len == 0)
		return NULL;

	while ((prec = pq->hi_prec) > 0 && pq->q[prec].head == NULL)
		pq->hi_prec--;

	q = &pq->q[prec];

	if ((p = q->head) == NULL)
		return NULL;

	if ((q->head = PKTLINK(p)) == NULL)
		q->tail = NULL;

	q->len--;

	pq->len--;

	if (prec_out)
		*prec_out = prec;

	PKTSETLINK(p, NULL);

	return p;
}

void *
BCMROMFN(pktq_deq_tail)(struct pktq *pq, int *prec_out)
{
	struct pktq_prec *q;
	void *p, *prev;
	int prec;

	if (pq->len == 0)
		return NULL;

	for (prec = 0; prec < pq->hi_prec; prec++)
		if (pq->q[prec].head)
			break;

	q = &pq->q[prec];

	if ((p = q->head) == NULL)
		return NULL;

	for (prev = NULL; p != q->tail; p = PKTLINK(p))
		prev = p;

	if (prev)
		PKTSETLINK(prev, NULL);
	else
		q->head = NULL;

	q->tail = prev;
	q->len--;

	pq->len--;

	if (prec_out)
		*prec_out = prec;

	PKTSETLINK(p, NULL);

	return p;
}

void *
BCMROMFN(pktq_peek)(struct pktq *pq, int *prec_out)
{
	int prec;

	if (pq->len == 0)
		return NULL;

	while ((prec = pq->hi_prec) > 0 && pq->q[prec].head == NULL)
		pq->hi_prec--;

	if (prec_out)
		*prec_out = prec;

	return (pq->q[prec].head);
}

void *
BCMROMFN(pktq_peek_tail)(struct pktq *pq, int *prec_out)
{
	int prec;

	if (pq->len == 0)
		return NULL;

	for (prec = 0; prec < pq->hi_prec; prec++)
		if (pq->q[prec].head)
			break;

	if (prec_out)
		*prec_out = prec;

	return (pq->q[prec].tail);
}

void
pktq_flush(osl_t *osh, struct pktq *pq, bool dir)
{
	int prec;
	for (prec = 0; prec < pq->num_prec; prec++)
		pktq_pflush(osh, pq, prec, dir);
	ASSERT(pq->len == 0);
}


int
BCMROMFN(pktq_mlen)(struct pktq *pq, uint prec_bmp)
{
	int prec, len;

	len = 0;

	for (prec = 0; prec <= pq->hi_prec; prec++)
		if (prec_bmp & (1 << prec))
			len += pq->q[prec].len;

	return len;
}


void *
BCMROMFN(pktq_mdeq)(struct pktq *pq, uint prec_bmp, int *prec_out)
{
	struct pktq_prec *q;
	void *p;
	int prec;

	if (pq->len == 0)
		return NULL;

	while ((prec = pq->hi_prec) > 0 && pq->q[prec].head == NULL)
		pq->hi_prec--;

	while ((prec_bmp & (1 << prec)) == 0 || pq->q[prec].head == NULL)
		if (prec-- == 0)
			return NULL;

	q = &pq->q[prec];

	if ((p = q->head) == NULL)
		return NULL;

	if ((q->head = PKTLINK(p)) == NULL)
		q->tail = NULL;

	q->len--;

	if (prec_out)
		*prec_out = prec;

	pq->len--;

	PKTSETLINK(p, NULL);

	return p;
}
#endif 

#ifndef	BCMROMOFFLOAD

const unsigned char BCMROMDATA(bcm_ctype)[] = {
	_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,			
	_BCM_C, _BCM_C|_BCM_S, _BCM_C|_BCM_S, _BCM_C|_BCM_S, _BCM_C|_BCM_S, _BCM_C|_BCM_S, _BCM_C,
	_BCM_C,	
	_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,			
	_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,			
	_BCM_S|_BCM_SP,_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_P,		
	_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_P,			
	_BCM_D,_BCM_D,_BCM_D,_BCM_D,_BCM_D,_BCM_D,_BCM_D,_BCM_D,			
	_BCM_D,_BCM_D,_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_P,			
	_BCM_P, _BCM_U|_BCM_X, _BCM_U|_BCM_X, _BCM_U|_BCM_X, _BCM_U|_BCM_X, _BCM_U|_BCM_X,
	_BCM_U|_BCM_X, _BCM_U, 
	_BCM_U,_BCM_U,_BCM_U,_BCM_U,_BCM_U,_BCM_U,_BCM_U,_BCM_U,			
	_BCM_U,_BCM_U,_BCM_U,_BCM_U,_BCM_U,_BCM_U,_BCM_U,_BCM_U,			
	_BCM_U,_BCM_U,_BCM_U,_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_P,			
	_BCM_P, _BCM_L|_BCM_X, _BCM_L|_BCM_X, _BCM_L|_BCM_X, _BCM_L|_BCM_X, _BCM_L|_BCM_X,
	_BCM_L|_BCM_X, _BCM_L, 
	_BCM_L,_BCM_L,_BCM_L,_BCM_L,_BCM_L,_BCM_L,_BCM_L,_BCM_L, 
	_BCM_L,_BCM_L,_BCM_L,_BCM_L,_BCM_L,_BCM_L,_BCM_L,_BCM_L, 
	_BCM_L,_BCM_L,_BCM_L,_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_C, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,		
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,		
	_BCM_S|_BCM_SP, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P,
	_BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P,	
	_BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P,
	_BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P,	
	_BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U,
	_BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U,	
	_BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_P, _BCM_U, _BCM_U, _BCM_U,
	_BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_L,	
	_BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L,
	_BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L,	
	_BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_P, _BCM_L, _BCM_L, _BCM_L,
	_BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L 
};

ulong
BCMROMFN(bcm_strtoul)(char *cp, char **endp, uint base)
{
	ulong result, last_result = 0, value;
	bool minus;

	minus = FALSE;

	while (bcm_isspace(*cp))
		cp++;

	if (cp[0] == '+')
		cp++;
	else if (cp[0] == '-') {
		minus = TRUE;
		cp++;
	}

	if (base == 0) {
		if (cp[0] == '0') {
			if ((cp[1] == 'x') || (cp[1] == 'X')) {
				base = 16;
				cp = &cp[2];
			} else {
				base = 8;
				cp = &cp[1];
			}
		} else
			base = 10;
	} else if (base == 16 && (cp[0] == '0') && ((cp[1] == 'x') || (cp[1] == 'X'))) {
		cp = &cp[2];
	}

	result = 0;

	while (bcm_isxdigit(*cp) &&
	       (value = bcm_isdigit(*cp) ? *cp-'0' : bcm_toupper(*cp)-'A'+10) < base) {
		result = result*base + value;
		
		if (result < last_result && !minus)
			return ((unsigned long) -1);
		last_result = result;
		cp++;
	}

	if (minus)
		result = (ulong)(result * -1);

	if (endp)
		*endp = (char *)cp;

	return (result);
}

int
BCMROMFN(bcm_atoi)(char *s)
{
	return (int)bcm_strtoul(s, NULL, 10);
}


char*
BCMROMFN(bcmstrstr)(char *haystack, char *needle)
{
	int len, nlen;
	int i;

	if ((haystack == NULL) || (needle == NULL))
		return (haystack);

	nlen = strlen(needle);
	len = strlen(haystack) - nlen + 1;

	for (i = 0; i < len; i++)
		if (memcmp(needle, &haystack[i], nlen) == 0)
			return (&haystack[i]);
	return (NULL);
}

char*
BCMROMFN(bcmstrcat)(char *dest, const char *src)
{
	char *p;

	p = dest + strlen(dest);

	while ((*p++ = *src++) != '\0')
		;

	return (dest);
}

char*
BCMROMFN(bcmstrncat)(char *dest, const char *src, uint size)
{
	char *endp;
	char *p;

	p = dest + strlen(dest);
	endp = p + size;

	while (p != endp && (*p++ = *src++) != '\0')
		;

	return (dest);
}



char* bcmstrtok(char **string, const char *delimiters, char *tokdelim)
{
	unsigned char *str;
	unsigned long map[8];
	int count;
	char *nextoken;

	if (tokdelim != NULL) {
		
		*tokdelim = '\0';
	}

	
	for (count = 0; count < 8; count++) {
		map[count] = 0;
	}

	
	do {
		map[*delimiters >> 5] |= (1 << (*delimiters & 31));
	}
	while (*delimiters++);

	str = (unsigned char*)*string;

	
	while (((map[*str >> 5] & (1 << (*str & 31))) && *str) || (*str == ' ')) {
		str++;
	}

	nextoken = (char*)str;

	
	for (; *str; str++) {
		if (map[*str >> 5] & (1 << (*str & 31))) {
			if (tokdelim != NULL) {
				*tokdelim = *str;
			}

			*str++ = '\0';
			break;
		}
	}

	*string = (char*)str;

	
	if (nextoken == (char *) str) {
		return NULL;
	}
	else {
		return nextoken;
	}
}


#define xToLower(C) \
	((C >= 'A' && C <= 'Z') ? (char)((int)C - (int)'A' + (int)'a') : C)



int bcmstricmp(const char *s1, const char *s2)
{
	char dc, sc;

	while (*s2 && *s1) {
		dc = xToLower(*s1);
		sc = xToLower(*s2);
		if (dc < sc) return -1;
		if (dc > sc) return 1;
		s1++;
		s2++;
	}

	if (*s1 && !*s2) return 1;
	if (!*s1 && *s2) return -1;
	return 0;
}



int bcmstrnicmp(const char* s1, const char* s2, int cnt)
{
	char dc, sc;

	while (*s2 && *s1 && cnt) {
		dc = xToLower(*s1);
		sc = xToLower(*s2);
		if (dc < sc) return -1;
		if (dc > sc) return 1;
		s1++;
		s2++;
		cnt--;
	}

	if (!cnt) return 0;
	if (*s1 && !*s2) return 1;
	if (!*s1 && *s2) return -1;
	return 0;
}


int
BCMROMFN(bcm_ether_atoe)(char *p, struct ether_addr *ea)
{
	int i = 0;

	for (;;) {
		ea->octet[i++] = (char) bcm_strtoul(p, &p, 16);
		if (!*p++ || i == 6)
			break;
	}

	return (i == 6);
}
#endif	

#if defined(CONFIG_USBRNDIS_RETAIL) || defined(NDIS_MINIPORT_DRIVER)

ulong
wchar2ascii(
	char *abuf,
	ushort *wbuf,
	ushort wbuflen,
	ulong abuflen
)
{
	ulong copyct = 1;
	ushort i;

	if (abuflen == 0)
		return 0;

	
	wbuflen /= sizeof(ushort);

	for (i = 0; i < wbuflen; ++i) {
		if (--abuflen == 0)
			break;
		*abuf++ = (char) *wbuf++;
		++copyct;
	}
	*abuf = '\0';

	return copyct;
}
#endif 

char *
bcm_ether_ntoa(const struct ether_addr *ea, char *buf)
{
	static const char template[] = "%02x:%02x:%02x:%02x:%02x:%02x";
	snprintf(buf, 18, template,
		ea->octet[0]&0xff, ea->octet[1]&0xff, ea->octet[2]&0xff,
		ea->octet[3]&0xff, ea->octet[4]&0xff, ea->octet[5]&0xff);
	return (buf);
}

char *
bcm_ip_ntoa(struct ipv4_addr *ia, char *buf)
{
	snprintf(buf, 16, "%d.%d.%d.%d",
	         ia->addr[0], ia->addr[1], ia->addr[2], ia->addr[3]);
	return (buf);
}

#ifdef BCMDRIVER

void
bcm_mdelay(uint ms)
{
	uint i;

	for (i = 0; i < ms; i++) {
		OSL_DELAY(1000);
	}
}

#if !defined(BCMDONGLEHOST)

char *
getvar(char *vars, const char *name)
{
#ifdef	_MINOSL_
	return NULL;
#else
	char *s;
	int len;

	if (!name)
		return NULL;

	len = strlen(name);
	if (len == 0)
		return NULL;

	
	for (s = vars; s && *s;) {
		
		if ((bcmp(s, name, len) == 0) && (s[len] == '='))
			return (&s[len+1]);

		while (*s++)
			;
	}

	
	return (nvram_get(name));
#endif	
}


int
getintvar(char *vars, const char *name)
{
#ifdef	_MINOSL_
	return 0;
#else
	char *val;

	if ((val = getvar(vars, name)) == NULL)
		return (0);

	return (bcm_strtoul(val, NULL, 0));
#endif	
}



static int
findmatch(char *string, char *name)
{
	uint len;
	char *c;

	len = strlen(name);
	
	while ((c = strchr(string, ',')) != NULL) {
		if (len == (uint)(c - string) && !strncmp(string, name, len))
			return 1;
		string = c + 1;
	}

	return (!strcmp(string, name));
}


uint
getgpiopin(char *vars, char *pin_name, uint def_pin)
{
	char name[] = "gpioXXXX";
	char *val;
	uint pin;

	
	for (pin = 0; pin < GPIO_NUMPINS; pin ++) {
		snprintf(name, sizeof(name), "gpio%d", pin);
		val = getvar(vars, name);
		if (val && findmatch(val, pin_name))
			return pin;
	}

	if (def_pin != GPIO_PIN_NOTDEFINED) {
		
		snprintf(name, sizeof(name), "gpio%d", def_pin);
		if (getvar(vars, name)) {
			def_pin =  GPIO_PIN_NOTDEFINED;
		}
	}

	return def_pin;
}
#endif 


#if defined(BCMPERFSTATS) || defined(BCMTSTAMPEDLOGS)

#define	LOGSIZE	256			
static struct {
	uint	cycles;
	char	*fmt;
	uint	a1;
	uint	a2;
} logtab[LOGSIZE];


static uint logi = 0;

static uint readi = 0;
#endif	

#ifdef BCMPERFSTATS
void
bcm_perf_enable()
{
	BCMPERF_ENABLE_INSTRCOUNT();
	BCMPERF_ENABLE_ICACHE_MISS();
	BCMPERF_ENABLE_ICACHE_HIT();
}

void
bcmlog(char *fmt, uint a1, uint a2)
{
	static uint last = 0;
	uint cycles, i;
	OSL_GETCYCLES(cycles);

	i = logi;

	logtab[i].cycles = cycles - last;
	logtab[i].fmt = fmt;
	logtab[i].a1 = a1;
	logtab[i].a2 = a2;

	logi = (i + 1) % LOGSIZE;
	last = cycles;
}


void
bcmstats(char *fmt)
{
	static uint last = 0;
	static uint32 ic_miss = 0;
	static uint32 instr_count = 0;
	uint32 ic_miss_cur;
	uint32 instr_count_cur;
	uint cycles, i;

	OSL_GETCYCLES(cycles);
	BCMPERF_GETICACHE_MISS(ic_miss_cur);
	BCMPERF_GETINSTRCOUNT(instr_count_cur);

	i = logi;

	logtab[i].cycles = cycles - last;
	logtab[i].a1 = ic_miss_cur - ic_miss;
	logtab[i].a2 = instr_count_cur - instr_count;
	logtab[i].fmt = fmt;

	logi = (i + 1) % LOGSIZE;

	last = cycles;
	instr_count = instr_count_cur;
	ic_miss = ic_miss_cur;
}


void
bcmdumplog(char *buf, int size)
{
	char *limit;
	int j = 0;
	int num;

	limit = buf + size - 80;
	*buf = '\0';

	num = logi - readi;

	if (num < 0)
		num += LOGSIZE;

	

	for (j = 0; j < num && (buf < limit); readi = (readi + 1) % LOGSIZE, j++) {
		if (logtab[readi].fmt == NULL)
		    continue;
		buf += sprintf(buf, "%d\t", logtab[readi].cycles);
		buf += sprintf(buf, logtab[readi].fmt, logtab[readi].a1, logtab[readi].a2);
		buf += sprintf(buf, "\n");
	}

}



int
bcmdumplogent(char *buf, uint i)
{
	bool hit;

	
	if (buf == NULL) {
		i = ((i > 0) && (i < (LOGSIZE - 1))) ? i : (LOGSIZE - 1);
		return ((logi - i) % LOGSIZE);
	}

	*buf = '\0';

	ASSERT(i < LOGSIZE);

	if (i == logi)
		return (-1);

	hit = FALSE;
	for (; (i != logi) && !hit; i = (i + 1) % LOGSIZE) {
		if (logtab[i].fmt == NULL)
			continue;
		buf += sprintf(buf, "%d: %d\t", i, logtab[i].cycles);
		buf += sprintf(buf, logtab[i].fmt, logtab[i].a1, logtab[i].a2);
		buf += sprintf(buf, "\n");
		hit = TRUE;
	}

	return (i);
}

#endif	

#if defined(BCMTSTAMPEDLOGS)

void
bcmtslog(uint32 tstamp, char *fmt, uint a1, uint a2)
{
	uint i = logi;

	logtab[i].cycles = tstamp;
	logtab[i].fmt = fmt;
	logtab[i].a1 = a1;
	logtab[i].a2 = a2;

	logi = (i + 1) % LOGSIZE;
}


void bcmprinttstamp(uint32 ticks)
{
	uint us, ms, sec;

	us = (ticks % TSF_TICKS_PER_MS) * 1000 / TSF_TICKS_PER_MS;
	ms = ticks / TSF_TICKS_PER_MS;
	sec = ms / 1000;
	ms -= sec * 1000;
	printf("%04u.%03u.%03u ", sec, ms, us);
}


void
bcmprinttslogs(void)
{
	int j = 0;
	int num;

	num = logi - readi;
	if (num < 0)
		num += LOGSIZE;

	
	for (j = 0; j < num; readi = (readi + 1) % LOGSIZE, j++) {
		if (logtab[readi].fmt == NULL)
		    continue;
		bcmprinttstamp(logtab[readi].cycles);
		printf(logtab[readi].fmt, logtab[readi].a1, logtab[readi].a2);
		printf("\n");
	}
}
#endif	

#if defined(BCMDBG) || defined(DHD_DEBUG)

void
prpkt(const char *msg, osl_t *osh, void *p0)
{
	void *p;

	if (msg && (msg[0] != '\0'))
		printf("%s:\n", msg);

	for (p = p0; p; p = PKTNEXT(osh, p))
		prhex(NULL, PKTDATA(osh, p), PKTLEN(osh, p));
}
#endif 


uint
BCMROMFN(pktsetprio)(void *pkt, bool update_vtag)
{
	struct ether_header *eh;
	struct ethervlan_header *evh;
	uint8 *pktdata;
	int priority = 0;
	int rc = 0;

	pktdata = (uint8 *) PKTDATA(NULL, pkt);
	ASSERT(ISALIGNED((uintptr)pktdata, sizeof(uint16)));

	eh = (struct ether_header *) pktdata;

	if (ntoh16(eh->ether_type) == ETHER_TYPE_8021Q) {
		uint16 vlan_tag;
		int vlan_prio, dscp_prio = 0;

		evh = (struct ethervlan_header *)eh;

		vlan_tag = ntoh16(evh->vlan_tag);
		vlan_prio = (int) (vlan_tag >> VLAN_PRI_SHIFT) & VLAN_PRI_MASK;

		if (ntoh16(evh->ether_type) == ETHER_TYPE_IP) {
			uint8 *ip_body = pktdata + sizeof(struct ethervlan_header);
			uint8 tos_tc = IP_TOS(ip_body);
			dscp_prio = (int)(tos_tc >> IPV4_TOS_PREC_SHIFT);
		}

		
		if (dscp_prio != 0) {
			priority = dscp_prio;
			rc |= PKTPRIO_VDSCP;
		} else {
			priority = vlan_prio;
			rc |= PKTPRIO_VLAN;
		}
		
		if (update_vtag && (priority != vlan_prio)) {
			vlan_tag &= ~(VLAN_PRI_MASK << VLAN_PRI_SHIFT);
			vlan_tag |= (uint16)priority << VLAN_PRI_SHIFT;
			evh->vlan_tag = hton16(vlan_tag);
			rc |= PKTPRIO_UPD;
		}
	} else if (ntoh16(eh->ether_type) == ETHER_TYPE_IP) {
		uint8 *ip_body = pktdata + sizeof(struct ether_header);
		uint8 tos_tc = IP_TOS(ip_body);
		priority = (int)(tos_tc >> IPV4_TOS_PREC_SHIFT);
		rc |= PKTPRIO_DSCP;
	}

	ASSERT(priority >= 0 && priority <= MAXPRIO);
	PKTSETPRIO(pkt, priority);
	return (rc | priority);
}

static char bcm_undeferrstr[BCME_STRLEN];

static const char *bcmerrorstrtable[] = BCMERRSTRINGTABLE;


const char *
bcmerrorstr(int bcmerror)
{
	
	ASSERT(ABS(BCME_LAST) == (ARRAYSIZE(bcmerrorstrtable) - 1));

	if (bcmerror > 0 || bcmerror < BCME_LAST) {
		snprintf(bcm_undeferrstr, BCME_STRLEN, "Undefined error %d", bcmerror);
		return bcm_undeferrstr;
	}

	ASSERT(strlen(bcmerrorstrtable[-bcmerror]) < BCME_STRLEN);

	return bcmerrorstrtable[-bcmerror];
}


#ifdef BCMDBG_PKT       

void
pktlist_add(pktlist_info_t *pktlist, void *pkt)
{
	uint i;
	ASSERT(pktlist->count < PKTLIST_SIZE);

	
	for (i = 0; i < pktlist->count; i++) {
		if (pktlist->list[i] == pkt)
			ASSERT(0);
	}
	pktlist->list[pktlist->count] = pkt;
	pktlist->count++;
	return;
}


void
pktlist_remove(pktlist_info_t *pktlist, void *pkt)
{
	uint i;
	uint num = pktlist->count;

	
	for (i = 0; i < num; i++)
	{
		
		if (pktlist->list[i] == pkt)
		{
			
			pktlist->list[i] = pktlist->list[num-1];
			pktlist->count--;
			return;
		}
	}
	ASSERT(0);
}



char *
pktlist_dump(pktlist_info_t *pktlist, char *buf)
{
	char *obuf;
	uint i;

	obuf = buf;

	buf += sprintf(buf, "Packet list dump:\n");

	for (i = 0; i < (pktlist->count); i++) {
		buf += sprintf(buf, "0x%p\t", pktlist->list[i]);

#ifdef NOTDEF     
		if (PKTTAG(pktlist->list[i])) {
			
			buf += sprintf(buf, "Pkttag(in hex): ");
			buf += bcm_format_hex(buf, PKTTAG(pktlist->list[i]), OSL_PKTTAG_SZ);
		}
		buf += sprintf(buf, "Pktdata(in hex): ");
		buf += bcm_format_hex(buf, PKTDATA(NULL, pktlist->list[i]),
			PKTLEN(NULL, pktlist->list[i]));
#endif 

		buf += sprintf(buf, "\n");
	}
	return obuf;
}
#endif  


const bcm_iovar_t*
bcm_iovar_lookup(const bcm_iovar_t *table, const char *name)
{
	const bcm_iovar_t *vi;
	const char *lookup_name;

	
	lookup_name = strrchr(name, ':');
	if (lookup_name != NULL)
		lookup_name++;
	else
		lookup_name = name;

	ASSERT(table != NULL);

	for (vi = table; vi->name; vi++) {
		if (!strcmp(vi->name, lookup_name))
			return vi;
	}
	

	return NULL; 
}

int
BCMROMFN(bcm_iovar_lencheck)(const bcm_iovar_t *vi, void *arg, int len, bool set)
{
	int bcmerror = 0;

	
	switch (vi->type) {
	case IOVT_BOOL:
	case IOVT_INT8:
	case IOVT_INT16:
	case IOVT_INT32:
	case IOVT_UINT8:
	case IOVT_UINT16:
	case IOVT_UINT32:
		
		if (len < (int)sizeof(int)) {
			bcmerror = BCME_BUFTOOSHORT;
		}
		break;

	case IOVT_BUFFER:
		
		if (len < vi->minlen) {
			bcmerror = BCME_BUFTOOSHORT;
		}
		break;

	case IOVT_VOID:
		if (!set) {
			
			bcmerror = BCME_UNSUPPORTED;
		} else if (len) {
			
			bcmerror = BCME_BUFTOOLONG;
		}
		break;

	default:
		
		ASSERT(0);
		bcmerror = BCME_UNSUPPORTED;
	}

	return bcmerror;
}

#endif	


#ifndef	BCMROMOFFLOAD


static const uint8 crc8_table[256] = {
    0x00, 0xF7, 0xB9, 0x4E, 0x25, 0xD2, 0x9C, 0x6B,
    0x4A, 0xBD, 0xF3, 0x04, 0x6F, 0x98, 0xD6, 0x21,
    0x94, 0x63, 0x2D, 0xDA, 0xB1, 0x46, 0x08, 0xFF,
    0xDE, 0x29, 0x67, 0x90, 0xFB, 0x0C, 0x42, 0xB5,
    0x7F, 0x88, 0xC6, 0x31, 0x5A, 0xAD, 0xE3, 0x14,
    0x35, 0xC2, 0x8C, 0x7B, 0x10, 0xE7, 0xA9, 0x5E,
    0xEB, 0x1C, 0x52, 0xA5, 0xCE, 0x39, 0x77, 0x80,
    0xA1, 0x56, 0x18, 0xEF, 0x84, 0x73, 0x3D, 0xCA,
    0xFE, 0x09, 0x47, 0xB0, 0xDB, 0x2C, 0x62, 0x95,
    0xB4, 0x43, 0x0D, 0xFA, 0x91, 0x66, 0x28, 0xDF,
    0x6A, 0x9D, 0xD3, 0x24, 0x4F, 0xB8, 0xF6, 0x01,
    0x20, 0xD7, 0x99, 0x6E, 0x05, 0xF2, 0xBC, 0x4B,
    0x81, 0x76, 0x38, 0xCF, 0xA4, 0x53, 0x1D, 0xEA,
    0xCB, 0x3C, 0x72, 0x85, 0xEE, 0x19, 0x57, 0xA0,
    0x15, 0xE2, 0xAC, 0x5B, 0x30, 0xC7, 0x89, 0x7E,
    0x5F, 0xA8, 0xE6, 0x11, 0x7A, 0x8D, 0xC3, 0x34,
    0xAB, 0x5C, 0x12, 0xE5, 0x8E, 0x79, 0x37, 0xC0,
    0xE1, 0x16, 0x58, 0xAF, 0xC4, 0x33, 0x7D, 0x8A,
    0x3F, 0xC8, 0x86, 0x71, 0x1A, 0xED, 0xA3, 0x54,
    0x75, 0x82, 0xCC, 0x3B, 0x50, 0xA7, 0xE9, 0x1E,
    0xD4, 0x23, 0x6D, 0x9A, 0xF1, 0x06, 0x48, 0xBF,
    0x9E, 0x69, 0x27, 0xD0, 0xBB, 0x4C, 0x02, 0xF5,
    0x40, 0xB7, 0xF9, 0x0E, 0x65, 0x92, 0xDC, 0x2B,
    0x0A, 0xFD, 0xB3, 0x44, 0x2F, 0xD8, 0x96, 0x61,
    0x55, 0xA2, 0xEC, 0x1B, 0x70, 0x87, 0xC9, 0x3E,
    0x1F, 0xE8, 0xA6, 0x51, 0x3A, 0xCD, 0x83, 0x74,
    0xC1, 0x36, 0x78, 0x8F, 0xE4, 0x13, 0x5D, 0xAA,
    0x8B, 0x7C, 0x32, 0xC5, 0xAE, 0x59, 0x17, 0xE0,
    0x2A, 0xDD, 0x93, 0x64, 0x0F, 0xF8, 0xB6, 0x41,
    0x60, 0x97, 0xD9, 0x2E, 0x45, 0xB2, 0xFC, 0x0B,
    0xBE, 0x49, 0x07, 0xF0, 0x9B, 0x6C, 0x22, 0xD5,
    0xF4, 0x03, 0x4D, 0xBA, 0xD1, 0x26, 0x68, 0x9F
};

#define CRC_INNER_LOOP(n, c, x) \
	(c) = ((c) >> 8) ^ crc##n##_table[((c) ^ (x)) & 0xff]

uint8
BCMROMFN(hndcrc8)(
	uint8 *pdata,	
	uint  nbytes,	
	uint8 crc	
)
{
	
	while (nbytes-- > 0)
		crc = crc8_table[(crc ^ *pdata++) & 0xff];

	return crc;
}



static const uint16 crc16_table[256] = {
    0x0000, 0x1189, 0x2312, 0x329B, 0x4624, 0x57AD, 0x6536, 0x74BF,
    0x8C48, 0x9DC1, 0xAF5A, 0xBED3, 0xCA6C, 0xDBE5, 0xE97E, 0xF8F7,
    0x1081, 0x0108, 0x3393, 0x221A, 0x56A5, 0x472C, 0x75B7, 0x643E,
    0x9CC9, 0x8D40, 0xBFDB, 0xAE52, 0xDAED, 0xCB64, 0xF9FF, 0xE876,
    0x2102, 0x308B, 0x0210, 0x1399, 0x6726, 0x76AF, 0x4434, 0x55BD,
    0xAD4A, 0xBCC3, 0x8E58, 0x9FD1, 0xEB6E, 0xFAE7, 0xC87C, 0xD9F5,
    0x3183, 0x200A, 0x1291, 0x0318, 0x77A7, 0x662E, 0x54B5, 0x453C,
    0xBDCB, 0xAC42, 0x9ED9, 0x8F50, 0xFBEF, 0xEA66, 0xD8FD, 0xC974,
    0x4204, 0x538D, 0x6116, 0x709F, 0x0420, 0x15A9, 0x2732, 0x36BB,
    0xCE4C, 0xDFC5, 0xED5E, 0xFCD7, 0x8868, 0x99E1, 0xAB7A, 0xBAF3,
    0x5285, 0x430C, 0x7197, 0x601E, 0x14A1, 0x0528, 0x37B3, 0x263A,
    0xDECD, 0xCF44, 0xFDDF, 0xEC56, 0x98E9, 0x8960, 0xBBFB, 0xAA72,
    0x6306, 0x728F, 0x4014, 0x519D, 0x2522, 0x34AB, 0x0630, 0x17B9,
    0xEF4E, 0xFEC7, 0xCC5C, 0xDDD5, 0xA96A, 0xB8E3, 0x8A78, 0x9BF1,
    0x7387, 0x620E, 0x5095, 0x411C, 0x35A3, 0x242A, 0x16B1, 0x0738,
    0xFFCF, 0xEE46, 0xDCDD, 0xCD54, 0xB9EB, 0xA862, 0x9AF9, 0x8B70,
    0x8408, 0x9581, 0xA71A, 0xB693, 0xC22C, 0xD3A5, 0xE13E, 0xF0B7,
    0x0840, 0x19C9, 0x2B52, 0x3ADB, 0x4E64, 0x5FED, 0x6D76, 0x7CFF,
    0x9489, 0x8500, 0xB79B, 0xA612, 0xD2AD, 0xC324, 0xF1BF, 0xE036,
    0x18C1, 0x0948, 0x3BD3, 0x2A5A, 0x5EE5, 0x4F6C, 0x7DF7, 0x6C7E,
    0xA50A, 0xB483, 0x8618, 0x9791, 0xE32E, 0xF2A7, 0xC03C, 0xD1B5,
    0x2942, 0x38CB, 0x0A50, 0x1BD9, 0x6F66, 0x7EEF, 0x4C74, 0x5DFD,
    0xB58B, 0xA402, 0x9699, 0x8710, 0xF3AF, 0xE226, 0xD0BD, 0xC134,
    0x39C3, 0x284A, 0x1AD1, 0x0B58, 0x7FE7, 0x6E6E, 0x5CF5, 0x4D7C,
    0xC60C, 0xD785, 0xE51E, 0xF497, 0x8028, 0x91A1, 0xA33A, 0xB2B3,
    0x4A44, 0x5BCD, 0x6956, 0x78DF, 0x0C60, 0x1DE9, 0x2F72, 0x3EFB,
    0xD68D, 0xC704, 0xF59F, 0xE416, 0x90A9, 0x8120, 0xB3BB, 0xA232,
    0x5AC5, 0x4B4C, 0x79D7, 0x685E, 0x1CE1, 0x0D68, 0x3FF3, 0x2E7A,
    0xE70E, 0xF687, 0xC41C, 0xD595, 0xA12A, 0xB0A3, 0x8238, 0x93B1,
    0x6B46, 0x7ACF, 0x4854, 0x59DD, 0x2D62, 0x3CEB, 0x0E70, 0x1FF9,
    0xF78F, 0xE606, 0xD49D, 0xC514, 0xB1AB, 0xA022, 0x92B9, 0x8330,
    0x7BC7, 0x6A4E, 0x58D5, 0x495C, 0x3DE3, 0x2C6A, 0x1EF1, 0x0F78
};

uint16
BCMROMFN(hndcrc16)(
    uint8 *pdata,  
    uint nbytes, 
    uint16 crc     
)
{
	while (nbytes-- > 0)
		CRC_INNER_LOOP(16, crc, *pdata++);
	return crc;
}

static const uint32 crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
    0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
    0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
    0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
    0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
    0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
    0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
    0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
    0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
    0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
    0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
    0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
    0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
    0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
    0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
    0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
    0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
    0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
    0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
    0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
    0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

uint32
BCMROMFN(hndcrc32)(
    uint8 *pdata,  
    uint   nbytes, 
    uint32 crc     
)
{
	uint8 *pend;
#ifdef __mips__
	uint8 tmp[4];
	ulong *tptr = (ulong *)tmp;

	
	pend = (uint8 *)((uint)(pdata + 3) & 0xfffffffc);
	nbytes -= (pend - pdata);
	while (pdata < pend)
		CRC_INNER_LOOP(32, crc, *pdata++);

	
	pend = pdata + (nbytes & 0xfffffffc);
	while (pdata < pend) {
		*tptr = *(ulong *)pdata;
		pdata += sizeof(ulong *);
		CRC_INNER_LOOP(32, crc, tmp[0]);
		CRC_INNER_LOOP(32, crc, tmp[1]);
		CRC_INNER_LOOP(32, crc, tmp[2]);
		CRC_INNER_LOOP(32, crc, tmp[3]);
	}

	
	pend = pdata + (nbytes & 0x03);
	while (pdata < pend)
		CRC_INNER_LOOP(32, crc, *pdata++);
#else
	pend = pdata + nbytes;
	while (pdata < pend)
		CRC_INNER_LOOP(32, crc, *pdata++);
#endif 

	return crc;
}

#ifdef notdef
#define CLEN 	1499 	
#define CBUFSIZ 	(CLEN+4)
#define CNBUFS		5 

void testcrc32(void)
{
	uint j, k, l;
	uint8 *buf;
	uint len[CNBUFS];
	uint32 crcr;
	uint32 crc32tv[CNBUFS] =
		{0xd2cb1faa, 0xd385c8fa, 0xf5b4f3f3, 0x55789e20, 0x00343110};

	ASSERT((buf = MALLOC(CBUFSIZ*CNBUFS)) != NULL);

	
	for (l = 0; l <= 4; l++) {
		for (j = 0; j < CNBUFS; j++) {
			len[j] = CLEN;
			for (k = 0; k < len[j]; k++)
				*(buf + j*CBUFSIZ + (k+l)) = (j+k) & 0xff;
		}

		for (j = 0; j < CNBUFS; j++) {
			crcr = crc32(buf + j*CBUFSIZ + l, len[j], CRC32_INIT_VALUE);
			ASSERT(crcr == crc32tv[j]);
		}
	}

	MFREE(buf, CBUFSIZ*CNBUFS);
	return;
}
#endif 


bcm_tlv_t *
BCMROMFN(bcm_next_tlv)(bcm_tlv_t *elt, int *buflen)
{
	int len;

	
	if (!bcm_valid_tlv(elt, *buflen))
		return NULL;

	
	len = elt->len;
	elt = (bcm_tlv_t*)(elt->data + len);
	*buflen -= (2 + len);

	
	if (!bcm_valid_tlv(elt, *buflen))
		return NULL;

	return elt;
}


bcm_tlv_t *
BCMROMFN(bcm_parse_tlvs)(void *buf, int buflen, uint key)
{
	bcm_tlv_t *elt;
	int totlen;

	elt = (bcm_tlv_t*)buf;
	totlen = buflen;

	
	while (totlen >= 2) {
		int len = elt->len;

		
		if ((elt->id == key) && (totlen >= (len + 2)))
			return (elt);

		elt = (bcm_tlv_t*)((uint8*)elt + (len + 2));
		totlen -= (len + 2);
	}

	return NULL;
}


bcm_tlv_t *
BCMROMFN(bcm_parse_ordered_tlvs)(void *buf, int buflen, uint key)
{
	bcm_tlv_t *elt;
	int totlen;

	elt = (bcm_tlv_t*)buf;
	totlen = buflen;

	
	while (totlen >= 2) {
		uint id = elt->id;
		int len = elt->len;

		
		if (id > key)
			return (NULL);

		
		if ((id == key) && (totlen >= (len + 2)))
			return (elt);

		elt = (bcm_tlv_t*)((uint8*)elt + (len + 2));
		totlen -= (len + 2);
	}
	return NULL;
}
#endif	

int
bcm_format_flags(const bcm_bit_desc_t *bd, uint32 flags, char* buf, int len)
{
	int i;
	char* p = buf;
	char hexstr[16];
	int slen = 0, nlen = 0;
	uint32 bit;
	const char* name;

	if (len < 2 || !buf)
		return 0;

	buf[0] = '\0';

	for (i = 0; flags != 0; i++) {
		bit = bd[i].bit;
		name = bd[i].name;
		if (bit == 0 && flags != 0) {
			
			snprintf(hexstr, 16, "0x%X", flags);
			name = hexstr;
			flags = 0;	
		} else if ((flags & bit) == 0)
			continue;
		flags &= ~bit;
		nlen = strlen(name);
		slen += nlen;
		
		if (flags != 0)
			slen += 1;
		
		if (len <= slen)
			break;
		
		strncpy(p, name, nlen + 1);
		p += nlen;
		
		if (flags != 0)
			p += snprintf(p, 2, " ");
		len -= slen;
	}

	
	if (flags != 0) {
		if (len < 2)
			p -= 2 - len;	
		p += snprintf(p, 2, ">");
	}

	return (int)(p - buf);
}


int
bcm_format_hex(char *str, const void *bytes, int len)
{
	int i;
	char *p = str;
	const uint8 *src = (const uint8*)bytes;

	for (i = 0; i < len; i++) {
		p += snprintf(p, 3, "%02X", *src);
		src++;
	}
	return (int)(p - str);
}


void
prhex(const char *msg, uchar *buf, uint nbytes)
{
	char line[128], *p;
	int len = sizeof(line);
	int nchar;
	uint i;

	if (msg && (msg[0] != '\0'))
		printf("%s:\n", msg);

	p = line;
	for (i = 0; i < nbytes; i++) {
		if (i % 16 == 0) {
			nchar = snprintf(p, len, "  %04d: ", i);	
			p += nchar;
			len -= nchar;
		}
		if (len > 0) {
			nchar = snprintf(p, len, "%02x ", buf[i]);
			p += nchar;
			len -= nchar;
		}

		if (i % 16 == 15) {
			printf("%s\n", line);		
			p = line;
			len = sizeof(line);
		}
	}

	
	if (p != line)
		printf("%s\n", line);
}

static const char *crypto_algo_names[] = {
	"NONE",
	"WEP1",
	"TKIP",
	"WEP128",
	"AES_CCM",
	"AES_OCB_MSDU",
	"AES_OCB_MPDU",
	"NALG"
	"UNDEF",
	"UNDEF",
	"UNDEF",
#ifdef BCMWAPI_WPI
	"WAPI",
#endif 
	"UNDEF"
};

const char *
bcm_crypto_algo_name(uint algo)
{
	return (algo < ARRAYSIZE(crypto_algo_names)) ? crypto_algo_names[algo] : "ERR";
}

#ifdef BCMDBG
void
deadbeef(void *p, uint len)
{
	static uint8 meat[] = { 0xde, 0xad, 0xbe, 0xef };

	while (len-- > 0) {
		*(uint8*)p = meat[((uintptr)p) & 3];
		p = (uint8*)p + 1;
	}
}
#endif 


char *
bcm_brev_str(uint32 brev, char *buf)
{
	if (brev < 0x100)
		snprintf(buf, 8, "%d.%d", (brev & 0xf0) >> 4, brev & 0xf);
	else
		snprintf(buf, 8, "%c%03x", ((brev & 0xf000) == 0x1000) ? 'P' : 'A', brev & 0xfff);

	return (buf);
}

#define BUFSIZE_TODUMP_ATONCE 512 


void
printbig(char *buf)
{
	uint len, max_len;
	char c;

	len = strlen(buf);

	max_len = BUFSIZE_TODUMP_ATONCE;

	while (len > max_len) {
		c = buf[max_len];
		buf[max_len] = '\0';
		printf("%s", buf);
		buf[max_len] = c;

		buf += max_len;
		len -= max_len;
	}
	
	printf("%s\n", buf);
	return;
}


uint
bcmdumpfields(bcmutl_rdreg_rtn read_rtn, void *arg0, uint arg1, struct fielddesc *fielddesc_array,
	char *buf, uint32 bufsize)
{
	uint  filled_len;
	int len;
	struct fielddesc *cur_ptr;

	filled_len = 0;
	cur_ptr = fielddesc_array;

	while (bufsize > 1) {
		if (cur_ptr->nameandfmt == NULL)
			break;
		len = snprintf(buf, bufsize, cur_ptr->nameandfmt,
		               read_rtn(arg0, arg1, cur_ptr->offset));
		
		if (len < 0 || (uint32)len >= bufsize)
			len = bufsize - 1;
		buf += len;
		bufsize -= len;
		filled_len += len;
		cur_ptr++;
	}
	return filled_len;
}

uint
bcm_mkiovar(char *name, char *data, uint datalen, char *buf, uint buflen)
{
	uint len;

	len = strlen(name) + 1;

	if ((len + datalen) > buflen)
		return 0;

	strncpy(buf, name, buflen);

	
	memcpy(&buf[len], data, datalen);
	len += datalen;

	return len;
}

#ifndef BCMROMOFFLOAD



#define QDBM_OFFSET 153		
#define QDBM_TABLE_LEN 40	


#define QDBM_TABLE_LOW_BOUND 6493 


#define QDBM_TABLE_HIGH_BOUND 64938 

static const uint16 nqdBm_to_mW_map[QDBM_TABLE_LEN] = {

      6683,	7079,	7499,	7943,	8414,	8913,	9441,	10000,
      10593,	11220,	11885,	12589,	13335,	14125,	14962,	15849,
      16788,	17783,	18836,	19953,	21135,	22387,	23714,	25119,
      26607,	28184,	29854,	31623,	33497,	35481,	37584,	39811,
      42170,	44668,	47315,	50119,	53088,	56234,	59566,	63096
};

uint16
BCMROMFN(bcm_qdbm_to_mw)(uint8 qdbm)
{
	uint factor = 1;
	int idx = qdbm - QDBM_OFFSET;

	if (idx >= QDBM_TABLE_LEN) {
		
		return 0xFFFF;
	}

	
	while (idx < 0) {
		idx += 40;
		factor *= 10;
	}

	
	return ((nqdBm_to_mW_map[idx] + factor/2) / factor);
}

uint8
BCMROMFN(bcm_mw_to_qdbm)(uint16 mw)
{
	uint8 qdbm;
	int offset;
	uint mw_uint = mw;
	uint boundary;

	
	if (mw_uint <= 1)
		return 0;

	offset = QDBM_OFFSET;

	
	while (mw_uint < QDBM_TABLE_LOW_BOUND) {
		mw_uint *= 10;
		offset -= 40;
	}

	for (qdbm = 0; qdbm < QDBM_TABLE_LEN-1; qdbm++) {
		boundary = nqdBm_to_mW_map[qdbm] + (nqdBm_to_mW_map[qdbm+1] -
		                                    nqdBm_to_mW_map[qdbm])/2;
		if (mw_uint < boundary) break;
	}

	qdbm += (uint8)offset;

	return (qdbm);
}


uint
BCMROMFN(bcm_bitcount)(uint8 *bitmap, uint length)
{
	uint bitcount = 0, i;
	uint8 tmp;
	for (i = 0; i < length; i++) {
		tmp = bitmap[i];
		while (tmp) {
			bitcount++;
			tmp &= (tmp - 1);
		}
	}
	return bitcount;
}

#endif 

#ifdef BCMDRIVER


void
bcm_binit(struct bcmstrbuf *b, char *buf, uint size)
{
	b->origsize = b->size = size;
	b->origbuf = b->buf = buf;
}


int
bcm_bprintf(struct bcmstrbuf *b, const char *fmt, ...)
{
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = vsnprintf(b->buf, b->size, fmt, ap);

	
	if ((r == -1) || (r >= (int)b->size) || (r == 0)) {
		b->size = 0;
	} else {
		b->size -= r;
		b->buf += r;
	}

	va_end(ap);

	return r;
}

void
bcm_inc_bytes(uchar *num, int num_bytes, uint8 amount)
{
	int i;

	for (i = 0; i < num_bytes; i++) {
		num[i] += amount;
		if (num[i] >= amount)
			break;
		amount = 1;
	}
}

int
bcm_cmp_bytes(uchar *arg1, uchar *arg2, uint8 nbytes)
{
	int i;

	for (i = nbytes - 1; i >= 0; i--) {
		if (arg1[i] != arg2[i])
			return (arg1[i] - arg2[i]);
	}
	return 0;
}

void
bcm_print_bytes(char *name, const uchar *data, int len)
{
	int i;
	int per_line = 0;

	printf("%s: %d \n", name ? name : "", len);
	for (i = 0; i < len; i++) {
		printf("%02x ", *data++);
		per_line++;
		if (per_line == 16) {
			per_line = 0;
			printf("\n");
		}
	}
	printf("\n");
}

#endif 
