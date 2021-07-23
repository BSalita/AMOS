
/* Convert tdv to terminfo file */

#include "swsubs.c"

/************************************************************************/
/*			TDV Interface Routines				*/
/************************************************************************/

#define ERR_TDV_TABLE_OVERFLOW 1
#define ERR_CANNOT_DECODE_TDV 2
#define ERR_TDV_TOO_LARGE 3
#define ERR_CANNOT_READ_TDV 4
#define ERR_CANNOT_OPEN_TDV 5

TEXT *findamtcrt __((UTINY *tdv, INT i, TEXT *xy, TEXT *clearscreen, BYTES *ntcrt, BYTES *otsize));
UTINY *findsvtcrt __((UTINY *tdv, INT i, TEXT *xy));
TEXT *otaddr __((TEXT *ot, BYTES otsize));
TEXT *printtcrt __((INT row, UINT col, TEXT *tcrtstr));

TEXT *findamtcrt(tdv,i,xy,clearscreen,ntcrt,otsize)
UTINY *tdv;
INT i;
TEXT *xy;
TEXT *clearscreen;
BYTES *ntcrt;
BYTES *otsize;
{
UINT16 *p,*pe,*w;
UTINY *ut;

	*xy = 0;
	p = (UINT16 *)tdv;
	pe = (UINT16 *)(tdv+i);
	while(p<pe)
		{
		w = p++;
		if (GETW(w) != 0x4a41)	/* tstw d1 */
			continue;
		p++;			/* bmi screen control */
		w = p++;
		if (GETW(w) != 0xa00c)	/* ttyi */
			continue;
		strcpy(xy,(TEXT *)p);
		break;
		}

	if (xy[1] != '[')	/* assume ESC[ (ANSI) or ESC= (other) */
		strcpy(xy,"\033=");

	*clearscreen = 0;
	p = (UINT16 *)tdv;
	pe = (UINT16 *)(tdv+i);
	while(p<pe)
		{
		w = p++;
		if (GETW(w) != 0x0281)	/* and d1,#^h000000ff */
			continue;
		w = p++;
		if (GETW(w) != 0x0000)
			continue;
		w = p++;
		if (GETW(w) != 0x00ff)
			continue;
		w = p++;
		if (GETW(w) != 0xa00c)	/* ttyi */
			{
			w = p++;
			if (GETW(w) != 0xa00c)	/* ttyi */
				continue;
			}
		strcpy(clearscreen,(TEXT *)p);
		break;
		}

	p = (UINT16 *)tdv;
	pe = (UINT16 *)(tdv+i);
	while(p<pe)
		{
		w = p++;
		if (GETW(w) != 0xb2bc)	/* cmp	d1,#ntcrt */
			continue;
		w = p++;
		if (GETW(w) != 0)	/* upper word of ntcrt */
			continue;
		w = p++;
		*ntcrt = GETW(w);
		p++;			/* bhi invalid */
		w = p++;
		if (GETW(w) != 0x45fa)	/* lea a2,OffsetTable */
			{
			w = p++;
			if (GETW(w) != 0x45fa)	/* lea a2,OffsetTable */
				continue;
			}
		if (GETW(p+2) == 0x1212)	/* movb @a2,d1 */
			{
			*otsize = 1;
			ut = (UTINY *)p+GETW(p);
			if (*ut < *ntcrt)
				ut++;
			return((TEXT *)ut);
			}
		else
			{
			p = (UINT16 *)((UTINY *)p+GETW(p));
			if (GETW(p) < *ntcrt || GETW(p) > *ntcrt+512) /* 512 is arbitrary */
				p++;
			*otsize = 2;
			*ntcrt /= 2;
			return((TEXT *)p);
			}
		}
	return(NULL);
}

UTINY *findsvtcrt(tdv,i,xy)
UTINY *tdv;
INT i;
TEXT *xy;
{
UINT16 *p,*pe,*w;

	*xy = 0;
	p = (UINT16 *)tdv;
	pe = (UINT16 *)(tdv+i);
	while(p<pe)
		{
		w = p++;
		if (GETW(w) != 0x4a41)	/* tstw d1 */
			continue;
		p++;			/* bmi screen control */
		w = p++;
		if (GETW(w) != 0xa00c)	/* ttyi */
			continue;
		strcpy(xy,(TEXT *)p);
		break;
		}

	if (xy[1] != '[')	/* assume ESC[ (ANSI) or ESC= (other) */
		strcpy(xy,"\033=");

	p = (UINT16 *)tdv;
	pe = (UINT16 *)(tdv+i);
	while(p<pe)
		{
		w = p++;
		if (GETW(w) != 0x48e7)	/* save */
			continue;
		w = p++;
		if (GETW(w) != 0x1880)	/* a0,d3,d4 */
			continue;
		w = p++;
		if (GETW(w) != 0x41fa)	/* lea a0, */
			continue;
		return((UTINY *)p+(INT16)(GETW(p)));
		}
	return(NULL);
}

