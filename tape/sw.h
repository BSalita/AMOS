/* Softworks C Definition File */

#ifndef __SW_H__
#define __SW_H__

/*      the pseudo storage classes
 */
#define FAST    register
#define GLOBAL  extern
#define IMPORT  extern
#define INTERN  static
#define LOCAL   static

/*      the pseudo types
 */
typedef char TBOOL, TEXT;
typedef double DOUBLE;
typedef float FLOAT;
typedef int ARGINT, BOOL, /*ERROR, */ INT, METACH;
typedef long LONG;
typedef short SHRT;
#ifdef EXAN
typedef short COUNT;
typedef unsigned short UCOUNT;
#endif
typedef unsigned char UTINY;
typedef unsigned long ULONG, MEMAD;
typedef unsigned short USHRT;
typedef unsigned int UINT;
#if __SCO_XENIX__
typedef unsigned int BYTES;
#else
typedef size_t BYTES;
#endif

#ifndef SIGNED
#define SIGNED signed
#endif
typedef SIGNED char TINY;
#ifdef RDONLY   /* AIX 6000 - vmstat.h */
#undef RDONLY
#endif
#define RDONLY const
#define TOUCHY volatile
#ifndef VOID /* patch - 11/11/94 - added - problem with MS header file */
#if __DOS_WIN__
typedef void VOID;
#else
#define VOID void
#endif
#endif
typedef VOID (*(*FNPTR)())();   /* pseudo type for onexit */

/*      system parameters
 */
#define STDIN   0
#define STDOUT  1
#define STDERR  2
#define YES             1
#define NO              0
#define FAIL    1
#define SUCCESS 0
#ifndef NULL
#define NULL    (VOID *)0
#endif
#define FOREVER for (;;)
#define BYTMASK 0377

#define R_SYSTEM        2               /* system terminal mode */
#define R_RAW           1               /* raw terminal mode */
#define R_COOKED        0               /* cooked terminal mode (line) */
#define R_QUERY         -1              /* query present terminal mode */

#define SECDAY          86400           /* seconds in a day */
#define SECHOUR         3600            /* seconds in an hour */

#ifdef MAX
#undef MAX
#undef MIN
#endif
#define MAX(x, y)       (((x) < (y)) ? (y) : (x))
#define MIN(x, y)       (((x) < (y)) ? (x) : (y))
#define IMAX(x, y)      (((INT)(x) < (INT)(y)) ? (y) : (x))
#define IMIN(x, y)      (((INT)(x) < (INT)(y)) ? (x) : (y))

#endif /* __SW_H__ */
