

/* vcr driver for SCO UNIX and XENIX */
/* Copyright 1991 Softworks Limited */

/* compile using "cc -c vcr.c" */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/signal.h>
#include <sys/errno.h>
#include <sys/dir.h>
#include <sys/seg.h>
#include <sys/page.h>
#include <sys/user.h>
#include <sys/file.h>
#include <sys/tty.h>
#include <sys/conf.h>

static unsigned char vcr_buf[512];

#define CHECKINT
#define clock_ticks_per_sec 50
#define msdelay(ms) delay((ms*clock_ticks_per_sec+999)/1000)
#define secdelay(secs) delay(secs*clock_ticks_per_sec)

#include "vcrsub.c"

int vcrclose(dev,flag)
dev_t dev;
int flag;
{
	if (vd.vcr_debug)
		printf("vcrclose: flag=%x\n",flag);
	vcr_close(-1);
	vd.vcr_in_use = 0;
}

int vcrinit()
{
	printcfg("vcr",0x2e4,2,-1,-1,"unit=0 type=AM6xx vers=910730");
}

int vcropen(dev,flag,id)
dev_t dev;
int flag;
int id;
{
	if (vd.vcr_debug)
		printf("vcropen: dev=%x flag=%x id=%x count=%d\n",dev,flag,id,vd.vcr_copy_count);
	if (vd.vcr_suser && !suser())
		{
		seterror(EPERM);
		return;
		}
	if (vd.vcr_in_use)
		{
		seterror(EBUSY);
		return;
		}
	vd.vcr_in_use = -1;
	vcr_open();
}

int vcrread(dev)
dev_t dev;
{
int i;

	if (vd.vcr_read_write != 1)
		{
		vcr_loadpoint(-1, vd.vcr_msgs);
		vcr_playback(-1, vd.vcr_msgs);
		}
	while(u.u_count)
		{
		if (vd.vcr_debug)
			printf("vcrread: u.u_count=%d u.u_base=%x\n",u.u_count,u.u_base);
		if (vcr_read(-1, vcr_buf) != 512)
			return;
		if (u.u_count > 512)
			i = 512;
		else
			i = u.u_count;
		if (copyout(vcr_buf,u.u_base,i) == -1)
			{
			seterror(EFAULT);
			return;
			}
		u.u_base += i;
		u.u_count -= i;
		}
}
		
int vcrwrite(dev)
dev_t dev;
{
int i;
	if (vd.vcr_read_write != 2)
		{
		vcr_loadpoint(-1, vd.vcr_msgs);
		vcr_record(-1, vd.vcr_msgs);
		}
	while(u.u_count)
		{
		if (vd.vcr_debug)
			printf("vcrwrite: u.u_count=%d u.u_base=%x\n",u.u_count,u.u_base);
		if (u.u_count > 512)
			i = 512;
		else
			i = u.u_count;
		if (copyin(u.u_base,vcr_buf,i) == -1)
			{
			seterror(EFAULT);
			return;
			}
		if (vcr_write(-1, vcr_buf) != 512)
			return;
		u.u_base += i;
		u.u_count -= i;
		}
}

int vcrioctl(dev, cmd, arg, mode)
dev_t dev;
int cmd;
char *arg;
int mode;
{
	vcr_ioctl(-1, cmd, arg);
}
