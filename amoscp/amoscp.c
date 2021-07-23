/*

Experimental utility to copy files to/from an AMOS filesystem compatible with DOS, Windows and *NIX.

to do:
	* fix up ignore_badblk_sys for /dev/hd03 and others?
	* test using ESDI (or other) drives (both PC and non-PC drives)
	* check out for other byte orders
	* test with amos32.mon
	* test for multiple mfd blocks
	* fill out driver_table
	* write "to amos" version and "dir"
*/

#include "swsubs.c"
#if __DOS__
#include "bios.h"
#endif

#pragma pack(1)

#define haswild(str) (strchr(str,'*') != NULL || strchr(str,'?') != NULL)

#define TEXT_FILE 0
#define BINARY_FILE 1
#define CONTIGUOUS_FILE 2

struct mfd_record
	{
	BYTE	mfd_ppn[2];
	WORD	mfd_ufd;
	WORD	mfd_passwd[2];
	};

struct ufd_record
	{
	WORD	ufd_fn[2];
	WORD	ufd_ext;
	WORD	ufd_blocks;
	WORD	ufd_active_bytes;
	WORD	ufd_rn;
	};

/* structure of table returned by Turbo C biosdisk cmd=8 call */
struct drive_parameters
	{
	UTINY	dp_sectors_per_track;	/* low order 6 bits only */
	UTINY	dp_number_of_cyl;	/* but steal two bits from above */
	UTINY	dp_drives_installed;
	UTINY	dp_heads;
	};

/* the master disk partition table is at cyl 0, head 0 sector 1 (boot record),
at offset 0x1be. There are 4 partitions of length 0x10 and are numbered
in descending order. */
struct master_disk_partition_table
	{
	UTINY	pt_boot;	/* 0x80 boot, 0x00 otherwise */
	UTINY	pt_head;
	UTINY	pt_sector;	/* 6 bits for sector, 2 bits for cyl */
	UTINY	pt_cyl; 	/* msb 2 bits come from above */
	UTINY	pt_os;		/* operating system code: */
				/* 0 - undefined */
				/* 1 - DOS with 12-bit FAT */
				/* 2 - XENIX */
				/* 3 - XENIX */
				/* 4 - DOS with 16-bit FAT */
				/* 5 - Extended DOS */
				/* 6 - Big DOS */
				/* 80 (0x50) - AMOS */
				/* 99 (0x63) - UNIX */
				/* 100 (0x64) - NOVELL */
				/* 117 (0x75) PC/IX */
				/* 219 (0xdb) CP/M */
				/* 255 (0xff) BBT */
	UTINY	pt_end_head;
	UTINY	pt_end_sector;
	UTINY	pt_end_cyl;
	UINT32	pt_starting_sector;
	UINT32	pt_sectors_in_partition;
	};

/* the master boot record is the 1st sector of the disk */
struct master_boot_record
	{
	UTINY	mboot_code[446];        /* master boot code */
	struct master_disk_partition_table mboot_partition_tables[4];
	UINT16	mboot_record_id;
	};

/* the DOS partition boot rec is the 1st sector of a formatted partition. */
struct dos_partition_boot_record
	{
	UTINY	boot_jmp[3];		/* near jmp to boot code */
	UTINY	boot_oem[8];		/* OEM name and version */
	UINT16	boot_bytes_per_sectors;
	UTINY	boot_sectors_per_cluster;
	UINT16	boot_reserved_sectors;
	UTINY	boot_number_of_fat_tables;
	UINT16	boot_number_of_dir_entries;
	UINT16	boot_number_of_logical_sectors;
	UTINY	boot_media_descriptor_byte;
	UINT16	boot_number_of_fat_sectors;
/* formatting information */
	UINT16	boot_sectors_per_track;
	UINT16	boot_number_of_heads;
	UINT16	boot_number_of_hidden_sectors;
/* boot code */
	UTINY	boot_code[480];
	UINT16	boot_record_id;         /* always 0xaa55 */
	};

/* AMOS badblk.sys structure */
struct badblk_sys
	{
	WORD	badblk_id;		/* always 0x8005 */
	TEXT	badblk_maker[10];	/* ascii text of formatter program */
	LWORD	badblk_hash;		/* sum of badblk_blks */
	LWORD	badblk_blks[1];		/* 1...n addresses of bad sectors */
					/* relative to beginning of partition */
	};

