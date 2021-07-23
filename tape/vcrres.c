
/* AMOS compatible MTURES/STRRES/VCRRES */
/* Copyright 1991 Softworks Limited */

/*
notes:
	select hardware and restore format (just below)
	compile on SCO UNIX (not xenix) using "cc -I../../c xxxres.c -lx"
	compile on 6000 using "cc -I../../c xxxres.c"
	compile on TI using "gcc -I../../c xxxres.c"
wish list:
	* optimize - do text open after first block is analyzed
	* allow selection of filenames on command line
	* process ersatz names
*/

#include "swsubs.c"
#if __UNIX__
#include <sys/tape.h>
#endif

/* select hardware */
#if !defined(EXABYTE) && !defined(STREAMER) && !defined(VCR)
#define EXABYTE         0                       /* EXABYTE 8mm cartridge */
#define STREAMER        0                       /* scsi streamer hardware */
#define VCR             1                       /* VCR hardware */
#endif

/* select restore format */
#if !defined(MTURES) && !defined(STRRES) && !defined(VCRRES)
#define MTURES          0                       /* MTURES format */
#define STRRES          0                       /* STRRES format */
#define VCRRES          1                       /* VCRRES format */
#endif

/* defining default device */
#if EXABYTE
#if __GCC_TI__ || __SVS_TI__
#define MT_DEV          "/dev/rvt/1h"
#define MT_DEV_REWIND   "/dev/rvt/1h"
#else
#define MT_DEV          "/dev/rmt1"
#define MT_DEV_REWIND   "/dev/rmt1"
#endif
#endif

#if STREAMER
#if __GCC_TI__ || __SVS_TI__
#define MT_DEV          "/dev/rqt/0h"
#define MT_DEV_REWIND   "/dev/rqt/0h"
#else
#if __SCO__
#define MT_DEV          "/dev/rct0"
#define MT_DEV_REWIND   "/dev/rct0"
#else
#define MT_DEV          "/dev/rmt0"
#define MT_DEV_REWIND   "/dev/rmt0"
#endif
#endif
#endif

#if VCR
#define MT_DEV          "/dev/vcr"
#else
#if __DOS__
#undef MT_DEV
#define MT_DEV          "/dev/nrmt0" /* no rewind device */
#endif
#endif

/* defining misc */
#if VCRRES || STRRES
#define TOC             1                       /* has table of contents */
#endif

#if EXABYTE
/* patch - 12/18/97 - using 512 because of dir change problem */
#define BIGBUF          /*8192*/512                    /* variable read() size */
#else
#define BIGBUF          1024                    /* read() size */
#endif

#define NOSSD 1
#ifndef NOSSD
#include "rtssd.c"
#endif

#define MT_EOF 0x40
#define MT_EOT 0x80
#define TEXT_FILE 0
#define BINARY_FILE 1
#define CONTIGUOUS_FILE 2
#define UNKNOWN_FILE 3

#if VCRRES
UTINY mt_toc_hdr_id_vcr[10] = {0xff,0xfe,0xfd,0x00,0x00,0xfc,0xfb,0x00,0xfa,0xff};
#else
UTINY mt_toc_hdr_id_mt1[16] = {0xff,0xfe,0xfd,0x00,0x00,0xfc,0xfb,0x00,0xfa,0xff,0,0,0,0,0,0};
#if EXABYTE /* patch - 12/18/97 */
UTINY mt_toc_hdr_id_mt2[16] = {0xff,0xfd,0xfc,0x00,0x00,0xfb,0xfa,0x00,0xf9,0xff,0,0,0x02,0,0,0};
#else
UTINY mt_toc_hdr_id_mt2[16] = {0xff,0xfd,0xfc,0x00,0x00,0xfb,0xfa,0x00,0xf9,0xff,0,0,0,0,0,0};
#endif
UTINY mt_1st_hdr_id[8] = {0xaa,0xff,0x55,0xff,0x00,0x00,0xc4,0x00};
UTINY mt_unknown_hdr_id[4] = {0xaa,0xff,0x55,0xff};
#endif
UTINY mt_lb_hdr_id[4] = {0xaa,0xaa,0x55,0x55};

#if __DOS__
struct ftime ft;
#define CHECKINT bdos(0x0b,0,0)         /* allow DOS to check for ^C */
#define OS "DOS"
#endif /* __DOS__ */

