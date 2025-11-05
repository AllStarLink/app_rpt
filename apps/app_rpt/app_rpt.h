#define VERSION_MAJOR 3
#define VERSION_MINOR 6
#define VERSION_PATCH 3

/* 99% of the DSP code in app_rpt exists in dsp.c as private functions. This code can mostly be
	converted to use public dsp.h API.
	Eventually, the app_rpt DSP could should be/will be removed and NATIVE_DSP will be assumed,
	thus the macro and any ifndef NATIVE_DSP and (NATIVE_DSP) else code will be removed.
	However, until we're 100% ready for that, that will not be done.
	Uncomment the following to test using native Asterisk DSP. */
#define NATIVE_DSP

/* Un-comment the following to include support decoding of MDC-1200 digital tone
 signalling protocol (using KA6SQG's GPL'ed implementation) */
#define USE_MDC1200

#ifdef USE_MDC1200
/* Start from the include directory, so that it works for both apps/app_rpt.c and files in apps/app_rpt */
#include "../apps/app_rpt/mdc_encode.h"
#include "../apps/app_rpt/mdc_decode.h"
#endif

/*! \note <sys/io.h> is not portable to all architectures, so don't call non-portable functions if we don't have them */
#if __has_include(<sys/io.h>)
#define HAVE_SYS_IO
#endif

/* Un-comment the following to include support for notch filters in the
	rx audio stream (using Tony Fisher's mknotch (mkfilter) implementation) */
/* #include "rpt_notch.c" */

#ifndef NATIVE_DSP
typedef struct {
	int v2;
	int v3;
	int chunky;
	int fac;
} goertzel_state_t;

typedef struct {
	int value;
	int power;
} goertzel_result_t;

typedef struct {
	int freq;
	int block_size;
	int squelch;				/* Remove (squelch) tone */
	goertzel_state_t tone;
	float energy;				/* Accumulated energy of the current block */
	int samples_pending;		/* Samples remain to complete the current block */
	int mute_samples;			/* How many additional samples needs to be muted to suppress already detected tone */

	int hits_required;			/* How many successive blocks with tone we are looking for */
	float threshold;			/* Energy of the tone relative to energy from all other signals to consider a hit */

	int hit_count;				/* How many successive blocks we consider tone present */
	int last_hit;				/* Indicates if the last processed block was a hit */

} tone_detect_state_t;
#endif

#define TONE_SAMPLE_RATE 8000
#define TONE_SAMPLES_IN_FRAME 160

#ifdef	__RPT_NOTCH
#define	MAXFILTERS 10
#endif

/* maximum digits in DTMF buffer, and seconds after * for DTMF command timeout */
#define	MAXDTMF 32
#define	MAXMACRO 2048
#define	MAXNODES 500			  /* Maximum number of nodes allowed in the link list */
#define	RPT_AST_STR_INIT_SIZE 500 /* initial guess for ast_str size */
#define	LINKLISTTIME 10000
#define	LINKLISTSHORTTIME 200
#define	LINKPOSTTIME 30000
#define LINKPOSTSHORTTIME 200
#define	KEYTIMERTIME 250
#define	MACROTIME 100
#define	MACROPTIME 500
#define TONEMACRO_REPEAT 'R'
#define DTMF_TIMEOUT 3
#define KENWOOD_RETRIES 5
#define TOPKEYN 32
#define TOPKEYWAIT 3
#define TOPKEYMAXSTR 30
#define NEWKEYTIME 2000

#define	AUTHTELLTIME 7000
#define	AUTHTXTIME 1000
#define	AUTHLOGOUTTIME 25000

#define	DISC_TIME 10000  /* report disc after 10 seconds of no connect */
#define	MAX_RETRIES 5
#define	MAX_RETRIES_PERM 1000000000

#define	REDUNDANT_TX_TIME 2000

#define RETRY_TIMER_MS 5000

#define PATCH_DIALPLAN_TIMEOUT 1500

#define RPT_THREAD_TIMEOUT 30 /* Time in seconds to consider rpt thread "stuck" if no activity */
#define RPT_LOCKOUT_SECS 10
#define RPT_CMD_SZ 299
#define RPT_SRC_SZ 299
#define RPT_DEST_SZ 299
#define RPT_TMP_SZ 511
#define RSSI_SZ 31
#define RPT_TO_STRING(x) #x
#define S_FMT(x) "%" RPT_TO_STRING(x) "s "
#define N_FMT(duf) "%30" #duf /* Maximum sscanf conversion to numeric strings */
#define LON_SZ 24
#define LAT_SZ 24
#define ELEV_SZ 24

#define MAXPEERSTR 31
#define MAXREMSTR 15

#define MONITOR_DISK_BLOCKS_PER_MINUTE 38

#define	DEFAULT_MONITOR_MIN_DISK_BLOCKS 10000
#define	DEFAULT_REMOTE_INACT_TIMEOUT (15 * 60)
#define	DEFAULT_REMOTE_TIMEOUT (60 * 60)
#define	DEFAULT_REMOTE_TIMEOUT_WARNING (3 * 60)
#define	DEFAULT_REMOTE_TIMEOUT_WARNING_FREQ 30

