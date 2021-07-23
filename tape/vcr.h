
/* vcr header file */

#ifndef __VCR_H__
#define __VCR_H__

#ifndef __IDEF_H__
typedef char TEXT;
typedef unsigned char UTINY;
typedef unsigned short UINT16;
typedef unsigned long UINT32;

/* Alpha Micro separated date */
typedef struct adate
	{
	UTINY   ad_month;
	UTINY   ad_day;
	UTINY   ad_year;
	UTINY   ad_wday;
	} ADATE;

typedef UTINY UINT_1;
typedef UTINY UINT_12[2];
typedef UTINY UINT_3412[4];
typedef UTINY AFLT6[6];
#endif

/* port status flags */
#define VCR_MAX_BUFFERED 0x07
#define VCR_BUSY 0x08
#define VCR_EOF 0x10
#define VCR_EOT 0x20
#define VCR_HARD_ERROR 0x40
#define VCR_HANDSHAKE 0x80

/* record types */
#define VCR_TYPE_DATA0 0x00
#define VCR_TYPE_DATA1 0x01
#define VCR_TYPE_EOF 0x40
#define VCR_TYPE_EOT 0x80

/* ioctl() calls */
#define VCR_WRITE_EOF 0x1d
#define VCR_WRITE_EOT 0x2d
#define VCR_RESET 0x40
#define VCR_BUFFERED_READS_OFF 0x48
#define VCR_BUFFERED_READS_ON 0x68
#define VCR_SET_COPY_COUNT 0x72
#define VCR_SET_RECORD_TYPE 0x74
#define VCR_SET_RECORD_ID 0x76
#define VCR_ABORT 0xff
#define VCR_LOADPOINT 0x100
#define VCR_PLAYBACK 0x101
#define VCR_RECORD 0x102
#define VCR_REWIND 0x103
#define VCR_GET_DRIVER_DATA 0x106
#define VCR_SET_DRIVER_DATA 0x107
#define VCR_VD_DUMP 0x108

typedef struct vcr_driver_data
	{
	int             vcr_msgs;
	unsigned long   vcr_record_id;
	unsigned long   vcr_record_counter;
	int             vcr_port;
	unsigned int    vcr_max_buffered;
	unsigned int    vcr_total_max_buffered;
	unsigned int    vcr_buffered_warnings;
	unsigned int    vcr_total_buffered_warnings;
	unsigned int    vcr_copy_count;
	int             vcr_debug;
	int             vcr_in_use;
	int             vcr_record_secdelay;
	int             vcr_msdelay;
	int             vcr_read_write;
	int             vcr_suser;
	unsigned int    vcr_port_status;
	unsigned int    vcr_max_expectations;
	int             vcr_record_type;
	int             vcr_int_spares[20];
	unsigned char   vcr_data_set_header[6];
	unsigned char   vcr_char_spares[2];
	} VCR_DRIVER_DATA;

int vcr_vd_dump();

/* AMOS label record */
typedef struct label_record
	{
	UINT_1  lb_hdr[4];              /* header */
	TEXT    lb_vln[40];             /* volume name */
	TEXT    lb_vid[10];             /* volume id */
	TEXT    lb_cre[30];             /* creator */
	TEXT    lb_ins[30];             /* installation */
	TEXT    lb_sys[30];             /* system date */
	ADATE   lb_crd;                 /* creation date */
	ADATE   lb_acd;                 /* access date */
	ADATE   lb_fbd;                 /* backup date 1 */
	TEXT    lb_fbi[10];             /* backup vol id 1 */
	ADATE   lb_gbd;                 /* backup date 2 */
	TEXT    lb_gbi[10];             /* backup vol id 2 */
	UINT_12 lb_lb_cnt;              /* number of extra label blocks */
	UINT_12 lb_vers;                /* label record version number */
	UINT_3412       lb_crt;                 /* creation time */
	UINT_1  lb_unknown;
	UINT_1  lb_extra_copies;
	} ALABEL_RECORD;

/* AMOS vcr/streamer table of contents entry */
#if VCRRES
typedef struct atoc_entry_vcr
	{
	UINT_12 toc_device;
	UTINY   toc_flag;               /* 0x40 means hash is valid */
	UTINY   toc_drivenum;
	UINT_12 toc_fn[2];
	UINT_12 toc_ext;
	UTINY   toc_prog;
	UTINY   toc_proj;
	UINT_3412       toc_blocks;
	UINT_12 toc_active_bytes;
	ADATE   toc_date;
	UINT_12 toc_time[2];
	UINT_12 toc_hash[2];
	UINT_12 toc_entry_number;
	UINT_12 toc_unknown;
        } ATOC_ENTRY_VCR;
#define MAX_ATOC_ENTRIES_VCR ((512-2)/sizeof(ATOC_ENTRY_VCR))
#else
typedef struct atoc_entry_mt1
	{
	UINT_12 toc_device;
	UTINY   toc_drivenum;
	UTINY   toc_flag;               /* 0x40 means hash is valid */
	UINT_12 toc_fn[2];
	UINT_12 toc_ext;
	UTINY   toc_prog;
	UTINY   toc_proj;
	UINT_12 toc_blocks;
	UINT_12 toc_active_bytes;
	ADATE   toc_date;
	UINT_12 toc_time[2];
	UINT_12 toc_hash[2];
	UINT_12 toc_entry_number;
	UINT_12 toc_unknown[2];
        } ATOC_ENTRY_MT1;
#define MAX_ATOC_ENTRIES_MT1 ((512-2)/sizeof(ATOC_ENTRY_MT1))

typedef struct atoc_entry_mt2
	{
	UINT_12 toc_entry_number;
	UTINY   toc_unknown[2];
	UINT_12 toc_blocks;
	UINT_12 toc_active_bytes;
	UTINY   toc_unknown1[24];
	UINT_12 toc_device;
	UINT_1  toc_drivenum;
	UINT_1  toc_flag;               /* 0x40 means hash is valid */
	UINT_12 toc_fn[2];
	UINT_12 toc_ext;
	UINT_1  toc_prog;
	UINT_1  toc_proj;
	ADATE   toc_date;
	UINT_12 toc_time[2];
	UINT_12 toc_hash[2];
        } ATOC_ENTRY_MT2;
#define MAX_ATOC_ENTRIES_MT2 ((512-2)/sizeof(ATOC_ENTRY_MT2))
#endif


/* Softworks toc - needs to be altered for AMOS 2.0 */
typedef struct toc_entry
	{
	UINT16  toc_device;
	UTINY   toc_flag;               /* 0x40 means hash is valid */
	UTINY   toc_drivenum;
	UINT16  toc_fn[2];
	UINT16  toc_ext;
	UTINY   toc_prog;
	UTINY   toc_proj;
	UINT32  toc_blocks;
	UINT16  toc_active_bytes;
	ADATE   toc_date;
	UINT32  toc_time;
	UINT16  toc_hash[2];
	UINT16  toc_entry_number;
	} TOC_ENTRY;

#endif /* __VCR_H__ */