/* record 0 on some newer Alpha drives */
/* number of cylinders, heads and sectors may be a contrived number for SCSI */
struct record0
	{
	BYTE	record0_id[8];		/* must be 0x305, 0x304, 0x405, 3 */
	WORD	unknown1[2];
	WORD	record0_cyls;		/* number of cylinders */
	WORD	record0_heads;		/* number of heads */
	WORD	record0_sectors;	/* number of sectors */
	WORD	unknown2[3];
	WORD	record0_logical_drives;	/* number of logical drives */
	WORD	unknown3[3];
	WORD	record0_flag;		/* flag word - 0x24 extended disk? */
	WORD	unknown4[4];
	LWORD	record0_hash;		/* hash total of this record */
	};

#if __DOS__
INT amos_open_1 __((VOID));
#else
INT amos_open_1 __((TEXT *s));
#endif
INT amos_read __((UINT dsk, LONG rn, TEXT *buf));
INT convert_text_block __((TEXT *buf, INT len, BOOL last_block));
INT wildmatch __((TEXT *mask, TEXT *str));
INT copy_amos_file __((struct ufd_record *ufd, UINT dsk));
INT search_amos_file __((TEXT *path, TEXT *device, TEXT *ppn, TEXT *name,
	TEXT *ext, TEXT *dest));
TEXT *amos_open __((TEXT *s));
INT update_driver_info __((VOID));

LOCAL TEXT afn[512];
LOCAL TEXT nfn[512];
LOCAL TEXT cur_device[16];
LOCAL TEXT cur_ppn[7];
LOCAL TEXT cur_fn[7];
LOCAL TEXT cur_ext[5];
LOCAL TEXT *dsk_drive = "dsk";
LOCAL UINT number_of_logicals;
LOCAL ULONG blocks_per_logical;
LOCAL struct badblk_sys *badblk_sys;
LOCAL LONG *badblks;
LOCAL UINT nbadblks = 0;
LOCAL struct master_boot_record mbr;
LOCAL struct master_disk_partition_table *pt = NULL;
LOCAL BOOL ignore_badblk_sys = 0, got_drive_info = 0;
LOCAL BOOL force_text = 0, force_binary = 0, debug_flag = 0;
LOCAL LONG starting_sector = 0, sector_offset = 0;
#if __DOS__
LOCAL INT amos_drive, next_amos_drive = 0x80;
LOCAL LONG sectors_per_track,sectors_per_drive,sectors_per_cyl,last_sector;
#else
LOCAL INT dskfd = -1;
#endif

INT wildmatch(mask,str)
TEXT *mask;
TEXT *str;
{
INT c;

/*printf("mask=%s str=%s\n",mask,str);*/
	while((c = *mask++) != 0 && *str != 0)
		if (c == '*')
			while(*str && *str != *mask)
				str++;
		else if (c == '?')
			str++;
		else if (c != *str++)
			return(c - *--str);
	return(c - *str);
}

#if __DOS__
INT amos_open_1()
{
INT i;
struct drive_parameters dp;

	/* read disk parameters */
	if (amos_drive >= 0x80)
		{
		i = biosdisk(8,amos_drive,(INT)pt->pt_head,(INT)pt->pt_cyl,(INT)pt->pt_sector,1,&dp);
		if (i != 0 && i != 0x11)
			{
			errno = EINVAL;
			return(-1);
			}
		sectors_per_track = dp.dp_sectors_per_track & 0x3f;
		sectors_per_cyl = (dp.dp_heads+1)*sectors_per_track;
		sectors_per_drive = (dp.dp_number_of_cyl+((dp.dp_sectors_per_track & 0xc0) << 2))*sectors_per_cyl;
		}
	else
		{
		sectors_per_track = 9;
		sectors_per_cyl = 2*sectors_per_track;
		sectors_per_drive = 80*sectors_per_cyl;
		}
	if (mbr.mboot_record_id == 0xaa55)
		{
		starting_sector = pt->pt_starting_sector;
		last_sector = starting_sector+pt->pt_sectors_in_partition;
		}
	else
		{
		starting_sector = 0;
		last_sector = starting_sector+sectors_per_drive;
		}
	return(0);
}

