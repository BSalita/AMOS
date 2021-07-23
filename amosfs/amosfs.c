/* AMOS Filesystem Mounter, for use under SCO UNIX */
/* Copyright 1992 Softworks Limited, Chicago, IL USA */

/*
notes:
	Supports only 1 mountable device at a time
	In SCO, look in /etc/conf/cf.d/mdevice for the major number for
	Sdsk device (i.e. 45), then do a mknod /dev/amos c 45 0.
	Or perform 'ls -l /dev/hd01', then do a mknod /dev/amos b 1 15
	Warning: Is 4000 badblocks enough?
	"ls -a -i -l" = Shows all entries, with inode #, in long format!
	Function 'strcmp' simply calls 'kstrcmp':
	Undocumented kernel calls: kstrcat, kstrcmp, kstrcpy, kstrlen, kstrncmp, kstrncpy, kstrchr

to do:
	* Implement 'amosioctl' (Could be very useful!) on a FS ?
	*   a) accept/change drive parameters
	*   b) enable/disable fdebugf messages switch
	* SSD protection (Using rainbow driver) - look at 'NOSSD' define
	* Support multiple devices 
	* 1K byte block problem? Using 'amoscp' when AMOS FS mounted
	* test using ESDI (or other) drives (both PC and non-PC drives)
	* test with amos32.mon
	* test for multiple mfd blocks (May be difficult to do)
	* write routines to read & update the bitmap 
*/

/*	Copyright (C) The Santa Cruz Operation, 1990.		*/

/*	This Module contains Proprietary Information of		*/
/*	The Santa Cruz Operation and should be treated		*/
/*	as Confidential.					*/

#include	"sys/types.h"
#include	"sys/sema.h"
#include	"sys/param.h"
#include	"sys/sysmacros.h"
#include	"sys/sysinfo.h"
#include	"sys/immu.h"
#include	"sys/fs/s5dir.h"
#include	"sys/signal.h"
#include	"sys/fstyp.h"
#include	"sys/systm.h"
#include	"sys/inode.h"
#include	"sys/mount.h"
#include	"sys/file.h"
#include	"sys/nami.h"
#include	"sys/buf.h"
#include	"sys/dirent.h"
#include	"sys/region.h"
#include 	"sys/proc.h"
#include	"sys/pfdat.h"
#include	"sys/fcntl.h"
#include	"sys/open.h"
#include	"sys/stat.h"
#include	"sys/statfs.h"
#include	"sys/flock.h"
#include	"sys/errno.h"
#include	"sys/cmn_err.h"
#include	"sys/user.h"
#include	"sys/fs/amosidef.h"
#include	"sys/fs/amosfilsys.h"
#include	"sys/fs/amosinode.h"
#include	"sys/fs/amosdir.h"
#include	"sys/debug.h"
#include	"sys/conf.h"

#ifndef	TRUE
#define	TRUE	1
#endif
#ifndef	FALSE
#define	FALSE	0
#endif

#define NOSSD

#define fdebugf(a) (amos_fdebugf_enabled ? (delay(1), printf a) : 0)

#ifndef	AMOSNMOUNT
#define	AMOSNMOUNT	1	/* number of mountable AMOS filesystems	*/
#endif

#ifndef	AMOSNINODE
#define	AMOSNINODE	40	/* number of AMOS inode structures	*/
#endif

LOCAL struct amosfilsys	amosfilsys[AMOSNMOUNT];
LOCAL struct amosinode	amosinode[AMOSNINODE];
LOCAL int		amosnmount = AMOSNMOUNT;
LOCAL int		amosninode = AMOSNINODE;

LOCAL short amosfstyp;

/* These globals must be removed to be able to mount multiple AMOS filesys */
/* also LOCAL/statics in funcs must be dealt with */
LOCAL struct amosinode *amosifreelist;	/* Head of AMOS inode free list */
LOCAL UINT amos_number_of_logicals;
LOCAL LONG amos_size_of_logical_in_bytes;
LOCAL LONG amos_size_of_logical_in_records;
LOCAL LONG amos_size_of_disk_in_records;
LOCAL LONG amos_starting_sector;
LOCAL dev_t amos_dev;
LOCAL UINT amos_nbadblks;
LOCAL LONG amos_badblks[4000]; /* assumes 4000 is max badblks!!! */
LOCAL INT amos_badblk_sys_processed;
LOCAL INT amos_fs_mounted;
LOCAL INT amos_fdebugf_enabled;
LOCAL INT amos_amosl_mon_found;
LOCAL INT amos_rec0_found;

#ifndef NOSSD
/* Rainbow Driver fields - to implement SSD checking */
GLOBAL UTINY rbsc_version[];
GLOBAL UTINY rbsc_cmsg[];
#endif

/*
 * the functions in this file:
 *	amosallocmap()
 *	amosreadmap()
 *	amosfreemap()
 * implement support for paging UNIX binaries from the filesystem
 *
 */

/*
 * FS_ALLOCMAP(ip)
 */
/*
 * amosalloc()	- allocate and build the block address map
 *		  return 0 if there was a problem.
 *		- the block address map is used to speed up
 *		  amosreadmap() - to avoid the need to acess
 *		  indirect blocks on a standard UNIX filesystem.
 *		  on some filesytems where files are recorded
 *		  as contiguous extents it may be un-necessary.
 *		- if a block address map is allocated it should
 *		  be pointed to by a filed in the fs dependent
 *		  inode structure
 */
amosallocmap(ip)
    struct inode *ip;
{
    struct amosinode	*amosip;
    unsigned int	fsize;

    fdebugf(("amosallocmap: ip = %x, inum = %d, i_fstyp=%d\n",ip , ip->i_number, ip->i_fstyp));

    amosip = (struct amosinode *)ip->i_fsptr;
    ASSERT(amosip != NULL);

    if (amosip->amos_map != NULL)	/* already got a map table allocated	*/
	return 1;

    /*
     * calculate file size rounded up to a complete number
     * of pages
     */
    fsize = ((ip->i_size + (NBPP-1)) / NBPP) * NBPP;

#if PSEUDO_CODE
    allocate block address map
    fill in block numbers for each block of the file
#endif

    return 1;
}

/*
 * FS_READMAP(ip, offset, size, vaddr, segflg)
 */
/*
 * amosreadmap()	- read page from a file
 *		  return # of bytes read if no error occurs
 *		  return -1 - when read error occurs
 */

amosreadmap(ip, offset, size, vaddr, segflg)
    struct inode	*ip;
    off_t		offset;
    int			size;
    caddr_t		vaddr;
    int			segflg;
{
    struct buf		*bp;
    struct amosinode	*amosip;
    int			*bnptr;
    int			i;
    int			bsize;
    int			nbytes;
    int			on;
    int			n;
    dev_t		dev;

    fdebugf(("amosreadmap: ip = %x, inum = %d, i_fstyp=%d, offset = %x, size = %d, vaddr = %x, segflg = %d\n", ip, ip->i_number, ip->i_fstyp, offset, size, vaddr, segflg));

    amosip = (struct amosinode *)ip->i_fsptr;
    ASSERT(amosip != NULL);
    ASSERT(amosip->amos_map != NULL);

    if (offset > ip->i_size)
    {
	u.u_error = EINVAL;
	return -1;
    }

    if (offset + size > ip->i_size)
	size = ip->i_size - offset;

    dev	  = ip->i_dev;
    bsize = FSBSIZE(ip);  /* 3/7/92 patch was FsBSIZE((ip->i_mntdev)->m_bsize); */

    i = offset/bsize;
    bnptr = &amosip->amos_map[i];
    on = offset - (i*bsize);

    nbytes = 0;

    while (nbytes < size)
    {
	if (*bnptr == -1)
	    break;

	if (*bnptr != NULL)
	{
	    if ((size > bsize) && *(bnptr+1))
		bp = breada(dev, *bnptr, *(bnptr+1), bsize);
	    else
		bp = bread(dev, *bnptr, bsize);
	}
	else
	{
	    bp = geteblk();
	    clrbuf(bp);
	}
	bnptr++;

	bp->b_flags |=  B_AGE;
	if (bp->b_flags & B_ERROR)
	{
	    prdev( "page read error - possible data loss -", dev );
	    brelse(bp);
	    return -1;
	}

	n = bsize - on;
	if (n > size - nbytes)
	    n = size - nbytes;

	if (segflg != 1)
	{
	    if (copyout(bp->b_un.b_addr + on, vaddr, n))
	    {
		u.u_error = EFAULT;
		brelse(bp);
		fdebugf(("amosreadpg:  failure in copyout\n"));
		return -1;
	    }
	}
	else
	    bcopy(bp->b_un.b_addr+on, vaddr, n);

	brelse(bp);
	nbytes	-= n;
	offset	+= n;
	vaddr	+= n;
	on	= 0;
    }

    return nbytes;
}

/*
 * FS_FREEMAP(ip)
 */
/*
 * amosfreemap()	- free the block list attached to an inode.
 */
amosfreemap(ip)
    struct inode *ip;
{
    struct amosinode	*amosip;

    fdebugf(("amosfreemap: ip = %x, inum = %d, i_fstyp=%d\n", ip, ip->i_number, ip->i_fstyp));

    ASSERT( (ip->i_flag & ITEXT) == 0);
    ASSERT( ip->i_flag & ILOCK );

    amosip = (struct amosinode *)ip->i_fsptr;
    ASSERT(amosip != NULL);

    if (ip->i_ftype != IFREG || amosip->amos_map == NULL )
	return;

    flushpgch(ip);

#if PSEUDO_CODE
    free block address map
#endif
    amosip->amos_map = NULL;
}

/*
 * FS_IREAD(ip)
 */
/*
 * amosiread()	- read an AMOS "inode" (Root, PPN list, & UFD Entry)
 */

struct inode * amosiread(ip)
    struct inode	*ip;
{
	struct amosinode	*amosip;
	struct buf	*bp;	/* change to mfd_bp or ufd_bp */
	ushort		bsize;

	int mfd_blk,ufd_blk,old_ufd_blk;
	int i,dsk_num,dsk_offset;
	struct mfd_record *mfd;
	struct ufd_record *ufd;

