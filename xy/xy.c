
#include "xtrmio.c"

int main(argc,argv)
int argc;
char **argv;
{
int x;
unsigned y;

	if (argc == 2)
		if (*argv[1] == '=')
			{
			x = -1;
			y = atoi(argv[1]+1);
			}
		else
			goto invalid_arg;
	else if (argc == 3)
		if (strcmp(argv[1],"=") == 0)
			{
			x = -1;
			y = atoi(argv[2]);
			}
		else
			{
			x = atoi(argv[1]);
			y = atoi(argv[2]);
			}
	else
		goto invalid_arg;

	tcrtinit();
	tcrt(x,y);
	tcrtend();
	exit(0);

invalid_arg:
	puts("?Invalid argument to XY");
	exit(1);
}