#define DEFAULT_ERXGAIN -3.0
#define DEFAULT_ETXGAIN 3.0
#define DEFAULT_TRXGAIN -3.0
#define DEFAULT_TTXGAIN 3.0
#define DEFAULT_LINKMONGAIN 0.0

#define	DEFAULT_EANNMODE 1
#define	DEFAULT_TANNMODE 1

#define	DEFAULT_RXBURST_TIME 250 
#define	DEFAULT_RXBURST_THRESHOLD 16

#define	DEFAULT_SPLIT_2M 600
#define	DEFAULT_SPLIT_70CM 5000

#define	MAX_TEXTMSG_SIZE 160

#define	MAX_EXTNODEFILES 50
#define	MAX_LOCALLINKNODES 50
#define	MAX_LSTUFF 20

#define	NODES "nodes"
#define	EXTNODES "extnodes"
#define MEMORY "memory"
#define MACRO "macro"
#define	FUNCTIONS "functions"
#define TELEMETRY "telemetry"
#define MORSE "morse"
#define	TONEMACRO "tonemacro"
#define	MDCMACRO "mdcmacro"
#define	DTMFKEYS "dtmfkeys"
#define	FUNCCHAR '*'
#define	ENDCHAR '#'
#define	EXTNODEFILE "/var/lib/asterisk/rpt_extnodes"
#define	NODENAMES "rpt/nodenames"
#define	PARROTFILE "/tmp/parrot_%s_%u"

#define	GPS_VALID_SECS 60
#define	GPS_UPDATE_SECS 30

#define	PARROTTIME 1000

#define	TELEM_HANG_TIME 120000
#define	LINK_HANG_TIME 120000

#define	DEFAULT_IOBASE 0x378

#define	DEFAULT_CIV_ADDR 0x58

#define	MAXCONNECTTIME 5000

#define MAXNODESTR 300

#define MAXNODELEN 16

#define MAXIDENTLEN 32

#define MAXPATCHCONTEXT 100

#define TELEPARAMSIZE 400

#define REM_SCANTIME 100

#define	DTMF_LOCAL_TIME 250
#define	DTMF_LOCAL_STARTTIME 500

#define	IC706_PL_MEMORY_OFFSET 50

#define	VOX_ON_DEBOUNCE_COUNT 3
#define	VOX_OFF_DEBOUNCE_COUNT 20
#define	VOX_MAX_THRESHOLD 10000.0
#define	VOX_MIN_THRESHOLD 3000.0
#define	VOX_TIMEOUT_MS 10000
#define	VOX_RECOVER_MS 2000
#define	SIMPLEX_PATCH_DELAY 25
#define	SIMPLEX_PHONE_DELAY 25

#define RX_LINGER_TIME 50

#define	STATPOST_PROGRAM "/usr/bin/wget,-q,--output-document=/dev/null,--no-check-certificate"

#define	ALLOW_LOCAL_CHANNELS

#define EL_DB_ROOT "echolink"

#define	DEFAULT_LITZ_TIME 3000
#define	DEFAULT_LITZ_CHAR "0"

/*
 * DAQ subsystem
 */

#define MAX_DAQ_RANGES 16  /* Max number of entries for range() */
#define MAX_DAQ_ENTRIES 10 /* Max number of DAQ devices */
#define MAX_DAQ_NAME 32 /* Max length of a device name */
#define MAX_DAQ_DEV 64 /* Max length of a daq device path */
#define MAX_METER_FILES 10 /* Max number of sound files in a meter def. */
#define DAQ_RX_TIMEOUT 50 /* Receive time out for DAQ subsystem */ 
#define DAQ_ADC_ACQINT 10 /* Acquire interval in sec. for ADC channels */
#define ADC_HIST_TIME 300 /* Time  in sec. to calculate short term avg, high and low peaks from. */
#define ADC_HISTORY_DEPTH ADC_HIST_TIME/DAQ_ADC_ACQINT

enum keypost {
	RPT_KEYPOST_NONE,
	RPT_KEYPOST_ACTIVE
};

enum {REM_OFF,REM_MONITOR,REM_TX};

enum rpt_linkmode {
	LINKMODE_OFF,
	LINKMODE_ON,
	LINKMODE_FOLLOW,
	LINKMODE_DEMAND,
	LINKMODE_GUI,
	LINKMODE_PHONE,
	LINKMODE_ECHOLINK,
	LINKMODE_TLB
};

enum rpt_phone_mode {
	RPT_PHONE_MODE_NONE,		  /* not in phone control mode */
	RPT_PHONE_MODE_PHONE_CONTROL, /* Phone control mode Reverse patch or Phone control mode */
	RPT_PHONE_MODE_DUMB_DUPLEX,	  /* Dumb duplex mode  (Full controls of radio)*/
	RPT_PHONE_MODE_DUMB_SIMPLEX	  /* Allows audio-only access */
};