    fdebugf(("amosiread: ip = %x, inum = %d, i_fstyp=%d\n", ip, ip->i_number, ip->i_fstyp));

    if ((amosip = (struct amosinode *)ip->i_fsptr) == NULL)
    {
	if ((amosip = amosifreelist) == NULL)
	{
	    cmn_err(CE_WARN, "AMOS inode table overflow\n");
	    u.u_error = ENFILE;
	    return NULL;
	}
	amosifreelist		= amosip->amos_next;
	amosip->amos_flags	= AMOSI_INUSE;
	ip->i_fsptr		= (int *)amosip;
	amosip->amos_map		= 0;
	/*
	 * NOTE: initialise the other fields of the fs dependent inode
	 *	 structure here
	 */
    }

/*
 * NOTE:	you may need to special-case an iget() for the
 *		inode of the root directory of your filesystem
 *		if the root directory is "special" in some way
 *		(eg DOS)
 */

/*
 * NOTE:	 now in some way you must use the device and
 *		 inode number ip->i_dev and ip->i_number to
 *		 fetch the relevant information about the file
 *		 and fill in the remainder of the fs dependent
 *		 inode structure and some parts of the fs
 *		 independent inode structure
 */
#if	PSEUDO_CODE
    /*
     * what you keep in the fs dependent inode structure will
     * depend on your filesystem ...
     */
    amosip->???		= ...
    amosip->???		= ...
    amosip->???		= ...

    brelse(bp);
#endif

    /*
     * you also need to fill in the following fields in the
     * fs independent inode structure ...
     */
	if (ip->i_number == AMOS_ROOTINO) 	
		{
		ip->i_nlink	= 1;		/* number of links */
		ip->i_uid	= 80;		/* user id */
		ip->i_gid	= 80;		/* group id */
		ip->i_size	= 0;		/* file size */
		ip->i_ftype	= IFDIR;  /* file type - IFDIR, IFREG etc .. */
		goto ret;
		}
	dsk_num = ip->i_number/amos_size_of_logical_in_bytes;
	dsk_offset = ip->i_number-dsk_num*amos_size_of_logical_in_bytes;
	if (dsk_offset == 0)
		{
		ip->i_nlink	= 1;		/* number of links */
		ip->i_uid	= 80;		/* user id */
		ip->i_gid	= 80;		/* group id */
		ip->i_size	= 0;		/* file size */
		ip->i_ftype	= IFDIR;  /* file type - IFDIR, IFREG etc .. */
		goto ret;
		}
	if (dsk_offset < 0x400)
		{
		ip->i_nlink	= 1;		/* number of links */
		ip->i_uid	= 80;		/* user id */
		ip->i_gid	= 80;		/* group id */
		ip->i_size	= 0;		/* file size */
		ip->i_ftype	= IFDIR;  /* file type - IFDIR, IFREG etc .. */
		goto ret;
		}
	else
		{
/* Read the UFD & retrieve the correct file size... */
		struct ufd_record *ufd;
		unsigned int n;
		ufd = amos_read_ufd(ip->i_number);
		ip->i_nlink	= 1;		/* number of links */
		ip->i_uid	= 80;		/* user id */
		ip->i_gid	= 80;		/* group id */
		if ((n = GETW(ufd->ufd_active_bytes)) == 0xffff)
			ip->i_size	= GETW(ufd->ufd_blocks)*512;
		else
			ip->i_size	= (GETW(ufd->ufd_blocks)-1)*510+(n == 0 ? n : n-2);
		ip->i_ftype	= IFREG;  /* file type - IFDIR, IFREG etc .. */
		}

ret:
    return ip;
} /* End of amosiread() */


/*
 * FS_IPUT(ip)
 */
/*
 * amosiput()
 */
amosiput(ip)
    struct inode	*ip;
{
    struct amosinode	*amosip;

    fdebugf(("amosiput: ip = %x, inum = %d, i_fstyp=%d\n",ip, ip->i_number, ip->i_fstyp));

    if ((amosip = (struct amosinode *)ip->i_fsptr) == NULL) 
	cmn_err(CE_PANIC, "NULL fs pointer in amosiput\n");
	/*
	 * if reference count is 0 put the in-core amos-inode structure
	 * back on the freelist
	 */
    if (ip->i_count == 0)
    {
	amosip->amos_flags	= AMOSI_FREE;
	amosip->amos_next		= amosifreelist;	
	amosifreelist		= amosip;
	ip->i_fsptr		= NULL;
	return;
    }

    ASSERT(ip->i_count == 1);

    /*
     * if link count is 0 free the storage associated with the file
     * (the last directory entry has already been removed)
     */
    if (ip->i_nlink <= 0)
    {
	amositrunc(ip);
	amosip->amos_flags	= AMOSI_FREE;
	amosip->amos_next		= amosifreelist;	
	amosifreelist		= amosip;
	ip->i_fsptr		= NULL;
	return;
    }

    if ((ip->i_flag & IUPD) && ip->i_ftype == IFREG && amosip->amos_map)
	amosfreemap(ip);

    /*
     * finally update the directory entry on disk
     */
    amosiupdat(ip, &time, &time);

    /*
     * if we are not cacheing inodes, return the
     * fs-dependent inode structure to the free list
     */
    if (fsinfo[amosfstyp].fs_flags & FS_NOICACHE)
    {
	amosip->amos_flags	= AMOSI_FREE;
	amosip->amos_next		= amosifreelist;	
	amosifreelist		= amosip;
	ip->i_fsptr		= NULL;
    }
}

/*
 * FS_IUPDAT(ip, atime, mtime)
 */
/*
 * amosiupdat()	- update the inode on disk
 */
amosiupdat(ip, atime, mtime)
    struct inode	*ip;
    time_t		*atime;
    time_t		*mtime;
{
    struct buf		*bp;
    struct amosinode	*amosip;
    struct amosdir	*dirp;
    ushort		bsize;
    daddr_t		bn;

    fdebugf(("amosiupdat: ip = %x, inum = %d, i_fstyp=%d, atime = %x, mtime = %x\n", ip, ip->i_number, ip->i_fstyp, atime, mtime));

    if (ip->i_nlink <= 0)
	return;

    if (rdonlyfs(ip->i_mntdev))
	return;

    bsize	= ip->i_mntdev->m_bsize;
    bn		= FsITOD(bsize, ip->i_number);
    bp		= bread(ip->i_dev, bn, bsize); /* patch - put in 'bn' */
    if (bp->b_flags & B_ERROR)
    {
	brelse(bp);
	return;
    }
    amosip	= (struct amosinode *)ip->i_fsptr;
    ASSERT(amosip != NULL);

/*
 * NOTE:	now actually update the inode on disk
 *		ip->i_flag indicates whether the accessed
 *		or modification times should be updated
 *		if ISYN is set, the update should be synchronous
 */
#if PSEUDO_CODE
   use information in inode structure to update inode on disk
#endif
    ip->i_flag &= ~(IACC|IUPD|ICHG|ISYN);
}

/*
 * FS_ITRUNC(ip)
 */
/*
 * amositrunc()	- free all the disk blocks associated with
 *		  the specified inode structure
 */
amositrunc(ip)
    struct inode *ip;
{
    struct amosinode *amosip;

    fdebugf(("amositrunc: ip = %x, inum = %d, i_fstyp=%d\n",ip, ip->i_number, ip->i_fstyp));

    ASSERT(ip->i_ftype == IFREG || ip->i_ftype == IFDIR);

    amosip	= (struct amosinode *)ip->i_fsptr;
    ASSERT(amosip != NULL);

    if (ip->i_ftype == IFREG && amosip->amos_map)
	amosfreemap(ip);

    ip->i_size	= 0;
    ip->i_flag	|= IUPD|ICHG|ISYN;
    amosiupdat(ip, &time, &time);
    amostruncate(ip);
}



/*
 * FS_INIT()
 */
/*
 * amosinit()	- initialise the fs-dependent inode freelist
 *		- initialise the filesystem type index
 */
amosinit()
{
    struct amosinode *amosip;

    printcfg("amosfs",0,0,-1,-1,"vers=0.0 %s",__DATE__);

    amosifreelist = &amosinode[0];
    for (amosip = amosifreelist; amosip < &amosinode[amosninode-1]; amosip++)
    {
	amosip->amos_flags	= AMOSI_FREE;
	amosip->amos_next	= amosip + 1;
    }
    /*
     * Ensure last element has NULL pointer to indicate end of
     * list and mark last element as free.
     */
    amosip->amos_next	= NULL;
    amosip->amos_flags	= AMOSI_FREE;

    for (amosfstyp = 0; amosfstyp < nfstyp; amosfstyp++)
    {
	if (fstypsw[amosfstyp].fs_init == amosinit)
	    break;
    }
    if (amosfstyp == 0 || amosfstyp == nfstyp)
	cmn_err(CE_PANIC, "amosinit: amosinit not found in fstypsw\n");
    /*
     * this is a kludge - we should do this in the configuration
     * files
     */
#define	AMOS_NOICACHE
#ifdef	AMOS_NOICACHE
    fsinfo[amosfstyp].fs_flags |= FS_NOICACHE;
#endif
}


/*
 * FS_NAMEI(ip, p, arg)
 */
/*
 * amosnamei()
 */
amosnamei(p, nmargp)
    struct nx		*p;
    struct argnamei	*nmargp;
{	
    struct inode	*dp;
    int			found;
    struct amosdir	dir;

    fdebugf(("amosnamei: nx = %x, argnamei = %x, bufp=%s %s\n", p, nmargp, p->bufp, p->comp));
    if (nmargp != NULL)
	fdebugf(("cmd=%d ftype=%x\n", nmargp->cmd, nmargp->ftype));

    dp = p->dp;
    fdebugf(("inum=%d ftype=%x fstyp=%d\n",dp->i_number,dp->i_ftype,dp->i_fstyp));

    /*
     * handle null pathname component
     */
    if (p->comp[0] == '\0')
    {
	if (nmargp && (nmargp->cmd == NI_RMDIR || nmargp->cmd == NI_DEL))
	{
	    u.u_error = EBUSY;
	    goto fail;
	}
	p->ino = dp->i_number;
	if (p->ino == AMOS_ROOTINO)
	    p->flags |= NX_ISROOT;
	return NI_PASS;
    }


