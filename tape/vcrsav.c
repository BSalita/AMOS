
/* AMOS compatible MTUSAV/STRSAV/VCRSAV */
/* Copyright 1991 Softworks Limited */

/*
notes:
	select hardware and save format (just below)
	compile on SCO UNIX (not xenix) using "cc -I../../c xxxsav.c -lx"
	compile on 6000 using "cc -I../../c xxxsav.c"
	compile on TI using "gcc -I../../c xxxsav.c"
wish list:
	* optimize - do text open after first block is analyzed
	* allow selection of filenames on command line
	* process ersatz names
*/

#include "swsubs.c"

/* select hardware */
#if !defined(EXABYTE) && !defined(STREAMER) && !defined(VCR)
#define EXABYTE		0			/* EXABYTE 8mm cartridge */
#define STREAMER	0			/* scsi streamer hardware */
#define VCR		1			/* VCR hardware */
#endif

/* select save format */
#if !defined(MTUSAV) && !defined(STRSAV) && !defined(VCRSAV)
#define MTUSAV		0			/* MTUSAV format */
#define STRSAV		0			/* STRSAV format */
#define VCRSAV		1			/* VCRSAV format */
#endif

/* defining default device */
#if EXABYTE
#if __GCC_TI__ || __SVS_TI__
#define MT_DEV		"/dev/rvt/1h"
#else
#define MT_DEV		"/dev/rmt1"
#endif
#endif

#if STREAMER
#if __GCC_TI__ || __SVS_TI__
#define MT_DEV		"/dev/rqt/0h"
#else
#if __SCO__
#define MT_DEV		"/dev/rct0"
#else
#define MT_DEV		"/dev/rmt0"
#endif
#endif
#endif

#if VCR
#define MT_DEV		"/dev/vcr"
#endif

/* defining misc */
#if VCRSAV || STRSAV
#define TOC		1			/* has table of contents */
#endif

#if EXABYTE
#define BIGBUF		8192			/* variable read() size */
#else
#define BIGBUF		1024			/* read() size */
#endif

#define NOSSD
#ifndef NOSSD
#include "rtssd.c"
#endif

#define MT_EOF 0x40
#define MT_EOT 0x80
#define TEXT_FILE 0
#define BINARY_FILE 1
#define CONTIGUOUS_FILE 2
#define UNKNOWN_FILE 3

#if __DOS__
struct ftime ft;
#define CHECKINT bdos(0x0b,0,0)		/* allow DOS to check for ^C */
#define OS "DOS"
#endif /* __DOS__ */

#if __UNIX__
#if __GCC_TI__ || __SVS_TI__
struct utimbuf {
	time_t	actime;			/* access time */
	time_t	modtime;		/* modification time */
	};
int utime __((char *fname, struct utimbuf *times));
#else
#include "utime.h"
#endif
#include "sys/tape.h"
#include "sys/statfs.h"

#define CHECKINT
#define OS "UNIX"
#endif /* __UNIX__ */

#include "vcrsub.c"

INT mt_devfd = -1;
TEXT *mt_pgm;
TEXT *mt_dev = MT_DEV;
UINT mt_files = 0;
ULONG mt_blocks = 0;
TEXT mt_path[128];		/* should be MAX_FILENAME or something */
TEXT mt_dir[128];
TEXT *mt_fn;
TEXT *mt_ext;
TEXT mt_afn[32];
UINT32 mt_toc_blocks;
TEXT *mt_toc_ipf = "mt_toc.ipf";
TEXT *errstep = "";
UINT mt_max_buffered = 0;
UINT mt_buffered_warnings = 0;
BOOL mt_msgs = 1;
LONG mt_bytes_written = 0;
LONG mt_bytes_to_write = 0;
LONG mt_weird_trigger = LONG_MAX;
#if VCRSAV
UTINY mt_toc_hdr_id[10] = {0xff,0xfe,0xfd,0x00,0x00,0xfc,0xfb,0x00,0xfa,0xff};
#else
UTINY mt_toc_hdr_id[16] = {0xff,0xfd,0xfc,0x00,0x00,0xfb,0xfa,0x00,0xf9,0xff,0,0,0,0,0,0};
UTINY mt_weird_hdr_id[4] = {0xaa,0xff,0x55,0xff};
#endif
UTINY mt_lb_hdr_id[4] = {0xaa,0xaa,0x55,0x55};

#if __UNIX__
#define OS "UNIX"
#define SW_MAXPATH 128
struct ffblk
	{
	TEXT ff_name[SW_MAXPATH];
	};

#define FA_DIREC -1
#define ENOFILE -1
#define ENMFILE -1

