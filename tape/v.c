#include "stdio.h"
#include "fcntl.h"
#include "errno.h"
#include "vcr.h"
#define O_BINARY 0
main()
{
int vcrfd;
struct vcr_driver_data vd;
	vcrfd = open("/dev/vcr",O_RDONLY|O_BINARY);
	printf("vcrfd=%d\n",vcrfd);
	printf("ioctl=%d\n",ioctl(vcrfd,VCR_GET_DRIVER_DATA,&vd));
	vd.vcr_copy_count = 1;
	printf("ioctl=%d\n",ioctl(vcrfd,VCR_SET_DRIVER_DATA,&vd));
	printf("ioctl=%d\n",ioctl(vcrfd,VCR_GET_DRIVER_DATA,&vd));
	printf("Pres ENTER to continue\n");
	getchar();
	printf("ioctl=%d\n",ioctl(vcrfd,VCR_VD_DUMP,&vd));
	printf("errno=%d\n",errno);
}