    /*
     * handle ".." from root directory of filesystem
     */
    if (dp->i_number  == AMOS_ROOTINO && p->comp[0] == '.'
	&& p->comp[1] == '.' && p->comp[2] == '\0')
    {
	p->ino	= dp->i_number;
	p->dp	= dp;
	p->flags |= NX_ISROOT;
	return NI_PASS;
    }

    found = amosfindentry(p, &dir);
    if (u.u_error)
	goto fail;

    if (nmargp == 0)
    {
	if (found)
	{
	    if (p->ino == AMOS_ROOTINO)
		p->flags |= NX_ISROOT;
	    return NI_PASS;
	}
	else
	{
	    u.u_error = ENOENT;
	    goto fail;
	}
    }

    u.u_count	= sizeof(struct amosdir);
    u.u_base	= (caddr_t)&dir;
    u.u_fmode	= FWRITE;

    switch (nmargp->cmd)
    {
	case NI_LINK:		/* link not supported for AMOS	*/
	    u.u_error = EMLINK;
	    break;

	case NI_MKNOD:		/* mknod not supported for AMOS	*/
	    u.u_error = EINVAL;
	    break;

	case NI_XCREAT:		/* exclusive create on a file	*/
	    if (found)
	    {
		u.u_error = EEXIST;
		break;
	    }

	case NI_CREAT:		/* create a new file		*/
	    if (found)
	    {
		struct mount	*mp;

		mp = dp->i_mntdev;
		iput(dp);
		if ((p->dp = iget(mp, p->ino)) == NULL)
		    return NI_FAIL;
		nmargp->rcode = FSN_FOUND;
	    }
	    else
	    {
		if ((p->dp = amoscreat(dp, &dir, nmargp->mode, nmargp->ftype))
			== NULL)
		    return NI_FAIL;
		nmargp->rcode = FSN_NOTFOUND;
	    }
	    return NI_DONE;

	case NI_MKDIR:		/* make a directory		*/
	    if (found)
	    {
		u.u_error = EEXIST;
		break;
	    }
	    return amosmkdir(dp, &dir, nmargp->mode) ? NI_NULL : NI_FAIL;
	
	case NI_RMDIR:		/* remove a directory		*/
	    if (! found)
	    {
		u.u_error = ENOENT;
		break;
	    }
	    return amosrmdir(dp, &dir, p->ino) ? NI_NULL : NI_FAIL;

	case NI_DEL:		/* delete the entry		*/
	    if (! found)
	    {
		u.u_error = ENOENT;
		break;
	    }
	    return amosdelete(dp, &dir, p->ino) ? NI_NULL : NI_FAIL;

	default:		/* should never happen		*/
	    u.u_error = EINVAL;
	    break;
    }
fail:	
    fdebugf(("amosnamei: failed, u.u_error = %d\n", u.u_error));
    iput(dp);
    return NI_FAIL;
}

LOCAL long amos_ntoi(ino, name)
long ino;
char *name;
{
	struct amos_dir *ad;
	struct dirent *de;

	ad = amos_iopendir(ino);
	while ( (de = amos_readdir(ad)) != NULL )
		{
		fdebugf(("ntoi: <%s> <%s>, result = %d\n",de->d_name, name, strcmp(de->d_name, name)));
		if ( strcmp(de->d_name, name) == 0 )
			break;
		}	
	amos_closedir(ad);
fdebugf(("ntoi: de=%x\n",de));
	return(de == NULL ? -1 : de->d_ino);
}

/*
 * amosfindentry()	- attempt to find a match for the AMOS filename
 *			  pointed to by p->comp in the directory
 *			  whose inode is p->dp
 *			- if the entry is found:
 *				u.u_offset points to the entry
 *				p->ino is set to the inode # of the entry
 *				TRUE is returned
 *			- if the entry is not found:
 *				u.u_offset points to the first available slot
 *				p->ino is set to 0
 *				FALSE is returned
 */
LOCAL amosfindentry(p, dirp)
    struct nx		*p;
    struct amosdir	*dirp;
{
	struct inode *ip;
	struct dirent *de;
	struct amos_dir *ad;

	ip = p->dp;
	if ((p->ino = amos_ntoi(ip->i_number, p->comp)) == -1)
		return((int)p->ino = 0);
	return(-1);

}

LOCAL struct amos_dir *amos_iopendir(i_number)
LONG i_number;
{
INT dsk_offset;
static struct amos_dir need_to_malloc_ad;	 /* need to malloc() */
struct amos_dir *ad = &need_to_malloc_ad;

fdebugf(("amos_iopendir: i_number=%d ad=%x\n",i_number,ad));
	bzero((caddr_t)ad,sizeof(struct amos_dir));
	ad->ad_dev = amos_dev;
	ad->ad_number_of_logicals = amos_number_of_logicals;
	ad->ad_size_of_logical = amos_size_of_logical_in_bytes;
	ad->ad_ino = i_number;
	ad->ad_parent_ino = AMOS_ROOTINO;
	if (i_number == AMOS_ROOTINO)
		goto found;	/* traverse disks */
	ad->ad_logical = i_number/ad->ad_size_of_logical;
	dsk_offset = i_number-ad->ad_logical*ad->ad_size_of_logical;
	if (amos_read(ad->ad_dev,ad->ad_logical*ad->ad_size_of_logical/512+1,ad->ad_mfd_buf))
		goto not_found;
	ad->ad_mfd = (struct mfd_record *)(ad->ad_mfd_buf);
	if (dsk_offset == 0)
		{
		fdebugf(("traversing a mfd\n"));
		goto found;	/* traverse a mfd */
		}
	if (dsk_offset < 0x400)
		{
		/* traverse a ufd */
		ad->ad_parent_ino = ad->ad_logical*ad->ad_size_of_logical;
		ad->ad_mfd = (struct mfd_record *)(ad->ad_mfd_buf+dsk_offset-0x200);
		ad->ad_ufd_blk = GETW(ad->ad_mfd->mfd_ufd);
fdebugf(("amos_iopendir: ppn=%d %d ufd_blk=%x\n",ad->ad_mfd->mfd_ppn[0],ad->ad_mfd->mfd_ppn[1],ad->ad_ufd_blk));
		if (ad->ad_ufd_blk != 0)
	     		{
			if (amos_read(ad->ad_dev,ad->ad_logical*ad->ad_size_of_logical/512+ad->ad_ufd_blk,ad->ad_ufd_buf))
				goto not_found;
			}
		else 
			;	/* MFD entry - has no UFD entries */
		ad->ad_ufd = (struct ufd_record *)(ad->ad_ufd_buf+sizeof(WORD));
		fdebugf(("traversing a ufd\n"));
		goto found;
		}
	u.u_error = ENOTDIR;	 /* not a directory */
not_found:
	fdebugf(("amos_iopendir: failed, u.u_error = %d\n",u.u_error));
	return(NULL);
found:
	fdebugf(("amos_iopendir: found, u.u_error = %d, ad = %x\n",u.u_error,ad));
	return(ad);
}

LOCAL struct dirent *amos_readdir(ad)
struct amos_dir *ad;
{
TEXT *p;

/* increment entry */
fdebugf(("amos_readdir: ad=%x dev=%d ncall=%d\n",ad,ad->ad_dev, ad->ad_ncall));
	if (ad->ad_ncall == 0)
		{
		strcpy(ad->ad_dirent.d_name,".");
		ad->ad_dirent.d_off = 0;
		ad->ad_dirent.d_ino = ad->ad_ino;
		goto found;
		}
	if (ad->ad_ncall == 1)
		{
		strcpy(ad->ad_dirent.d_name,"..");
		ad->ad_dirent.d_off = 0;
		ad->ad_dirent.d_ino = ad->ad_parent_ino;
		goto found;
		}
	if (ad->ad_mfd == NULL) /* traverse dsk devices */
		{
fdebugf(("amos_readdir: logical=%d\n",ad->ad_logical));
		if (ad->ad_logical >= ad->ad_number_of_logicals)
			goto not_found; 
/* need sprintf here; for the %d option - using u16tod */
		p = ad->ad_dirent.d_name;
		strcpy(p,"dsk");
		p = u16tod(p+3,ad->ad_logical);
		*p = 0;
		ad->ad_dirent.d_off = 0;
		ad->ad_dirent.d_ino = ad->ad_logical*ad->ad_size_of_logical;
		ad->ad_logical++;
		goto found;
		}
	if (ad->ad_ufd == NULL) /* traverse ppns */
		{
fdebugf(("amos_readdir: mfd ppn=%d %d\n",ad->ad_mfd->mfd_ppn[0],ad->ad_mfd->mfd_ppn[1]));
/* assumes [0,0] is end of mfd, even in chained mfds */
		if (ad->ad_mfd >= (struct mfd_record *)ad->ad_mfd_buf+63)
			goto not_found;
		if (ad->ad_mfd->mfd_ppn[0] == 0 && ad->ad_mfd->mfd_ppn[1] == 0)
			goto not_found;
/* need sfprintf here; for the %o option - using u16too */
		p = ad->ad_dirent.d_name;
		p = u16too(p,ad->ad_mfd->mfd_ppn[1]);
		p = u16too(p,ad->ad_mfd->mfd_ppn[0]);
		*p = 0;
		ad->ad_dirent.d_off = (int)ad->ad_mfd-(int)ad->ad_mfd_buf;
		ad->ad_dirent.d_ino = 0x200+ad->ad_logical*ad->ad_size_of_logical+ad->ad_dirent.d_off;
		ad->ad_mfd++;
		goto found;
		}
	while(1)
		{
		if (ad->ad_ufd_blk == 0)
			goto not_found;		/* MFD entry - Had no UFD blk */
		if (ad->ad_ufd >= (struct ufd_record *)(ad->ad_ufd_buf+512))
			{
			if ((ad->ad_ufd_blk = GETW(ad->ad_ufd_buf)) == 0)
				goto not_found;
			if (amos_read(ad->ad_dev,ad->ad_logical*ad->ad_size_of_logical/512+ad->ad_ufd_blk,ad->ad_ufd_buf))
				goto not_found;
			ad->ad_ufd = (struct ufd_record *)(ad->ad_ufd_buf+2);
			}
		for(;ad->ad_ufd<(struct ufd_record *)(ad->ad_ufd_buf+512);ad->ad_ufd++)
			{
			if (GETW(ad->ad_ufd->ufd_fn[0]) == 0xffff)
				continue;
/* assumes a fn==0 signals end current UFD block */
			if (GETW(ad->ad_ufd->ufd_fn[0]) == 0)
				{
				ad->ad_ufd = (struct ufd_record *)(ad->ad_ufd_buf+512);
				break;
				}
			p = ad->ad_dirent.d_name;
			p = rad50toa(p,GETW(ad->ad_ufd->ufd_fn[0]));
			p = rad50toa(p,GETW(ad->ad_ufd->ufd_fn[1]));
			*p++ = '.';
			p = rad50toa(p,GETW(ad->ad_ufd->ufd_ext));
			*p = 0;
			strlwr(ad->ad_dirent.d_name);
			ad->ad_dirent.d_off = (int)ad->ad_ufd-(int)ad->ad_ufd_buf;
			ad->ad_dirent.d_ino = ad->ad_ufd_blk*512+ad->ad_logical*ad->ad_size_of_logical+ad->ad_dirent.d_off;
			ad->ad_ufd++;
			goto found;
			}
		}

not_found:
fdebugf(("amos_readdir: not found\n"));
	return(NULL);
found:
	ad->ad_ncall += 1;
	ad->ad_dirent.d_reclen = (sizeof(struct dirent)+strlen(ad->ad_dirent.d_name)+3) & ~3;
fdebugf(("amos_readdir: name=%s off=%d ino=%x reclen=%d ncall=%d\n",ad->ad_dirent.d_name,ad->ad_dirent.d_off,ad->ad_dirent.d_ino,ad->ad_dirent.d_reclen,ad->ad_ncall));
	return(&ad->ad_dirent);
}

