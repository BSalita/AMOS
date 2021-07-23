
/* generate alpha-micro compatible file hashes */

/* Updated on 2008-Mar-08 to default to 8.3 (-s) but implement (-l) */

#include "swsubs.c"

/* define prototypes */
VOID print_hash __((SW_UINT32 hash));
INT hash_file __((INT fd, SW_UINT32 size, SW_UINT32 *hash));
UINT16 getword __((INT fd));
VOID read_buf __((INT fd));	/* clean up */
INT cnv_size __((INT fd, SW_UINT32 *size));
INT determineMapSwitch __((INT fd));

/* SW_UINT32 st1,st2; */   /* debugging tool */

/* GLOBAL - conversion options */
BOOL newline_cnv = 0;
BOOL newline_spec = 0;
BOOL remove_ctrlz = 0;
BOOL short_filename = 1; /* default to short filenames as requested by VCS */
/* GLOBAL - buffer/read operations */
#define BSIZ 8192	/* must be on word boundary */
UTINY buf[BSIZ], *p, *pe;
INT cnt;
BOOL ctrlz = 0;
BOOL text_file = 0;

int main(argc, argv)
INT argc;
TEXT *argv[];
{
	SW_UINT32 hash;
	SW_LONG size;
	INT fd;
	PHDR phdr;
	TEXT *p, *fn;
	TEXT wildcard_path[SW_PATH_MAX + 2];
	SW_UINT files_processed = 0;
#if __DOS_WIN__
	DIR *dir;
	struct dirent *dirent;
#endif
	struct stat statbuf;

	if (argc-- < 2)
	{
		puts("Copyright 2008 Softworks Limited. Version 2.0\n");
		printf("usage: %s {-m}{-r}{-s}{-z} filename1 filename2 ...\n", argv[0]);
		puts("\toptions: (default will detect and convert text files)");
		puts("\t\t-l	display long filenames");
		puts("\t\t-m	always convert LF to CR/LF");
		puts("\t\t-r	don't perform any conversions");
		puts("\t\t-s	display short filenames");
		puts("\t\t-z	hash without trailing ^Z");
		puts("\tcodes:");
		puts("\t\tT	file is a text file");
		puts("\t\t^Z	text file contains a trailing ^Z");
		puts("\t\tC	file required LF to CR/LF or ^Z conversion");
		exit(1);
	}

	/* print out all command line options - to be used by VERIFY */
	printf(";;* ");
	for (cnt = 0; cnt < argc + 1; cnt++)
	{
		p = argv[cnt];
		printf("%s ", p);
	}
	puts("");

	/* scan for program options */
	argv++;
#if __UNIX__
	while (**argv == '-')
#else
	while (**argv == '-' || **argv == '/')
#endif
	{
		p = *argv++;
		--argc;
		while (*++p)
			if (*p == 'l')
			{
				short_filename = 0;
			}
			else if (*p == 'm' && !newline_spec)
			{
				newline_cnv = 1;
				newline_spec = 1;
			}
			else if (*p == 'r' && !newline_spec)
			{
				newline_cnv = 0;
				newline_spec = 1;
			}
			else if (*p == 's')
			{
				short_filename = 1;
			}
			else if (*p == 'z')
			{
				remove_ctrlz = 1;
			}
			else
			{
				printf("?Invalid option.\n");
				exit(0);
			}
	}

	/*	scanf("%lx%lx",&st1,&st2);
		printf("from %lx to %lx\n",st1,st2);
		printf("newline_cnv = %d\n",newline_cnv);
		printf("newline_spec = %d\n",newline_spec);
		printf("short_filename = %d\n",short_filename);
	*/   /* debugging tool */

	/* obtain filenames */
	while (argc--)
	{
		if (aopen(*argv++, NULL, NULL, NULL, NULL, -1, SH_DENYNO, 0666, wildcard_path, sizeof(wildcard_path), NULL, NULL, NULL))
		{
			printf("?Invalid filename.\n");
			exit(0);
		}

#if __DOS_WIN__
		for (dir = opendir(wildcard_path); (dir != NULL && (dirent = readdir(dir)) != NULL) || *wildcard_path; *wildcard_path = 0)
#endif
		{
			if ((p = strrchr(wildcard_path, *PATH_SEPARATOR)) == NULL)
				fn = wildcard_path;
			else
				fn = ++p;

#if __DOS_WIN__
			/* copy in wildcarded filenames */
			if (dir != NULL && dirent != NULL)
				if (fn + strlen(dirent->d_name) > wildcard_path + sizeof(wildcard_path) - 1)
				{
					printf("?Cannot obtain file size.\n");
					continue;
				}
				else
					strcpy(fn, dirent->d_name);
#endif

			if (stat(fn, &statbuf))
			{
				printf("?Cannot stat file: %s\n", fn);
				continue;
			}

			if (statbuf.st_mode & S_IFDIR) /* skip directories */
				continue;

#if __WIN__
			if (short_filename)
			{
				TEXT sfn[MAX_PATH];
				if (GetShortPathName(fn, sfn, sizeof(sfn)) == 0)
				{
					printf("?Cannot GetShortPathName for: %s\n", fn);
					continue;
				}
				printf("%-16s  ", sfn);
			}
			else
#endif
				printf("%-16s  ", fn);

			if ((fd = open(fn, O_RDONLY | O_BINARY)) == -1)
			{
				printf("?Cannot open file.\n");
				continue;
			}

			if ((size = lseek(fd, 0L, SEEK_END)) == -1L)
			{
				printf("?Cannot obtain file size.\n");
				continue;
			}
			/* read phdr, if any */
			if (lseek(fd, 0L, SEEK_SET) == -1)
			{
				printf("?Cannot seek to start of file.\n");
				continue;
			}
			SET_UINT8_1(phdr.phdr_flags + 0, 0);
			SET_UINT8_1(phdr.phdr_flags + 1, 0);
			if (size >= sizeof phdr && read(fd, &phdr, sizeof phdr) != sizeof phdr)
			{
				printf("?Cannot read alpha-micro program header\n");
				continue;
			}
			/* puts(""); */
			/* printf("old size = %ld\n",size); */   /* debugging tool */

			printf("%-8ld", (size + 511) / 512);

			if (newline_cnv || !newline_spec)
				if (cnv_size(fd, (SW_UINT32 *)&size) == -1)
				{
					printf("?Error while pre-analyzing file!\n");
					continue;
				}
			/* printf("new size = %ld\n",size); */   /* debugging tool */

			if (hash_file(fd, size, &hash) == -1)
			{
				printf("?Error hashing file!\n");
				continue;
			}
			print_hash(hash);
			if (text_file)
				printf("  T");
			else
				printf("   ");
			if (ctrlz)
				printf(" ^Z");
			else
				printf("   ");
			if (newline_cnv)
				printf(" C");
			else
				printf("  ");
			/* check for PHDR */
			if (GET_UINT8_1(phdr.phdr_flags + 0) == 0xff && GET_UINT8_1(phdr.phdr_flags + 1) == 0xff)
			{
				printf("  %-20s", vcvt(GET_UINT32_3412(phdr.phdr_version)));
				determineMapSwitch(fd);
			}
			printf("\n");
			close(fd);
		} /* end of wildcards */
	} /* end of argc */
	return(0);
}