enum rpt_tele_mode {
	ID,
	PROC,
	TERM,
	COMPLETE,
	UNKEY,
	REMDISC,
	REMALREADY,
	REMNOTFOUND,
	REMGO,
	CONNECTED,
	CONNFAIL,
	STATUS,
	TIMEOUT,
	ID1,
	STATS_TIME,
	PLAYBACK,
	LOCALPLAY,
	STATS_VERSION,
	IDTALKOVER,
	ARB_ALPHA,
	TEST_TONE,
	REV_PATCH,
	TAILMSG,
	MACRO_NOTFOUND,
	MACRO_BUSY,
	LASTNODEKEY,
	FULLSTATUS,
	MEMNOTFOUND,
	INVFREQ,
	REMMODE,
	REMLOGIN,
	REMXXX,
	REMSHORTSTATUS,
	REMLONGSTATUS,
	LOGINREQ,
	SCAN,
	SCANSTAT,
	TUNE,
	SETREMOTE,
	TOPKEY,
	TIMEOUT_WARNING,
	ACT_TIMEOUT_WARNING,
	LINKUNKEY,
	UNAUTHTX,
	PARROT,
	STATS_TIME_LOCAL,
	VARCMD,
	LOCUNKEY,
	METER,
	USEROUT,
	PAGE,
	STATS_GPS,
	STATS_GPS_LEGACY,
	MDC1200,
	LASTUSER,
	REMCOMPLETE,
	PFXTONE
};

enum rpt_offset {
	REM_SIMPLEX,
	REM_MINUS,
	REM_PLUS
};

enum rpt_power {
	REM_LOWPWR,
	REM_MEDPWR,
	REM_HIPWR
};

enum rpt_function_response {
	DC_INDETERMINATE,
	DC_REQ_FLUSH,
	DC_ERROR,
	DC_COMPLETE,
	DC_COMPLETEQUIET,
	DC_DOKEY
};

enum rpt_command_source {
	SOURCE_RPT,
	SOURCE_LNK,
	SOURCE_RMT,
	SOURCE_PHONE,
	SOURCE_DPHONE,
	SOURCE_ALT
};

enum rpt_delay {
	DLY_TELEM,
	DLY_ID,
	DLY_UNKEY,
	DLY_CALLTERM,
	DLY_COMP,
	DLY_LINKUNKEY,
	DLY_PARROT,
	DLY_MDC1200
};

enum rpt_mode {
	REM_MODE_FM,
	REM_MODE_USB,
	REM_MODE_LSB,
	REM_MODE_AM
};

enum rpt_hf_mode {
	HF_SCAN_OFF,
	HF_SCAN_DOWN_SLOW,
	HF_SCAN_DOWN_QUICK,
	HF_SCAN_DOWN_FAST,
	HF_SCAN_UP_SLOW,
	HF_SCAN_UP_QUICK,
	HF_SCAN_UP_FAST
};

/*
 * DAQ Subsystem
 */

enum rpt_daq_mode {
	DAQ_PS_IDLE = 0,
	DAQ_PS_START,
	DAQ_PS_BUSY,
	DAQ_PS_IN_MONITOR
};

enum rpt_daq_cmd {
	DAQ_CMD_IN,
	DAQ_CMD_ADC,
	DAQ_CMD_OUT,
	DAQ_CMD_PINSET,
	DAQ_CMD_MONITOR
};

enum rpt_daq_filter {
	DAQ_SUB_CUR = 0,
	DAQ_SUB_MIN,
	DAQ_SUB_MAX,
	DAQ_SUB_STMIN,
	DAQ_SUB_STMAX,
	DAQ_SUB_STAVG
};

enum rpt_daq_pintype {
	DAQ_PT_INADC = 1,
	DAQ_PT_INP,
	DAQ_PT_IN,
	DAQ_PT_OUT
};

enum rpt_daq_type {
	DAQ_TYPE_UCHAMELEON
};

/* general setting - rpt_node_lookup */
enum  rpt_dns_method {
	LOOKUP_BOTH,
	LOOKUP_DNS,
	LOOKUP_FILE
};

enum patch_call_mode {
	CALLMODE_DOWN,
	CALLMODE_DIALING,
	CALLMODE_CONNECTING,
	CALLMODE_UP,
	CALLMODE_FAILED
};

#define DEFAULT_NODE_LOOKUP_METHOD LOOKUP_DNS
#define DEFAULT_VOTERGAIN 10
#define DEFAULT_TELEMDUCKDB -15
#define DEFAULT_TELEMNOMDB -3
#define DEFAULT_RPT_TELEMDEFAULT 1
#define DEFAULT_RPT_TELEMDYNAMIC 1
#define DEFAULT_GUI_LINK_MODE LINKMODE_ON
#define DEFAULT_GUI_LINK_MODE_DYNAMIC 1
#define DEFAULT_PHONE_LINK_MODE LINKMODE_ON
#define DEFAULT_PHONE_LINK_MODE_DYNAMIC 1
#define DEFAULT_ECHOLINK_LINK_MODE LINKMODE_DEMAND
#define DEFAULT_ECHOLINK_LINK_MODE_DYNAMIC 1
#define DEFAULT_TLB_LINK_MODE LINKMODE_DEMAND
#define DEFAULT_TLB_LINK_MODE_DYNAMIC 1