LOCAL VOID amos_closedir(ad)
struct amos_dir *ad;
{
fdebugf(("amos_closedir: ad=%x\n",ad));
	/* free(ad); */
}

/*
 * amoscreat()
 */
LOCAL struct inode *amoscreat(dp, dirp, mode, ftype)
    struct inode	*dp;
    struct amosdir	*dirp;
    long		mode;
    ushort		ftype;
{
    daddr_t		bn;
    struct buf		*bp;
    long		ino;
    struct inode	*dip;
    ushort		bsize;
		
    if (amosaccess(dp, IWRITE))
	goto fail;

    if (ftype != 0 && ftype != IFREG)
    {
	u.u_error = EINVAL;
	goto fail;
    }
#if PSEUDO_CODE

    set up new directory entry

#endif

    /*
     * get an inode pointer for the new file
     */
    if ((dip = iget(dp->i_mntdev, ino)) == NULL)
	goto fail;

    iput(dp);
    if (u.u_error)
    {
	iput(dip);
	dip = NULL;
    }
    return dip;

fail:
    iput(dp);
    return NULL;
}


/*
 * amosmkdir()
 */
LOCAL amosmkdir(dp, dirp, mode)
    struct inode	*dp;
    struct amosdir	*dirp;
    long		mode;
{
    daddr_t		bn;
    struct buf		*bp;
    long		ino;
    struct inode	*dip;

#if PSEUDO_CODE
    make directory entry in parent direcory
#endif

    /*
     * get an inode pointer for the new directory
     */
    if ((dip = iget(dp->i_mntdev, ino)) == NULL)
	goto fail;
    /*
     * set up the new directory
     */
#if PSEUDO_CODE
    initialise the new directory
#endif

    dp->i_flag |= ICHG|ISYN;
    iput(dp);
    iput(dip);
    return TRUE;

fail:
    iput(dp);
    return FALSE;
}


/*
 * amosrmdir()
 */
LOCAL amosrmdir(dp, dirp, ino)
    struct inode	*dp;
    struct amosdir	*dirp;
    long		ino;
{
    struct amosdir	*d;
    ushort		n;
    daddr_t		bn;
    struct buf 		*bp;
    struct inode	*dip;
    ushort		bsize;

    if (amosaccess(dp, IWRITE))
	goto fail;
/*
 * NOTE:	should check for attempts to remove
 *		current directory or parent and
 *		reject them
 */

    if ((dip = iget(dp->i_mntdev, ino)) == NULL)
	goto fail;

    if (dip->i_dev != dp->i_dev)
    {
	u.u_error = EBUSY;
	goto bad;
    }
    if (dip->i_ftype != IFDIR)
    {
	u.u_error = ENOTDIR;
	goto bad;
    }
    if (dip == u.u_cdir)
    {
	u.u_error = EINVAL;
	goto bad;
    }
/*
 * NOTE:	should check that the directory is empty
 */

 #if PSEUDO_CODE
     delete the directory entry
#endif
    iput(dp);
    dip->i_nlink	= 0;
    dip->i_flag		|= ICHG;
    iput(dip);
    return TRUE;

bad:
    iput(dip);
fail:
    iput(dp);
    return FALSE;
}


/*
 * amosdelete()
 */
LOCAL amosdelete(dp, dirp, ino)
    struct inode	*dp;
    struct amosdir	*dirp;
    long		ino;
{
    struct inode	*dip;
    struct amosdir	*d;
    struct buf 		*bp;
    ushort		bsize;

    if (amosaccess(dp, IWRITE))
	goto fail;

    if ((dip = iget(dp->i_mntdev, ino)) == NULL)
	goto fail;

    if (dip->i_dev != dp->i_dev)
    {
	u.u_error = EBUSY;
	goto delfail;
    }

    if (dip->i_ftype == IFDIR && !suser()) 
	goto delfail;

    if (dip->i_flag & ITEXT)	/* try to free busy text */
	xrele(dip);
    if (dip->i_flag & ITEXT && dip->i_nlink == 1)
    {
	u.u_error = ETXTBSY;
	goto delfail;
    }
#if PSEUDO_CODE
    delete the directory entry for the file
#endif

    dip->i_nlink--;
    dip->i_flag  |= ICHG;

delfail:
	iput(dip);

fail:
	iput(dp);
	return (u.u_error == 0) ? TRUE : FALSE;
}


/*
 * FS_SETATTR(ip, argp)
 */
/*
 * amossetattr()
 */
amossetattr(ip, nmargp)
    struct inode	*ip;
    struct argnamei	*nmargp;
{
    struct amosinode	*amosip;
    struct amosfilsys	*fp;

    fdebugf(("amossetattr: ip = %x, inum = %d, i_fstyp=%d, nmargp = %x\n", ip, ip->i_number, ip->i_fstyp, nmargp));

    amosip = (struct amosinode *)ip->i_fsptr;
    ASSERT(amosip != NULL);

    fp = (struct amosfilsys *)ip->i_mntdev->m_bufp;
    ASSERT( NULL != fp );

    if (rdonlyfs(ip->i_mntdev))
    {
	u.u_error = EROFS;
	return 0;
    }

    switch (nmargp->cmd)
    {
	case NI_CHMOD:
#if PSEUDO_CODE
	    file_mode = nmargp->mode & 0777;
#endif
	    return 1;

	case NI_CHOWN:
	    ip->i_uid = nmargp->uid;
	    ip->i_gid = nmargp->gid;
	    ip->i_flag |= ICHG;
	    return 1;

	}
	return 0;
}

/*
 * FS_NOTIFY(ip, argp)
 */
long amosnotify(ip, noargp)
    struct inode	*ip;
    struct argnotify	*noargp;
{
    ASSERT(noargp != NULL);
    fdebugf(("amosnotify: ip = %x, inum = %d, i_fstyp=%d, noargp = %x\n",ip, ip->i_number, ip->i_fstyp, noargp));

    switch (noargp->cmd)
    {
	case NO_SEEK:
	    return noargp->data1;
    }
    return 0L;
}

LOCAL struct ufd_record *amos_read_ufd(i_number)
long i_number;
{
unsigned char ufd_buf[512];
static struct ufd_record ufd;

fdebugf(("amos_read_ufd: i_number=%d\n",i_number));
	if (amos_read(amos_dev, i_number/512, ufd_buf))
		return(NULL);
	ufd = *(struct ufd_record *)(ufd_buf+i_number%512);
fdebugf(("amos_read_ufd: blocks=%d active_bytes=%d rn=%x\n",GETW(ufd.ufd_blocks),GETW(ufd.ufd_active_bytes),GETW(ufd.ufd_rn)));
	return(&ufd);
}

/*
 * The routines in this file implement the fs_readi() and fs_writei()
 * fs dependent functions. In fact these routines are essentially
 * generic for byte stream files with normal UNIX semantics. No changes
 * should be necessary here since all of the real filesystem dependencies
 * are hidden in the fs dependent bmap() routine.
 */

/*
 * FS_READI(ip)
 */
/*
 * amosreadi()	- read the file corresponding to
 *		  the inode pointed at by the argument
 *		  the actual read arguments are found
 *		  in the variables:
 *			u_base		core address for destination
 *			u_offset	byte offset in file
 *			u_count		number of bytes to read
 *			u_segflg	read to kernel/user/user I
 */
amosreadi(ip)
    struct inode	*ip;
{
    static unsigned		on, n;
    static daddr_t		first_bn, bn;
struct ufd_record *ufd;
static unsigned int blks, active_bytes;
static long size, logical_offset, lrn, prev_i_number = 0;
unsigned char buf[512];

    fdebugf(("amosreadi: ip = %x, inum = %d, i_fstyp=%d, offset=%d count=%d pbsize=%d pboff=%d pbdev=%d bsize=%d\n",ip, ip->i_number, ip->i_fstyp, u.u_offset, u.u_count, u.u_pbsize, u.u_pboff, u.u_pbdev, u.u_bsize));