TEXT *amos_open(s)
TEXT *s;
{
INT i;
LOCAL TEXT amos_device_name[60];

	blocks_per_logical = 65535L;
	number_of_logicals = 1;
	if (s != NULL)
		sscanf(s,"%x",&next_amos_drive);
	do
		{
		i = biosdisk(2,next_amos_drive,0,0,1,1,&mbr);
		if (i != 0 && i != 0x11)
			{
			errno = EINVAL;
			return(NULL);
			}
		/* test if DOS drive, if so scan partition table */
		if (mbr.mboot_record_id != 0xaa55)
				break;
		pt = mbr.mboot_partition_tables+4;
		for(i=0;i<4 && (--pt)->pt_os != 80;i++)
			;
		}
	while(i == 4 && s == NULL && next_amos_drive++);
	amos_drive = next_amos_drive++;
	if (i == 4)
		sprintf(amos_device_name,"0x%02x",amos_drive);
	else
		sprintf(amos_device_name,"0x%02x, partition %d",amos_drive,i+1);
	return(amos_open_1() ? NULL : amos_device_name);
}

#else
INT amos_open_1(s)
TEXT *s;
{
	printf("Checking %s for AMOS partition\n",s);
	return(dskfd = open(s,O_RDONLY|O_BINARY));
}

/* warning: never closes dskfd! */
TEXT *amos_open(s)
TEXT *s;
{
INT partition = 0;
LONG blocks_per_drive;
LOCAL TEXT amos_device_name[60];
LOCAL TEXT *amos_drive_names[] =
	{
	"/dev/hd00",
	"/dev/hd10",
	"/dev/dsk/1s0",
	"/dev/dsk/2s0",
	"/dev/dsk/3s0",
	"/dev/dsk/4s0",
	"/dev/dsk/5s0",
	"/dev/dsk/6s0",
	"/dev/dsk/7s0",
	NULL
	};
LOCAL TEXT **p = amos_drive_names;
LOCAL TEXT lr[512];

	blocks_per_logical = 65535L;
	number_of_logicals = 1;
	if (s == NULL)
		{
		do
			{
			s = amos_device_name;
			strcpy(s,*p++);
			amos_open_1(s);
			if (dskfd != -1)
				if (read(dskfd,&mbr,512) == 512)
					{
					if (mbr.mboot_record_id == 0xaa55)
						{
						pt = mbr.mboot_partition_tables+4;
/* note: only finds first AMOS partition on dos drive */
						for(partition=1;partition<=4 && (--pt)->pt_os != 80;partition++)
							;
						if (partition > 4)
							partition = 0;
						else
							{
							s[strlen(s)-1] = '0'+partition;
							amos_open_1(s);
							break;
							}
						}
					if (lseek(dskfd,0,SEEK_SET) == -1)
						return(NULL);
					break;
					}
			}
		while(*p != NULL);
		if (*p == NULL)
			return(NULL);
		}
	else if (amos_open_1(s) == -1)
		return(NULL);
/* check for label record in the first 10 records (only for Softworks) */
/* if mfd is not record 1, assume disk has label or mfd is first non-zero
record after record 0 */
	for(starting_sector=0;partition == 0 && starting_sector<=10;starting_sector++)
		if (read(dskfd,lr,512) == 512)
			if (starting_sector == 0 && memcmp(((struct record0 *)lr)->record0_id,"\x05\x03\x04\x03\x05\x04\x03\x00",8) == 0)
				{
				blocks_per_drive = GETW(((struct record0 *)lr)->record0_cyls)*GETW(((struct record0 *)lr)->record0_heads)*GETW(((struct record0 *)lr)->record0_sectors);
				number_of_logicals = GETW(((struct record0 *)lr)->record0_logical_drives);
				blocks_per_logical = blocks_per_drive/number_of_logicals;
				starting_sector = 1;
				got_drive_info = -1;
				break;
				}
			else if (memcmp(lr,"\xaa\xaa\x55\x55",4) == 0)
				break;
	if (partition == 0)
		{
		ignore_badblk_sys = -1;
		sprintf(amos_device_name,"%s",s);
		}
	else
		sprintf(amos_device_name,"%s, partition %d",s,partition);
	if (starting_sector <= 10)
		return(amos_device_name);
/* if disk has no label */
	starting_sector = 0;
	return(amos_device_name);
}
#endif /* DOS */