/* for DNS resolution of node data */
#define DEFAULT_DNS_NODE_DOMAIN "nodes.allstarlink.org"

#define REMOTE_RIG_FT950 "ft950"
#define REMOTE_RIG_FT897 "ft897"
#define REMOTE_RIG_FT100 "ft100"
#define REMOTE_RIG_RBI "rbi"
#define REMOTE_RIG_KENWOOD "kenwood"
#define REMOTE_RIG_TM271 "tm271"
#define REMOTE_RIG_TMD700 "tmd700"
#define REMOTE_RIG_IC706 "ic706"
#define REMOTE_RIG_XCAT "xcat"
#define REMOTE_RIG_RTX150 "rtx150"
#define REMOTE_RIG_RTX450 "rtx450"
#define REMOTE_RIG_PPP16 "ppp16" /* parallel port programmable 16 channels */

#define ISRIG_RTX(x) ((!strcmp(x,REMOTE_RIG_RTX150)) || (!strcmp(x,REMOTE_RIG_RTX450)))
#define	IS_XPMR(x) (!strncasecmp(x->rxchanname,"rad",3))

#define MSWAIT 20
#define HANGTIME 5000
#define SLEEPTIME 900					 /* default # of seconds for of no activity before entering sleep mode */
#define TOTIME 180000					 /* default timeout time to 180000ms (3 minutes) */
#define TIMEOUTRESETUNKEYINTERVAL 0		 /* default timeout reset time to 0ms (0 seconds) - disabled by default */
#define TIMEOUTRESETKERCHUNKINTERVAL 250 /* Minimum local keyed time to reset a time out condition caused by a remote link */
#define IDTIME 300000
#define MAXRPTS 500
#define MAX_STAT_LINKS 256
#define POLITEID 30000
#define FUNCTDELAY 1500
#define TMP_SIZE 256 /* Size of tmp char* */
#define MAXXLAT 20
#define MAXXLATTIME 3

#define MAX_SYSSTATES 10

#define FT897_SERIAL_DELAY 75000		/* # of usec to wait between some serial commands on FT-897 */
#define FT100_SERIAL_DELAY 75000		/* # of usec to wait between some serial commands on FT-897 */

#define DISCSTR "!!DISCONNECT!!"
#define NEWKEYSTR "!NEWKEY!"
#define NEWKEY1STR "!NEWKEY1!"
#define IAXKEYSTR "!IAXKEY!"

/*! \brief Repeater link connection newkey handshake state */
enum newkey { 
	RADIO_KEY_ALLOWED, /*!< AST_CONTROL_RADIO_KEY is allowed on repeater channel */
	RADIO_KEY_ALLOWED_REDUNDANT, /*!< "!NEWKEY!" - AST_CONTROL_RADIO_KEY allowed on the repeater channel */
	RADIO_KEY_NOT_ALLOWED /*!< "!NEWKEY1!" message - AST_CONTROL_RADIO_KEY are not allowed on the repeater channel */
};

enum link_mode {
	MODE_MONITOR,
	MODE_TRANSCEIVE,
	MODE_LOCAL_MONITOR
};

enum rpt_parrot_mode {
	PARROT_MODE_OFF = 0,
	PARROT_MODE_ON_COMMAND,
	PARROT_MODE_ON_ALWAYS
};

enum rpt_parrot_states {
	PARROT_STATE_IDLE,
	PARROT_STATE_RECORDING,
	PARROT_STATE_PLAYING
};

struct vox {
	float	speech_energy;
	float	noise_energy;
	int	enacount;
	char	voxena;
	char	lastvox;
	int	offdebcnt;
	int	ondebcnt;
};

struct rpt_topkey {
	char	node[TOPKEYMAXSTR];
	int	timesince;
	int	keyed;
};

struct rpt_xlat {
	char	funccharseq[MAXXLAT];
	char	endcharseq[MAXXLAT];
	char	passchars[MAXXLAT];
	int	funcindex;
	int	endindex;
	time_t	lastone;
};

struct rpt_frame_queue {
	struct ast_frame *lastf1, *lastf2;
};

/*! \brief Structure that holds information regarding app_rpt operation */
struct rpt;