    /*
     * assume that we want normal System V file locking semantics
     */
    if (ip->i_ftype == IFREG && !amosaccess(ip, IMNDLCK))
    {
	s5chklock(ip, FREAD);	/* standard S5 locking code	*/
	if (u.u_error)
	    return;
    }

if (ip->i_number == AMOS_ROOTINO || ip->i_number % amos_size_of_logical_in_bytes < 0x400)
	return;

/* need to save final offset, size, n and on in ip structure!!!! */

if (u.u_offset < 0)
	{
	u.u_error = EINVAL;
	goto ret;
	}

if (ip->i_number == prev_i_number)
	{
	if (u.u_offset > size)
		{
		u.u_error = ESPIPE;
		goto ret;
		}
	if (active_bytes == 0xffff)
		bn = first_bn+(lrn = u.u_offset/512);
	else
		{
		if (u.u_offset/510 < lrn)
			{
			bn = first_bn;
			lrn = 0;
			}
		while(lrn<u.u_offset/510)
			{
			if (amos_read(amos_dev, bn+logical_offset, buf))
				goto ret;
			lrn += 1;
			if ((bn = GETW(buf)) == 0 || lrn > blks)
				{
				u.u_error = ESPIPE;
				goto ret;
				}
			}
		}
	}
else
	{
	ufd = amos_read_ufd(ip->i_number);
	logical_offset = ip->i_number/amos_size_of_logical_in_bytes*amos_size_of_logical_in_bytes/512;
	first_bn = bn = GETW(ufd->ufd_rn);
	blks = GETW(ufd->ufd_blocks);
	if ((active_bytes = GETW(ufd->ufd_active_bytes)) == 0xffff)
		size = 512*blks;
	else
		size = 510*(blks-1)+(active_bytes < 2 ? 0 : active_bytes-2);
	prev_i_number = ip->i_number;
	lrn = 0;
	}

    while(u.u_offset < size && u.u_count > 0)
    {
	if (bn == 0)
		u.u_error = EIO;
	if (active_bytes == 0xffff)
		n = 512;
	else
		n = 510;
	on = u.u_offset % n + 512 - n;
	if (u.u_offset+n > size)
		n = size-u.u_offset;
	if (n > 512-on)
		n = 512-on;
fdebugf(("amosreadi: count=%d bn=%d lrn=%d n=%d\n",u.u_count,bn,lrn,n));
/* assumes that we can return less than u.u_count bytes in buffer */
	if (u.u_count < n)
		n = u.u_count;
#ifdef NEVER /* replace amosbmap with ufd code? */
	if ((bn = amosbmap(ip, B_READ)) < 0)
	    break;
#endif
	if (u.u_error)
	    break;
	if (amos_read(amos_dev, bn+logical_offset, buf))
		break;

	if (u.u_segflg != 1)
	{
	    if (copyout(buf+on, u.u_base, n))
		u.u_error = EFAULT;
	}
	else
	    bcopy(buf+on, u.u_base, n);

if (active_bytes == 0xffff)
	bn++;
else
	bn = GETW(buf);

	if (lrn == blks-1 && active_bytes != 0xffff && bn != 0)
		u.u_error = EIO; /* expecting EOF */
	lrn += 1;

	u.u_offset	+= n;
	u.u_count	-= n;
	u.u_base	+= n;
    }
ret:
fdebugf(("amosreadi: bn=%d error=%d offset=%d count=%d n=%d\n",bn,u.u_error,u.u_offset,u.u_count,n));
}

/*
 * FS_WRITEI(ip)
 */
/*
 * amoswritei()	- write the file corresponding to
 *		  the inode pointed at by the argument
 *		  the actual write arguments are found
 *		  in the variables:
 *			u_base		core address for source
 *			u_offset	byte offset in file
 *			u_count		number of bytes to write
 *			u_segflg	write to kernel/user/user I
 */
amoswritei(ip)
    struct inode *ip;
{
    struct buf		*bp;
    unsigned		n;
    unsigned		on;
    daddr_t		bn;

    fdebugf(("amoswritei: ip = %x, inum = %d, i_fstyp=%d\n",ip, ip->i_number, ip->i_fstyp));

    if (ip->i_ftype == IFREG && !amosaccess(ip, IMNDLCK))
    {
	s5chklock(ip, FWRITE);	/* standard S5 locking code	*/
	if (u.u_error)
	    return;
    }

    if (u.u_error)
	return;

    if (u.u_offset < 0)
    {
	u.u_error = EINVAL;
	return;
    }

    n = 0;
    while (u.u_count != 0)
    {
	bn = amosbmap(ip, B_WRITE);
	if (u.u_error)
	{
	    if (n)
		u.u_error = 0;
	    break;
	}
	on = u.u_pboff;

	if (u.u_pbsize == u.u_bsize)  /* i think i goofed this line */
	    bp = getblk(u.u_pbdev, bn, u.u_bsize);
	else
	{
	    bp = bread(u.u_pbdev, bn, u.u_bsize);
	    if (bp->b_flags & B_ERROR)
	    {
		brelse(bp);
		break;
	    }
	}

	if (u.u_segflg != 1)
	{
	    if (copyin(u.u_base, bp->b_un.b_addr+on, n))
		u.u_error = EFAULT;
	}
	else
	    bcopy(u.u_base, bp->b_un.b_addr+on, n);

	if (u.u_error)
	{
	    brelse(bp);
	    break;
	}

	u.u_offset	+= n;
	u.u_count	-= n;
	u.u_base	+= n;

	if (u.u_fmode & FSYNC)
	    bwrite(bp);
	else
	    bdwrite(bp);

	if (u.u_offset > ip->i_size)
	{
	    register struct amosinode *amosip;

	    amosip = (struct amosinode *)ip->i_fsptr;
	    ASSERT(amosip != NULL);
	    if (amosip->amos_map)
		amosfreemap(ip);
	    ip->i_size = u.u_offset;
	}
	ip->i_flag |= IUPD|ICHG;
    }
}

/*
 * amosbmap()
 */
LOCAL daddr_t amosbmap(ip, flag)
    struct inode	*ip;
    int			flag;
{
    struct amosinode	*amosip;
    struct amosfilsys	*fp;
    struct mount	*mp;
    daddr_t		bn;

    amosip	= (struct amosinode *)ip->i_fsptr;
    ASSERT(amosip != NULL);
    mp		= ip->i_mntdev;
    ASSERT(mp != NULL);
    fp		= (struct amosfilsys *) mp->m_bufp;
    ASSERT(fp != NULL);


    u.u_rablock	= 0;
    u.u_bsize	= mp->m_bsize;
    u.u_pbdev	= ip->i_dev;


    u.u_pboff	= u.u_offset & FsBMASK(u.u_bsize);
    u.u_pbsize	= u.u_bsize - u.u_pboff;
    if (u.u_count < u.u_pbsize)
	u.u_pbsize = u.u_count;

    if (flag == B_WRITE)
    {
	if (u.u_offset >= (uint)(u.u_limit << SCTRSHFT))
	{
	    u.u_error = EFBIG;
	    return (daddr_t) -1;
	}
    }
    else if (ip->i_ftype == IFREG && u.u_offset + u.u_pbsize >= ip->i_size)
    {
	if (u.u_offset >= ip->i_size)
	{
	    u.u_pbsize = 0;
	    return (daddr_t) -1;
	}
	u.u_pbsize = ip->i_size - u.u_offset;
    }

/*
 * NOTE: now actually calculate the block # which corresponds
 *	 to u.u_offset in this file
 *	 if (flag == B_WRITE) this may involve growing the file
 */
 #if PSEUDO_CODE
     bn = .....
 #endif
bn = 1;
    return bn;
}


/*
 * FS_UPDATE(mp)
 */
/*
 * amosupdate()	- write out the superblock
 *
 */
/*ARGSUSED*/
amosupdate(mp)
    struct mount *mp;
{
    struct amosfilsys *fp;

/*    fdebugf(("amosupdate: mp = %x, dev = %x\n", mp, mp->m_dev)); */

    fp = (struct amosfilsys *)mp->m_bufp;
    ASSERT(fp != NULL);
#if PSEUDO_CODE
    update superblock on disk
#endif
}

/*
 * FS_ACCESS(ip, mode)
 */
/*
 * amosaccess()	- check mode permission on inode pointer
 *		  in the case of WRITE, the read-only status of the file
 *		  system is checked - also in WRITE, prototype text
 *		  segments cannot be written
 */
amosaccess(ip, mode)
    struct inode *ip;
    int mode;
{
    struct amosinode	*amosip;

    fdebugf(("amosaccess: ip = %x, inum = %d, i_fstyp=%d, mode = %x\n",ip, ip->i_number, ip->i_fstyp, mode));

    amosip = (struct amosinode *)ip->i_fsptr;
    ASSERT(amosip != NULL);

/*
 * now check the actual access
 */

 /*
  * return 0 if access is allowed
  * return 1 with u.u_error set in access is not allowed
  */

    switch (mode)
    {
	case IMNDLCK:
	case ICDEXEC:
	case IREAD:
		return 0;
	case IWRITE:
		{
		u.u_error = EROFS; 
		return 1;
		}
	case ISUID:
	case ISGID:
	case ISVTX:
	case IOBJEXEC:
	case IEXEC:
	default:
		{
		u.u_error = EACCES; 
		return 1;
		}
    }

}

/*
 * FS_OPENI(ip)
 */
/*
 * amosopeni()	- openi() is a no-op unless you support device files
 */
amosopeni(ip, flag)
    struct inode	*ip;
    int			flag;
{
	fdebugf(("amosopeni: ip = %x, inum = %d, i_fstyp=%d, flag = %d\n", ip, ip->i_number, ip->i_fstyp, flag));
}

/*
 * FS_CLOSEI(ip, flag, count, offset)
 */
/*
 * amosclosei()	- closei() is almost a no-op unless you support device files
 */
amosclosei(ip, flag, count, offset)
    struct inode	*ip;
    int			flag;
    int			count;
    off_t		offset;
{
    fdebugf(("amosclosei: ip = %x, inum = %d, i_fstyp=%d, flag = %d, count = %d, offset = %x\n", ip, ip->i_number, ip->i_fstyp, flag, count, offset));
    cleanlocks(ip, USE_PID);
    if ((unsigned)count <= 1)
	ip->i_flag &= ~IXLOCKED;
}

/* MISC FUNCTIONS */

/* Warning: strcmp - may not be a kernel function */