INT amos_read(dsk,rn,buf)
UINT dsk;
LONG rn;
TEXT *buf;
{
#if __DOS__
INT i;
LONG cyl,head,sectors;

	sectors = dsk*blocks_per_logical+rn;
	for(i=0;i<nbadblks;++i)
		if (sectors >= badblks[i])
			sectors += 1;
	sectors += starting_sector;
	if (sectors > last_sector)
		{
		errno = EINVAL;
		return(-1);
		}
	cyl = sectors/sectors_per_cyl;
	sectors -= cyl*sectors_per_cyl;
	head = sectors/sectors_per_track;
	sectors -= head*sectors_per_track;
	if (debug_flag)
		printf("amos_drive=%d head=%d cyl=%d sectors=%d\n",amos_drive,(INT)head,(INT)cyl,(INT)sectors);
	sectors += sector_offset;
	i = biosdisk(2,amos_drive,(INT)head,(INT)cyl,(INT)sectors+1,1,buf);
	if (i != 0 && i != 0x11)
		{
		errno = EINVAL;
		return(-1);
		}
#else
INT i;
LONG sectors;

	sectors = dsk*blocks_per_logical+rn;
	for(i=0;i<nbadblks;++i)
		if (sectors >= badblks[i])
			sectors += 1;

	sectors += starting_sector;

	if (lseek(dskfd,sectors*512,SEEK_SET) == -1)
		return(-1);

	if ((i = read(dskfd,buf,512)) == -1)
		return(-1);
	if (i != 512)
		{
		errno = EINVAL;
		return(-1);
		}
#endif

	return(0);
}

INT convert_text_block(buf,len,last_block)
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
				case 9:		/* tab */
					break;
				case 10:	/* line-feed */
					break;
				case 13:	/* carriage-return */
#if __UNIX__
					--q;
#endif
					break;
				case 26:	/* ^Z */
					if (last_block && p == pe)
						{
						--q;
						break;
						}
				default:
					if (!force_text)
						return(-1);
				}
		}
	return(diffptr(q,buf));
}

INT copy_amos_file(ufd,dsk)
struct ufd_record *ufd;
UINT dsk;
{
BOOL done = 0;
LONG rn;
UINT blocks;
INT active_bytes;
INT i,j;
INT nfd = -1;
TEXT *p,*q;
LOCAL TEXT buf[512];
INT file_type = force_binary ? BINARY_FILE : TEXT_FILE;

	if ((p = strrchr(nfn,*PATH_SEPARATOR)) != NULL)
		{
		*p = 0;
		q = strrchr(nfn,*PATH_SEPARATOR);
		*q = 0;
#if __DOS__
		if (mkdir(nfn) && errno != EACCES)
#else
		if (mkdir(nfn,0777) && errno != EEXIST)
#endif
			{
			printf("Cannot mkdir %s (errno=%d)\n",nfn,errno);
			exit(0);
			}
		*q = *PATH_SEPARATOR;
#if __DOS__
		if (mkdir(nfn) && errno != EACCES)
#else
		if (mkdir(nfn,0777) && errno != EEXIST)
#endif
			{
			printf("Cannot mkdir %s (errno=%d)\n",nfn,errno);
			exit(0);
			}
		*p = *PATH_SEPARATOR;
		}
	active_bytes = (INT)(INT16)GETW(ufd->ufd_active_bytes);
	if (active_bytes == -1)
		{
		file_type = CONTIGUOUS_FILE;
		if ((nfd = open(nfn,O_RDWR|O_BINARY|O_TRUNC|O_CREAT,0666)) == -1)
			{
			printf("Cannot open %s (errno=%d)\n",nfn,errno);
			goto oops;
			}
		rn = GETW(ufd->ufd_rn);
		blocks = GETW(ufd->ufd_blocks);
		for(i=0;i<blocks;i++,rn++)
			{
			if (amos_read(dsk,rn,buf))
				{
				printf("Cannot read %s (errno=%d)\n",afn,errno);
				goto oops;
				}
			if (write(nfd,buf,512) != 512)
				{
				printf("Cannot write %s (errno=%d)\n",nfn,errno);
				goto oops;
				}
			}
		}
	else do
		{
		if (nfd != -1)
			if (close(nfd))
				{
				printf("Cannot close %s (errno=%d)\n",nfn,errno);
				goto oops;
				}
		if ((nfd = open(nfn,O_WRONLY|O_BINARY|O_TRUNC|O_CREAT,0666)) == -1)
			{
			printf("Cannot open %s (errno=%d)\n",nfn,errno);
			goto oops;
			}
		rn = GETW(ufd->ufd_rn);
		blocks = GETW(ufd->ufd_blocks);
		for(i=0;i<blocks;i++)
			{
			if (amos_read(dsk,rn,buf))
				{
				printf("Cannot read %s (errno=%d)\n",afn,errno);
				goto oops;
				}
			rn = GETW(buf);
			j = 510;
			if (i == blocks-1)
				j = active_bytes-2;
			if (file_type == TEXT_FILE)
				if ((j = convert_text_block(buf+sizeof(WORD),j,i==blocks-1)) == -1)
					{
					file_type = BINARY_FILE;
					break;
					}
			if (j > 0 && write(nfd,buf+sizeof(WORD),j) != j)
				{
				printf("Cannot write %s (errno=%d)\n",nfn,errno);
				goto oops;
				}
			}
		} while(i != blocks);
	done = -1;
oops:
	if (nfd != -1)
		if (close(nfd))
			{
			printf("Cannot close %s (errno=%d)\n",nfn,errno);
			if (remove(nfn))
				{
				printf("Cannot remove %s (errno=%d)\n",nfn,errno);
				exit(0);
				}
			}
	if (!done)
		if (remove(nfn))
			{
			printf("Cannot remove %s (errno=%d)\n",nfn,errno);
			exit(0);
			}
	return(file_type);
}