INT determineMapSwitch(fd)
int fd;
{
	BOOL endOfMap = 0;
	BOOL endOfFile = 0;
	UINT16 variable_table_length = 0;
	struct run_descriptor_tab
	{
		PHDR phdr;
		UINT16_12 run_version;
		UINT32_3412 codeLength;
		UINT32_3412 dataLength;
		UINT32_3412 mapLength;
		UINT32_3412 variableSize;
	} run_descriptor;

	if (lseek(fd, 0, SEEK_SET) == -1)
	{
		printf("?Cannot re-seek to start of file.\n");
		return(-1);
	}
	if (read(fd, &run_descriptor, sizeof run_descriptor) != sizeof run_descriptor)
	{
		printf("?Cannot read .RUN descriptor.\n");
		return(-1);
	}
	if (lseek(fd, GET_UINT32_3412(&run_descriptor.codeLength) + GET_UINT32_3412(&run_descriptor.dataLength), SEEK_CUR) == -1)
	{
		printf("?Cannot seek to variable table length.\n");
		return(-1);
	}
	if (read(fd, &variable_table_length, sizeof variable_table_length) != sizeof variable_table_length)
	{
		printf("?Cannot read variable table length descriptor.\n");
		return(-1);
	}
	if ((endOfMap = lseek(fd, GET_UINT32_3412(&run_descriptor.mapLength) - sizeof variable_table_length, SEEK_CUR)) == -1)
	{
		printf("?Cannot seek to end of map.\n");
		return(-1);
	}
	if ((endOfFile = lseek(fd, 0L, SEEK_END)) == -1)
	{
		printf("?Cannot seek to end of map.\n");
		return(-1);
	}
	if (GET_UINT16_12(&variable_table_length))
		if (endOfMap == endOfFile)
			printf("-b");
		else
			printf("-z");
	else
		printf("-c");
	return(0);
}