/* patch - added 6/25/90 - bug SCO UNIX, rcc compares signed - s/b unsigned */
LOCAL INT memcmp(s1, s2, l)
RDONLY VOID *s1;
RDONLY VOID *s2;
FAST BYTES l;
{
FAST UTINY *p1, *p2;

	p1 = (UTINY *)s1;
	p2 = (UTINY *)s2;
	while(l--)
		if (*p1++ != *p2++)
			return(*--p1 - *--p2);
	return(0);
}

LOCAL char *strcpy(dst, src)
char *dst;
char *src;
{
char *p = dst;

	while(*p++ = *src++)
		;
	return(dst);
}

LOCAL char *strncpy(dst, src, cnt)
char *dst;
char *src;
size_t cnt;
{
char *p = dst;

	while(cnt--)
		if (*src)
			*p++ = *src++;
		else
			*p++ = 0;
	return(dst);
}

LOCAL int strlen(s)
char *s;
{
char *p = s;

	while(*p++)
		;
	return((int)p-(int)s);
}

#define isdigit(c) (c >= '0' && c <= '9')
#define isupper(c) (c >= 'A' && c <= 'Z')
#define islower(c) (c >= 'a' && c <= 'z')
#define tolower(c) (isupper(c) ? c | 0x20 : c)


LOCAL TEXT *u16too(str,u16)
TEXT *str;
UINT u16;
{
	*str++ = '0' + ((u16 & 0300) >> 6); 
	*str++ = '0' + ((u16 &  070) >> 3);
	*str++ = '0' +  (u16 &   07);
	return(str);
}

LOCAL TEXT *u16tod(s,u16)
TEXT *s;
UINT u16;
{
FAST UINT i;
	for(i=10000;i!=0;u16 %= i,i/=10)
		if (u16 >= i || i == 1)
			*s++ = '0'+u16/i;
	return(s);
}

LOCAL TEXT *atorad50(rad50,str)
UINT16 *rad50;
TEXT *str;
{
FAST INT i;
FAST TEXT *p;
FAST UINT16 r50;

	for (p=str,r50=0,i=0;i<3;++i)
		{
		r50 *= 050;
		if (isdigit(*p))
			r50 += *p-'0'+036;
		else if (isupper(*p))
			r50 += *p-'A'+001;
		else if (islower(*p))
			r50 += *p-'a'+001;
		else if (*p == '$')
			r50 += 033;
		else if (*p == '.')
			r50 += 034;
		else if (*p == '%')
			r50 += 035;
		else continue;
		p++;
		}
	*rad50 = r50;
	return(p);
}


LOCAL TEXT *rad50toa(s,u16)
FAST TEXT *s;
FAST UINT u16;
{
LOCAL TEXT *rad50string = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789:";

	*s++ = rad50string[u16/1600];
	u16 %= (UINT)1600;
	*s++ = rad50string[u16/40];
	u16 %= (UINT)40;
	*s = rad50string[u16];
	if (*s == ' ')
		if (*--s == ' ')
			if (*--s == ' ')
				--s;
	return(s+1);
}


LOCAL TEXT *strlwr(str)
TEXT *str;
{
FAST TEXT *p;

	for(p=str;*p;p++)
		*p = (TEXT)tolower(*p);
	return(str);
}


LOCAL VOID cvtpathcase(device,name,ext)
TEXT *device,*name,*ext;
{
		if (device != NULL)
			strlwr(device);
		if (name != NULL)
			strlwr(name);
		if (ext != NULL)
			strlwr(ext);
}

LOCAL LONG use_badblk_sys(blk_num)
long blk_num;
{
	INT i;	
	long old_blk_num = blk_num;

	for(i=0;i<amos_nbadblks;++i)
		if (blk_num >= amos_badblks[i])
			blk_num += 1; 

	fdebugf(("badblk: oblknum = %x, nblknum = %x\n",old_blk_num,blk_num));

	return(blk_num += amos_starting_sector);
}

LOCAL INT amos_read(dev_num,blk_num,blk_buf)
dev_t dev_num;
long blk_num;
unsigned char *blk_buf;
{
	struct buf *blk_bp;

fdebugf(("amos_read: dev_num = %d, blk_num = %d\n", dev_num, blk_num));

	if ((blk_num = use_badblk_sys(blk_num)) >= amos_size_of_disk_in_records)
		{
		fdebugf(("amos_read: Invalid block number\n"));
		u.u_error = EINVAL;
		return(u.u_error);
		}
	blk_bp = bread(dev_num, blk_num, 512);
	if (blk_bp->b_flags & B_ERROR)
		{
		fdebugf(("amos_read: Error reading block from device, u.u_error = %d\n",u.u_error));
		u.u_error = EINVAL;
		return(u.u_error);
		}
	bcopy((unsigned char *)blk_bp->b_un.b_addr,blk_buf,512);
	brelse(blk_bp);
	return(0);
}

/*
 * FS_GETDENTS(ip, buf, size)
 */
/*
 * amosgetdents() - reads directory entries and returns them
 *		   in filesytem independent format
 *		   on entry u.u_offset indicates the starting
 *		   point in the directory.
 *		   getdents() copies as many complete directory
 *		   entries as will fit into the supplied buffer
 *		   (or the number of remaining entries in the
 *		   directory if fewer) and returns the actual
 *		   number of bytes copied into the buffer
 */

 /*
  * the format of a filesystem independent directory entry is
  * defined in /usr/include/sys/dirent.h
  *
  * struct dirent
  * {
  *	long		d_ino;		inode number of entry
  *	off_t		d_off;		offset of disk directory entry
  *	unsigned short	d_reclen;	length of this record
  * 	char		d_name[1];	name of file
  * };
  *
  * the file name is null terminated, and the entire structure is padded
  * so that it is a multiple of 4 bytes in size
  */

amosgetdents(ip, bufp, bufsz)
    struct inode	*ip;
    char		*bufp;
    unsigned int	bufsz;
{
	struct dirent *de, *dp;
	static struct amos_dir *dirp;

	fdebugf(("amosgetdents: ip = %x, inum = %d, i_fstyp=%d, bufp = %x, bufsz = %d, u.u_offset= %d\n", ip, ip->i_number, ip->i_fstyp, bufp, bufsz, u.u_offset));

	if (u.u_offset == -1)
		{
		amos_closedir(dirp);
		u.u_offset = 0;
		return(0);
		}
	if (u.u_offset == 0)
		dirp = amos_iopendir(ip->i_number);
	de = (struct dirent *)bufp;
	while ( (dp = amos_readdir(dirp)) != NULL )
		{
		if ((char *)de+sizeof(struct dirent)+6+1+3+3 > bufp+bufsz)
			{
			u.u_offset += 1;
			return((int)de-(int)bufp);
			}
		*de = *dp; /* stucture assignment */
		strcpy(de->d_name,dp->d_name);
		de = (struct dirent *)((char *)de+de->d_reclen);
		} 
	u.u_offset = -1;	/* Indicate all files processed */
	return((int)de-(int)bufp);
}

/*
 * FS_STATF(ip, argp)
 */
/*
 * amosstatf()	- filesystem dependent support for stat() and fstat()
 */
amosstatf(ip, st)
    struct inode	*ip;
    struct stat		*st;
{
    struct amosinode	*amosip;

    fdebugf(("amosstatf: ip = %x, inum = %d, i_fstyp=%d, st = %x\n",ip, ip->i_number, ip->i_fstyp, st));

    amosip = (struct amosinode *)ip->i_fsptr;
    ASSERT(amosip != NULL);

    /*
     * NOTE:	now extract the relevant information from the
     *		file-system dependent inode structure and store
     *		it in the stat structure
     */

    st->st_mode		= ip->i_ftype;		/* set file type bits	*/
    st->st_mode		|= 0666;/*0444;*/	/* set file mode	*/
    st->st_atime	= 0;			/* access time		*/
    st->st_mtime	= 0;			/* modification time	*/
    st->st_ctime	= 0;			/* creation time	*/
    st->st_uid		= 80;			/* uid			*/
    st->st_gid		= 80;			/* gid			*/

#if 0
    st->st_mode		|= amosip->XXX;		/* set file mode	*/
    st->st_atime	= amosip->XXX;		/* access time		*/
    st->st_mtime	= amosip->XXX;		/* modification time	*/
    st->st_ctime	= amosip->XXX;		/* creation time	*/
    st->st_uid		= amosip->XXX;		/* uid			*/
    st->st_gid		= amosip->XXX;		/* gid			*/
#endif
}

/*
 * FS_FCNTL(ip, cmd, arg, flag, offset)
 */
/*
 * amosfcntl()	- filesystem dependent support for fcntl()
 *		- locking sub-functions are handled by the
 *		  standard S5 code
 */
amosfcntl(ip, cmd, arg, flag, offset)
    struct inode	*ip;
    int			cmd;
    int			arg;
    int			flag;
    off_t		offset;
{
    struct flock	bf;
    int			i;

    fdebugf(("amosfcntl: ip = %x, inum = %d, i_fstyp=%d, cmd = %d, arg = %d, flag = %d, offset = %x\n", ip, ip->i_number, ip->i_fstyp, cmd, arg, flag, offset));

    ASSERT(ip->i_ftype == IFREG || ip->i_ftype == IFDIR);

    switch (cmd)
    {
	case F_GETLK:
	case F_SETLK:
	case F_SETLKW:
	case F_CHKFL:
	case F_LK_UNLCK:
	case F_LK_LOCK:
	case F_LK_NBLCK:
	case F_LK_RLCK:
	case F_LK_NBRLCK:
	    s5fcntl(ip, cmd, arg, flag, offset);
	    break;

	case F_RDCHK:
	    u.u_rval1 = 1;
	    break;

	case F_FREESP:		/* free file storage space */
	    if (copyin((caddr_t)arg, &bf, sizeof bf))
		u.u_error = EFAULT;
	    else if ((i = convoff(ip, &bf, 0, offset)) != 0)
		u.u_error = i;
	    else
		amosfreesp(ip, &bf, flag);
	    break;

	case F_CHSIZE:
	    if (arg < ip->i_size)
	    {
		bf.l_whence = 0;
		bf.l_start = arg;
		bf.l_len = 0;	
		amosfreesp(ip, &bf, flag);
	    }
	    else
	    { 
		u.u_offset	= arg;
		u.u_count	= 0;
		amosbmap(ip, B_WRITE);
		ip->i_size	= arg; 
		ip->i_flag	|= IUPD|ICHG; 
	    } 
	    break;

	/*
	 * NOTE:	add support for any filesystem specific
	 *		fcntl() functions here
	 */

	default:
	    u.u_error = EINVAL;
    }
}