INT search_amos_file(path,device,ppn,name,ext,dest)
TEXT *path;
TEXT *device;
TEXT *ppn;
TEXT *name;
TEXT *ext;
TEXT *dest;
{
INT file_type;
struct mfd_record *mfd;
struct ufd_record *ufd;
LONG rn;		/* AMOS record number */
TEXT *p;
UINT prog,proj;
UINT device_matches,ppn_matches,fn_matches;
BOOL device_wildcard,ppn_wildcard,fn_wildcard;
LOCAL TEXT mfd_buf[512];
LOCAL TEXT ufd_buf[512];
UINT dsk;		/* offset in bytes from beginning of disk */

	device_wildcard = haswild(device);
	device_matches = ppn_matches = fn_matches = 0;
	for(dsk=0;dsk<number_of_logicals&&(device_wildcard||device_matches==0);++dsk)
		{
		sprintf(cur_device,"%s%u",dsk_drive,dsk);
		cvtpathcase(cur_device,NULL,NULL);
		if (debug_flag)
			printf("device=%s cur_device=%s\n",device,cur_device);
		if (wildmatch(device,cur_device) == 0)
			{
			device_matches += 1;
			rn = 1;
			do
				{
				if (amos_read(dsk,rn,mfd_buf))
					{
					printf("Cannot read MFD (errno=%d)\n",errno);
					exit(0);
					}
				ppn_wildcard = haswild(ppn);
				for(mfd=(struct mfd_record *)mfd_buf;
				  mfd<(struct mfd_record *)mfd_buf+63
				  && (ppn_wildcard || ppn_matches == 0);
				  mfd++)
					{
					if (mfd->mfd_ppn[0] == 0 && mfd->mfd_ppn[1] == 0)
						break;
					sprintf(cur_ppn,"%03o%03o",mfd->mfd_ppn[1],mfd->mfd_ppn[0]);
					if (debug_flag)
						printf("ppn=%s cur_ppn=%s\n",ppn,cur_ppn);
					if (wildmatch(ppn,cur_ppn) == 0)
						{
						ppn_matches += 1;
						sscanf(cur_ppn,"%3o%3o",&prog,&proj);
						rn = GETW(mfd->mfd_ufd);
						if (rn == 0)
							continue;
						fn_wildcard = haswild(name) || haswild(ext);
						do
							{
							if (amos_read(dsk,rn,ufd_buf))
								{
								printf("Cannot read UFD (errno=%d)\n",errno);
								exit(0);
								}
							for(ufd=(struct ufd_record *)(ufd_buf+2);
							  ufd<(struct ufd_record *)(ufd_buf+512)
							  && (fn_wildcard || fn_matches == 0)
							  && GETW(ufd->ufd_fn[0]) != 0;ufd++)
								{
								if (GETW(ufd->ufd_fn[0]) == 0xffff)
									continue;
								p = cur_fn;
								p = rad50toa(p,GETW(ufd->ufd_fn[0]));
								p = rad50toa(p,GETW(ufd->ufd_fn[1]));
								*p = 0;
								cvtpathcase(NULL,cur_fn,NULL);
								if (debug_flag)
									printf("name=%s cur_fn=%s\n",name,cur_fn);
								if (wildmatch(name,cur_fn) == 0)
									{
									p = rad50toa(cur_ext,GETW(ufd->ufd_ext));
									*p = 0;
									cvtpathcase(NULL,NULL,cur_ext);
									if ((*ext == 0 && *name && name[strlen(name)-1]=='*') || wildmatch(ext,cur_ext) == 0)
										{
										fn_matches += 1;
										sprintf(afn,"%s:%s.%s[%o,%o]",cur_device,cur_fn,cur_ext,prog,proj);
#if __DOS__
										if (strcmp(cur_fn,"AUX") == 0
									   || strcmp(cur_fn,"CON") == 0
									   || strcmp(cur_fn,"LST") == 0
									   || strcmp(cur_fn,"NUL") == 0
									   || strcmp(cur_fn,"PRN") == 0)
											{
											strcat(cur_fn,"XXX");
											printf("Name conflict!!!!! New name is %s\n",name);
											}
#endif
										if (dest == NULL)
											{
											sprintf(nfn,"%s%s%s%s%s%s%s.%s",path,PATH_SEPARATOR,cur_device,PATH_SEPARATOR,cur_ppn,PATH_SEPARATOR,cur_fn,cur_ext);
											printf("%s to %s",afn,nfn);
											}
										else
											strcpy(nfn,dest);
										file_type = copy_amos_file(ufd,dsk);
										if (dest == NULL)
											if (file_type == TEXT_FILE)
												puts("(T)");
											else if (file_type == BINARY_FILE)
												puts("(B)");
											else
												puts("(C)");
										} /* if ext match */
									} /* if fn match */
								} /* ufd entry loop */
							} /* ufd block do */
						while((rn = GETW(ufd_buf)) != 0 && (fn_wildcard || fn_matches == 0));
						} /* if ppn match */
					} /* mfd entry loop */
				} /* mfd block loop */
				while((rn = GETW(mfd->mfd_ufd)) != 0 && (ppn_wildcard || ppn_matches == 0));
			} /* if device match */
		} /* device loop */
	if (!device_wildcard && !ppn_wildcard && !fn_wildcard && fn_matches == 0)
		printf("File not found - %s:%s.%s[%.3s,%.3s]\n",device,name,ext,ppn,ppn+3);
	return(fn_matches);
}