TEXT *printtcrt(row, col, tcrtstr)
INT row;
UINT col;
TEXT *tcrtstr;
{
	printf("tcrt(%d,%u)=",row,col);
	while(*tcrtstr != 0)
		printf("%02x,",*(UTINY *)tcrtstr++);
	puts("0");
	return(tcrtstr+1);
}

/*
Convert ptr to Alpha byte or word TCRT offset table to tcrt code ptr.
*/

TEXT *otaddr(ot,otsize)
TEXT *ot;
BYTES otsize;
{
	if (otsize == 1)
		return(ot+*(UTINY *)ot);
	return(ot+GETW(ot));
}

LOCAL TEXT tcrttab[4096];		/* tdv's tcrt table (sv format) */
LOCAL TEXT xy[16];			/* xy cursor position esc sequence */

INT gettdv(tdv)
TEXT *tdv;
{
LOCAL UTINY buf[4096];
INT i;
UTINY *xtb;
TEXT *ot;
BYTES otsize;
BYTES ntcrt;
FILE *pf;
TEXT clearscreen[16];
UTINY col;
TEXT *p,*q;
ULONG ul;

	if ((pf = fopen(tdv,"rb")) == NULL)
		return(ERR_CANNOT_OPEN_TDV);

	i = fread(buf,1,sizeof(buf),pf);
	if (i == sizeof(buf))
		return(ERR_TDV_TOO_LARGE);
	if (ferror(pf))
		return(ERR_CANNOT_READ_TDV);	/* read error */
	fclose(pf);

	xtb = findsvtcrt(buf,i,xy);
	p = (TEXT *)xtb;
	if (xtb != NULL)
		{
		for(q=p+*(INT16 *)p-1;q > p && q > (TEXT *)buf && q < (TEXT *)buf+i && (*q == (TEXT)0xff || (!*q && *--q == (TEXT)0xff));q=p+*(INT16 *)p-1)
			{
			p += 3;
			while(*p++ != (TEXT)0xff)
				p += strlen(p)+1;
			if ((BYTES)p&1)
				p += 1;
			}
		if (p == (TEXT *)xtb)
			while(*p++ != (TEXT)0xff)
#ifdef NEVER /* don't need to flip 8th bit anymore 2/5/90 */
				do
					if (*p != 0 && iscntrl(*p))
						*p |= 0x80;
#endif
				while(*p++);
		if (diffptr(p,xtb) > sizeof(tcrttab))
			return(ERR_TDV_TABLE_OVERFLOW);
		memcpy(tcrttab,xtb,diffptr(p,xtb));
		return(0);
		}

	ot = findamtcrt(buf,i,xy,clearscreen,&ntcrt,&otsize);
	if (ot != NULL)
		{
		p = tcrttab;
		col = 0;
		if (*clearscreen == 0)
			;
		else if (strcmp(clearscreen,otaddr(ot,otsize)) == 0)
			ot += otsize;
		else
			{
			*p++ = col;
			col += 1;
			ntcrt += 1;
			strcpy(p,clearscreen);
			p += strlen(p)+1;
			}
		for(;col<ntcrt;++col,ot += otsize)
			{
			*p++ = col;
			q = otaddr(ot,otsize);
#ifdef NEVER /* don't need to flip 8th bit anymore 2/5/90 */
			do
				if (*q != 0 && iscntrl(*q))
					*q |= 0x80;
#endif
			while((*p++ = *q++) != 0);
			}
		*p++ = 0xff;
		return(0);
		}

	return(ERR_CANNOT_DECODE_TDV);	/* Cannot decode tdv file */
}

INT main(argc,argv)
INT argc;
TEXT *argv[];
{
TEXT *p;
TINY row;
UTINY col;

	gettdv(argv[1]);
	p = xy;
	printf("\nCursor positioning sequence:");
	while(*p)
		printf(" %02x",*p++);
	puts("\n");

	p = tcrttab;
	if (*p == 0)
		while((col = *(UTINY *)p++) != 0xff)
			p = printtcrt(-1,col,p);
	else
		while(*(INT16 *)p != 0)
			{
			p += 2;
			row = *p++;
			while((col = *(UTINY *)p++) != 0xff)
				p = printtcrt(row,col,p);
			if ((BYTES)p&1)
				p += 1;
			}
}