INT cnv_size(fd, siz)
INT fd;
SW_UINT32 *siz;
{
	/*	LOCAL UTINY buf[8192], *p = NULL, *pe = NULL; */
	/*	INT cnt; */
	UTINY c, prev_c = 0;
	SW_UINT32 org_size = *siz;

	ctrlz = 0;
	if (*siz == 0)
	{
		text_file = 0;
		return(0);
	}		/* patch - fix pe[-1] below;; bob 7/10/90 */
	text_file = 1;		/* assume a text file */
	p = pe = NULL;		/* reset buffer pointers */
	if (lseek(fd, 0L, SEEK_SET) == -1L)
		return(-1);		/* seek to begining of file */

	if (!newline_spec)
		newline_cnv = 1;	/* if not specified; assume conversion */

	while (1)
	{
		/* buffer reads */
		if (p == pe)
		{
			if ((cnt = read(fd, buf, sizeof buf)) == -1)
			{
				printf("?Cannot read file.\n");
				exit(1);
			}
			if (cnt == 0)
			{
				if (pe[-1] == 26)
				{
					ctrlz = 1;
					if (remove_ctrlz)
						*siz -= 1;
				}
				break; /* eof */
			}
			p = buf;
			pe = buf + cnt;
		}

		/* process single character */
		c = *p++;
		if (c == '\n' && prev_c != '\r')
			*siz += 1;
		prev_c = c;

		/* 1/2/92 tager - patch; recognize text files correctly */
		/* Note: ^L (Form Feed) = ASCII value 12. and ^Z = ASCII value 26. */
		if (c > 127 || (c < 32 && c != '\n' && c != '\r' && c != '\t' && c != 12 && c != 26))
		{
			text_file = 0;
			if (!newline_spec)
			{
				*siz = org_size;
				break;
			}
		}
	} /* end of while */
/* After scaning file & if SIZE did not change; turn off conversion */
	if (*siz == org_size)
		newline_cnv = 0;
	return(0);
}

VOID print_hash(hash)
SW_UINT32 hash;
{
	SW_UINT32 hi, lo;
	UINT16 hi1, hi2, lo1, lo2;

	hi = (hash >> 16) & 0x0FFFF;
	lo = (hash & 0x0FFFF);

	hi1 = hi & 0x01FF;
	hi2 = ((((hi & 0xFF00) >> 8) | ((hi & 0x00FF) << 8)) & 0x01FF);
	lo1 = lo & 0x01FF;
	lo2 = ((((lo & 0xFF00) >> 8) | ((lo & 0x00FF) << 8)) & 0x01FF);
	printf("  %03o-%03o-%03o-%03o", hi1, hi2, lo1, lo2);
}