INT findfirst(pathname,ffblk,attrib)
RDONLY TEXT *pathname;
struct ffblk *ffblk;
INT attrib;
{
RDONLY TEXT *p;
	p = strrchr(pathname,*PATH_SEPARATOR);
	if (p == NULL)
		p = pathname;
	else
		p++;
	strcpy(ffblk->ff_name,p);
	return(0);
}
#define findnext(ffblk) (errno = ENMFILE)
#endif
struct ffblk wd_ffblk, wp_ffblk, wf_ffblk;
TEXT wildfn[SW_MAXPATH];
TEXT wilddevice[SW_MAXPATH];
TEXT wildppn[SW_MAXPATH];

void mt_label(lr)
struct label_record *lr;
{
	if (memcmp(lr->lb_hdr,mt_lb_hdr_id,4) != 0)
		puts("\nInvalid label record\n");
	else
		{
		printf("\nVolume Name: %.40s\n",lr->lb_vln);
		printf("Volume ID: %.10s\n",lr->lb_vid);
		printf("Installation: %.30s\n",lr->lb_ins);
		printf("System: %.30s\n",lr->lb_sys);
		printf("Creator: %.30s\n",lr->lb_cre);
		printf("\nTape made with %u extra copies\n\n",lr->lb_extra_copies);
		}
}

VOID mt_toc_to_atoc(ate,te)
ATOC_ENTRY *ate;
TOC_ENTRY *te;
{
#define SWAP_BYTE(s) (ate->s = te->s)
#define SWAP_BYTES(s) memcpy((VOID *)ate->s,(VOID *)te->s,sizeof(ate->s))
#define SWAP_WORD(s) PUTW(ate->s,te->s)
#define SWAP_LWORD(s) PUTLW(ate->s,te->s)

	SWAP_WORD(toc_device);
	SWAP_BYTE(toc_flag);
	SWAP_BYTE(toc_drivenum);
	SWAP_WORD(toc_fn[0]);
	SWAP_WORD(toc_fn[1]);
	SWAP_WORD(toc_ext);
	SWAP_BYTE(toc_prog);
	SWAP_BYTE(toc_proj);	
#if VCRSAV
	SWAP_LWORD(toc_blocks);
#else
	SWAP_WORD(toc_blocks);
#endif
	SWAP_WORD(toc_active_bytes);
	SWAP_BYTE(toc_date);
	PUTW(ate->toc_time[0],te->toc_time);
	PUTW(ate->toc_time[1],te->toc_time << 16);
	SWAP_WORD(toc_hash[0]);
	SWAP_WORD(toc_hash[1]);
#if VCRSAV
	SWAP_WORD(toc_entry_number);
#else
	PUTW(ate->toc_entry_number,0);
#endif
#undef SWAP_BYTE
#undef SWAP_BYTES
#undef SWAP_WORD
#undef SWAP_LWORD
/*printf("fn=%x %x %x %lu %d\n",ate->toc_fn[0],ate->toc_fn[1],ate->toc_ext,ate->toc_blocks,ate->toc_active_bytes);
getchar();*/
}

VOID mkte(te,adevice,adrivenum,aname,aext,aprog,aproj,blocks,active_bytes,tm,entry_number)
struct toc_entry *te;
TEXT *adevice;
UINT16 adrivenum;
TEXT *aname, *aext;
UINT aprog, aproj;
UINT32 blocks;
UINT16 active_bytes;
struct tm *tm;
UINT16 entry_number;
{
	atorad50(&te->toc_device,adevice);
	te->toc_flag = 0;	/* 0x40 means hash is valid */
	te->toc_drivenum = adrivenum;
	atorad50(&te->toc_fn[1],atorad50(&te->toc_fn[0],aname));
	atorad50(&te->toc_ext,aext);
	te->toc_prog = (UTINY)aprog;
	te->toc_proj = (UTINY)aproj;
	te->toc_blocks = blocks;
	te->toc_active_bytes = active_bytes;
	tmtoadate(&te->toc_date, tm);
	tmtoatime(&te->toc_time, tm);
	te->toc_hash[0] = te->toc_hash[1] = 0;
	te->toc_entry_number = entry_number;
}

