
/* vcr subroutines */

#include "vcr.h"

#if VCR
#ifndef __
#if __STDC__
#define __(a) a
#else
#define __(a) ()
#endif
#endif
int vcr_close __((int fd));
int vcr_cmd __((int port, int cmd));
int vcr_ioctl __((int fd, int cmd, void *arg));
int vcr_init __((int port));
int vcr_open __((void));
int vcr_read __((int vcr_fd, char *buf));
int vcr_write __((int vcr_fd, char *buf));
int vcr_vd_dump __((struct vcr_driver_data *vd));
int vcr_loadpoint __((int vcr_fd, int msg));
int vcr_playback __((int vcr_fd, int msg));
int vcr_record __((int vcr_fd, int msg));
int vcr_rewind __((int vcr_fd, int msg));
int vcr_get_driver_data __((int vcr_fd, struct vcr_driver_data *vd));

static char *vcr_errstep = "";
static struct vcr_driver_data vd = {0, 0, 0, 0x2e4, 0, 0, 0, 0, 20, 0, 0, 3, 1, -1, -1, 0, 500, 1};

#define vcr_inportb(port) inp(port)
#define vcr_outportb(port, value) outp(port, value)

static int vcr_cmd(port, cmd)
int port;
int cmd;
{
int b;
/* wait for handshake and ready to be cleared */
	while(((b = vcr_inportb(port)) & 0x88) != 0x00)
		CHECKINT;
/* output command to controller */
	vcr_outportb(port,cmd);
/* loop until command is acknowledged */
	while((vcr_inportb(port) & 0x80) == 0x00)
		CHECKINT;
/* clear command */
	vcr_outportb(port,0x00);
/* wait for command completion */
	while(((b = vcr_inportb(port)) & 0x80) == 0x80)
		CHECKINT;
	return(b);
}

static int vcr_ioctl(vcr_fd, cmd, arg)
int vcr_fd;
int cmd;
void *arg;
{
	if (vcr_fd != -1)
		return(ioctl(vcr_fd, cmd, arg));
	switch(cmd)
	{
	case VCR_WRITE_EOF:
		vcr_cmd(vd.vcr_port, 0x1d);
		break;
	case VCR_WRITE_EOT:
/* is a 1 second delay needed here? its used in vcrsav */
		vcr_cmd(vd.vcr_port, 0x2d);
		break;
	case VCR_RESET:
		vcr_cmd(vd.vcr_port, 0x40);     /* reset controller */
		break;
	case VCR_BUFFERED_READS_OFF:
		vcr_cmd(vd.vcr_port, 0x48);     /* turn buffered reads off */
		break;
	case VCR_BUFFERED_READS_ON:
		vcr_cmd(vd.vcr_port, 0x68);     /* turn buffered reads on */
		break;
	case VCR_SET_COPY_COUNT:
		vcr_cmd(vd.vcr_port, 0x72);     /* set copy count */
		msdelay(vd.vcr_msdelay);        /* may not be needed */
		vcr_outportb(vd.vcr_port+1, vd.vcr_copy_count+1);/* set copy count */
/*printf("copy count=%d\n",vd.vcr_copy_count);*/
		break;
	case VCR_SET_RECORD_TYPE:
		vcr_cmd(vd.vcr_port, 0x74);     /* set record type to 1 */
		msdelay(vd.vcr_msdelay);        /* needed on 486 UNIX */
		vcr_outportb(vd.vcr_port+1, vd.vcr_record_type);
		break;
	case VCR_SET_RECORD_ID:
		vcr_cmd(vd.vcr_port, 0x76);     /* set record ID to 0 */
		msdelay(vd.vcr_msdelay);        /* may not be needed */
		vcr_outportb(vd.vcr_port+1, (char)(vd.vcr_record_counter));
		vcr_outportb(vd.vcr_port+1, (char)(vd.vcr_record_counter >> 8));
		vcr_outportb(vd.vcr_port+1, (char)(vd.vcr_record_counter >> 16));
		vcr_outportb(vd.vcr_port+1, (char)(vd.vcr_record_counter >> 24));
		break;
	case VCR_ABORT:
		vcr_outportb(vd.vcr_port, 0xff);        /* abort current cmd */
		vcr_outportb(vd.vcr_port, 0x00);
		break;
	case VCR_LOADPOINT:
		vcr_loadpoint(-1, -1);
		break;
	case VCR_PLAYBACK:
		vcr_playback(-1, -1);
		break;
	case VCR_RECORD:
		vcr_record(-1, -1);
		break;
	case VCR_REWIND:
		vcr_rewind(-1, -1);
		break;
	case VCR_GET_DRIVER_DATA:
		copyout(&vd,arg,sizeof(vd));
		break;
	case VCR_SET_DRIVER_DATA:
		copyin(arg,&vd,sizeof(vd));
		break;
	case VCR_VD_DUMP:
		vcr_vd_dump(&vd);
		break;
	default:
		seterror(EINVAL);
		break;
	}
	return(0);
}