INT hash_file(fd, siz, hash)		/* calculate & return hash total */
INT fd;
SW_UINT32 siz;
SW_UINT32 *hash;
{
	SW_UINT32 h;
#if __INTEL__
	UINT16 *lo = (UINT16 *)&h, *hi = (UINT16 *)&h + 1;
#else
	FAST UINT16 *hi = (UINT16 *)&h, *lo = (UINT16 *)&h + 1;
#endif
	size_t i;
	SW_UINT32 siz_cnt;

	h = 0;
	for (i = 0; i < 2; i++)
	{
		p = pe = NULL;		/* reset buffer pointers */
		if (lseek(fd, 0L, SEEK_SET) == -1L)
			return(-1);		/* seek to begining of file */
		for (siz_cnt = (siz + 1) >> 1; siz_cnt > 0; siz_cnt--)
		{
			/*
			UINT16 tmp;
			tmp = getword(fd);
			if (siz_cnt <= st1 && siz_cnt >= st2)
				printf("#0 %lx = %x, %x;; %x\n",siz_cnt,*lo,*hi,tmp);
			h += tmp;
			*/  /* debugging tool */
			if (remove_ctrlz && ctrlz && siz_cnt == 1 && siz & 1)
				h += (UTINY)getword(fd);
			else
				h += getword(fd);/* add word to hash total */

			if ((UINT16)siz_cnt > *hi)
				*lo += 1;

			*hi -= siz_cnt;		/* sub cnt from hi */

			if ((SW_UINT32)*hi + *lo > 0xffff)	/* adc hi+lo */
				*hi += (*lo)++;
			else
				*hi += *lo;	/* add lo to hi */

			if (h & 0x80000000L)	/* rotate 1 bit */
				h = (h << 1) + 1;
			else
				h <<= 1;
		}
		/* printf("after FOR    %x,  %x\n",*lo,*hi); */  /* debug code */
	}
	*hash = h;
	return(0);
}


/* read buffer */
VOID read_buf(fd)
INT fd;
{
	if ((cnt = read(fd, buf, sizeof buf)) == -1)
	{
		printf("?Cannot read file.\n");
		exit(1);
	}
	p = buf;
	pe = buf + cnt;
	if (cnt == 0)
		*pe = 0;	/* patch - null byte */
	if (cnt & 1)
		*pe++ = 0;
}


UINT16 getword(fd)
INT fd;
{
	UINT16 u16;
	/* conversion variables */
	LOCAL UTINY old_c2, prev_c;
	UTINY c1, c2;

	/* reset local variables - 1st read operation on new file */
	if (p == NULL)
	{
		old_c2 = 0;
		prev_c = 0;
	}

	/* temp; debugging info */
	/* LEAVE; CODE IN UNTIL WE ARE SATISFIED IT WORKS; FLAWLESSLY */
	if (p > pe)
	{
		printf("?Fatal - buffer pointer OVER-RAN end of buffer.\n");
		exit(0);
	}

	/* buffer read */
	if (p == pe)
		read_buf(fd);	/* read file into buffer */

	/* NO CONVERSION; SIMPLE CASE */
	if (!newline_cnv)
	{
		u16 = *p++;
		u16 |= *p++ << 8;
		return(u16);
	}

	/* CONVERSION; COMPLEX CASE */
	/* process buffer */
	if (old_c2)
	{
		/* last byte OLD_C2 was a LF; and the CR was already returned */
		c2 = *p++;
		if (c2 == '\n' && prev_c != '\r')
		{
			prev_c = c2;
			return((UINT16)('\n' | ('\r' << 8)));
		}
		else
		{
			old_c2 = 0;	/* reset LF flag */
			prev_c = c2;
			return((UINT16)('\n' | (c2 << 8)));
		}
	}
	else
	{
		/* form word */
		c1 = *p++;
		if (c1 == '\n' && prev_c != '\r')
		{
			/* simple case; LF found in 1st byte; so return CR/LF */
			prev_c = c1;
			return((UINT16)('\r' | ('\n' << 8)));
		}
		else
		{
			/* patch - check if a (read file) is needed */
			if (p == pe)
				read_buf(fd);	/* read file into buffer */

			c2 = *p++;
			prev_c = c2;
			if (c2 == '\n' && c1 != '\r')
			{
				old_c2 = c2;	/* set LF flag */
				return((UINT16)(c1 | ('\r' << 8)));
			}
			else
				return((UINT16)(c1 | (c2 << 8))); /* raw */
		}
	}
	/* should never drop down here */
}
/* END OF FILE */