void mt_toc(te)
struct toc_entry *te;
{
TEXT device[4],*p;

/*
	adatetotm(tm, te->toc_date);
	atimetotm(tm, te->toc_time);
	if ((ftime = mktime(&ftm)) != (time_t)-1)
		puts(asctime(&ftm));
	else
		puts("mktime() error");
*/
/* make file names */
	p = rad50toa(device,te->toc_device);
	*p = 0;
	p = mt_path;
	p += sprintf(p,"%s%u%c%03o%03o%c",
		device,te->toc_drivenum,*PATH_SEPARATOR,te->toc_proj,te->toc_prog,*PATH_SEPARATOR);
	mt_fn = p;
	p = rad50toa(p,te->toc_fn[0]);
	p = rad50toa(p,te->toc_fn[1]);
	*p++ = '.';
	mt_ext = p;
	p = rad50toa(p,te->toc_ext);
	*p = 0;
	sprintf(mt_afn,"%s%u:%s[%o,%o]",device,te->toc_drivenum,mt_fn,te->toc_proj,te->toc_prog);
	if (pathcase() == 'L')
		strlwr(mt_path);
	memcpy(mt_dir,mt_path,diffptr(mt_fn,mt_path));
	mt_dir[(unsigned)mt_fn-(unsigned)mt_path] = 0;
/*	printf("afn=%s fn=%s blocks=%lu active bytes=%u #%u\n",mt_afn,mt_path,te->toc_blocks,te->toc_active_bytes,te->toc_entry_number);*/
}

void sigint(sig)
int sig;
{
	REMOVE(mt_toc_ipf);
	if (mt_devfd > 0)
		close(mt_devfd);
	if (*errstep)
		printf("\nProgram aborted by user while trying to %s.\n",errstep);
	else
		puts("\nProgram aborted by user.");
	if (mt_buffered_warnings > 0)
		printf("There were %u buffer warnings.\nMost records buffered was %u.\n",mt_buffered_warnings,mt_max_buffered);
	exit(0);
}

#define NOSSD
#ifndef NOSSD
#include "rtssd.c"
#endif

TEXT *nextdevice()
{
INT i;
LOCAL TEXT *p = NULL;

	while(1)
		{
		if (p == NULL)
			{
			strcpy(mt_path,mt_dir);
			p = mt_path+strlen(mt_path);
			strcpy(p,wilddevice);
			if (pathcase() == 'L')
				strlwr(mt_path);
			else if (pathcase() == 'U')
				strupr(mt_path);
			i = findfirst(mt_path, &wd_ffblk, FA_DIREC);
			}
		else
			i = findnext(&wd_ffblk);
		if (i)
			if (errno == ENOFILE || errno == ENMFILE)
				return(p = NULL);
			else
				{
				printf("Cannot find device - %s (errno = %d)\n",mt_path,errno);
				exit(0);
				}
		if (strcmp(wd_ffblk.ff_name,".") == 0 || strcmp(wd_ffblk.ff_name,"..") == 0)
			continue;
		for(i=0;i<3;++i)
			if (!isalpha(wd_ffblk.ff_name[i]))
				break;
		if (i == 3)
			{
			while(isdigit(wd_ffblk.ff_name[i++]))
				;
			if (--i > 3 && wd_ffblk.ff_name[i] == 0)
				return(strcpy(p,wd_ffblk.ff_name));
			}
		printf("Invalid AMOS device name - %s\n",wd_ffblk.ff_name);
		}
}

TEXT *nextppn()
{
INT i;
LOCAL TEXT *p = NULL;

	while(1)
		{
		if (p == NULL)
			{
			i = strlen(mt_path);
			p = mt_path+i;
			if (i == 0)
				*p++ = '.';
			*p++ = *PATH_SEPARATOR;
			strcpy(p,wildppn);
			if (pathcase() == 'L')
				strlwr(mt_path);
			else if (pathcase() == 'U')
				strupr(mt_path);
			i = findfirst(mt_path, &wp_ffblk, FA_DIREC);
			}
		else
			i = findnext(&wp_ffblk);
		if (i)
			if (errno == ENOFILE || errno == ENMFILE)
				return(p = NULL);
			else
				{
				printf("Cannot find ppn - %s (errno = %d)\n",mt_path,errno);
				exit(0);
				}
		if (strcmp(wp_ffblk.ff_name,".") == 0 || strcmp(wp_ffblk.ff_name,"..") == 0)
			continue;
		for(i=0;i<6;++i)
			if (wp_ffblk.ff_name[i] < '0' || wp_ffblk.ff_name[i] > '7')
				break;
		if (i == 6 && wp_ffblk.ff_name[i] == 0)
			return(strcpy(p,wp_ffblk.ff_name));
		printf("Invalid ppn - %s\n",wp_ffblk.ff_name);
		}
}