/*
 * FS_IOCTL(ip, cmd, arg, flag)
 */
/*
 * amosioctl()
 */
/*ARGSUSED*/
amosioctl(ip, cmd, arg, flag)
    struct inode	*ip;
    int			cmd;
    int			arg;
    int			flag;
{
/* Implement 'amosioctl' (Could be very useful!) on a FS ?
 *   a) accept/change drive parameters
 *   b) enable/disable fdebugf messages switch
 */

    fdebugf(("amosioctl: ip = %x, inum = %d, i_fstyp=%d, cmd = %d, arg = %d, flag = %d\n", ip, ip->i_number, ip->i_fstyp, cmd, arg, flag));

    ASSERT(ip->i_ftype == IFREG || ip->i_ftype == IFDIR);
    u.u_error = ENOTTY;		/* Not a TTY */
}


/*
 * FS_MOUNT()
 */
/*
 * amosmount()	- the mount system call
 */
amosmount(bip, mp, flags, dataptr, datalen)
    struct inode	*bip;
    struct mount	*mp;
    int			flags;
    char		*dataptr;
    int			datalen;
{
    int			i;
    dev_t		dev;
    int			rdonly;
    struct amosfilsys	*fp		= NULL;

    printf("AMOS Filesystem Mounter. Copyright 1992 Softworks Limited, Chicago, IL USA\n");

/* If mounted with READ/WRITE then enable 'debug messages' & force READ ONLY */
    if (flags & MS_RDONLY)
	amos_fdebugf_enabled = 0;
    else
	{
	amos_fdebugf_enabled = -1; 
	flags |= MS_RDONLY;
	}

    /*
     * Give debugging information
     */
    fdebugf(("\n\namosmount: bip = %x, mount = %x, flags = %x, dataptr = %s, datalen = %x m_flags=%x\n", bip, mp, flags, dataptr, datalen, mp->m_flags));
    fdebugf(("m_dev = %d, inum = %d, i_fstyp=%d, i_rdev = %d, i_dev = %d\n",mp->m_dev, bip->i_number, bip->i_fstyp, bip->i_rdev, bip->i_dev));

    /*
     * mountable filesystems must be on block devices
     */
    if (bip->i_ftype != IFBLK)
    {
	u.u_error = ENOTBLK;
	return;
    }

    dev = (dev_t)bip->i_rdev;
    if (bmajor(dev) >= bdevcnt)
    {
	u.u_error = ENXIO;
	return;
    }

    /*
     * allocate a structure for the in-core superblock
     */
    for (i = 0; i < amosnmount; i++)
    {
	if (amosfilsys[i].s_flags == AMOS_SFREE)
	{
	    fp		= &amosfilsys[i];
	    fp->s_flags	= AMOS_SINUSE;
	    break;
	}
    }

    if (fp == NULL)
    {
	u.u_error = EBUSY;
	return;
    }

    fdebugf(("amosmount: #1\n"));

    dev		= mp->m_dev;
    rdonly	= (flags & MS_RDONLY);
    (*bdevsw[bmajor(dev)].d_open)(dev, rdonly ? FREAD : FREAD|FWRITE, OTYP_MNT);

    if (u.u_error)
	goto out;

    fdebugf(("amosmount: #2, dev = %d, rdonly = %x\n", dev, rdonly));

    /*
     * go fetch the actual filesystem data from the device and
     * fill in the in-core superblock
     */
    amosgetfilsys(dev, fp);

    fdebugf(("amosmount: #3, fp = %x\n", fp));

    if (u.u_error)
	goto out;


    mp->m_bsize = FsGETBSIZE(fp);
    mp->m_bufp	= (caddr_t)fp;

    fdebugf(("amosmount: #4, mp->m_bsize = %d\n", mp->m_bsize));

    /*
     * invalidate blocks in buffer cache
     */
    binval(dev);

    if (rdonly)
	mp->m_flags |= MRDONLY;

    mp->m_fstyp = amosfstyp;

    fdebugf(("amosmount: #5, mp->m_fstyp=%d\n", mp->m_fstyp));

    if ((mp->m_mount = iget(mp, AMOS_ROOTINO)) == 0)
	goto out;

    fdebugf(("amosmount: #6, mp->m_mount = %x, AMOS_ROOTINO = %x\n", mp->m_mount, AMOS_ROOTINO));

    mp->m_mount->i_flag |= IISROOT;
    prele(mp->m_mount);

    fdebugf(("amosmount: #7, mp->m_mount->i_flag = %x\n", mp->m_mount->i_flag));

    /*
     * Display information about AMOS partition
     */
    printf("Drive Parameters:\n");
    if (!amos_rec0_found && !amos_amosl_mon_found)
	printf("\tWarning: Drive parameters not found. Using defaults.\n");
    printf("\tNumber of logical units: %d\n",amos_number_of_logicals);
    printf("\tNumber of blocks per logical unit: %d\n", amos_size_of_logical_in_records);
    /* Starting sector != 0; can put information about 1st label record */
    printf("\tStarting sector: %d\n",amos_starting_sector);
    if (!amos_badblk_sys_processed)
	printf("\tWARNING: DSK0:BADBLK.SYS[1,2] not found - no bad blocks assumed!\n");
    else
	printf("\tNumber of bad blocks: %d\n",amos_nbadblks);
#ifndef NOSSD 
/* TODO: Implement SSD protection for AMOSFS */
 *       1) rbsc_xxxx as GLOBALS, forces Rainbow Driver to be installed
 *	 2) Verify rbsc_version & rbsc_cmsg - contain the correct info 
 *       3) The SSD code should go into 'amosgetfilsys' or 'amosmount'
 *
 * rbsc_version = 'Sentinel-C Driver Ver. 2.0 ' 
 * rbsc_cmsg    = 'Copyright 1989,1990 Rainbow Technologies, Inc., Irvine, CA'
 *
 */
    printf("Rainbow driver info (SSD device):\n");
    printf("\tversion = <%s>\n",rbsc_version);
    printf("\tcmsg = <%s>\n",rbsc_cmsg);
#endif
    printf("\n");

    amos_fs_mounted = -1;

    return;

    /*
     * cleanup on error
     */
out:
    fdebugf(("amosmount: Error - #5b, mp->m_mount = %x\n", mp->m_mount));

    binval(dev);
    (*bdevsw[bmajor(dev)].d_close)(dev, rdonly, OTYP_MNT);
    fp->s_flags = AMOS_SFREE;
}

/*
 * FS_UMOUNT()
 */
/*
 * amosumount()	- the umount system call
 */
amosumount(mp)
    struct mount	*mp;
{
    struct amosfilsys	*fp;
    dev_t		dev;

    fdebugf(("amosumount: mp = %x, dev = %d\n\n\n", mp, mp->m_dev));

    dev = mp->m_dev;

    xumount(mp);	/* remove unused sticky files from text table */

    if (iflush(mp) < 0)
    {
	u.u_error = EBUSY;
	return;
    }

    plock(mp->m_mount);

    if (! (mp->m_flags & MRDONLY))
	bflush(dev);

    (*bdevsw[bmajor(dev)].d_close)(dev, 0, OTYP_MNT);
    if (u.u_error)
	return;

    punmount(mp);
    binval(dev);
    fp		= (struct amosfilsys *) mp->m_bufp;
    fp->s_flags	= AMOS_SFREE;
    amos_fs_mounted = 0;		/* filesystem is not mounted */

    mp->m_bufp	= NULL;
    iput(mp->m_mount);
    iunhash(mp->m_mount);
    mp->m_mount	= NULL;

/*
 * NOTE: if there are any other resources associated with
 *	 a mounted filesystem they should be freed here
 */

    u.u_error = 0;
}

/*
 * FS_STATFS()
 */
/*
 * amosstatfs()	- the statfs() system call
 */
amosstatfs(ip, sp, ufstyp)
    struct inode	*ip;
    struct statfs	*sp;
    int			ufstyp;
{
    struct amosfilsys	*fp;
    struct amosfilsys	amosfs;
    dev_t 		dev;
    unsigned char	label_buf[512];
    struct alabel_record *lb = (struct alabel_record *)label_buf;

    fdebugf(("amosstatfs: ip = %x, inum = %d, sp = %x, ufstyp=%d fstyp=%d\n", ip, ip->i_number, sp, ufstyp, ip->i_fstyp));

    bzero(&amosfs, sizeof(struct amosfilsys));

    if (ip->i_fstyp != amosfstyp)
    {
	u.u_error = EINVAL;
	return;
    }

    if (ufstyp)		/* file system not mounted	*/
    {
	dev = ip->i_rdev;
	if (ip->i_ftype != IFBLK)
	{
	    u.u_error = EINVAL;
	    return;
	}
	(*bdevsw[bmajor(dev)].d_open)(dev, 0, OTYP_LYR);
	if (u.u_error)
	    return;
	fp = &amosfs;
	amosgetfilsys(dev, fp);
	if (u.u_error)
	    goto out;
    }
    else
    {
	dev	= ip->i_dev;
	fp	= (struct amosfilsys *)ip->i_mntdev->m_bufp;
    }

    ASSERT(fp != NULL);

#if 0
    Note: TODO: Change the values below to be actually true!!!! 3/17/92
#endif

    sp->f_fstyp		= amosfstyp;
    sp->f_bsize		= FsGETBSIZE(fp);
    sp->f_frsize	= 0;
    sp->f_blocks	= 163295;   /* total # of blocks on filesystem */ 
    sp->f_bfree		= 40823;    /* 25% of free space is available */ 
    sp->f_files		= 20000;    /* # of file entries (inodes) present */ 
    sp->f_ffree		= 1000;     /* total # of free inodes present */

    /*
     * volume name and pack name are only 6 characters long
     * If you don't have anything useful to put in them they
     * should be set to all zeros
     */
/* may have to move label record read elsewhere */
	if (amos_read(dev, 0, label_buf))
		goto out;
	if (GETLW(label_buf) != 0xaaaa5555)
		{
fdebugf(("amosstatfs: invalid label id (0x%x, expecting 0xAAAA5555)\n",GETLW(label_buf)));
		u.u_error = EINVAL;
		goto out;
		}
fdebugf(("lb_vln = <%s>, lb_vid = <%s>\n",lb->lb_vln,lb->lb_vid)); 
    strncpy(sp->f_fname, lb->lb_vln, sizeof(sp->f_fname));
    strncpy(sp->f_fpack, lb->lb_vid, sizeof(sp->f_fpack));

out:
    if (ufstyp)
    {
	(*bdevsw[bmajor(dev)].d_close)(dev, 0, OTYP_LYR);
	binval(dev);
    }
}

