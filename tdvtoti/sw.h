/* Softworks C Definition File */

/*	the pseudo storage classes
 */
#define FAST	register
#define GLOBAL	extern
#define IMPORT	extern
#define INTERN	static
#define LOCAL	static

/*	the pseudo types
 */
typedef char TBOOL, TEXT;
typedef double DOUBLE;
typedef float FLOAT;
typedef int ARGINT, BOOL, ERROR, INT, METACH;
typedef long LONG;
typedef short COUNT, FD, SHRT;
typedef unsigned char TBITS, UTINY;
typedef unsigned long LBITS, ULONG, MEMAD;
typedef unsigned short BITS, UCOUNT, USHRT;
typedef unsigned int BYTES, UINT;

#ifndef SIGNED
#define SIGNED signed
#endif
typedef SIGNED char TINY;
#define RDONLY const
#define TOUCHY volatile
#ifndef VOID
#define VOID void
#endif
typedef VOID (*(*FNPTR)())();	/* pseudo type for onexit */

/*	system parameters
 */
#define STDIN	0
#define STDOUT	1
#define STDERR	2
#define YES		1
#define NO		0
#define FAIL	1
#define SUCCESS	0
#ifndef NULL
#define NULL	(VOID *)0
#endif
#define FOREVER	for (;;)
#define BYTMASK	0377

#define R_SYSTEM	2		/* system terminal mode */
#define R_RAW		1		/* raw terminal mode */
#define R_COOKED	0		/* cooked terminal mode (line) */
#define R_QUERY		-1		/* query present terminal mode */

#define SECDAY		86400		/* seconds in a day */
#define SECHOUR		3600		/* seconds in an hour */

typedef struct tm TIMEVEC;

#define ABS(x)		((x) < (INT)0 ? -(x) : (x))
#define MAX(x, y)	(((x) < (y)) ? (y) : (x))
#define MIN(x, y)	(((x) < (y)) ? (x) : (y))