TEXT *nextfn()
{
INT i;
LOCAL TEXT *p = NULL;

	while(1)
		{
		if (p == NULL)
			{
			i = strlen(mt_path);
			p = mt_path+i;
			if (i == 0)
				*p++ = '.';
			*p++ = *PATH_SEPARATOR;
			strcpy(p,wildfn);
			if (pathcase() == 'L')
				strlwr(mt_path);
			else if (pathcase() == 'U')
				strupr(mt_path);
			i = findfirst(mt_path, &wf_ffblk, 0);
			}
		else
			i = findnext(&wf_ffblk);
		if (i)
			if (errno == ENOFILE || errno == ENMFILE)
				return(p = NULL);
			else
				{
				printf("Cannot find filename - %s (errno = %d)\n",mt_path,errno);
				exit(0);
				}
		if (strcmp(wf_ffblk.ff_name,".") == 0 || strcmp(wf_ffblk.ff_name,"..") == 0)
			continue;
		return(strcpy(p,wf_ffblk.ff_name));
		}
}

TEXT *ctg_ext[] = {
	"IDA",
	"IDX",
	"LIB",
	"OVL",
	"SSH",
	NULL
};

BOOL isctg(fn,ext)
TEXT *fn;
TEXT *ext;
{
TEXT **p;
TEXT buf[10];

	for(p = ctg_ext;*p != NULL;p++)
		if (stricmp(ext,*p) == 0)
			return(-1);
	do
		{
		printf("Is %s.%s contiguous? (Y/N) ",fn,ext);
		fgets(buf,sizeof(buf),stdin);
		*buf = toupper(*buf);
		}
	while(strlen(buf) != 2 || (*buf != 'Y' && *buf != 'N'));
	return(*buf == 'Y');
}

INT mt_write(fd,buf)
INT fd;
TEXT *buf;
{
INT ret;

#if VCR
	ret = vcr_write(fd,buf);
	if (ret > 0)
		mt_bytes_written += ret;
#else
	ret = write(fd,buf,512);
	if (ret > 0)
		mt_bytes_written += ret;
	if (mt_bytes_written == mt_weird_trigger)
		{
		memcpy(buf,mt_weird_hdr_id,sizeof(mt_weird_hdr_id));
		mt_bytes_written += 512;
		PUTLW(buf+sizeof(mt_weird_hdr_id),mt_bytes_to_write-mt_bytes_written);
		memset(buf+sizeof(mt_weird_hdr_id)+sizeof(LWORD),4,512-sizeof(mt_weird_hdr_id)-sizeof(LWORD));
		if (write(mt_devfd, buf, 512) != 512)
			{
			printf("Cannot write weird EOT record (errno=%d)\n",errno);
			exit(0);
			}
		}
#endif
	return(ret);
}