/*! \brief Structure used to manage links */
struct rpt_link {
	struct rpt_link *next;
	struct rpt_link *prev;
	enum link_mode mode;
	char isremote;
	enum rpt_phone_mode phonemode;
	char phonevox;		   /* vox the phone */
	char phonemonitor;	   /* no tx or funs for the phone */
	char name[MAXNODESTR]; /* identifier (routing) string */
	unsigned int lasttx:1;
	unsigned int lasttx1:1;
	unsigned int lastrx:1;
	unsigned int lastrealrx:1;
	unsigned int lastrx1:1;
	unsigned int last_frame_sent:1; /* We have written a single frame */
	unsigned int wouldtx:1;
	unsigned int connected:1;
	unsigned int hasconnected:1;
	unsigned int perma:1; /* permanent link */
	unsigned int thisconnected:1;
	unsigned int outbound:1;
	unsigned int disced:2;
	unsigned int killme:1;
	unsigned int dtmfed:1;
	unsigned int gott:1;
	long elaptime;
	int disctime;
	int	retrytimer;
	int	retxtimer;
	int	rerxtimer;
	int	rxlingertimer;
	int	rssi;
	int	retries;
	int	max_retries;
	int	reconnects;
	struct timeval connecttime;
	struct ast_channel *chan;	
	struct ast_channel *pchan;
	struct ast_str *linklist;
	time_t	linklistreceived;
	int linklisttimer;
	int linkunkeytocttimer;
	struct timeval lastlinktv;
	struct rpt_frame_queue frame_queue;
	struct vox vox;
	char wasvox;
	int voxtotimer;
	char voxtostate;
	enum newkey link_newkey;
	int linkmode;
	int newkeytimer;
	int		voterlink;      /*!< \brief set if node is defined as a voter rx */
	int		votewinner;		/*!< \brief set if node won the rssi competition */
	time_t	lastkeytime;
	time_t	lastunkeytime;
	pthread_t connect_threadid;
	AST_LIST_HEAD_NOLOCK(, ast_frame) rxq;
	AST_LIST_HEAD_NOLOCK(, ast_frame) textq;
};

/*!
 * \brief Initialize doubly linked list of RPT links
 */
void rpt_links_init(struct rpt_link *l);

/*! \brief Structure used to manage link status */
struct rpt_lstat {
	struct rpt_lstat *next;
	struct rpt_lstat *prev;
	char peer[MAXPEERSTR];
	char name[MAXNODESTR];
	enum link_mode mode;
	unsigned int outbound:1;
	unsigned int thisconnected:1;
	int reconnects;
	struct timeval connecttime;
};

struct rpt_tele {
	struct rpt_tele *next;
	struct rpt_tele *prev;
	struct rpt *rpt;
	struct ast_channel *chan;
	enum rpt_tele_mode mode;
	struct rpt_link mylink;
	char param[TELEPARAMSIZE];
	union {
		int i;
		void *p;
		char _filler[8];
	} submode;
	unsigned int parrot;
	char killed;
	pthread_t threadid;
};

struct function_table_tag {
	char *action;
	enum rpt_function_response (*function)(struct rpt *myrpt, char *param, char *digitbuf, enum rpt_command_source command_source,
		struct rpt_link *mylink);
	int minargs;
};

/*
 * Structs used in the DAQ code
 */
struct daq_tx_entry_tag {
	char txbuff[32];
	struct daq_tx_entry_tag *prev;
	struct daq_tx_entry_tag *next;
};

struct daq_pin_entry_tag {
	int num;
	enum rpt_daq_pintype pintype;
	enum rpt_daq_cmd command;
	enum rpt_daq_mode state;
	int value;
	int valuemax;
	int valuemin;
	int ignorefirstalarm;
	int alarmmask;
	int adcnextupdate;
	int adchistory[ADC_HISTORY_DEPTH];
	char alarmargs[64];
	void (*monexec)(struct daq_pin_entry_tag *);
	struct daq_pin_entry_tag *next;
};

struct daq_entry_tag {
	char name[MAX_DAQ_NAME];
	char dev[MAX_DAQ_DEV];
	enum rpt_daq_type type;
	int fd;
	int active;
	time_t adcacqtime;
	pthread_t threadid;
	ast_mutex_t lock;
	struct daq_tx_entry_tag *txhead;
	struct daq_tx_entry_tag *txtail;
	struct daq_pin_entry_tag *pinhead;
	struct daq_entry_tag *next;
};

struct daq_tag {
	int ndaqs;
	struct daq_entry_tag *hw;
};


/*! \brief Used to store the morse code patterns */
struct morse_bits {
	int len;
	int ddcomb;
};

struct telem_defaults {
	char name[20];
	char value[200];
};

struct sysstate {
	char txdisable;
	char totdisable;
	char linkfundisable;
	char autopatchdisable;
	char schedulerdisable;
	char userfundisable;
	char alternatetail;
	char noincomingconns;
	char sleepena;
};

/* rpt cmd support */
#define CMD_DEPTH 1
#define CMD_STATE_IDLE 0
#define CMD_STATE_BUSY 1
#define CMD_STATE_READY 2
#define CMD_STATE_EXECUTING 3