static int vcr_init(port)
int port;
{
/* if init routines hang power off system */
	vcr_errstep = "abort current cmd";
	vcr_outportb(port, 0xff);
/* could have to loop on ff waiting for ok status and not send 0x00 */
	vcr_outportb(port, 0x00);
	vcr_errstep = "reset controller";
	vcr_cmd(port, 0x40);
	vcr_errstep = "set record type to 1";
	vcr_cmd(port, 0x74);
	msdelay(vd.vcr_msdelay);                /* needed on 486 UNIX */
	vcr_outportb(port+1, 0x01);
	vcr_errstep = "set block counter to 0";
	vcr_cmd(port, 0x76);
	msdelay(vd.vcr_msdelay);                /* may not be needed */
	vcr_outportb(port+1, 0x00);
	vcr_outportb(port+1, 0x00);
	vcr_outportb(port+1, 0x00);
	vcr_outportb(port+1, 0x00);
	vcr_errstep = "";
	vd.vcr_record_counter = 0;
	return(0);
}

static int vcr_close(vcr_fd)
{
	if (vcr_fd != -1)
		return(close(vcr_fd));
	if (vd.vcr_read_write == 2)
		{
/* using eof/eof/eot conventions from vcrsav */
		vcr_cmd(vd.vcr_port, 0x1d);
		vcr_cmd(vd.vcr_port, 0x1d);
/* vcrsav uses a 1 second sleep here. Should we? */
		vcr_cmd(vd.vcr_port, 0x2d);
		}
	return(0);
}

static int vcr_open()
{
	vd.vcr_record_id = 0;
	vd.vcr_record_counter = 0;
	vd.vcr_max_buffered = 0;
	vd.vcr_buffered_warnings = 0;
	vd.vcr_read_write = -1;
	vd.vcr_record_type = 1;
	vcr_init(vd.vcr_port);
	return(0);
}

static int vcr_read(vcr_fd,buf)
int vcr_fd;
char *buf;
{
int b;
int attempts = 0;

	if (vcr_fd != -1)
		return(read(vcr_fd, buf, 512));
do
	{
/* should status of other calls be checked? */
	b = vcr_cmd(vd.vcr_port, 0x01); /* read data into controller ram */

	if (b & VCR_HARD_ERROR)
		{
		printf("vcr_read: Hard error attempting to read vcr block %lu\n",vd.vcr_record_counter);
		seterror(EIO);
		return(-1);
		}
	if ((b &= VCR_MAX_BUFFERED) > 0)
		{
		vd.vcr_buffered_warnings += 1;
		vd.vcr_total_buffered_warnings += 1;
		if (b > vd.vcr_max_buffered)
			vd.vcr_max_buffered = b;
		if (b > vd.vcr_total_max_buffered)
			vd.vcr_total_max_buffered = b;
		}

	vcr_cmd(vd.vcr_port, 0x4a); /* read 6+512 bytes from controller ram */
	msdelay(vd.vcr_msdelay);        /* needed on 486 DOS and UNIX */
	vcr_inportb(vd.vcr_port+1);     /* why? checksum? */

/* do 6+512 inputs from port */
	repinsb(vd.vcr_port+1,vd.vcr_data_set_header,6);
	repinsb(vd.vcr_port+1,buf,512);

	vcr_outportb(vd.vcr_port,0x00); /* seems to need this */

/* vcr block number is byte ordered low to high */
#if 0
	vd.vcr_record_id = buf[2] + (buf[3] << 8) + ((unsigned long)buf[4] << 16) + ((unsigned long)buf[5] << 24);
#else   /* assumes INTEL byte order */
	vd.vcr_record_id = *(unsigned long *)(vd.vcr_data_set_header+2);
#endif

	if (vd.vcr_debug)
		printf("vcr_read: type=%x copy=%x id=%lx\n",vd.vcr_data_set_header[0],vd.vcr_data_set_header[1],vd.vcr_record_id);

	if (vd.vcr_record_id != vd.vcr_record_counter)
		{
		printf("vcr_read: Expecting block %lu but read block %lu\n",vd.vcr_record_counter,vd.vcr_record_id);
		if (attempts++ == 0)
			putchar(7);     /* bell */
		else if (attempts > vd.vcr_max_expectations)
			{
			printf("vcr_read: Too many expectations.\n");
			seterror(EIO);
			return(-1);
			}
		}
	}
while(vd.vcr_record_id != vd.vcr_record_counter);

	vd.vcr_record_counter += 1;

	switch(vd.vcr_data_set_header[0])
		{
	case VCR_TYPE_DATA0:
	case VCR_TYPE_DATA1:
		return(512);
	case VCR_TYPE_EOF:
		return(0);
	case VCR_TYPE_EOT:
		return(0);
		}
	printf("vcr_read: Unknown record type %x\n",vd.vcr_data_set_header[0]);
	seterror(EIO);
	return(-1);
}