INT main(argc,argv)
INT argc;
TEXT **argv;
{
INT i,j,k;
LONG l;
UINT32 u32;
TEXT *p;
TEXT buf[512+6];
FILE *tocpf,*inpf;
struct atoc_entry ate, *pate;
struct toc_entry te;
struct stat statbuf;
struct label_record lr;
UINT16 fake_link = 2;
time_t lt;
struct tm *tm;
TEXT afn[32],adevice[4],aname[7],aext[4];
UINT16 adrivenum;
UINT aproj,aprog;
TEXT **pctg;
TEXT *wd,*wp,*wf;
BOOL wrote_weird = 0;
#if __DOS__
struct dfree dtable;
UINT clusters_needed[26];		/* Drive A: to Z: */
TEXT drive[MAXDRIVE],dir[MAXDIR],name[MAXFILE],ext[MAXEXT];
#endif

	setbuf(stdout,NULL);

	signal(SIGINT,sigint);

	memset(&te, 0, sizeof(struct toc_entry));

	if ((mt_pgm = strrchr(argv[0],*PATH_SEPARATOR)) == NULL)
		mt_pgm = argv[0];
	else
		mt_pgm++;

	printf("\n%s Version 1.2 for %s systems\n",mt_pgm,OS);
	puts("Copyright 1989,1991 Softworks Limited, Chicago, IL");

#ifndef NOSSD
	if (rtssd('D','V') == NULL)
		exit(0);
#endif

#if __DOS__
#if VCR
	vcr_open();
#else
	if (argc > 2 && strcmp(argv[1],"/d") == 0)
		{
		mt_dev = argv[2];
		mt_devfd = open(mt_dev,O_WRONLY|O_BINARY);
		if (mt_devfd < 0)
			{
			printf("\nCannot open %s - errno=%d",mt_dev,errno);
			exit(0);
			}
		}
	else
		{
		printf("\nusage: %s /d device-name files ...\n",argv[0]);
		exit(0);
		}
#endif
#endif
#if __UNIX__

	if (argc > 2 && strcmp(argv[1],"-d") == 0)
		mt_dev = argv[2];
	mt_devfd = open(mt_dev,O_WRONLY);
	if (mt_devfd < 0)
		{
		printf("\nCannot open %s - ",mt_dev);
		if (errno == ENOENT)
			printf("%s not created\n",mt_dev); 
		else if (errno == ENODEV)
			printf("%s not installed in kernal\n",mt_dev);
		else
			printf("errno=%d\n",mt_devfd,errno);
#if VCR
		if (errno == ENOENT || errno == ENODEV)
			{
			puts("Using /dev/inb and /dev/outb drivers instead\nExtra copies will be required");
			vcr_open();
			}
		else
#endif
			exit(0);
		}
#endif /* __UNIX__ */

#if VCR
	vcr_ioctl(mt_devfd, VCR_GET_DRIVER_DATA, &vd);
#endif
	memset((VOID *)&lr, 0, sizeof(lr));
	memcpy(lr.lb_hdr,mt_lb_hdr_id,sizeof(mt_lb_hdr_id));

/* find tape creation date and time */
	lt = time(NULL);
	tm = localtime(&lt);
	tmtoadate(&lr.lb_crd, tm);
	tmtoatime(&u32, tm);

#if VCRSAV
/* need to get extra copy count */
	lr.lb_extra_copies = vd.vcr_copy_count;
	printf("\nTape made with %u extra copies\n",lr.lb_extra_copies);
#endif

/* read label */
	printf("\nVolume Name: ");
	gets(buf);
	strncpy(lr.lb_vln,buf,sizeof(lr.lb_vln));
	printf("Volume ID: ");
	gets(buf);
	strncpy(lr.lb_vid,buf,sizeof(lr.lb_vid));
	printf("Installation: ");
	gets(buf);
	strncpy(lr.lb_ins,buf,sizeof(lr.lb_ins));
	printf("System: ");
	gets(buf);
	strncpy(lr.lb_sys,buf,sizeof(lr.lb_sys));
	printf("Creator: ");
	gets(buf);
	strncpy(lr.lb_cre,buf,sizeof(lr.lb_cre));
	puts("");
	PUTW(lr.lb_lb_cnt,0);
	PUTW(lr.lb_vers,2);
	PUTLW(lr.lb_crt,GETW((UINT16 *)&u32) | (GETW((UINT16 *)&u32+1) << 16));
/* give contiguous file info */
	printf("Warning: This program will attempt to determine whether a file should be saved\n");
	printf("as sequential or contiguous by using the following rules:\n");
	printf("1) A file whose size is not a multiple of 512 will be saved as sequential.\n");
	printf("2) Otherwise, files with the following extensions will be saved as contiguous.\n");
	for(pctg=ctg_ext;*pctg != NULL;pctg++)
		printf("\t*.%s\n",*pctg);
	printf("3) Otherwise, you will be asked if a file is contiguous.\n\n");

/* build toc */
	if ((tocpf = fopen(mt_toc_ipf,"wb")) == NULL)
		{
		printf("Cannot open %s (errno = %d)\n",mt_toc_ipf,errno);
		exit(0);
		}

/* need to check if write is permitted */
	for(i=1;i<argc;i++)
		{
/* write out a atoc_entry to mt_toc */
		if (*argv[i] == *PATH_SEPARATOR)
			strcpy(mt_path,argv[i]);
		else
			{
			getcwd(mt_path,sizeof(mt_path));
			strcat(mt_path,PATH_SEPARATOR);
			strcat(mt_path,argv[i]);
			}
/* if device or ppn add wildcarding */
		if (stat(mt_path,&statbuf) == 0 && (statbuf.st_mode & S_IFDIR))
			{
			p = strrchr(mt_path,*PATH_SEPARATOR);
			if (p == NULL)
				p = mt_path;
			else
				p++;
			if (strlen(p) != 6 || strspn(p,"0123456789") != 6)
				{
				strcat(mt_path,PATH_SEPARATOR);
				strcat(mt_path,"*");
				}
			strcat(mt_path,PATH_SEPARATOR);
			strcat(mt_path,"*.*");
			}
		p = strrchr(mt_path,*PATH_SEPARATOR);
		if (p == NULL)
			{
			strcpy(wildfn,mt_path);
			*mt_path = 0;
			}
		else
			{
			*p++ = 0;
			strcpy(wildfn,p);
			}
		p = strrchr(mt_path,*PATH_SEPARATOR);
		if (p == NULL)
			{
			strcpy(wildppn,mt_path);
			*mt_path = 0;
			}
		else
			{
			*p++ = 0;
			strcpy(wildppn,p);
			}
		p = strrchr(mt_path,*PATH_SEPARATOR);
		if (p == NULL)
			{
			strcpy(wilddevice,mt_path);
			*mt_path = 0;
			}
		else
			{
			*p++ = 0;
			strcpy(wilddevice,p);
			}
		if (*mt_path)
			{
			strcpy(mt_dir,mt_path);
			strcat(mt_dir,PATH_SEPARATOR);
			}
/*
		fnsplit(argv[i],drive,dir,name,ext);
		p = dir+strlen(dir);
		if (*--p != *PATH_SEPARATOR)
			error
		for(j=0;j<6;j++)
			if (!isdigit(*--p))
				error
		if (*--p != *PATH_SEPARATOR)
			error
		while(isdigit(*--p))
			;
		p++;
		for(j=0;j<3;j++)
			if (!isalpha(*--p))
				error
		if (*--p != PATH_SEPARATOR)
			error
		if (sscanf(dir,"%c%3s%d%c%3o%3o%c",*PATH_SEPARATOR,adevice,&adrivenum,*PATH_SEPARATOR,&aproj,&aprog,*PATH_SEPARATOR) != 4)
			{
			printf("Invalid dir %s\n",dir);
			exit(0);
			}
*/
		while((wd = nextdevice()) != NULL)
			{
			if (sscanf(wd,"%3s%d",adevice,&adrivenum) != 2)
				puts("oops 1");
			while((wp = nextppn()) != NULL)
				{
				if (sscanf(wp,"%3o%3o",&aproj,&aprog) != 2)
					puts("oops 2");
				while((wf = nextfn()) != NULL)
				{
				strcpy(afn,wf);
				if (stat(mt_path, &statbuf))
					continue;
/* validate filename */
				do
					{
					for(j=0;j<6;j++)
						if (!isalnum(afn[j]))
							break;
					k = j;
					if (afn[j] == '.')
						for(j++;j<=k+3;++j)
							if (!isalnum(afn[j]))
								break;
					if (afn[j] == 0)
						break;
					printf("Invalid AMOS filename %s - re-enter >",afn);
					gets(afn); /* could overflow */
					}
				while (*afn);
				if (*afn == 0)
					{
					puts("Skipping file.");
					continue;
					}
				if (afn[k])
					afn[k++] = 0;
				strcpy(aext,afn+k);
				strcpy(aname,afn);
				sprintf(mt_afn,"%s%d:%s.%s[%o,%o]",adevice,adrivenum,aname,aext,aproj,aprog);
/* test whether file is sequential or randon - a nasty problem */
				if (statbuf.st_size == 0)
					{
					l = 1;
					j = 2;
					}
				else if (statbuf.st_size % 512 == 0 && isctg(aname,aext))
					{
					l = statbuf.st_size/512;
					j = -1;
					mt_blocks += l;
					}
				else
					{
					l = (statbuf.st_size+510-1)/510;
					j = (INT)(statbuf.st_size % 510) + 2;
					mt_blocks += l;
					}
				tm = localtime(&statbuf.st_mtime);
/*				puts(asctime(tm));*/
				mt_files += 1;
				mkte(&te,adevice,adrivenum,aname,aext,
					aprog,aproj,l,j,tm,mt_files);
				printf("%s to %s: %s%s\n",mt_path,mt_dev,mt_afn,j == -1 ? "(C)" : "");
/* write toc entry */
				if (fwrite((VOID *)&te,sizeof(te),1,tocpf) != 1)
					{
					printf("Cannot write %s (errno = %d)\n",mt_toc_ipf,errno);
					exit(0);
					}
/* write native path name */
				if (fwrite((VOID *)mt_path,sizeof(mt_path),1,tocpf) != 1)
					{
					printf("Cannot write %s (errno = %d)\n",mt_toc_ipf,errno);
					exit(0);
					}
				} /* nextfn */
				} /* nextppn */
		} /* nextdevice */
	} /* argv[i] */

	printf("Total of %u files in %lu blocks selected.\n",mt_files,mt_blocks);

/* write out 1 extra toc entry to conform to Alpha bug */
	mt_files += 1;
	memset(buf,0,sizeof(te)+sizeof(mt_path));
	((struct toc_entry *)buf)->toc_flag = 0x01;
	if (fwrite(buf,sizeof(te)+sizeof(mt_path),1,tocpf) != 1)
		{
		printf("Cannot write %s (errno = %d)\n",mt_toc_ipf,errno);
		exit(0);
		}

	if (fclose(tocpf))
		{
		printf("Cannot close %s (errno = %d)\n",mt_toc_ipf,errno);
		exit(0);
		}

#if VCR
/* position to load point */
	vcr_loadpoint(mt_devfd, -1);
#endif

#if VCR
/* start recording */
	vcr_record(mt_devfd, -1);
	vcr_ioctl(mt_devfd, VCR_GET_DRIVER_DATA, &vd);
	vd.vcr_copy_count = 255;
	vcr_ioctl(mt_devfd, VCR_SET_DRIVER_DATA, &vd);
	vcr_ioctl(mt_devfd, VCR_SET_COPY_COUNT, NULL);
#else
/* write 1st hdr+label+file hdrs+data blocks+eof rec */
	mt_bytes_to_write = (1+1+mt_files-1+mt_blocks+1)*512;
	mt_weird_trigger = MAX(mt_bytes_to_write-11*512,2*512)&~(BIGBUF-1);
	memcpy(buf,mt_weird_hdr_id,sizeof(mt_weird_hdr_id));
	PUTLW(buf+sizeof(mt_weird_hdr_id),0xc4);
	memset(buf+sizeof(mt_weird_hdr_id)+sizeof(LWORD),4,512-sizeof(mt_weird_hdr_id)-sizeof(LWORD));
	if (mt_write(mt_devfd, buf) != 512)
		{
		printf("Cannot write 1st header record (errno=%d)\n",errno);
		exit(0);
		}
#endif

/* write label to mt_ */
	memcpy(buf, (VOID *)&lr, sizeof(lr));
	memset(buf+sizeof(lr),0,sizeof(buf)-sizeof(lr));
	if (mt_write(mt_devfd, buf) != 512)
		{
		printf("Cannot write label record (errno=%d)\n",errno);
		exit(0);
		}
#if VCR
	vcr_ioctl(mt_devfd, VCR_WRITE_EOF, NULL);
#endif

#if VCRSAV
/* write toc to mt_ */
	memcpy(buf,mt_toc_hdr_id,sizeof(mt_toc_hdr_id));
	pate = (struct atoc_entry *)(buf+sizeof(mt_toc_hdr_id));
	l = (mt_files+MAX_ATOC_ENTRIES-1)/MAX_ATOC_ENTRIES;
	j = (mt_files%MAX_ATOC_ENTRIES)*sizeof(*pate)+2;
	lt = time(NULL);
	tm = localtime(&lt);
	mkte(&te,"SWK",0,"VCRTOC","IPF",2,1,l,j,&tm,0);
	mt_toc_to_atoc(pate,&te);
	if (mt_write(mt_devfd, buf) != 512)
		{
		printf("Cannot write toc header record (errno=%d)\n",errno);
		exit(0);
		}
	puts("Writing table of contents from VCR.");
	if ((tocpf = fopen(mt_toc_ipf,"rb")) == NULL)
		{
		printf("Cannot open %s (errno = %d)\n",mt_toc_ipf,errno);
		exit(0);
		}
	do
		{
		PUTW(buf,fake_link);
		fake_link += 1;
		for(pate=(struct atoc_entry *)(buf+sizeof(UINT16));pate<(struct atoc_entry *)(buf+sizeof(UINT16))+MAX_ATOC_ENTRIES;pate++)
			{
			if (fread((VOID *)&te,sizeof(te),1,tocpf) != 1)
				break;
			if (fread(mt_path,sizeof(mt_path),1,tocpf) != 1)
				{
				printf("Cannot read %s (errno = %d)\n",mt_toc_ipf,errno);
				exit(0);
				}
			mt_toc_to_atoc(pate,&te);
			}
		memset((VOID *)pate,0,diffptr(buf+512,pate));
		if (mt_write(mt_devfd, buf) != 512)
			{
			printf("Cannot write toc record (errno=%d)\n",errno);
			exit(0);
			}
		}
	while(!feof(tocpf));
	vcr_ioctl(mt_devfd, VCR_WRITE_EOF, NULL);
	if (fclose(tocpf))
		{
		printf("Cannot close %s (errno = %d)\n",mt_toc_ipf,errno);
		exit(0);
		}

/* write data records */
	puts("Writing files.");
	vcr_ioctl(mt_devfd, VCR_GET_DRIVER_DATA, &vd);
	vd.vcr_copy_count = lr.lb_extra_copies;
	vcr_ioctl(mt_devfd, VCR_SET_DRIVER_DATA, &vd);
	vcr_ioctl(mt_devfd, VCR_SET_COPY_COUNT, NULL);
#endif /* VCRSAV */

	if ((tocpf = fopen(mt_toc_ipf,"rb")) == NULL)
		{
		printf("Cannot open %s (errno = %d)\n",mt_toc_ipf,errno);
		exit(0);
		}

	for(i=1;i<mt_files;++i)	/* do mt_files-1 times */
		{
		if (fread((VOID *)&te,sizeof(te),1,tocpf) != 1)
			{
			printf("Cannot read %s (errno = %d)\n",mt_toc_ipf,errno);
			exit(0);
			}
		mt_toc(&te);
		if (fread(mt_path,sizeof(mt_path),1,tocpf) != 1)
			{
			printf("Cannot read %s (errno = %d)\n",mt_toc_ipf,errno);
			exit(0);
			}
		printf("%s to %s: %s\n",mt_path,mt_dev,mt_afn);
		if ((inpf = fopen(mt_path,"rb")) == NULL)
			{
			printf("Cannot open %s (errno = %d)\n",mt_path,errno);
			exit(0);
			}
		memcpy(buf,mt_toc_hdr_id,sizeof(mt_toc_hdr_id));
		mt_toc_to_atoc(&ate,&te);
		memcpy(buf+sizeof(mt_toc_hdr_id),(VOID *)&ate,sizeof(ate));
		memset(buf+sizeof(mt_toc_hdr_id)+sizeof(ate),0,512-sizeof(mt_toc_hdr_id)-sizeof(ate));
		if (mt_write(mt_devfd, buf) != 512)
			{
			printf("Cannot write data header (errno=%d)\n",errno);
			exit(0);
			}
		for(l=1;l<=te.toc_blocks;++l)
			{
/* write out file differently if sequential vs contiguous */
			if (te.toc_active_bytes == (UINT16)-1)
				{
				if (fread(buf, 512, 1, inpf) != 1)
					{
					printf("Cannot read %s (errno = %d)\n",mt_path,errno);
					exit(0);
					}
				}
			else
				{
				if (l < te.toc_blocks)
					{
					k = 512;
					PUTW(buf,fake_link);
					fake_link += 1;
					}
				else
					{
					if (te.toc_active_bytes <= sizeof(UINT16))
						break;
					k = te.toc_active_bytes;
					memset(buf+k,0,512-k);
					PUTW(buf,0);
					}
/* printf("te.toc_blocks=%lu l=%lu k=%d\n",te.toc_blocks,lu,k);
errno=0;*/
				if (/*k > sizeof(UINT16) &&*/ fread(buf+sizeof(UINT16), k-sizeof(UINT16), 1, inpf) != 1)
					{
					printf("Cannot read %s (errno = %d)\n",mt_path,errno);
					exit(0);
					}
				}
			if (mt_write(mt_devfd, buf) != 512)
				{
				printf("Cannot write data record (errno=%d)\n",errno);
				exit(0);
				}
#ifdef NEVER
#if !VCRSAV
			if (!wrote_weird && i == mt_files-1 && te.toc_blocks-l <= 12 && mt_bytes_written % BIGBUF == 0)
				{
				memcpy(buf,mt_weird_hdr_id,sizeof(mt_weird_hdr_id));
				PUTLW(buf+sizeof(mt_weird_hdr_id),(te.toc_blocks-l)*0x200);
				memset(buf+sizeof(mt_weird_hdr_id)+sizeof(LWORD),4,512-sizeof(mt_weird_hdr_id)-sizeof(LWORD));
				if (mt_write(mt_devfd, buf) != 512)
					{
					printf("Cannot write weird data record (errno=%d)\n",errno);
					exit(0);
					}
				wrote_weird = -1;
				}
#endif
#endif /* NEVER */
			}
		if (fclose(inpf))
			{
			printf("Cannot close %s (errno = %d)\n",mt_path,errno);
			exit(0);
			}
		}

	if (fclose(tocpf))
		{
		printf("Cannot close %s (errno = %d)\n",mt_toc_ipf,errno);
		exit(0);
		}

	if (REMOVE(mt_toc_ipf))
		{
		printf("Cannot remove %s (errno = %d)\n",mt_toc_ipf,errno);
		exit(0);
		}
#if VCR
	vcr_close(mt_devfd);
#else
#ifdef NEVER	/* necessary??? */
	if (ioctl(mt_devfd,MT_WFM,0))
		{
		printf("\nCannot ioctl %s - errno=%d",mt_dev,errno);
		exit(0);
		}
#endif
	if (close(mt_devfd))
		{
		printf("Cannot close %s (errno = %d)\n",mt_dev,errno);
		exit(0);
		}
#endif

	puts("\nSave completed.");
}