struct rpt_cmd_struct {
	int state;
	int functionNumber;
	char param[MAXMACRO];
	char digits[MAXDTMF];
	enum rpt_command_source command_source;
};

struct rpt_conf {
	/* Conference bridge channels */
	struct ast_bridge *conf;
	struct ast_bridge *txconf;
};

/*! \brief Populate rpt structure with data */
struct rpt {
	ast_mutex_t lock;
	ast_mutex_t remlock;
	ast_mutex_t statpost_lock;
	struct ast_config *cfg;
	char reload;
	char reload1;
	char deleted;
	char xlink; /*!< cross link state of a share repeater/remote radio */
	unsigned int statpost_seqno;

	char *name;
	char *rxchanname;
	char *txchanname;
	char remote;
	char *remoterig;
	unsigned int scram;
#ifdef	_MDC_DECODE_H_
	mdc_decoder_t *mdc;
#endif

	struct {
		const char *ourcontext;
		const char *ourcallerid;
		const char *acctcode;
		const char *ident;
		const char *tonezone;
		char simple;
		const char *functions;
		const char *link_functions;
		const char *phone_functions;
		const char *dphone_functions;
		const char *alt_functions;
		const char *nodes;
		const char *extnodes;
		const char *extnodefiles[MAX_EXTNODEFILES];
		int  extnodefilesn;
		const char *patchconnect;
		const char *lnkactmacro;
		const char *lnkacttimerwarn;
		const char *rptinactmacro;
		const char *dtmfkeys;
		int hangtime;
		int althangtime;
		int totime;
		int time_out_reset_unkey_interval;
		int time_out_reset_kerchunk_interval;
		int idtime;
		int tailmessagetime;
		int tailsquashedtime;
		int sleeptime;
		int lnkacttime;
		int rptinacttime;
		int duplex;
		int politeid;
		const char *tailmessages[500];
		int tailmessagemax;
		const char *memory;
		const char *macro;
		const char *tonemacro;
		const char *mdcmacro;
		const char *startupmacro;
		const char *morse;
		const char *telemetry;
		int iobase;
		const char *ioport;
		int iospeed;
		char funcchar;
		char endchar;
		unsigned int archiveaudio:1;
		unsigned int nobusyout:1;
		unsigned int notelemtx:1;
		unsigned int propagate_dtmf:1;
		unsigned int propagate_phonedtmf:1;
		unsigned int linktolink:1;
		unsigned char civaddr;
		struct rpt_xlat inxlat;
		struct rpt_xlat outxlat;
		const char *archivedir;
		const char *archivedatefmt;
		const char *archiveformat;
		int authlevel;
		const char *csstanzaname;
		const char *skedstanzaname;
		const char *txlimitsstanzaname;
		long monminblocks;
		int remoteinacttimeout;
		int remotetimeout;
		int remotetimeoutwarning;
		int remotetimeoutwarningfreq;
		int sysstate_cur;
		struct sysstate s[MAX_SYSSTATES];
		enum rpt_parrot_mode parrotmode;
		int parrottime;
		const char *rptnode;
		char remote_mars;
		int voxtimeout_ms;
		int voxrecover_ms;
		int simplexpatchdelay;
		int simplexphonedelay;
		char telemdefault;
		unsigned int telemdynamic:1;
		unsigned int lnkactenable:1;
		const char *statpost_program;
		const char *statpost_url;
		enum rpt_linkmode linkmode[10];
		char linkmodedynamic[10];
		const char *locallist[16];
		int nlocallist;
		char ctgroup[16];
		float telemnomgain;	 /*!< \brief nominal gain adjust for telemetry */
		float telemduckgain; /*!< \brief duck on busy gain adjust for telemetry */
		float erxgain;
		float etxgain;
		float linkmongain;
		char eannmode; /* {NONE,NODE,CALL,BOTH} */
		float trxgain;
		float ttxgain;
		char tannmode; /* {NONE,NODE,CALL,BOTH} */
		const char *discpgm;
		const char *connpgm;
		const char *mdclog;
		unsigned int nolocallinkct:1;
		unsigned int nounkeyct:1;
		unsigned int holdofftelem:1;
		unsigned int beaconing:1;
		int rxburstfreq;
		int rxbursttime;
		int rxburstthreshold;
		int litztime;
		const char *litzchar;
		const char *litzcmd;
		unsigned int itxctcss:1;
		unsigned int gpsfeet:1;
		int default_split_2m;
		int default_split_70cm;
		int votertype;                                  /*!< \brief 0 none, 1 repeater, 2 voter rx      */
		int votermode;                                  /*!< \brief 0 none, 1 one shot, 2 continuous    */
		int votermargin;                                /*!< \brief rssi margin to win a vote           */
		unsigned int dtmfkey:1;
		char dias;
		char dusbabek;
		const char *outstreamcmd;
		char dopfxtone;
		const char *events;
		const char *locallinknodes[MAX_LOCALLINKNODES];
		int locallinknodesn;
		const char *eloutbound;
		int elke;
		const char *aprstt;
		const char *lconn[MAX_LSTUFF];
		int nlconn;
		const char *ldisc[MAX_LSTUFF];
		int nldisc;
		const char *timezone;
	} p;
	struct ao2_container *links;
	int unkeytocttimer;
	time_t lastkeyedtime;
	time_t lasttxkeyedtime;
	char keyed;
	char txkeyed;
	char rxchankeyed;					/*!< \brief Receiver RxChan Key State */
	char exttx;
	char localtx;
	char remrx;	
	char remoterx;
	char remotetx;
	char remoteon;
	char remtxfreqok;
	unsigned int tounkeyed:1;
	char tonotify;
	char dtmfbuf[MAXDTMF];
	struct ast_str *macrobuf;
	char rem_dtmfbuf[MAXDTMF];
	char lastdtmfcommand[MAXDTMF];
	char cmdnode[50];
	char nowchan;						/*!< channel now */
	char waschan;						/*!< channel selected initially or by command */
	char bargechan;						/*!< barge in channel */
	char macropatch;					/*!< autopatch via tonemacro state */
	enum rpt_parrot_states parrotstate;
	unsigned int parrotonce:1;
	char linkactivityflag;
	char rptinactwaskeyedflag;
	char lastitx;
	char remsetting;
	char tunetx;
	int  parrottimer;
	unsigned int parrotcnt;
	int telemmode;
	struct ast_channel *rxchannel, *txchannel, *monchannel;
	struct ast_channel *pchannel, *txpchannel, *localrxchannel, *localtxchannel;
	struct rpt_frame_queue frame_queue;
	struct rpt_tele tele;
	struct timeval lasttv,curtv;
	struct rpt_conf rptconf;
	pthread_t rpt_call_thread,rpt_thread;
	time_t dtmf_time,rem_dtmf_time,dtmf_time_rem;
	int calldigittimer;
	int tailtimer, totimer, idtimer, cidx, scantimer, tmsgtimer, skedtimer, linkactivitytimer, elketimer;
	int remote_time_out_reset_unkey_interval_timer, time_out_reset_unkey_interval_timer;
	enum patch_call_mode callmode;
	int mustid,tailid;
	int rptinacttimer;
	int tailevent;
	int dtmfidx, rem_dtmfidx;
	int dailytxtime, dailykerchunks, totalkerchunks, dailykeyups, totalkeyups, timeouts;
	int totalexecdcommands, dailyexecdcommands;
	int	retxtimer;
	int	rerxtimer;
	long long totaltxtime;
	char mydtmf;
	char exten[AST_MAX_EXTENSION];
	char freq[MAXREMSTR],rxpl[MAXREMSTR],txpl[MAXREMSTR];
	int  splitkhz;
	enum rpt_offset offset;
	enum rpt_power powerlevel;
	char txplon;
	char rxplon;
	enum rpt_mode remmode;
	char tunerequest;
	enum rpt_hf_mode hfscanmode;
	int hfscanstatus;
	char hfscanstop;
	char lastlinknode[MAXNODESTR];
	char savednodes[MAXNODESTR];
	int stopgen;
	int remstopgen;
	char patchfarenddisconnect;
	char patchnoct;
	char patchquiet;
	char patchvoxalways;
	char patchcontext[MAXPATCHCONTEXT];
	char patchexten[AST_MAX_EXTENSION];
	int patchdialtime;
	int macro_longest;
	int phone_longestfunc;
	int alt_longestfunc;
	int dphone_longestfunc;
	int link_longestfunc;
	int longestfunc;
	int longestnode;
	int threadrestarts;		
	int tailmessagen;
	time_t disgorgetime;
	time_t lastthreadrestarttime;
	time_t lastthreadupdatetime; /* Thread activity timestamp.  Used to detect a "stuck" thread */
	int macrotimer;
	char lastnodewhichkeyedusup[MAXNODESTR];
	int dtmf_local_timer;
	char dtmf_local_str[100];
	struct ast_filestream *monstream, *parrotstream;
	char loginuser[50];
	char loginlevel[10];
	long authtelltimer;
	long authtimer;
	int iofd;
	time_t start_time,last_activity_time;
	char lasttone[32];
	struct rpt_tele *active_telem;
	struct rpt_topkey topkey[TOPKEYN];
	int topkeystate;
	time_t topkeytime;
	int topkeylong;
	struct vox vox;
	char wasvox;
	int voxtotimer;
	char voxtostate;
	unsigned int patch_talking:1;
	int linkposttimer;
	enum keypost keypost;
	int lastkeytimer;
	enum newkey rpt_newkey;
	char inpadtest;
	int rxlingertimer;
	char localoverride;
	char ready;
	char lastrxburst;
	char reallykeyed;
	char dtmfkeyed;
	char dtmfkeybuf[MAXDTMF];
	char localteleminhibit;		/*!< \brief local telemetry inhibit */
	char noduck;				/*!< \brief no ducking of telemetry  */
	char sleepreq;
	char sleep;
	struct rpt_link *voted_link; /*!< \brief last winning link or NULL */
	int  rxrssi;				/*!< \brief rx rssi from the rxchannel */
	int  voted_rssi;			/*!< \brief last winning rssi */
	int  vote_counter;			/*!< \brief count to frame used to vote the winner */
	int  voter_oneshot;
	int  votewinner;
	int  voteremrx;             /* 0 no voters are keyed, 1 at least one voter is keyed */
	char lastdtmfuser[MAXNODESTR];
	char curdtmfuser[MAXNODESTR];
	int  sleeptimer;
	time_t lastgpstime;			/* monotonic time */
	int outstreampipe[2];
	int outstreampid;
	time_t outstreamlasterror;	/*!< \brief set when there is an outstream error and is reset when error cleared */
	struct ast_channel *remote_webtransceiver;
	struct timeval lastdtmftime;
	int keyed_time_ms; /*!< Time in milliseconds that a user has been keyed on the local RX */
#ifdef NATIVE_DSP
	struct ast_dsp *dsp;
#else
	tone_detect_state_t burst_tone_state;
#endif
	AST_LIST_HEAD_NOLOCK(, ast_frame) txq;
	AST_LIST_HEAD_NOLOCK(, ast_frame) rxq;
	char txrealkeyed;
#ifdef	__RPT_NOTCH
	struct rptfilter
	{
		char	desc[100];
		float	x0;
		float	x1;
		float	x2;
		float	y0;
		float	y1;
		float	y2;
		float	gain;
		float	const0;
		float	const1;
		float	const2;
	} filters[MAXFILTERS];
#endif
#ifdef	_MDC_DECODE_H_
	unsigned short lastunit;
	char lastmdc[32];
#endif
	struct rpt_cmd_struct cmdAction;
	struct timeval paging;
	char deferid;
	struct timeval lastlinktime;
	unsigned int last_statpost_failed:1;
};