#if __UNIX__
#if __GCC_TI__ || __SVS_TI__
struct utimbuf {
	time_t  actime;                 /* access time */
	time_t  modtime;                /* modification time */
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
UINT mt_files = UINT_MAX;
ULONG mt_blocks = 0;
TOC_ENTRY toc;
INT mt_fd = 0;
TEXT mt_path[128];              /* should be MAX_FILENAME or something */
TEXT mt_dir[128];
TEXT *mt_fn;
TEXT *mt_ext;
TEXT mt_afn[32];
TEXT mt_toc_ipf[6+1+3+1];
BOOL mt_msgs = 1;
TEXT *mt_pgm;
TEXT *mt_dev = MT_DEV;
TEXT *mt_dev_rewind = MT_DEV_REWIND;
BOOL mt_force_text = 0, mt_force_binary = 0;
UINT mt_version = 0;
UINT mt_max_atoc_entries;
#if EXABYTE
UINT link_size = 4;
#else
UINT link_size = 2;
#endif

VOID mt_rewind __((VOID));
INT mt_read __((VOID *buf));
INT mt_readeof __((VOID));
VOID mt_loadpoint __((VOID));
VOID mt_playback __((VOID));
VOID mt_readlabel __((ALABEL_RECORD *lb));
VOID mt_term __((VOID));
#if VCRRES
VOID mt_atoc_to_toc_vcr __((TOC_ENTRY *te, ATOC_ENTRY_VCR *ate));
#else
VOID mt_atoc_to_toc_mt1 __((TOC_ENTRY *te, ATOC_ENTRY_MT1 *ate));
VOID mt_atoc_to_toc_mt2 __((TOC_ENTRY *te, ATOC_ENTRY_MT2 *ate));
#endif
INT mt_readtoc __((TOC_ENTRY *te));
VOID mt_label __((ALABEL_RECORD *lr));
VOID mt_toc __((TOC_ENTRY *te));
VOID mt_mkdir __((TEXT *mt_dir, INT mode));
VOID mt_sigint __((INT sig));
INT mt_convert_text_block __((TEXT *buf, INT len, BOOL last_block));

LOCAL UTINY mt_eofbuf[512];

VOID mt_rewind()
{
#if !VCR
	if (strncmp(mt_dev_rewind,"/dev/",5))
		return;
	if (mt_devfd != -1)
        	close(mt_devfd);
        mt_devfd = open(mt_dev_rewind,O_RDONLY|O_BINARY);
	if (mt_devfd < 0)
		{
                printf("\nCannot open %s - errno=%d\n",mt_dev_rewind,errno);
		exit(0);
		}
	close(mt_devfd);
#endif
}

INT mt_read(buf)
VOID *buf;
{
#if VCR
INT ret;
	ret = vcr_read(mt_devfd, buf);
	if (ret == 512)
		return(0);
	if (ret == 0)
		{
		vcr_get_driver_data(mt_devfd, &vd);
		switch(vd.vcr_data_set_header[0])
			{
		case VCR_TYPE_EOF:
			return(MT_EOF);
		case VCR_TYPE_EOT:
			return(MT_EOT);
			}
		}
	printf("mt_read: Error reading vcr block (%d)\n",ret);
	exit(0);
#else /* VCR */
LOCAL UTINY bigbuf[BIGBUF], *p = bigbuf+sizeof(bigbuf);
LOCAL INT i = 0;

/*printf("mt_read: p=%lx bigbuf=%lx bigbuf+sizeof(bigbuf)=%lx\n",p,bigbuf,bigbuf+sizeof(bigbuf));*/
	if (p >= bigbuf+i)
		{
		p = bigbuf;
		if ((i = read(mt_devfd, bigbuf, sizeof(bigbuf))) == -1)
			return(-1);
/*printf("mt_read: i=%d %02x %02x %02x %02x\n",i,bigbuf[0],bigbuf[1],bigbuf[2],bigbuf[3]);*/
#if STREAMER
/* warning: kludged to throw away any record beginning with aaff55ff.
Don't know why streamer tape has records with aaff55ff inserted. They are
even inserted in the middle of a data file. */
		if (memcmp(bigbuf,mt_unknown_hdr_id,sizeof(mt_unknown_hdr_id)) == 0)
			{
#ifdef NEVER
			puts("aaff55ff record found");
#endif
			p += 512;
/* patch - 10/31/93 - added p = */
			if (p >= bigbuf+i && (i = read(mt_devfd, p = bigbuf, sizeof(bigbuf))) == -1)
				return(-1);
/*printf("mt_read: i=%d %x %x %x %x\n",i,p[0],p[1],p[2],p[3]);*/
			if (memcmp(p,mt_unknown_hdr_id,sizeof(mt_unknown_hdr_id)) == 0)
				return(MT_EOT);
			}
#endif /* STREAMER */
		if (i <= 1)
			{
			i = 0;
			return(MT_EOF);
			}
		memset(bigbuf+i,0,(i+511)/512*512-i);
		}
/*
	if (p >= bigbuf+i)
		{
		p = NULL;
		return(MT_EOF);
		}
*/
	memcpy(buf,p,512);
	p += 512;
#endif /* VCR */
#ifdef NEVER
{
int j;
for(j=0;j<16;j++)
  printf("%02x ",((UTINY *)buf)[j]);
printf("\n");
}
#endif
	return(0);
}

INT mt_readeof()
{
UTINY buf[512];
#if VCR
	return(mt_read(buf));
#else
INT ret;
	ret = mt_read(mt_eofbuf);
	if (ret != MT_EOF)
		{
#if __SCO__
LONG l,ll;
UTINY mt_unblocking_hdr_id[4] = {0xa5,0x5a,0,0};
		if ((l = lseek(mt_devfd, 0L, SEEK_CUR)) == -1L)
			return(-1);
		ll = ((l+4096L-1)/4096L)*4096L-4096L;
		do
			{
			ll += 4096L;
			while(l<=ll)
				{
				if (mt_read(mt_eofbuf))
					return(-1);
				l += 512L;
				}
			}
		while(memcmp(mt_eofbuf,mt_unblocking_hdr_id,sizeof(mt_unblocking_hdr_id)) == 0);
#endif
		}
	return(MT_EOF);
#endif /* VCR */
}

VOID mt_loadpoint()
{
#if VCRRES
	vcr_loadpoint(mt_devfd, -1);
#endif /* VCRRES */
}

VOID mt_playback()
{
#if VCRRES
	vcr_playback(mt_devfd, -1);
#endif /* VCRRES */
}

VOID mt_readlabel(lb)
ALABEL_RECORD *lb;
{
UTINY buf[512];
UINT i,j;

#if EXABYTE /* patch - 12/18/97 - added read() - needed for ALS */
	if (read(mt_devfd, buf, 0xc4) != 0xc4)
#else
	if (mt_read(buf))
#endif
		{
		puts("Cannot read first label record");
#if __AIX__
		puts("Be sure to set tape block size to zero, using smit");
#endif
		exit(0);
		}
	memcpy(lb,buf,sizeof(ALABEL_RECORD));
#if VCRRES
	if (GET_UINT16_12(lb->lb_vers) != 2)
		{
		puts("Incompatible tape format");
		exit(0);
		}
#endif
	for(i=0,j=GET_UINT16_12(lb->lb_lb_cnt);i<j;++i)
		if (mt_read(buf))
			{
			puts("Cannot read extra label record");
			exit(0);
			}
/* swap label here */
}

VOID mt_term()
{
INT i;

#if VCRRES
	return;
#endif

	if (mt_readeof() != MT_EOF)
		{
		puts("Cannot find data EOF");
		exit(0);
		}

#if !VCRRES
	if (mt_readeof() != MT_EOF)
		puts("Warning: Cannot find EOF EOF");

        mt_rewind();
#else
	i = mt_readeof();
	if (i == MT_EOF)
		{
		if (mt_readeof() != MT_EOT)
			puts("Warning: Cannot find EOT");
		}
	else if (i != MT_EOT)
		puts("Warning: Cannot find 2nd EOF or EOT");

	vcr_get_driver_data(mt_devfd, &vd);

	if (vd.vcr_buffered_warnings > 0)
		printf("There were %u buffer warnings.\nMost records buffered was %u.\n",vd.vcr_buffered_warnings,vd.vcr_max_buffered);

	vcr_close(mt_devfd);
#endif /* VCRRES */
}

#if VCRRES
VOID mt_atoc_to_toc_vcr(te,ate)
TOC_ENTRY *te;
ATOC_ENTRY_VCR *ate;
{
#define SWAP_BYTE(s) (te->s = GET_UINT8_1(ate->s))
#define SWAP_BYTES(s) memcpy((VOID *)te->s,(VOID *)ate->s,sizeof(te->s))
#define SWAP_WORD(s) (te->s=GET_UINT16_12(ate->s))
#define SWAP_LWORD(s) (te->s=GET_UINT32_3412(ate->s))

	SWAP_WORD(toc_device);
	SWAP_BYTE(toc_flag);
	SWAP_BYTE(toc_drivenum);
	SWAP_WORD(toc_fn[0]);
	SWAP_WORD(toc_fn[1]);
	SWAP_WORD(toc_ext);
	SWAP_BYTE(toc_prog);
	SWAP_BYTE(toc_proj);
	SWAP_LWORD(toc_blocks);
	SWAP_WORD(toc_active_bytes);
	SWAP_BYTES(toc_date);
        te->toc_time = GET_UINT16_12(ate->toc_time[0]) | ((UINT32)GET_UINT16_12(ate->toc_time[1]) << 16);
	SWAP_WORD(toc_hash[0]);
	SWAP_WORD(toc_hash[1]);
	SWAP_WORD(toc_entry_number);
#undef SWAP_BYTE
#undef SWAP_BYTES
#undef SWAP_WORD
#undef SWAP_LWORD
/*printf("fn=%x %x %x %lu %d\n",te->toc_fn[0],te->toc_fn[1],te->toc_ext,te->toc_blocks,te->toc_active_bytes);
getchar();*/
}
#else
VOID mt_atoc_to_toc_mt1(te,ate)
TOC_ENTRY *te;
ATOC_ENTRY_MT1 *ate;
{
#define SWAP_BYTE(s) (te->s = GET_UINT8_1(ate->s))
#define SWAP_BYTES(s) memcpy((VOID *)te->s,(VOID *)ate->s,sizeof(te->s))
#define SWAP_WORD(s) (te->s=GET_UINT16_12(ate->s))
#define SWAP_LWORD(s) (te->s=GET_UINT32_3412(ate->s))

	SWAP_WORD(toc_device);
	SWAP_BYTE(toc_drivenum);
	SWAP_BYTE(toc_flag);
	SWAP_WORD(toc_fn[0]);
	SWAP_WORD(toc_fn[1]);
	SWAP_WORD(toc_ext);
	SWAP_BYTE(toc_prog);
	SWAP_BYTE(toc_proj);
	SWAP_WORD(toc_blocks);
	SWAP_WORD(toc_active_bytes);
#ifdef NEVER /* temp */
	SWAP_BYTE(toc_date);
#endif
        te->toc_time = GET_UINT16_12(ate->toc_time[0]) | ((UINT32)GET_UINT16_12(ate->toc_time[1]) << 16);
	SWAP_WORD(toc_hash[0]);
	SWAP_WORD(toc_hash[1]);
	te->toc_entry_number = 0;
#undef SWAP_BYTE
#undef SWAP_BYTES
#undef SWAP_WORD
#undef SWAP_LWORD
/*printf("fn=%x %x %x %lu %d\n",te->toc_fn[0],te->toc_fn[1],te->toc_ext,te->toc_blocks,te->toc_active_bytes);
getchar();*/
}
VOID mt_atoc_to_toc_mt2(te,ate)
TOC_ENTRY *te;
ATOC_ENTRY_MT2 *ate;
{
#define SWAP_BYTE(s) (te->s = GET_UINT8_1(ate->s))
#define SWAP_BYTES(s) memcpy((VOID *)te->s,(VOID *)ate->s,sizeof(te->s))
#define SWAP_WORD(s) (te->s=GET_UINT16_12(ate->s))
#define SWAP_LWORD(s) (te->s=GET_UINT32_3412(ate->s))

	SWAP_WORD(toc_entry_number);
	SWAP_WORD(toc_blocks);
	SWAP_WORD(toc_active_bytes);
	SWAP_WORD(toc_device);
	SWAP_BYTE(toc_drivenum);
	SWAP_BYTE(toc_flag);
	SWAP_WORD(toc_fn[0]);
	SWAP_WORD(toc_fn[1]);
	SWAP_WORD(toc_ext);
	SWAP_BYTE(toc_prog);
	SWAP_BYTE(toc_proj);
#ifdef NEVER /* temp */
	SWAP_BYTE(toc_date);
#endif
        te->toc_time = GET_UINT16_12(ate->toc_time[0]) | ((UINT32)GET_UINT16_12(ate->toc_time[1]) << 16);
	SWAP_WORD(toc_hash[0]);
	SWAP_WORD(toc_hash[1]);
#undef SWAP_BYTE
#undef SWAP_BYTES
#undef SWAP_WORD
#undef SWAP_LWORD
/*printf("fn=%x %x %x %lu %d\n",te->toc_fn[0],te->toc_fn[1],te->toc_ext,te->toc_blocks,te->toc_active_bytes);
getchar();*/
}
#endif /* VCRRES */

INT mt_readtoc(te)
TOC_ENTRY *te;
{
UTINY buf[512];
BOOL opened = 0;
INT retry = 1;

/*printf("ftell=%lx\n",lseek(mt_devfd,0,SEEK_CUR));*/
read_again:
	if (mt_read(buf))
{
/* use ioctl(mt_devfd,MT_...) to detect EOF, EOT? */
/*puts("mt_readtoc: eof 1");*/
		if (mt_read(buf))
{
/*puts("mt_readtoc: eof 2");*/
	if (opened || strncmp(mt_dev,"/dev/",5))
		return(MT_EOT);
	opened = -1;
#if STREAMER
#ifdef MT_RFM
/*puts("mt_readtoc: MT_RFM\n");*/
	if (ioctl(mt_devfd,MT_RFM,0))
		{
                printf("\nCannot MT_RFM ioctl %s - errno=%d\n",mt_dev,errno);
		exit(0);
		}
#else
#ifdef STIOCTOP
/*puts("mt_readtoc: STIOCTOP\n");*/
	{
	struct stop st;
	st.st_op = STFSF;
	st.st_count = 1;
	if (ioctl(mt_devfd,STIOCTOP,&st))
		{
		if (errno == EIO) /* warning! assumes EIO is EOT! */
			return(MT_EOT);
                printf("\nCannot STIOCTOP ioctl %s - errno=%d\n",mt_dev,errno);
		exit(0);
		}
	}
#else
	close(mt_devfd);
	mt_devfd = open(mt_dev,O_RDONLY|O_BINARY);
	if (mt_devfd < 0)
		{
                printf("\nCannot open %s - errno=%d\n",mt_dev,errno);
		exit(0);
		}
#endif /* STIOCTOP */
#endif /* MT_RFM */
	goto read_again;
#endif /* STREAMER */
}
}
#if VCRRES
        if (memcmp(buf,mt_toc_hdr_id_vcr,sizeof(mt_toc_hdr_id_vcr)) == 0)
                mt_atoc_to_toc_vcr(te,(ATOC_ENTRY_VCR *)(buf+sizeof(mt_toc_hdr_id_vcr)));
#else
        if (memcmp(buf,mt_toc_hdr_id_mt1,sizeof(mt_toc_hdr_id_mt1)) == 0)
                mt_version = 1;
        else if (memcmp(buf,mt_toc_hdr_id_mt2,sizeof(mt_toc_hdr_id_mt2)) == 0)
                mt_version = 2;
        else if (memcmp(buf+1,mt_toc_hdr_id_mt2,sizeof(mt_toc_hdr_id_mt2)) == 0)
		{
		/* patch - 12/18/97 - added - dir change code */
		/* directory change is signaled by a leading 0xff */
		memmove(buf,buf+1,sizeof(buf)-1);
		if (read(mt_devfd,buf+sizeof(buf)-1,1) != 1)
			exit(0);
                mt_version = 2;
		}
	else
		mt_version = 0;
        if (mt_version == 1)
                {
                mt_max_atoc_entries = MAX_ATOC_ENTRIES_MT1;
                mt_atoc_to_toc_mt1(te,(ATOC_ENTRY_MT1 *)(buf+sizeof(mt_toc_hdr_id_mt1)));
                }
        else if (mt_version == 2)
                {
                mt_max_atoc_entries = MAX_ATOC_ENTRIES_MT2;
                mt_atoc_to_toc_mt2(te,(ATOC_ENTRY_MT2 *)(buf+sizeof(mt_toc_hdr_id_mt2)));
                }
#endif
        else
                {
                INT i;
                printf("Invalid toc header record:");
                for(i=0;i<16;i++)
                        printf(" %02x",buf[i]);
                printf("\n");
		if (retry--)
			goto read_again;
		exit(0);
                }
	return(0);
}

VOID mt_label(lr)
ALABEL_RECORD *lr;
{
	if (memcmp(lr->lb_hdr,mt_lb_hdr_id,sizeof(mt_lb_hdr_id)) != 0)
		puts("\nInvalid label record\n");
	else
		{
		printf("\nVolume Name: %.40s\n",lr->lb_vln);
		printf("Volume ID: %.10s\n",lr->lb_vid);
		printf("Installation: %.30s\n",lr->lb_ins);
		printf("System: %.30s\n",lr->lb_sys);
		printf("Creator: %.30s\n",lr->lb_cre);
#if VCRRES
		printf("\nTape made with %u extra copies\n",lr->lb_extra_copies);
#endif /* VCRRES */
		printf("\n");
		}
}

VOID mt_toc(te)
TOC_ENTRY *te;
{
TEXT device[4],*p;

/*
	adatetotm(&ftm, te.toc_date);
	atimetotm(&ftm, te.toc_time);
	if ((ftime = mktime(&ftm)) != (time_t)-1)
		puts(asctime(&ftm));
	else
		puts("mktime() error");
*/
/* make file names */
	p = rad50toa(device,te->toc_device);
	*p = 0;
#ifdef NEVER /* used to process extended logicals */
if (strcmp(device,"DSK") == 0 && te->toc_drivenum == 1)
link_size = 4;
else
link_size = 2;
printf("link_size=%d %s%u\n",link_size,device,te->toc_drivenum);
#endif
	p = mt_path;
	p += sprintf(p,"%s%u%c%03o%03o%c",
		device,te->toc_drivenum,*PATH_SEPARATOR,te->toc_proj,te->toc_prog,*PATH_SEPARATOR);
	mt_fn = p;
	if (te->toc_fn[0] == 0 && te->toc_fn[1] == 0)
		{
		strcpy(p,"NONAME");
		p += strlen(p);
		}
	else
		{
		p = rad50toa(p,te->toc_fn[0]);
		p = rad50toa(p,te->toc_fn[1]);
		}
	*p++ = '.';
	mt_ext = p;
	p = rad50toa(p,te->toc_ext);
	*p = 0;
	sprintf(mt_afn,"%s%u:%s[%o,%o]",device,te->toc_drivenum,mt_fn,te->toc_proj,te->toc_prog);
	if (pathcase() == 'L')
		strlwr(mt_path);
	memcpy(mt_dir,mt_path,diffptr(mt_fn,mt_path));
	mt_dir[diffptr(mt_fn,mt_path)] = 0;
/*printf("afn=%s fn=%s blocks=%lu active bytes=%u #%u\n",mt_afn,mt_path,te->toc_blocks,te->toc_active_bytes,te->toc_entry_number);*/
}

VOID mt_mkdir(mt_dir,mode)
TEXT *mt_dir;
INT mode;
{
TEXT dir[128],*p;

/* need to check if write is permitted */
	p = strcpy(dir,mt_dir);
	while(*p != 0 && (p = strchr(p+1,*PATH_SEPARATOR)) != NULL)
		{
		*p = 0;
#if __DOS__
		if (mkdir(dir) && errno != EACCES)
#endif
#if __UNIX__
#if __SCO_UNIX__
		if (mkdir(dir,(mode_t)mode) && errno != EEXIST)
#else
		if (mkdir(dir,mode) && errno != EEXIST)
#endif
#endif
			{
			printf("Cannot mkdir %s (errno = %d)\n",dir,errno);
			exit(0);
			}
		*p = *PATH_SEPARATOR;
		}
}

VOID mt_sigint(sig)
INT sig;
{
	REMOVE(mt_toc_ipf);
	if (mt_fd > 0)
		{
		close(mt_fd);
		REMOVE(mt_path);
		}
#if VCR
	if (*vcr_errstep)
		printf("\nProgram aborted by user while trying to %s.\n",vcr_errstep);
	else
		puts("\nProgram aborted by user.");

	vcr_get_driver_data(mt_devfd, &vd);

	if (vd.vcr_buffered_warnings > 0)
		printf("There were %u buffer warnings.\nMost records buffered was %u.\n",vd.vcr_buffered_warnings,vd.vcr_max_buffered);
#else
        mt_rewind();
#endif
	exit(0);
}

INT mt_convert_text_block(buf,len,last_block)
TEXT *buf;
INT len;
BOOL last_block;
{
TEXT *p,*pe,*q;
INT c;

	for(p=buf,pe=buf+len,q=buf;p<pe;)
		{
		c = *p++;
		*q++ = c;
		if (c < ' ' || c > 127)
			switch(c)
				{
				case 9:         /* tab */
					break;
				case 10:        /* line-feed */
					break;
				case 13:        /* carriage-return */
#if __UNIX__
					--q;
#endif
					break;
				case 26:        /* ^Z */
					if (last_block && p == pe)
						{
						--q;
						break;
						}
				default:
					if (!mt_force_text)
						return(-1);
				}
		}
	return(diffptr(q,buf));
}
INT main(argc,argv)
INT argc;
TEXT *argv[];
{
INT arg,i,j,k,kk;
LONG l;
UTINY buf[512];
FILE *tocpf;
TOC_ENTRY te;
TEXT dir[sizeof(mt_dir)];
#if __DOS__
struct dfree dtable;
UINT clusters_needed[26];               /* Drive A: to Z: */
TEXT drive[MAXDRIVE];
#endif
#if __UNIX__
struct utimbuf ut;
struct tm ftm;
time_t ftime;
ULONG blocks_needed = 0;
UINT fnodes_needed = 0;
struct statfs xstatfs;
#endif
ALABEL_RECORD lb;
INT file_type;
BOOL display_usage = 0;
time_t lt;
struct tm *tm;
ADATE todays_adate;
UINT32 todays_atime;
TEXT *text_path = "text.tmp";
INT text_fd;


	setbuf(stdout,NULL);

	signal(SIGINT,mt_sigint);

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
	for(arg=1;arg<argc && *argv[arg] == SW;++arg)
		switch(*(argv[arg]+1))
			{
			case 'd':
                                mt_dev_rewind = mt_dev = argv[++arg];
				break;
			case 'm':
				mt_force_text = -1;
				break;
			case 'r':
				mt_force_binary = -1;
				break;
			default:
				printf("\nUndefined switch %s\n",argv[arg]);
				display_usage = -1;
			}

	if (display_usage)
		{
		puts("\nusage:");
		printf("\t%s optional-flags\n",mt_pgm);
		puts("optional-flags:");
#if __DOS__
		puts("\t/d device-number {i.e. /d 0 {A:} /d 0x80 {C:}}");
#else
		puts("\t-d device-node {i.e. -d /dev/dsk/4s0, -d /dev/rfd048ds9}");
#endif
		printf("\t%cm force removing of carriage-returns (0x0d)\n",SW);
		printf("\t%cr force no translation\n",SW);
		puts("copy codes:\n\t(T) - sequential text file\n\t(B) - sequential binary file\n\t(C) - contiguous binary file");
		exit(0);
		}

	mt_rewind();

#if __DOS__
	memset(clusters_needed,0,sizeof(clusters_needed));
#if VCR
	vcr_open();
#else
	mt_devfd = open(mt_dev,O_RDONLY|O_BINARY);
	if (mt_devfd < 0)
		{
                printf("\nCannot open %s - errno=%d\n",mt_dev,errno);
		exit(0);
		}
#endif
#endif
#if __UNIX__
	mt_devfd = open(mt_dev,O_RDONLY);
	if (mt_devfd < 0)
		{
		printf("\nCannot open %s - ",mt_dev);
		if (errno == ENOENT)
			printf("%s not created\n",mt_dev);
		else if (errno == ENODEV)
			printf("%s not installed in kernal\n",mt_dev);
		else
                        printf("errno=%d\n",errno);
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

/* position to load point */
	mt_loadpoint();

/* start playback */
	mt_playback();

/* read label record */
	mt_readlabel(&lb);
	mt_label(&lb);

#if TOC
/* read toc */
#if VCRRES
	printf("Reading table of contents from VCR.");
#endif /* VCR */
	mt_readtoc(&toc);
	mt_toc(&toc);

	strcpy(mt_toc_ipf,mt_fn);
	if ((tocpf = fopen(mt_toc_ipf,"wb")) == NULL)
		{
		printf("Cannot open %s (errno = %d)\n",mt_toc_ipf,errno);
		exit(0);
		}

#if VCRRES
        mt_files = ((UINT)toc.toc_blocks-1)*mt_max_atoc_entries+(toc.toc_active_bytes-link_size)/sizeof(ATOC_ENTRY_VCR);
	mt_files -= 1;  /* fix count - Alpha bug? */
#else
        if (mt_version == 1)
                mt_files = ((UINT)toc.toc_blocks-1)*mt_max_atoc_entries+(toc.toc_active_bytes-link_size)/sizeof(ATOC_ENTRY_MT1);
        else if (mt_version == 2)
                mt_files = ((UINT)toc.toc_blocks-1)*mt_max_atoc_entries+(toc.toc_active_bytes-link_size)/sizeof(ATOC_ENTRY_MT2);
#endif
/* probably should just dump out blocks and reformat later */
	for(i=0,k=0;i<(INT)toc.toc_blocks;++i)
		{
if (mt_read(buf))
		if (mt_read(buf))
			{
			puts("Cannot read toc record");
			exit(0);
			}
                for(j=0;j<mt_max_atoc_entries && k < mt_files;++j,++k)
			{
#if VCRRES
                        mt_atoc_to_toc_vcr(&te,(ATOC_ENTRY_VCR *)(buf+link_size)+j);
#else
                        if (mt_version == 1)
                                {
                                mt_atoc_to_toc_mt1(&te,(ATOC_ENTRY_MT1 *)(buf+8)+j);
                                te.toc_entry_number = k+1;
                                }
                        else if (mt_version == 2)
                                {
                                mt_atoc_to_toc_mt2(&te,(ATOC_ENTRY_MT2 *)(buf+8)+j);
                                te.toc_entry_number = j+1;
                                }
#endif
			if (fwrite(&te,sizeof(TOC_ENTRY),1,tocpf) != 1)
				{
				printf("Cannot write %s (errno = %d)\n",mt_toc_ipf,errno);
				exit(0);
				}
/* mt_toc(&te); puts(mt_afn); */
			}
		}
	if (fclose(tocpf) != 0)
		{
		printf("Cannot close %s (errno = %d)\n",mt_toc_ipf,errno);
		exit(0);
		}

/* position to load point */
#if VCRRES
	if (mt_readeof() != MT_EOF)
		{
		puts("Cannot find toc EOF");
		exit(0);
		}
#endif
	mt_loadpoint();

/* display toc */
	if ((tocpf = fopen(mt_toc_ipf,"rb")) == NULL)
		{
		printf("Cannot open %s (errno = %d)\n",mt_toc_ipf,errno);
		exit(0);
		}
#if __UNIX__
	if (statfs(".", &xstatfs, sizeof(xstatfs), 0))
		{
		printf("statfs() failed (errno = %d)\n",errno);
		exit(0);
		}
#endif
	for(i=1;i<=mt_files;++i)
		{
		if (fread(&te,sizeof(TOC_ENTRY),1,tocpf) != 1)
			{
			printf("Cannot read %s (errno = %d)\n",mt_toc_ipf,errno);
			exit(0);
			}
		mt_toc(&te);
		if (te.toc_entry_number != i)
			{
			printf("Missing toc entry %d found %d\n",i,te.toc_entry_number);
			exit(0);
			}
		printf("Restoring %s: %s%s to %s\n",mt_dev,mt_afn,te.toc_active_bytes == (UINT16)-1 ? "(C)" : "",mt_path);
/* create directories */
		mt_mkdir(mt_dir,0777);
/* remove file */
		if (REMOVE(mt_path) && errno != ENOENT)
			{
			printf("Cannot remove %s (errno = %d)\n",mt_path,errno);
			exit(0);
			}
		mt_blocks += te.toc_blocks;
/* calculate new file sizes */
		if (te.toc_active_bytes == (UINT16)-1)
			l = te.toc_blocks*512;
		else
			{
			l = (te.toc_blocks-1)*(512-link_size);
			if (te.toc_active_bytes > link_size)
				l += te.toc_active_bytes-link_size;
			}
#if __DOS__
		fnsplit(mt_path, drive, NULL, NULL, NULL);
		j = *drive ? *drive-'A' : getdisk();
		getdfree(j+1,&dtable);
		k = (INT)((l+dtable.df_bsec-1)/dtable.df_bsec+dtable.df_sclus-1)/dtable.df_sclus;
		clusters_needed[j] += k;
/*printf("drive=%c: l=%ld k=%d clusters_needed=%u\n",j+'A',l,k,clusters_needed[j]);
if (k > 1) getchar();*/
#endif
#if __UNIX__
/* doesn't consider the possibility of multiple file systems */
/* warning: f_bsize is >= 1024 but other f_ items may assume 512 byte blocks! */
		blocks_needed += (l+xstatfs.f_bsize-1)/xstatfs.f_bsize;
		fnodes_needed += 1;
/*printf("l=%ld need=%lu nodes=%u\n",l,blocks_needed,fnodes_needed);
getchar();*/
#endif
		}
	if (fclose(tocpf) != 0)
		{
		printf("Cannot close %s (errno = %d)\n",mt_toc_ipf,errno);
		exit(0);
		}
	if (REMOVE(mt_toc_ipf))
		{
		printf("Cannot remove %s (errno = %d)\n",mt_toc_ipf,errno);
		exit(0);
		}
	printf("Total of %u files in %lu blocks selected.\n\n",mt_files,mt_blocks);
#if __DOS__
	for(i=0;i<26;++i)
		{
		if (clusters_needed[i] == 0)
			continue;
		getdfree(i+1,&dtable);
		if (dtable.df_sclus == (UINT)-1)
			{
			printf("getdfree() failed for drive %c:\n",'A'+i);
			exit(0);
			}
		printf("Drive %c: has %lu bytes free. %lu bytes will be allocated.\n",'A'+i,(ULONG)dtable.df_bsec*dtable.df_sclus*dtable.df_avail,(ULONG)dtable.df_bsec*dtable.df_sclus*clusters_needed[i]);
		if (clusters_needed[i] > dtable.df_avail)
			{
			printf("Not enough disk space available on drive %c:\n",'A'+i);
			exit(0);
			}
		}
#endif
#if __UNIX__
	printf("%s file system:\n",*xstatfs.f_fname ? xstatfs.f_fname : "root");
	printf("\t%blocks free:    %8lu\tfile nodes free:    %8ld\n",xstatfs.f_bfree*xstatfs.f_bsize/512,xstatfs.f_ffree);
/*      blocks_needed *= xstatfs.f_bsize/512;*/
	printf("\t%blocks required:%8lu\tfile nodes required:%8u\n",blocks_needed*xstatfs.f_bsize/512,fnodes_needed);
	if (blocks_needed > xstatfs.f_bfree)
		{
		puts("Not enough disk space free.");
		exit(0);
		}
	if (fnodes_needed > xstatfs.f_ffree)
		{
		puts("Not enough file nodes free.");
		exit(0);
		}
#endif

/* start playback */
	mt_playback();
#if VCRRES
	if (mt_read(buf))
		{
		puts("Cannot bypass label record");
		exit(0);
		}
	if (mt_readeof() != MT_EOF)
		{
		puts("Cannot bypass label EOF");
		exit(0);
		}
	if (mt_read(buf))
		{
		puts("Cannot bypass toc header record");
		exit(0);
		}
	puts("Bypassing table of contents.");
	for(i=0;i<(INT)toc.toc_blocks;++i)
		if (mt_read(buf))
			{
			puts("Cannot bypass toc record");
			exit(0);
			}
	if (mt_readeof() != MT_EOF)
		{
		puts("Cannot bypass toc EOF");
		exit(0);
		}
#endif /* VCRRES */
#endif /* TOC */

/* read data records and create files */
	lt = time(NULL);
	tm = localtime(&lt);
	tmtoadate(&todays_adate, tm);
	tmtoatime(&todays_atime, tm);
	*dir = 0;
	for(i=1;i<=mt_files;++i)
		{
		if (mt_readtoc(&te) == MT_EOT)
			break;
		mt_toc(&te);
		if (mt_msgs)
			{
			if (strcmp(mt_dir,dir) != 0)
				{
				printf("\n%s\n",strcpy(dir,mt_dir));
#if !TOC
/* create directories */
				mt_mkdir(mt_dir,0777);
#endif
				}
			printf("%5d  %-10s%8lu",i,mt_fn,te.toc_blocks);
			}
		if (te.toc_active_bytes == (UINT16)-1)
			file_type = CONTIGUOUS_FILE;
		else if (mt_force_binary)
			file_type = BINARY_FILE;
		else if (mt_force_text)
			file_type = TEXT_FILE;
		else
			file_type = UNKNOWN_FILE;
/* need to set execute permission for .CMD and .DO files */
#if VCRRES      /* to reduce irratic delays due to syncing - use O_SYNC */
		if ((mt_fd = open(mt_path,O_SYNC|O_WRONLY|O_CREAT|O_TRUNC|O_BINARY,0666)) == -1)
#else           /* for better performace, we can omit O_SYNC here */
		if ((mt_fd = open(mt_path,O_WRONLY|O_CREAT|O_TRUNC|O_BINARY,0666)) == -1)
#endif
			{
			printf("Cannot open %s (errno = %d)\n",mt_path,errno);
			exit(0);
			}
		if (file_type == TEXT_FILE)
			text_fd = mt_fd;
		else if (file_type == UNKNOWN_FILE)
#if VCRRES      /* to reduce irratic delays due to syncing - use O_SYNC */
			if ((text_fd = open(text_path,O_SYNC|O_WRONLY|O_CREAT|O_TRUNC|O_BINARY,0666)) == -1)
#else           /* for better performace, we can omit O_SYNC here */
			if ((text_fd = open(text_path,O_WRONLY|O_CREAT|O_TRUNC|O_BINARY,0666)) == -1)
#endif
				{
				printf("Cannot open %s (errno = %d)\n",text_path,errno);
				exit(0);
				}
		for(l=1;l<=te.toc_blocks;++l)
			{
#if !VCRRES     /* does this apply to VCRRES also? */
			if (l == te.toc_blocks && te.toc_active_bytes == link_size)
				continue;       /* block not written to tape */
#endif
			if (mt_read(buf))
				{
				puts("Cannot read data record");
				exit(0);
				}
			if (file_type == CONTIGUOUS_FILE)
				{
				if (write(mt_fd,buf,512) != 512)
					{
					printf("Cannot write %s (errno = %d)\n",mt_path,errno);
					exit(0);
					}
				}
			else
				{
				if (l == te.toc_blocks)
					k = te.toc_active_bytes-link_size;
				else
					k = 512-link_size;
				if (file_type != TEXT_FILE)
					if (k > 0 && write(mt_fd,buf+sizeof(UINT16_12),k) != k)
						{
						printf("Cannot write %s (errno = %d)\n",mt_path,errno);
						exit(0);
						}
				if (file_type != BINARY_FILE)
					if ((kk = mt_convert_text_block((TEXT *)buf+sizeof(UINT16_12),k,l == te.toc_blocks)) == -1 && file_type == UNKNOWN_FILE)
						{
						file_type = BINARY_FILE;
						if (close(text_fd))
							{
							printf("Cannot close %s (errno = %d)\n",text_path,errno);
							exit(0);
							}
						}
					else if (kk > 0 && write(text_fd,buf+sizeof(UINT16_12),kk) != kk)
						{
						printf("Cannot write %s (errno = %d)\n",text_path,errno);
						exit(0);
						}
				}
			}
		if (file_type == UNKNOWN_FILE)
			{
			if (close(mt_fd))
				{
				printf("Cannot close %s (errno = %d)\n",mt_path,errno);
				exit(0);
				}
			if (REMOVE(mt_path))
				{
				printf("Cannot remove %s (errno = %d)\n",mt_path,errno);
				exit(0);
				}
			mt_fd = text_fd;
			}
		if (mt_msgs)
			if (file_type == CONTIGUOUS_FILE)
				puts(" (C)");
			else if (file_type == BINARY_FILE)
				puts(" (B)");
			else
				puts(" (T)");
		if (*(UINT32 *)&te.toc_date == 0)
			te.toc_date = todays_adate;
		if (te.toc_time == 0)
			te.toc_time = todays_atime;
#if __DOS__
/* should use struct tm as intermediate */
/* set time and date of file (from mt_) */
		ft.ft_tsec = (UTINY)(te.toc_time / 120 % 30);
		ft.ft_min = (UTINY)(te.toc_time / 3600 % 60);
		ft.ft_hour = (UTINY)(te.toc_time / 216000L);
		ft.ft_day = te.toc_date.ad_day;
		ft.ft_month = te.toc_date.ad_month;
		ft.ft_year = te.toc_date.ad_year - 80;
/*              printf("time=%lx sec=%d min=%d hour=%d day=%d month=%d year=%d\n",te.toc_time,ft.ft_tsec*2,ft.ft_min,ft.ft_hour,ft.ft_day,ft.ft_month,ft.ft_year);*/
		if (setftime(mt_fd,&ft))
			printf("Cannot set time/date for %s (errno = %d)\n",mt_path,errno);
#endif /* __DOS__ */
		if (close(mt_fd))
			{
			printf("Cannot close %s (errno = %d)\n",mt_path,errno);
			exit(0);
			}
		mt_fd = 0;      /* show file is closed (if aborted) */
		if (file_type == UNKNOWN_FILE)
			{
			if (RENAME(text_path,mt_path))
				{
				printf("Cannot rename %s to %s (errno = %d)\n",text_path,mt_path,errno);
				exit(0);
				}
			file_type = TEXT_FILE;
			}
#if __UNIX__
		adatetotm(&ftm, te.toc_date);
		atimetotm(&ftm, te.toc_time);
		if ((ftime = mktime(&ftm)) != (time_t)-1)
			{
/* printf("%s\n",asctime(&ftm)); */
			ut.actime = ftime;
			ut.modtime = ftime;
			if (utime(mt_path,&ut))
				printf("Cannot set time/date for %s (errno = %d)\n",mt_path,errno);
			}
#endif /* __UNIX__ */

                if (!mt_msgs && i > 0 && i / mt_max_atoc_entries * mt_max_atoc_entries == i)
			printf(".");
		}

/* restore is done - terminate */

	REMOVE(text_path);

	mt_term();

	puts("\nRestore completed.");

	return(0);
}