INT update_driver_info()
{
INT tmpfd;
LONG lo;
UINT i;
UINT32 u32;
LOCAL UTINY driver_table[256];
LOCAL TEXT dsk0[5];
LOCAL TEXT *badblk = "badblk";
LOCAL TEXT *sys = "sys";
LOCAL TEXT *amosl = "amosl";
LOCAL TEXT *amos32 = "amos32";
LOCAL TEXT *mon = "mon";
LOCAL TEXT buf[512];

	strcpy(dsk0,dsk_drive);
	strcat(dsk0,"0");
	if (!ignore_badblk_sys)
	{
	cvtpathcase(dsk0,badblk,sys);
	if (search_amos_file("",dsk0,"001002",badblk,sys,"badblk.sys"))
		{
		if ((tmpfd = open("badblk.sys",O_RDONLY|O_BINARY)) == -1)
			return(-1);
		if ((lo = lseek(tmpfd,0,SEEK_END)) == -1)
			return(-2);
		if (lo > UINT_MAX-16)
			return(-3);
		if (lseek(tmpfd,0,SEEK_SET) == -1)
			return(-4);
		if ((badblk_sys = malloc((UINT)lo)) == NULL)
			return(-5);
		if (read(tmpfd,badblk_sys,(UINT)lo) == -1)
			return(-6);
		if (close(tmpfd))
			return(-7);
		if (remove("badblk.sys"))
			return(-8);
		if (GETW(badblk_sys->badblk_id) != 0x8005)
			return(-9);
		nbadblks = (UINT)(lo/sizeof(LONG));
		badblks = (LONG *)badblk_sys->badblk_blks;
		for(i=0;i<nbadblks;++i)
			if ((badblks[i] = GETLW(badblk_sys->badblk_blks[i])) == 0)
				nbadblks = i;
		}
	else
		puts("WARNING: BADBLK.SYS not found - no bad blocks assumed");
	}

	if (!got_drive_info)
	{
	cvtpathcase(dsk0,amosl,mon);
	cvtpathcase(NULL,amos32,NULL);
	if (search_amos_file("",dsk0,"001004",amosl,mon,"amosl.mon")
		|| search_amos_file("",dsk0,"001004",amos32,mon,"amos32.mon"))
		{
		if ((tmpfd = open("amosl.mon",O_RDONLY|O_BINARY)) == -1)
			return(-10);
/* 0x47c is offset to driver in amosl.mon */
/* warning: assuming 0x47c is offset to driver for amos32.mon also */
		if (lseek(tmpfd,0x47c,SEEK_SET) == -1)
			return(-11);
		if (read(tmpfd,&u32,sizeof(u32)) == -1)
			return(-12);
		if (lseek(tmpfd,GETLW(&u32),SEEK_SET) == -1)
			return(-13);
		if (read(tmpfd,driver_table,sizeof(driver_table)) != sizeof(driver_table))
			return(-14);
		if (close(tmpfd))
			return(-15);
/*		if (remove("amosl.mon"))
			return(-16);*/
		blocks_per_logical = GETLW(driver_table+0x14);
		number_of_logicals = GETW(driver_table+0x48);
		}
	else
		{
		puts("\tAMOSL.MON not found - disk information unavailable");
		printf("\tEnter drive name [%s]: ",dsk_drive);
		gets(buf);
		if (*buf)
			strncpy(dsk_drive,buf,3);
		printf("\tEnter number of blocks per logical disk [%lu]: ",blocks_per_logical);
		gets(buf);
		if (*buf)
			blocks_per_logical = atol(buf);
		printf("\tEnter number of logical disks [%u]: ",number_of_logicals);
		gets(buf);
		if (*buf)
			number_of_logicals = atoi(buf);
		}
	}

	return(0);
}