struct nodelog {
	struct nodelog *next;
	struct nodelog *prev;
	time_t	timestamp;
	char archivedir[MAXNODESTR];
	char str[MAXNODESTR * 2];
};

struct statpost {
	struct rpt *myrpt;
	char *stats_url;
};

/*! \brief Whether a channel is using a specified technology */
#define CHAN_TECH(c, s) (!strcasecmp(ast_channel_tech(c)->type, s))

#define IS_PSEUDO(c) (!strncasecmp(ast_channel_name(c), "DAHDI/pseudo", 12))
#define IS_PSEUDO_NAME(c) (!strncasecmp(c, "DAHDI/pseudo", 12))

#define IS_LOCAL(c) (!strncasecmp(ast_channel_name(c), "Local", 5))
#define IS_LOCAL_NAME(c) (!strncasecmp(c, "Local", 5))

int rpt_debug_level(void);
int rpt_set_debug_level(int newlevel);
int rpt_num_rpts(void);
int rpt_nullfd(void);
time_t rpt_starttime(void);
int rpt_function_lookup(const char *f);
int rpt_function_minargs(int index);

void donodelog(struct rpt *myrpt, char *str);
#define donodelog_fmt(myrpt, fmt, ...) __donodelog_fmt(myrpt, __FILE__, __LINE__, __FUNCTION__, fmt, __VA_ARGS__)
void __donodelog_fmt(struct rpt *myrpt, const char *file, int lineno, const char *func, const char *fmt, ...);