/*
 * amosgetfilsys()	- internal filesystem specific routine
 *			  which is used by both mount() and
 *			  statfs() to fill in the in-core superblock
 *			  structure with information about the
 *			  filesystem
 */
LOCAL amosgetfilsys(dev, fp)
    dev_t		dev;
    struct amosfilsys	*fp;
{
    struct buf		*bp;
    int			blk;
    unsigned char	*b;

	struct ufd_record *ufd;
	struct record0 *rec0;
	unsigned int i, j, blks, ufd_blk, rec0_blk, active_bytes, offset, len, w1, w2, w3;
	long size, ino_dsk0, ino_001002, ino_001004, ino_badblk_sys, ino_amosl_mon;
	unsigned char *p,buf[512];

	fdebugf(("amosgetfilsys: dev = %x, fp = %x, TODO CODE THIS!\n",dev,fp));

/* WARNING: Put all global information into the pointer 'struct amosfilsys *fp'
 *          that way all information about a certain device is in this struct
 *
 * note: this should be 'fp->amos_number_of_logicals' instead!!! */

/*
 * NOTE: this is completely filesystem specific code.
 *	 Here you have to extract whatever critical
 *	 information about the filesystem is needed in
 *	 order to set up the in-core superblock structure
 */

	if (amos_fs_mounted)
		return;

	amos_number_of_logicals = 1;
	amos_size_of_logical_in_bytes = LONG_MAX;
	amos_size_of_logical_in_records = LONG_MAX;
	amos_size_of_disk_in_records = LONG_MAX;
	amos_starting_sector = 0;
	amos_nbadblks = 0;
	amos_badblk_sys_processed = 0;
	amos_dev = dev;
	amos_rec0_found = amos_amosl_mon_found = 0;

/* check for label record in the first 10 records (only for Softworks) */
	fdebugf(("Searching for Record 0 - Need drive parameters!\n"));
	rec0 = (struct record0 *)buf;
	for(rec0_blk=0;rec0_blk<=10;rec0_blk++)
		{
		if (amos_read(amos_dev, rec0_blk, buf))
			return;
		if (rec0_blk == 0 && memcmp(rec0->record0_id,"\x05\x03\x04\x03\x05\x04\x03\x00",8) == 0)
			{
			amos_size_of_disk_in_records = GETW(rec0->record0_cyls)*GETW(rec0->record0_heads)*GETW(rec0->record0_sectors);
			amos_number_of_logicals = GETW(((struct record0 *)rec0)->record0_logical_drives);
			amos_size_of_logical_in_records = amos_size_of_disk_in_records/amos_number_of_logicals;
			amos_size_of_logical_in_bytes = amos_size_of_logical_in_records*512;
			amos_starting_sector = 1;
			amos_rec0_found = -1;
			break;
			}
		else if (memcmp(buf,"\xaa\xaa\x55\x55",4) == 0)
				{
				amos_starting_sector = rec0_blk;
				break;
				}
		}

/* Find BADBLK.SYS */
	fdebugf(("Searching for BADBLK.SYS!\n"));
	if ((ino_dsk0 = amos_ntoi(AMOS_ROOTINO, "dsk0")) != -1 &&
	    (ino_001002 = amos_ntoi(ino_dsk0, "001002")) != -1 &&
	    (ino_badblk_sys = amos_ntoi(ino_001002, "badblk.sys")) != -1)
		{
fdebugf(("amosgetfilsys: ino for badblk.sys=%d\n",ino_badblk_sys));
		ufd = amos_read_ufd(ino_badblk_sys);
		blks = GETW(ufd->ufd_blocks);
		ufd_blk = GETW(ufd->ufd_rn);
		active_bytes = GETW(ufd->ufd_active_bytes);
/* need func to compute file size from ufd */
		size = (blks-1)*510+active_bytes-2;
		if (size-sizeof(struct badblk_sys) > sizeof(amos_badblks))
			{
			u.u_error = EIO; /* does this work?? */
			return;
			}
		for(i=0,p=(unsigned char *)amos_badblks;i<blks;++i,ufd_blk = GETW(buf))
			{
			if (amos_read(amos_dev, ufd_blk, buf))
				return;
			if (i == 0)
				offset = 2+sizeof(struct badblk_sys);
			else
				offset = 2;
			if (i == blks-1)
				len = active_bytes-offset;
			else
				len = 512-offset;
			bcopy(buf+offset,p,len); 
			p += len;
			}
		amos_nbadblks = ((int)p - (int)amos_badblks) / sizeof(long);
fdebugf(("nbadblks=%d %d\n",amos_nbadblks,size-sizeof(struct badblk_sys)));
/* Validate badblk.sys by checking '(badblk_sys->badblk_id) != 0x8005' */

		if (amos_nbadblks*sizeof(long) != size - sizeof(struct badblk_sys))
			{
			u.u_error = EIO; /* does this work?? */
			return;
			}
/* assumes badblk.sys is sorted with 0's at the end */
		for(i=0;i<amos_nbadblks;++i)
			if ((amos_badblks[i] = GETLW(amos_badblks+i)) == 0)
				break;
		for(j=amos_nbadblks,amos_nbadblks=i;i<j;++i)
			if (amos_badblks[i] != 0)
				{
				u.u_error = EIO; /* does this work?? */
				return;
				}
fdebugf(("nbadblks=%d\n",amos_nbadblks));
		amos_badblk_sys_processed = -1;
		}
	else if (u.u_error)
		return;		/* BADBLK.SYS not found! */

/* read in amosl.mon */
	if (!amos_rec0_found)
		{	
		fdebugf(("Searching for AMOSL.MON - Need drive parameters!\n"));
		if ((ino_dsk0 = amos_ntoi(AMOS_ROOTINO, "dsk0")) != -1 &&
		    (ino_001004 = amos_ntoi(ino_dsk0, "001004")) != -1 &&
		    (ino_amosl_mon = amos_ntoi(ino_001004, "amosl.mon")) != -1)
			{
fdebugf(("mount: ino for amosl.mon=%d\n",ino_amosl_mon));
			ufd = amos_read_ufd(ino_amosl_mon);
			blks = GETW(ufd->ufd_blocks);
			ufd_blk = GETW(ufd->ufd_rn);
			active_bytes = GETW(ufd->ufd_active_bytes);
/* need func to compute file size from ufd */
			size = (blks-1)*510+active_bytes-2;
			for(i=0,w1=w2=w3=LONG_MAX;i<blks;++i,ufd_blk = GETW(buf))
				{
				if (amos_read(amos_dev, ufd_blk, buf))
					return;
				if (i == blks-1)
					len = active_bytes-offset;
				else
					len = 510;
	
/* 0x47c is offset to driver in amosl.mon */
/* warning: assuming 0x47c is offset to driver for amos32.mon also */
				if (i == 0x47c/510)
					{
					w1 = GETLW(buf+2+0x47c % 510)+0x14+2;
					w2 = GETLW(buf+2+0x47c % 510)+0x16+2;
					w3 = GETLW(buf+2+0x47c % 510)+0x48+2;
fdebugf(("i=%d w1=%x w2=%x w3=%x\n",i,w1,w2,w3));
					}
/* problem is w1w2 could be split across disk blocks */
				if (i == w1/510)
					amos_size_of_logical_in_records = GETW(buf+w1 % 510) << 16;
				if (i == w2/510)
					amos_size_of_logical_in_records += GETW(buf+w2 % 510);
				if (i == w3/510)
					amos_number_of_logicals = GETW(buf+w3 % 510);
fdebugf(("i=%d log recs=%d num log=%d\n",i,amos_size_of_logical_in_records,amos_number_of_logicals));
				}
			amos_size_of_logical_in_bytes = amos_size_of_logical_in_records*512;
			amos_size_of_disk_in_records = amos_number_of_logicals * amos_size_of_logical_in_records; 
			amos_amosl_mon_found = -1;
			}
		else
			; /* AMOSL.MON not found! */
		}
}

LOCAL amosfreesp(ip, lp, flag)
    struct inode	*ip;
    struct flock	*lp;
    int			flag;
{
    int			i;
    struct amosinode	*amosip;

    amosip = (struct amosinode *)ip->i_fsptr;
    ASSERT(amosip != NULL);
    ASSERT(ip->i_ftype == IFREG);
    ASSERT(lp->l_start >= 0);

    if (lp->l_len != 0)
    {
	u.u_error = EINVAL;
	return;
    }

    if (ip->i_size <= lp->l_start)
	return;

    lp->l_type = F_WRLCK;
    i = (flag & FNDELAY || flag & FNONBLOCK) ? INOFLCK : SLPFLCK|INOFLCK;
    if ((i = reclock(ip, lp, i, 0, lp->l_start)) != 0 || lp->l_type != F_UNLCK)
    {
	u.u_error = i ? i : EAGAIN;
	if (BADVISE_PRE_SV && (u.u_error == EAGAIN))
		u.u_error = EACCES;
	return;
    }

    if (amosip->amos_map)
	amosfreemap(ip);

    ip->i_size = lp->l_start;
    ip->i_flag |= IUPD|ICHG|ISYN;
    amosiupdat(ip);

    amostruncate(ip);
}

/* Patch - Added */
LOCAL amostruncate(ip)
    struct node		*ip;
{
/* Go thru and release the actual 'blocks' to the bitmap 
*/ 
}

/* End of amosfs.c - Filesystem Mounter driver */