INT main(argc,argv)
int argc;
char *argv[];
{
INT i,arg;
TEXT *p,*q;
UINT drive,prog,proj;
LOCAL TEXT device[10],path[512],name[FILE_NAME_MAX+1],ext[FILE_NAME_MAX+1],ppn[10];
LOCAL TEXT disk[4];
LOCAL TEXT cwd_ppn[7];
LOCAL TEXT cwd_device[4];
LOCAL TEXT cwd_path[512];
LOCAL TEXT *amos_device = NULL;
LOCAL TEXT buf[512];

	for(arg=1;arg<argc && *argv[arg] == SW;++arg)
		switch(*(argv[arg]+1))
			{
			case 'b':
				ignore_badblk_sys = -1;
				break;
			case 'd':
				amos_device = argv[++arg];
				break;
			case 'm':
				force_text = -1;
				break;
			case 'o':
				sector_offset = atoi(argv[++arg]);
				break;
			case 'r':
				force_binary = -1;
				break;
			case 'z':
				debug_flag = -1;
				break;
			default:
				printf("Undefined switch %s\n",argv[arg]);
				exit(0);
			}

	if (arg == argc)
		{
#if __DOS__
		puts("usage:");
		puts("\tamoscp optional-flags path\\device\\ppn\\filename");
		puts("\tamoscp optional-flags filename {if in AMOS style directory}");
		puts("optional-flags:");
		puts("\t/b {ignore badblk.sys}");
		puts("\t/d device-number {i.e. /d 0 {A:} /d 0x80 {C:}}");
		puts("\t/m {force removing of carriage-returns (0x0d)");
		puts("\t/o sector-offset {add sector-offset to sector number}");
		puts("\t/r {force no translation}");
		puts("\t/z {turn on debug messages}");
		puts("examples:");
		puts("\tamoscp dsk0\\001004\\amosl.ini");
		puts("\tamoscp /d d: amosl.ini {if in dsk0\\001004}");
		puts("\tamoscp /r dsk?\\001*\\ab?d*.* {? and * matching ok}");
#else
		puts("usage:");
		puts("\tamoscp optional-flags path/device/ppn/filename");
		puts("\tamoscp optional-flags filename {if in AMOS style directory}");
		puts("optional-flags:");
		puts("\t-b {ignore badblk.sys}");
		puts("\t-d device-node {i.e. -d /dev/dsk/4s0, -d /dev/rfd048ds9}");
		puts("\t-m {force removing of carriage-returns (0x0d)");
		puts("\t-o sector-offset {add sector-offset to sector number}");
		puts("\t-r {force no translation}");
		puts("\t-z {turn on debug messages}");
		puts("examples:");
		puts("\tamoscp dsk0/001004/amosl.ini");
		puts("\tamoscp amosl.ini {if in dsk0/001004}");
		puts("\tamoscp \"dsk?/001*/ab?d*\" {? and * matching ok}");
#endif
		puts("copy codes:\n\t(T) - sequential text file\n\t(B) - sequential binary file\n\t(C) - contiguous binary file");
		while((p = amos_open(amos_device)) != NULL)
			{
			if (amos_device == NULL)
				printf("AMOS partition found on drive %s at record %ld:\n",p,starting_sector);
			if ((i = update_driver_info()) != 0)
				printf("Cannot access AMOS driver information (error=%d)\n",i);
			else
				{
				printf("\tnumber of logical units: %u\n",number_of_logicals);
				printf("\tnumber of blocks per logical unit: %lu\n",blocks_per_logical);
				}
			if (amos_device != NULL)
				break;
			}
		exit(0);
		}

	if (amos_open(amos_device) == NULL)
		{
		printf("Cannot open %s (errno=%d)\n",amos_device,errno);
		exit(0);
		}

	if ((i = update_driver_info()) != 0)
		{
		printf("Cannot access AMOS driver information (error=%d)\n",i);
		exit(0);
		}

	if (getcwd(cwd_path,sizeof(cwd_path)) == NULL)
		{
		printf("Cannot getcwd (errno=%d)\n",errno);
		exit(0);
		}

	*cwd_ppn = 0;
	*cwd_device = 0;
	if (getppn(NULL,&prog,&proj) == 0)
		{
		p = cwd_path+strlen(cwd_path)-7;
		*p = 0;
		if (p > cwd_path
		&& (q = strrchr(cwd_path,*PATH_SEPARATOR)) != NULL
		&& sscanf(q+1,"%3s%u",disk,&drive) == 2)
			{
			strcpy(cwd_ppn,p+1);
			*p = 0;
			strcpy(cwd_device,q+1);
			*q = 0;
			}
		else
			*p = *PATH_SEPARATOR;
		}

	for(;arg<argc;++arg)
		{
		strcpy(buf,argv[arg]);
		if ((p = strrchr(buf,'.')) == NULL)
			p = ".";
		strcpy(ext,p+1);
		*p = 0;

		if ((p = strrchr(buf,*PATH_SEPARATOR)) == NULL)
			if (*buf)
				strcpy(name,p = buf);
			else
				{
				printf("Invalid fspec - %s\n",argv[arg]);
				continue;
				}
		else
			strcpy(name,p+1);
		*p = 0;
		strcpy(ppn,cwd_ppn);
		strcpy(device,cwd_device);
		strcpy(path,cwd_path);
		p = path+strlen(path)-1;
		if (*p == *PATH_SEPARATOR)
			*p = 0;
		if ((p = strrchr(buf,*PATH_SEPARATOR)) == NULL)
			{
			if (*buf)
				strcpy(ppn,buf);
			}
		else
			{
			strcpy(ppn,p+1);
			*p = 0;
			if ((p = strrchr(buf,*PATH_SEPARATOR)) == NULL)
				{
				if (*buf)
					strcpy(device,buf);
				}
			else
				{
				strcpy(device,p+1);
				*p = 0;
				if (*buf)
					strcpy(path,buf);
				}
			}
		cvtpathcase(device,name,ext);
		search_amos_file(path,device,ppn,name,ext,NULL);
		} /* argv[arg] loop */
	return(0);
}