void rpt_event_process(struct rpt *myrpt);
void *rpt_call(void *this);

/*!
 * \brief Generates a command line completion list for rpt cmd third argument
 */
char *rpt_complete_function_list(const char *line, const char *word, int pos, int rpos);

#define RPT_MUTE_FRAME(f) \
	if (f) \
	ast_frame_clear(f)

/*!
* \brief Loops over (traverses) the entries in an AO2 container list.
* \param container This is a pointer to the AO2 container
* \param var This is the name of the variable that will hold a pointer to the
* current list entry on each iteration. It must be declared before calling
* this macro.
* \param iterator This is the name of the variable that will be used for
* the AO2_ITERATOR. It must be declared before calling this macro.
*
* This macro is used to loop over (traverse) the entries in an AO2 container list. It uses a
* \a for loop, and supplies the enclosed code with a pointer to each list
* entry as it loops. It is typically used as follows:
* \code
* static ao2_container container;
* ao2_iterator_t iterator;
* ...
* struct list_entry {
*    ...
* }
* ...
* struct list_entry *current;
* ...
* RPT_LIST_TRAVERSE(&container, current, iterator) {
*	(do something with current here)
* }
* ao2_iterator_destroy(&iterator);
* \endcode

*/

#define RPT_LIST_TRAVERSE(container, var, iterator) \
	(iterator) = (container) ? ao2_iterator_init((container), 0) : (struct ao2_iterator) { 0 }; \
	for (; ((var) = ao2_iterator_next(&(iterator))); ao2_ref((var), -1))
