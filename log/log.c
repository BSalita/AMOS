
/* AMOS-like log utility to change directories in DOS, Windows, *NIX */

#include "swsubs.c"

int main(argc,argv)
int argc;
char **argv;
{
char s[80],newdir[128+4],curdir[128+4],*p;
char drive[4],device[16],ppn[10],devppn[26];
int i,drivenum,prog,proj;

/* get current directory path */
#if __DOS__
	*curdir = 'A'+getdisk();
	strcpy(curdir+1,":\\");
	getcurdir(0, curdir+3);
#else
	getcwd(curdir, sizeof(curdir));
#endif

	if (argc < 2)
		{
		printf("Current login is ");
		goto current_dir;
		}

	strcpy(s,argv[1]);
	p = getenv("PATHCASE");
	if (p != NULL)
		if (toupper(*p) == 'U')
			strupr(s);
		else if (toupper(*p) == 'L')
			strlwr(s);
	if (strchr(s,*PATH_SEPARATOR) != NULL)
		{
		strcpy(newdir,s);
		goto new_dir;
		}

/* AMOS does not allow any spaces between fspec parts in LOG */
/* parse device */
	if ((p = strchr(s,':')) != NULL)
		{
		i = sscanf(s,"%3s%u:",drive,&drivenum);
		if (i != 2)
			goto invalid_fspec;
		sprintf(device,"%s%u:",drive,drivenum);
		p++;
		}
	else
		{
		*device = 0;
		p = s;
		}

/* parse ppn */
	strcpy(ppn,curdir+strlen(curdir)-6);
	if (sscanf(p,"[%o,%o]",&prog,&proj) == 2)
		;
	else if (sscanf(p,"%o,%o",&prog,&proj) == 2)
		;
	else if (sscanf(p,"[,%o]",&proj) == 1)
		sscanf(ppn,"%3o",&prog);
	else if (sscanf(p,",%o",&proj) == 1)
		sscanf(ppn,"%3o",&prog);
	else if (sscanf(p,"[%o,]",&prog) == 1)
		sscanf(ppn+3,"%3o",&proj);
	else if (sscanf(p,"%o,",&prog) == 1)
		sscanf(ppn+3,"%3o",&proj);
	else prog = proj = 0;

	if (prog == 0 && proj == 0)
		*ppn = 0;
	else if (prog > 0377 || proj > 0377)
		goto invalid_fspec;
	else sprintf(ppn,"[%o,%o]",prog,proj);

/* form new directory path */
	strcpy(devppn,device);
	strcat(devppn,ppn);
	if ((p = getenv(devppn)) != NULL)
		strcpy(newdir,*p == ';' ? p+1 : p);
	else
		{
		*newdir = 0;
		if (*device)
			if ((p = getenv(device)) != NULL)
				strcat(newdir,*p == ';' ? p+1 : p);
			else
				{
				sprintf(newdir,"..%s..%s",PATH_SEPARATOR,PATH_SEPARATOR);
				strcat(newdir,device);
				p = newdir+strlen(newdir)-1;
				if (*p == ':')
					*p = 0;
				}
		else
			{
			strcat(newdir,curdir);
			*strrchr(newdir,*PATH_SEPARATOR) = 0;
			}
		strcat(newdir,PATH_SEPARATOR);
		if (*ppn)
			if ((p = getenv(ppn)) != NULL)
				strcat(newdir,*p == ';' ? p+1 : p);
			else
				{
				sprintf(ppn,"%.3o%.3o",prog,proj);
				strcat(newdir,ppn);
				}
		else
			strcat(newdir,strrchr(curdir,*PATH_SEPARATOR));
		}

new_dir:
/* remove any trailing '\\' */
	if ((p = strrchr(newdir,*PATH_SEPARATOR)) != NULL)
		if (!*(p+1))
			*p = 0;

	printf("Transferred from %s to ", curdir);

/* change current directory path */
	_chdir(newdir);

/* form new current directory path */
#if __DOS__
	*curdir = 'A'+getdisk();
	curdir[1] = ':';
	strcpy(curdir+2,PATH_SEPARATOR);
	getcurdir(0, curdir+3);
#else
	getcwd(curdir, sizeof curdir);
#endif

current_dir:
	printf("%s\n",curdir);
	exit(0);

invalid_fspec:
	printf("?Invalid LOG fspec - %s\n", argv[1]);
	exit(1);
}