static int vcr_write(vcr_fd, buf)
int vcr_fd;
char *buf;
{
	if (vcr_fd != -1)
		return(write(vcr_fd, buf, 512));
	vcr_cmd(vd.vcr_port, 0x7e);     /* write tape into controller ram */
	msdelay(vd.vcr_msdelay);                /* may not be needed */
	repoutsb(vd.vcr_port+1,buf,512);
	vcr_outportb(vd.vcr_port,0x00);         /* seems to need this */
	vcr_cmd(vd.vcr_port, 0x0d);             /* increment block count */
	return(512);
}

static int vcr_vd_dump(vd)
struct vcr_driver_data *vd;
{
int i;
	printf("vcr_msgs=%d\n",vd->vcr_msgs);
	printf("vcr_record_id=%lu\n",vd->vcr_record_id);
	printf("vcr_record_counter=%lu\n",vd->vcr_record_counter);
	printf("vcr_port=%x\n",vd->vcr_port);
	printf("vcr_max_buffered=%u\n",vd->vcr_max_buffered);
	printf("vcr_total_max_buffered=%u\n",vd->vcr_total_max_buffered);
	printf("vcr_buffered_warnings=%u\n",vd->vcr_buffered_warnings);
	printf("vcr_total_buffered_warnings=%u\n",vd->vcr_total_buffered_warnings);
	printf("vcr_copy_count=%u\n",vd->vcr_copy_count);
	printf("vcr_debug=%d\n",vd->vcr_debug);
	printf("vcr_in_use=%d\n",vd->vcr_in_use);
	printf("vcr_record_secdelay=%d\n",vd->vcr_record_secdelay);
	printf("vcr_msdelay=%d\n",vd->vcr_msdelay);
	printf("vcr_read_write=%d\n",vd->vcr_read_write);
	printf("vcr_suser=%d\n",vd->vcr_suser);
	printf("vcr_port_status=%u\n",vd->vcr_port_status);
	printf("vcr_max_expectations=%u\n",vd->vcr_max_expectations);
	printf("vcr_record_type=%d\n",vd->vcr_record_type);
	printf("vcr_data_set_header=");
	for(i=0;i<6;++i)
		printf(" %x",vd->vcr_data_set_header[i]);
	printf("\n");
	return(0);
}

static int vcr_loadpoint(vcr_fd, msg)
int vcr_fd;
int msg;
{
	if (vcr_fd != -1)
		return(ioctl(vcr_fd, VCR_LOADPOINT, NULL));
	vcr_cmd(vd.vcr_port, 0x48);     /* turn off buffered reads */
	if (msg)
		{
		printf("\nPlace cassette at load point.\nPress ENTER when ready.");
		getchar();
		}
	vd.vcr_record_counter = 0;
	return(0);
}

static int vcr_playback(vcr_fd, msg)
int vcr_fd;
int msg;
{
	if (vcr_fd != -1)
		return(ioctl(vcr_fd, VCR_PLAYBACK, NULL));
	if (msg)
		{
		printf("\nPress PLAY on VCR.\nPress ENTER when ready.");
		getchar();
		}
	vcr_cmd(vd.vcr_port, 0x68);     /* turn on buffered reads */
	vd.vcr_read_write = 1;
	return(0);
}

static int vcr_record(vcr_fd, msg)
int vcr_fd;
int msg;
{
	if (vcr_fd != -1)
		return(ioctl(vcr_fd, VCR_RECORD, NULL));
	if (msg)
		{
		printf("\nPress RECORD on VCR.\nPress ENTER when ready.");
		getchar();
		secdelay(vd.vcr_record_secdelay);
		}
	vcr_ioctl(-1, VCR_SET_COPY_COUNT, NULL);
	vd.vcr_read_write = 2;
	return(0);
}

static int vcr_rewind(vcr_fd, msg)
int vcr_fd;
int msg;
{
	if (vcr_fd != -1)
		return(ioctl(vcr_fd, VCR_REWIND, NULL));
	vcr_cmd(vd.vcr_port, 0x48);     /* turn off buffered reads */
	if (msg)
		printf("\nRewind VCR.\n");
	vd.vcr_record_counter = 0;
	return(0);
}

static int vcr_get_driver_data(vcr_fd, vd)
int vcr_fd;
struct vcr_driver_data *vd;
{
	if (vcr_fd != -1)
		return(ioctl(vcr_fd, VCR_GET_DRIVER_DATA, vd));
	return(0);
}
#endif
