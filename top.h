/*
 * top.h - Header file:         stock hunter or show Linux processes 
 *
 * Copyright (c) 2015-2016, by: Sun Qijiang 
 *    All rights reserved.  
 *
 */
#ifndef _IStock_top
#define _IStock_top

#include "../proc/readproc.h"

        /* Defines represented in configure.ac ----------------------------- */
//#define BOOST_PERCNT              /* enable extra precision for two % fields */
//#define NOBOOST_MEMS              /* disable extra precision for mem fields  */
//#define NUMA_DISABLE              /* disable summary area NUMA/Nodes display */
//#define OOMEM_ENABLE              /* enable the SuSE out-of-memory additions */
//#define SIGNALS_LESS              /* favor reduced signal load over response */

        /* Development/Debugging defines ----------------------------------- */
//#define AT_END_OF_JOB_REPORT_HASH /* report on hash specifics, at end-of-job */
#define AT_END_OF_JOB_REPORT_STD  /* report on misc stuff, at end-of-job     */
//#define CASEUP_HEXES              /* show any hex values in upper case       */
//#define CASEUP_SUFIX              /* show time/mem/cnts suffix in upper case */
//#define CPU_ZEROTICS              /* tolerate few tics when cpu off vs. idle */
//#define EQUCOLHDRYES              /* yes, do equalize column header lengths  */
//#define INSP_JUSTNOT              /* don't smooth unprintable right margins  */
//#define INSP_OFFDEMO              /* disable demo screens, issue msg instead */
//#define INSP_SAVEBUF              /* preserve 'Insp_buf' contents in a file  */
//#define INSP_SLIDE_1              /* when scrolling left/right don't move 8  */
#define DISABLE_HISTORY_HASH        /* use BOTH qsort+bsrch vs. hashing scheme */
//#define OFF_SCROLLBK              /* disable tty emulators scrollback buffer */
//#define OFF_STDIOLBF              /* disable our own stdout _IOFBF override  */
//#define PRETEND2_5_X              /* pretend we're linux 2.5.x (for IO-wait) */
//#define PRETEND8CPUS              /* pretend we're smp with 8 ticsers (sic)  */
//#define PRETENDNOCAP              /* use a terminal without essential caps   */
//#define PRETEND_NUMA              /* pretend we've got some linux NUMA Nodes */
//#define RCFILE_NOERR              /* rcfile errs silently default, vs. fatal */
//#define RECALL_FIXED              /* don't reorder saved strings if recalled */
//#define RMAN_IGNORED              /* don't consider auto right margin glitch */
//#define SCROLLVAR_NO              /* disable intra-column horizontal scroll  */
//#define STRINGCASENO              /* case insenstive compare/locate versions */
//#define TERMIOS_ONLY              /* just limp along with native input only  */
//#define TREE_NORESET              /* sort keys do NOT force forest view OFF  */
//#define USE_X_COLHDR              /* emphasize header vs. whole col, for 'x' */
//#define VALIDATE_NLS              /* validate the integrity of all nls tbls  */


/*######  Notes, etc.  ###################################################*/

        /* The following convention is used to identify those areas where
           adaptations for hotplugging are to be found ...
              *** hotplug_acclimated ***
           ( hopefully libproc will also be supportive of our efforts ) */

        /* For introducing inaugural cgroup support, thanks to:
              Jan Gorig <jgorig@redhat.com> - April, 2011 */

        /* For the motivation and path to nls support, thanks to:
              Sami Kerola, <kerolasa@iki.fi> - December, 2011 */

        /* There are still some short strings that may yet be candidates
           for nls support inclusion.  They're identified with:
              // nls_maybe */

        /* For initiating the topic of potential % CPU distortions due to
           to kernel and/or cpu anomalies (see CPU_ZEROTICS), thanks to:
              Jaromir Capik, <jcapik@redhat.com> - February, 2012 */

        /* For the impetus and NUMA/Node prototype design, thanks to:
              Lance Shelton <LShelton@fusionio.com> - April, 2013 */

#ifdef PRETEND2_5_X
#define linux_version_code LINUX_VERSION(2,5,43)
#endif

   // pretend as if #define _GNU_SOURCE
char *strcasestr(const char *haystack, const char *needle);

#ifdef STRINGCASENO
#define STRSTR  strcasestr
#define STRCMP  strcasecmp
#else
#define STRSTR  strstr
#define STRCMP  strcmp
#endif


/*######  Some Miscellaneous constants  ##################################*/

        /* The default delay twix updates */
#define DEF_DELAY  3.0

        /* Length of time a message is displayed and the duration
           of a 'priming' wait during library startup (in microseconds) */
#define MSG_USLEEP  1250000
#define LIB_USLEEP  150000

        /* Specific process id monitoring support (command line only) */
#define MONPIDMAX  20

        /* Output override minimums (the -w switch and/or env vars) */
#define W_MIN_COL  3
#define W_MIN_ROW  3

        /* Miscellaneous buffers with liberal values and some other defines
           -- mostly just to pinpoint source code usage/dependancies */
#define SCREENMAX   512
   /* the above might seem pretty stingy, until you consider that with every
      field displayed the column header would be approximately 250 bytes
      -- so SCREENMAX provides for all fields plus a 250+ byte command line */
#define CAPBUFSIZ    32
#define CLRBUFSIZ    64
#define PFLAGSSIZ    88
#define SMLBUFSIZ   128
#define MEDBUFSIZ   256
#define LRGBUFSIZ   512
#define OURPATHSZ  1024
#define BIGBUFSIZ  2048
   /* in addition to the actual display data, our row might have to accommodate
      many termcap/color transitions - these definitions ensure we have room */
#define ROWMINSIZ  ( SCREENMAX +  4 * (CAPBUFSIZ + CLRBUFSIZ) )
#define ROWMAXSIZ  ( SCREENMAX + 16 * (CAPBUFSIZ + CLRBUFSIZ) )
   // minimum size guarantee for dynamically acquired 'readfile' buffer
#define READMINSZ  2048
   // size of preallocated search string buffers, same as ioline()
#define FNDBUFSIZ  MEDBUFSIZ


   // space between task fields/columns
#define COLPADSTR   " "
#define COLPADSIZ   ( sizeof(COLPADSTR) - 1 )
   // continuation ch when field/column truncated
#define COLPLUSCH   '+'

   // support for keyboard stuff (cursor motion keystrokes, mostly)
#define kbd_ESC    '\033'
#define kbd_SPACE  ' '
#define kbd_ENTER  '\n'
#define kbd_UP     129
#define kbd_DOWN   130
#define kbd_LEFT   131
#define kbd_RIGHT  132
#define kbd_PGUP   133
#define kbd_PGDN   134
#define kbd_HOME   135
#define kbd_END    136
#define kbd_BKSP   137
#define kbd_INS    138
#define kbd_DEL    139
#define kbd_CtrlO  '\017'

// asicc code to keyboard scan code
#define ctrl_a     'a'&0x1f
#define ctrl_b     'b'&0x1f
#define ctrl_c     'c'&0x1f
#define ctrl_d     'd'&0x1f
#define ctrl_e     'e'&0x1f
#define ctrl_f     'f'&0x1f
#define ctrl_g     'g'&0x1f
#define ctrl_h     'h'&0x1f
#define ctrl_i     'i'&0x1f
#define ctrl_j     'j'&0x1f
#define ctrl_k     'k'&0x1f
#define ctrl_l     'l'&0x1f
#define ctrl_m     'm'&0x1f
#define ctrl_n     'n'&0x1f
#define ctrl_o     'o'&0x1f
#define ctrl_p     'p'&0x1f
#define ctrl_q     'q'&0x1f
#define ctrl_r     'r'&0x1f
#define ctrl_s     's'&0x1f
#define ctrl_t     't'&0x1f
#define ctrl_u     'u'&0x1f
#define ctrl_v     'v'&0x1f
#define ctrl_w     'w'&0x1f
#define ctrl_x     'x'&0x1f
#define ctrl_y     'y'&0x1f
#define ctrl_z     'z'&0x1f

        /* Special value in Pseudo_row to force an additional procs refresh
           -- used at startup and for task/thread mode transitions */
#define PROC_XTRA  -1

#ifndef CPU_ZEROTICS
        /* This is the % used in establishing the tics threshold below
           which a cpu is treated as 'idle' rather than displaying
           misleading state percentages */
#define TICS_EDGE  20
#endif


/* #####  Enum's and Typedef's  ############################################ */

        /* Flags for each possible field (and then some) --
           these MUST be kept in sync with the Field_t Fieldstab[] array !! */
enum pflag 
{
#ifdef BUILD_4_STOCK
   S_INDEX = 0,
   S_StockName,
   S_StockID,
   S_OPEN,
   S_CLOSE_YESTODAY,
   S_MIN,
   S_MAX,
   S_CURRENT,
   S_VOLUME,
   S_RMB,
   S_PERCENT,
   S_TURNOVER_RATE,
   S_LIUTONGA,
   S_VALUE_LIUTONGA,
   S_DAY,
   S_TIME,
   S_VRATIO,
   S_AVERAGE_PRICE,
   S_SWING,
   S_OUTER,
   S_INNER,
   S_PE_RATIO,
   S_MA5,
   S_MA10,
   S_MA20,
   S_MA30,
   S_MA60,
   S_DDR,
   S_MNR,
   S_TOTALS,
   S_VALUE_TOTALS,
   S_SWEIGHT,
   S_DOWNSHADOW,
   P_PID,
#else
   P_PID = 0,
#endif
   P_PPD,
   P_UED, P_UEN, P_URD, P_URN, P_USD, P_USN,
   P_GID, P_GRP, P_PGD, P_TTY, P_TPG, P_SID,
   P_PRI, P_NCE, P_THD,
   P_CPN, P_CPU, P_TME, P_TM2,
   P_MEM, P_VRT, P_SWP, P_RES, P_COD, P_DAT, P_SHR,
   P_FL1, P_FL2, P_DRT,
   P_STA, P_CMD, P_WCH, P_FLG, P_CGR,
   P_SGD, P_SGN, P_TGD,
#ifdef OOMEM_ENABLE
   P_OOA, P_OOM,
#endif
   P_ENV,
   P_FV1, P_FV2,
   P_USE,
   P_NS1, 
   P_NS2, 
   P_NS3, 
   P_NS4, 
   P_NS5, 
   P_NS6,
#ifdef USE_X_COLHDR
   // not really pflags, used with tbl indexing
   P_MAXPFLAGS
#else
   // not really pflags, used with tbl indexing & col highlighting
   P_MAXPFLAGS, X_XON, X_XOFF
#endif
};

        /* The scaling 'target' used with memory fields */
enum scale_enum 
{
   SK_Kb, SK_Mb, SK_Gb, SK_Tb, SK_Pb, SK_Eb, SK_SENTINEL
};

#ifdef BUILD_4_STOCK
enum volume_enum
{
   VE_gu = 0, VE_shou
};

enum rmb_enum
{
   rmb_yuan = 0, rmb_wan
};
#endif
        /* Used to manipulate (and document) the Frames_signal states */
enum resize_states 
{
   BREAK_off = 0, BREAK_keyboard, BREAK_signal
};

        /* These typedefs attempt to ensure consistent 'ticks' handling */
typedef unsigned long long TIC_t;
typedef          long long SIC_t;

        /* Sort support, callback function signature */
typedef int (*QFunc_Sort_Cb)(const void *, const void *);

        /* This structure consolidates the information that's used
           in a variety of display roles. */
typedef struct Field_t 
{
   int           	 width;         // field width, if applicable
   int           	 scale;         // scaled target, if applicable
   const int    	 align;         // the default column alignment flag
   const QFunc_Sort_Cb   sort;          // sort function
   const int    	 fillflag;      // PROC_FILLxxx flag(s) needed by this field
} Field_t;

#ifdef DISABLE_HISTORY_HASH
        /* This structure supports 'history' processing and records the
           bare minimum of needed information from one frame to the next --
           we don't calc and save data that goes unused like the old top. */
typedef struct HISTORY_t 
{
   TIC_t tics;                  // last frame's tics count
   unsigned long maj, min;      // last frame's maj/min_flt counts
   int pid;                     // record 'key'
} HISTORY_t;
#else
        /* This structure supports 'history' processing and records the
           bare minimum of needed information from one frame to the next --
           we don't calc and save data that goes unused like the old top nor
           do we incure the overhead of sorting to support a binary search
           (or worse, a friggin' for loop) when retrieval is necessary! */
typedef struct HISTORY_t 
{
   TIC_t tics;                  // last frame's tics count
   unsigned long maj, min;      // last frame's maj/min_flt counts
   int pid;                     // record 'key'
   int lnk;                     // next on hash chain
} HISTORY_t;
#endif

        /* These 2 structures store a frame's cpu tics used in history
           calculations.  They exist primarily for SMP support but serve
           all environments. */
typedef struct CT_t 
{
   /* other kernels: u == user/us, n == nice/ni, s == system/sy, i == idle/id
      2.5.41 kernel: w == IO-wait/wa (io wait time)
      2.6.0  kernel: x == hi (hardware irq time), y == si (software irq time)
      2.6.11 kernel: z == st (virtual steal time) */
   TIC_t u, n, s, i, w, x, y, z;  // as represented in /proc/stat
#ifndef CPU_ZEROTICS
   SIC_t tot;                     // total from /proc/stat line 1
#endif
} CT_t;

typedef struct CPU_t 
{
   CT_t cur;                      // current frame's cpu tics
   CT_t sav;                      // prior frame's cpu tics
#ifndef CPU_ZEROTICS
   SIC_t edge;                    // tics adjustment threshold boundary
#endif
   int id;                        // the cpu id number (0 - nn)
#ifndef NUMA_DISABLE
   int node;                      // the numa node it belongs to
#endif
} CPU_t;

        /* /////////////////////////////////////////////////////////////// */
        /* Special Section: multiple windows/field groups  --------------- */
        /* ( kind of a header within a header: constants, types & macros ) */

#define CAPTABMAX  9             /* max entries in each win's caps table   */
#define GROUPSMAX  4             /* the max number of simultaneous windows */
#define WINNAMSIZ  4             /* size of RCWindow_t winname buf (incl '\0')  */
#define GRPNAMSIZ  WINNAMSIZ+2   /* window's name + number as in: '#:...'  */

#ifdef BUILD_4_STOCK
  #define FILTER_PREFIX_SIZE  8          
#endif
        /* The Persistent 'Mode' flags!
           These are preserved in the rc file, as a single integer and the
           letter shown is the corresponding 'command' toggle */
        // 'View_' flags affect the summary (minimum), taken from 'Curwin'
#define View_CPUSUM  0x008000     // '1' - show combined cpu stats (vs. each)
#define View_CPUNOD  0x400000     // '2' - show numa node cpu stats ('3' also)
#define View_LOADAV  0x004000     // 'l' - display load avg and uptime summary
#define View_TASKS   0x002000     // 't' - display task/cpu(s) states summary
#define View_MEMORY  0x001000     // 'm' - display memory summary
#define View_NOBOLD  0x000008     // 'B' - disable 'bold' attribute globally
#define View_SCROLL  0x080000     // 'C' - enable coordinates msg w/ scrolling
        // 'Show_' & 'Qsrt_' flags are for task display in a visible window
#define Show_COLORS  0x000800     // 'z' - show in color (vs. mono)
#define Show_Highlight_BOLD  0x000400     // 'b' - rows and/or cols bold (vs. reverse)
#define Show_Highlight_COLS  0x000200     // 'x' - show sort column emphasized
#define Show_Highlight_ROWS  0x000100     // 'y' - show running tasks highlighted
#define Show_CMDLIN  0x000080     // 'c' - show cmdline vs. name
#define Show_CTIMES  0x000040     // 'S' - show times as cumulative
#define Show_IDLEPS  0x000020     // 'i' - show idle processes (all tasks)
#define Show_TASKON  0x000010     // '-' - tasks showable when Mode_altscreen
#define Show_FOREST  0x000002     // 'V' - show cmd/cmdlines with ascii art
#define Qsrt_NORMAL  0x000004     // 'R' - reversed column sort (high to low)
#define Show_JRSTRS  0x040000     // 'j' - right justify "string" data cols
#define Show_JRNUMS  0x020000     // 'J' - right justify "numeric" data cols
        // these flag(s) have no command as such - they're for internal use
#define INFINDS_xxx  0x010000     // build rows for find_string, not display
#define EQUWINS_xxx  0x000001     // rebalance all wins & tasks (off i,n,u/U)
#ifndef USE_X_COLHDR
#define NOHISEL_xxx  0x200000     // restrict Show_Highlight_COLS for osel temporarily
#define NOHIFND_xxx  0x100000     // restrict Show_Highlight_COLS for find temporarily
#endif

#ifdef BUILD_4_STOCK
#define STOCK_CURRENT_ROW_HIGHLIGHT  0x01000000     // 
#endif

#ifdef BUILD_4_STOCK

#define DEF_WINFLGS ( View_CPUSUM | View_LOADAV | View_TASKS | View_NOBOLD \
   | Show_COLORS | Show_Highlight_BOLD | Show_Highlight_COLS | Show_CTIMES \
   | Show_IDLEPS | Show_TASKON | Qsrt_NORMAL | STOCK_CURRENT_ROW_HIGHLIGHT )

#define IFILTER_WINFLGS ( View_CPUSUM | View_LOADAV | View_TASKS | View_NOBOLD \
   | Show_COLORS | Show_Highlight_BOLD | Show_Highlight_COLS | Show_CTIMES \
   | Show_TASKON | Qsrt_NORMAL | STOCK_CURRENT_ROW_HIGHLIGHT )

#else
        // Default flags if there's no rcfile to provide user customizations
#define DEF_WINFLGS ( View_LOADAV | View_TASKS | View_CPUSUM | View_MEMORY \
   | Show_Highlight_BOLD | Show_Highlight_ROWS | Show_IDLEPS | Show_TASKON | Show_JRNUMS \
   | Qsrt_NORMAL )

#endif

extern FILE *LogPtr;
        /* These are used to direct wins_reflag */
enum reflag_enum 
{
   Flags_TOG, Flags_SET, Flags_OFF
};

        /* These are used to direct win_warn */
enum warn_enum 
{
   Warn_ALT, Warn_VIZ
};

        /* This type helps support both a window AND the rcfile */
typedef struct RCWindow_t 
{                               // the 'window' portion of an rcfile
   int    sortindex,            // sort field, represented as a procflag
          winflags,             // 'view', 'show' and 'sort' mode flags
          maxtasks,             // user requested maximum, 0 equals all
          summcolor,            // color num used in summ info
          msgscolor,            //        "       in msgs/pmts
          headcolor,            //        "       in cols head
          taskcolor;            //        "       in task rows
   char   winname[WINNAMSIZ],   // window name, user changeable
          fieldscur[PFLAGSSIZ]; // fields displayed and ordered
#ifdef BUILD_4_STOCK
   char   filter_prefix[FILTER_PREFIX_SIZE];
   int    ustype;
#endif
} RCWindow_t;

        /* This represents the complete rcfile */
typedef struct RCFull_t 
{
   char   id;                   // rcfile version id
   int    mode_altscreen;       // 'A' - Alt display mode (multi task windows)
   int    mode_irixps;          // 'I' - Irix vs. Solaris mode (SMP-only)
   float  delay_time;           // 'd'/'s' - How long to sleep twixt updates
   int    win_index;            // Curwin, as index
   RCWindow_t  win [GROUPSMAX]; // a 'WIN_t.rc' for each window
   int    fixed_widest;         // 'X' - wider non-scalable col addition
   int    summary_unit_scale;   // 'E' - scaling of summary memory values
   int    task_unit_scale;      // 'e' - scaling of process memory values
   int    zero_suppress;        // '0' - suppress scaled zeros toggle
} RCFull_t;

        /* This structure stores configurable information for each window.
           By expending a little effort in its creation and user requested
           maintenance, the only real additional per frame cost of having
           windows is an extra sort -- but that's just on pointers! */
typedef struct WIN_t 
{
   unsigned char  pflagsall  [PFLAGSSIZ];  // all 'active/on' fieldscur, as enum
   unsigned char  proc_flags [PFLAGSSIZ];  // fieldscur subset, as enum
   RCWindow_t  rc;                         // stuff that gets saved in the rcfile
   int    winnum,          		   // a window's number (array pos + 1)
          winlines,        		   // current task window's rows (volatile)
          max_displayable_pflags, 	   // number of displayed proc_flags ("on" in fieldscur)
          totalpflags,               	   // total of displayable proc_flags in pflagsall array
          begin_column_flag,    	   // scrolled beginning pos into pflagsall array
          end_column_flag,      	   // scrolled ending pos into pflagsall array
          begintask,         		   // scrolled beginning pos into Frame_maxtask

#ifdef BUILD_4_STOCK
          current_index,
          max_matched_row,
          ups,
          draws,
          downs,
          last_matched_index,
	  ifilter,                         // for the first time check Show_IDLEPS 
          ufilter,
#endif

#ifndef SCROLLVAR_NO
          variable_column_begin,	   // scrolled position within variable width col
#endif
          variable_column_size, 	   // max length of variable width column(s)
          usrseluid,       		   // validated uid for 'u/U' user selection
          user_select_type,    		   // the basis for matching above uid
          usrselflg,       		   // flag denoting include/exclude matches
          hdrcaplen;       		   // column header xtra caps len, if any
#ifdef BUILD_4_STOCK
   int    stock_select_flags;
   char   filter_stock_prefix[FILTER_PREFIX_SIZE];
#endif

   char   capclr_sum [CLRBUFSIZ],      // terminfo strings built from
          capclr_msg [CLRBUFSIZ],      //   RCWindow_t colors (& rebuilt too),
          capclr_pmt [CLRBUFSIZ],      //   but NO recurring costs !
          capclr_hdr [CLRBUFSIZ],      //   note: sum, msg and pmt strs
          capclr_rowhigh [CLRBUFSIZ],  //         are only used when this
          capclr_rownorm [CLRBUFSIZ],  //         window is the 'Curwin'!
          cap_bold [CAPBUFSIZ],        // support for View_NOBOLD toggle
          grpname [GRPNAMSIZ];         // window number:name, printable
#ifdef USE_X_COLHDR
   char   columnheader [ROWMINSIZ];    // column headings for proc_flags
#else
   char   columnheader [ROWMINSIZ];    // column headings for proc_flags
   //char   columnheader [SCREENMAX];  // column headings for proc_flags
#endif
   char  *captab [CAPTABMAX];          // captab needed by show_special()
   struct osel_s *osel_1st;            // other selection criteria anchor
   int    osel_tot;                    // total of other selection criteria
   char  *osel_prt;                    // other stuff printable as status line
   char  *findstr;                     // window's current/active search string
   int    findlen;                     // above's strlen, without call overhead
   proc_t **ppt;                       // this window's proc_t ptr array
   struct WIN_t *next,                 // next window in window stack
                *prev;                 // prior window in window stack
} WIN_t;

        // Used to test/manipulate the window flags
#define CHKwinflags(q,f)    ((int)((q)->rc.winflags & (f)))
#define TOGGLEwinflags(q,f) ((q)->rc.winflags ^=  (f))
#define SETwinflags(q,f)    ((q)->rc.winflags |=  (f))
#define OFFwinflags(q,f)    ((q)->rc.winflags &= ~(f))
#define ALTCHKwinflags      ((Rc.mode_altscreen ? 1 : win_warn(Warn_ALT)))
#define VIZISw(q)    ((!Rc.mode_altscreen || CHKwinflags(q,Show_TASKON)))
#define VIZCHKwinflags(q)   ((VIZISw(q)) ? 1 : win_warn(Warn_VIZ))
#define VIZTOGGLEwinflags(q,f) ((VIZISw(q)) ? TOGGLEwinflags(q,(f)) : win_warn(Warn_VIZ))

        // Used to test/manipulte fieldscur values
#define FieldOn(c)     ((c) |= 0x80)
#define FieldGetValue(q,i)  ((unsigned char)((q)->rc.fieldscur[i] & 0x7f) - FLD_OFFSET)
#define FieldToggle(q,i)  ((q)->rc.fieldscur[i] ^= 0x80)
#define FieldIsVisible(q,i)  ((q)->rc.fieldscur[i] &  0x80)
#define ENUchk(w,E)  (NULL != strchr((w)->rc.fieldscur, (E + FLD_OFFSET) | 0x80))
#define ENUset(w,E)  do { char *t; \
      if ((t = strchr((w)->rc.fieldscur, E + FLD_OFFSET))) \
         *t = (E + FLD_OFFSET) | 0x80; \
   /* else fieldscur char already has high bit on! */ \
   } while (0)
#define ENUviz(w,E)  (NULL != memchr((w)->proc_flags, E, (w)->max_displayable_pflags))
#define ENUpos(w,E)  ((int)((unsigned char*)memchr((w)->pflagsall, E, (w)->totalpflags) - (w)->pflagsall))

        // Support for variable width columns (and potentially scrolling too)
#define IsVariableColumn(E)    (-1 == Fieldstab[E].width)
#ifndef SCROLLVAR_NO
#ifdef USE_X_COLHDR
#define VARright(w)  (1 == w->max_displayable_pflags && IsVariableColumn(w->proc_flags[0]))
#else
#define VARright(w) ((1 == w->max_displayable_pflags && IsVariableColumn(w->proc_flags[0])) || \
                     (3 == w->max_displayable_pflags && X_XON == w->proc_flags[0] && IsVariableColumn(w->proc_flags[1])))
#endif
#define VARleft(w)   (w->variable_column_begin && VARright(w))
#define SCROLLAMT    8
#endif

        /* Special Section: end ------------------------------------------ */
        /* /////////////////////////////////////////////////////////////// */


/*######  Some Miscellaneous Macro definitions  ##########################*/

        /* Yield table size as 'int' */
#define MAXTBL(t)  ((int)(sizeof(t) / sizeof(t[0])))

        /* A null-terminating strncpy, assuming strlcpy is not available.
           ( and assuming callers don't need the string length returned ) */
#define STRLCPY(dst,src) { strncpy(dst, src, sizeof(dst)); dst[sizeof(dst) - 1] = '\0'; }

        /* Used to clear all or part of our Pseudo_screen */
#define PSU_CLEARSCREEN(y) memset(&Pseudo_screen[ROWMAXSIZ*y], '\0', Pseudo_size-(ROWMAXSIZ*y))

        /* Used as return arguments in *some* of the sort callbacks */
#define SORT_lt  ( Frame_srtflg > 0 ?  1 : -1 )
#define SORT_gt  ( Frame_srtflg > 0 ? -1 :  1 )
#define SORT_eq  0

        /* Used to create *most* of the sort callback functions
           note: some of the callbacks are NOT your father's callbacks, they're
                 highly optimized to save them ol' precious cycles! */
#define SCB_NAME(f) sort_P_ ## f
#define SCB_NUM1(f,n) \
   static int SCB_NAME(f) (const proc_t **P, const proc_t **Q) { \
      if ( (*P)->n < (*Q)->n ) return SORT_lt; \
      if ( (*P)->n > (*Q)->n ) return SORT_gt; \
      return SORT_eq; }
#define SCB_NUM2(f,n1,n2) \
   static int SCB_NAME(f) (const proc_t **P, const proc_t **Q) { \
      if ( ((*P)->n1+(*P)->n2) < ((*Q)->n1+(*Q)->n2) ) return SORT_lt; \
      if ( ((*P)->n1+(*P)->n2) > ((*Q)->n1+(*Q)->n2) ) return SORT_gt; \
      return SORT_eq; }
#define SCB_NUMx(f,n) \
   static int SCB_NAME(f) (const proc_t **P, const proc_t **Q) { \
      return Frame_srtflg * ( (*Q)->n - (*P)->n ); }
#define SCB_STRS(f,s) \
   static int SCB_NAME(f) (const proc_t **P, const proc_t **Q) { \
      if (!(*P)->s || !(*Q)->s) return SORT_eq; \
      return Frame_srtflg * STRCMP((*Q)->s, (*P)->s); }
#define SCB_STRV(f,b,v,s) \
   static int SCB_NAME(f) (const proc_t **P, const proc_t **Q) { \
      if (b) { \
         if (!(*P)->v || !(*Q)->v) return SORT_eq; \
         return Frame_srtflg * STRCMP((*Q)->v[0], (*P)->v[0]); } \
      return Frame_srtflg * STRCMP((*Q)->s, (*P)->s); }
#define SCB_STRX(f,s) \
   int strverscmp(const char *s1, const char *s2); \
   static int SCB_NAME(f) (const proc_t **P, const proc_t **Q) { \
      if (!(*P)->s || !(*Q)->s) return SORT_eq; \
      return Frame_srtflg * strverscmp((*Q)->s, (*P)->s); }

/*
 * The following three macros are used to 'inline' those portions of the
 * display process involved in formatting, while protecting against any
 * potential embedded 'millesecond delay' escape sequences.
 */
        /**  PUTT - Put to Tty (used in many places)
               . for temporary, possibly interactive, 'replacement' output
               . may contain ANY valid terminfo escape sequences
               . need NOT represent an entire screen row */
#define PUTT(fmt,arg...) do { \
      char _str[ROWMAXSIZ]; \
      snprintf(_str, sizeof(_str), fmt, ## arg); \
      putp(_str); \
   } while (0)

        /**  PUFF - Put for Frame (used in only 3 places)
               . for more permanent frame-oriented 'update' output
               . may NOT contain cursor motion terminfo escapes
               . assumed to represent a complete screen ROW
               . subject to optimization, thus MAY be discarded */
#define PUFF(fmt,arg...) do { \
      char _str[ROWMAXSIZ], *_eol; \
      _eol = _str + snprintf(_str, sizeof(_str), fmt, ## arg); \
      if (Batch) { \
         while (*(--_eol) == ' '); *(++_eol) = '\0'; putp(_str); } \
      else { \
         char *_ptr = &Pseudo_screen[Pseudo_row * ROWMAXSIZ]; \
         if (Pseudo_row + 1 < Screen_rows) ++Pseudo_row; \
         if (!strcmp(_ptr, _str)) putp("\n"); \
         else { \
            strcpy(_ptr, _str); \
            putp(_ptr); } } \
   } while (0)

        /**  POOF - Pulled Out of Frame (used in only 1 place)
               . for output that is/was sent directly to the terminal
                 but would otherwise have been counted as a Pseudo_row */
#define POOF(str,cap) do { \
      putp(str); putp(cap); \
      Pseudo_screen[Pseudo_row * ROWMAXSIZ] = '\0'; \
      if (Pseudo_row + 1 < Screen_rows) ++Pseudo_row; \
   } while (0)

        /* Orderly end, with any sort of message - see fmtmk */
#define debug_END(s) { \
           void error_exit (const char *); \
           fputs(Cap_clr_scr, stdout); \
           error_exit(s); \
        }

        /* A poor man's breakpoint, if he's too lazy to learn gdb */
#define its_YOUR_fault { *((char *)0) = '!'; }


/*######  Display Support *Data*  ########################################*/
/*######  Some Display Support *Data*  ###################################*/
/*      ( see module top_nls.c for the nls translatable data ) */

        /* Configuration files support */
#define SYS_RCFILESPEC  "/etc/toprc"
#define RCF_EYECATCHER  "Config File (Linux processes with windows)\n"
#define RCF_VERSION_ID  'h'
#define RCF_PLUS_H      "\\]^_`abcdefghijklmnopqrstuvw"

        /* The default fields displayed and their order, if nothing is
           specified by the loser, oops user.
           note: any *contiguous* ascii sequence can serve as fieldscur
                 characters as long as the initial value is coordinated
                 with that specified for FLD_OFFSET
           ( we're providing for up to 70 fields currently, )
           ( with just one escaped value, the '\' character ) */
#define FLD_OFFSET  '%' //0x25=00100101
   //   seq_fields  "%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghij"
//#define DEF_FIELDS  "¥¨³´»½ÀÄ·º¹Å&')*+,-./012568<>?ABCFGHIJKLMNOPQRSTUVWXYZ[" RCF_PLUS_H
#ifdef BUILD_4_STOCK
#define DEF_FIELDS  "¥¦§¬¶°µ¯²·º¸¹­®±³¨©´«ª»¼½¾¿ÀÁÂÃÄÅFGHIJKLMNOPQRSTUVWXYZ[" RCF_PLUS_H
#define DEF_FIELDS2 "¥¦§¬¶°µ¯²·º¸¹­®±³¨©´«ª»¼½¾¿ÀÁÂÃÄÅFGHIJKLMNOPQRSTUVWXYZ[" RCF_PLUS_H
#define DEF_FIELDS3 "¥¦§¬¶°µ¯²·º¸¹­®±³¨©´«ª»¼½¾¿ÀÁÂÃÄÅFGHIJKLMNOPQRSTUVWXYZ[" RCF_PLUS_H
//#define DEF_FIELDS4 "¥¦§¬¶°µ¯Ä²·º¸¹­®±³¨©´«ª»¼½¾¿ÀÁÂÃÅFGHIJKLMNOPQRSTUVWXYZ[" RCF_PLUS_H
#define DEF_FIELDS4 "¥¦§¬¶°µ¯ÄÅ³²·º¸¹­®±¨©´«ª»¼½¾¿ÀÁÂÃFGHIJKLMNOPQRSTUVWXYZ[" RCF_PLUS_H
#else
#define DEF_FIELDS  "¥¦§¨©ª«¬­®¯°±²³´µ¶·¸¹º»¼=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[" RCF_PLUS_H
#endif
#define LEN_OF_FIELDSTR sizeof(DEF_FIELDS)
        /* Pre-configured windows/field groups */
#define JOB_FIELDS  "¥¦¹·º³´Ä»¼½§Å()*+,-./012568>?@ABCFGHIJKLMNOPQRSTUVWXYZ[" RCF_PLUS_H
#define MEM_FIELDS  "¥º»¼½¾¿ÀÁÃÄ³´·Å&'()*+,-./0125689BFGHIJKLMNOPQRSTUVWXYZ[" RCF_PLUS_H
#define USR_FIELDS  "¥¦§¨ª°¹·ºÄÅ)+,-./1234568;<=>?@ABCFGHIJKLMNOPQRSTUVWXYZ[" RCF_PLUS_H
#ifdef OOMEM_ENABLE
        // the suse old top fields ( 'a'-'z' + '{|' ) in positions 0-27
        // ( the extra chars above represent the 'off' state )
#define CVT_FIELDS  "%&*'(-0346789:;<=>?@ACDEFGML)+,./125BHIJKNOPQRSTUVWXYZ["
#define CVT_FLDMAX  28
#else
        // other old top fields ( 'a'-'z' ) in positions 0-25
#define CVT_FIELDS  "%&*'(-0346789:;<=>?@ACDEFG)+,./125BHIJKLMNOPQRSTUVWXYZ["
#define CVT_FLDMAX  26
#endif
        // defined in ncurses.h
        /* colors */
        /*
                 #define COLOR_BLACK     0
                 #define COLOR_RED       1
                 #define COLOR_GREEN     2
                 #define COLOR_YELLOW    3
                 #define COLOR_BLUE      4
                 #define COLOR_MAGENTA   5
                 #define COLOR_CYAN      6
                 #define COLOR_WHITE     7
        */
#ifdef BUILD_4_STOCK
        /* The default values for the local config file */
#define DEF_RCFILE { \
   RCF_VERSION_ID, 0, 1, DEF_DELAY, 0, { \
   { S_PERCENT, DEF_WINFLGS, 0, \
      COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_MAGENTA, \
      "All", DEF_FIELDS, "N", 0}, \
   { S_VRATIO, DEF_WINFLGS, 0, \
      COLOR_RED, COLOR_GREEN, COLOR_GREEN, COLOR_CYAN, \
      "Job", DEF_FIELDS2, "6018", 'U'}, \
   { S_TURNOVER_RATE, DEF_WINFLGS, 0, \
      COLOR_YELLOW, COLOR_GREEN, COLOR_MAGENTA, COLOR_BLUE, \
      "Mem", DEF_FIELDS3, "0028", 'u'}, \
   { S_SWEIGHT, IFILTER_WINFLGS, 0, \
      COLOR_RED, COLOR_GREEN, COLOR_RED, COLOR_BLUE, \
      "Usr", DEF_FIELDS4, "N", 0} \
   }, 0, SK_Kb, SK_Kb, 0 }
#else
        /* The default values for the local config file */
#define DEF_RCFILE { \
   RCF_VERSION_ID, 0, 1, DEF_DELAY, 0, { \
   { P_CPU, DEF_WINFLGS, 0, \
      COLOR_RED, COLOR_RED, COLOR_YELLOW, COLOR_RED, \
      "Def", DEF_FIELDS }, \
   { P_PID, DEF_WINFLGS, 0, \
      COLOR_CYAN, COLOR_CYAN, COLOR_WHITE, COLOR_CYAN, \
      "Job", JOB_FIELDS }, \
   { P_MEM, DEF_WINFLGS, 0, \
      COLOR_MAGENTA, COLOR_MAGENTA, COLOR_BLUE, COLOR_MAGENTA, \
      "Mem", MEM_FIELDS }, \
   { P_UEN, DEF_WINFLGS, 0, \
      COLOR_YELLOW, COLOR_YELLOW, COLOR_GREEN, COLOR_YELLOW, \
      "Usr", USR_FIELDS } \
   }, 0, SK_Kb, SK_Kb, 0 }
#endif

        /* Summary Lines specially formatted string(s) --
           see 'show_special' for syntax details + other cautions. */
#define LOADAV_line  "%s -%s\n"
#define LOADAV_line_alt  "%s~6 -%s\n"


/*######  For Piece of mind  #############################################*/

        /* just sanity check(s)... */
#if defined(AT_END_OF_JOB_REPORT_HASH) && defined(DISABLE_HISTORY_HASH)
# error 'AT_END_OF_JOB_REPORT_HASH' conflicts with 'DISABLE_HISTORY_HASH'
#endif
#if defined(RECALL_FIXED) && defined(TERMIOS_ONLY)
# error 'RECALL_FIXED' conflicts with 'TERMIOS_ONLY'
#endif
#if defined(PRETEND_NUMA) && defined(NUMA_DISABLE)
# error 'PRETEND_NUMA' conflicts with 'NUMA_DISABLE'
#endif
#if (LRGBUFSIZ < SCREENMAX)
# error 'LRGBUFSIZ' must NOT be less than 'SCREENMAX'
#endif
#if defined(TERMIOS_ONLY)
# warning 'TERMIOS_ONLY' disables input recall and makes man doc incorrect
#endif


/*######  Some Prototypes (ha!)  #########################################*/

   /* These 'prototypes' are here exclusively for documentation purposes. */
   /* ( see the find_string function for the one true required protoype ) */
/*------  Sort callbacks  ------------------------------------------------*/
/*        for each possible field, in the form of:                        */
/*static int           sort_P_XXX (const proc_t **P, const proc_t **Q);     */
/*------  Tiny useful routine(s)  ----------------------------------------*/
//static const char   *fmtmk (const char *fmts, ...);
//static inline char  *scat (char *dst, const char *src);
//static const char   *tg2 (int x, int y);
/*------  Exit/Interrput routines  ---------------------------------------*/
//static void          at_eoj (void);
//static void          bye_bye (const char *str);
//static void          error_exit (const char *str);
//static void          library_err (const char *fmts, ...);
//static void          sig_abexit (int sig);
//static void          sig_endpgm (int dont_care_sig);
//static void          sig_paused (int dont_care_sig);
//static void          sig_resize (int dont_care_sig);
/*------  Misc Color/Display support  ------------------------------------*/
//static void          capsmk (WIN_t *q);
//static void          show_msg (const char *str);
//static int           show_pmt (const char *str);
//static void          show_special (int interact, const char *glob);
//static void          updt_scroll_msg (void);
/*------  Low Level Memory/Keyboard/File I/O support  --------------------*/
//static void         *alloc_c (size_t num);
//static void         *alloc_r (void *ptr, size_t num);
//static char         *alloc_s (const char *str);
//static inline int    ioa (struct timespec *ts);
//static int           ioch (int ech, char *buf, unsigned cnt);
//static int           iokey (int action);
//static char         *ioline (const char *prompt);
//static int           readfile (FILE *fp, char **baddr, size_t *bsize, size_t *bread);
/*------  Small Utility routines  ----------------------------------------*/
//static float         get_float (const char *prompt);
//static int           get_int (const char *prompt);
//static inline const char *hex_make (KLONG num, int noz);
//static void          osel_clear (WIN_t *q);
//static inline int    osel_matched (const WIN_t *q, unsigned char enu, const char *str);
//static const char   *user_certify (WIN_t *q, const char *str, char typ);
//static inline int    user_matched (const WIN_t *q, const proc_t *p);
/*------  Basic Formatting support  --------------------------------------*/
//static inline const char *justify_pad (const char *str, int width, int justr);
//static inline const char *make_chr (const char ch, int width, int justr);
//static inline const char *make_num (long num, int width, int justr, int col);
//static inline const char *make_str (const char *str, int width, int justr, int col);
//static const char   *scale_mem (int target, unsigned long num, int width, int justr);
//static const char   *scale_num (unsigned long num, int width, int justr);
//static const char   *scale_pcnt (float num, int width, int justr);
//static const char   *scale_tics (TIC_t tics, int width, int justr);
/*------  Fields Management support  -------------------------------------*/
/*static Field_t       Fieldstab[] = { ... }                                */
//static void          adj_geometry (void);
//static void          build_headers (void);
//static void          calibrate_fields (void);
//static void          display_fields (int focus, int extend);
//static void          fields_utility (void);
//static inline void   widths_resize (void);
//static void          zap_fieldstab (void);
/*------  Library Interface  ---------------------------------------------*/
//static CPU_t        *cpus_refresh (CPU_t *cpus);
#ifdef DISABLE_HISTORY_HASH
//static inline HISTORY_t *history_bsearch (HISTORY_t *hst, int max, int pid);
#else
//static inline HISTORY_t *hstget (int pid);
//static inline void   hstput (unsigned idx);
#endif
//static void          procs_statistic (proc_t *p);
//static void          procs_refresh (void);
//static void          sysinfo_refresh (int forced);
/*------  Inspect Other Output  ------------------------------------------*/
//static void          insp_cnt_nl (void);
#ifndef INSP_OFFDEMO
//static void          insp_do_demo (char *fmts, int pid);
#endif
//static void          insp_do_file (char *fmts, int pid);
//static void          insp_do_pipe (char *fmts, int pid);
//static inline int    insp_find_ofs (int col, int row);
//static void          insp_find_str (int ch, int *col, int *row);
//static inline void   insp_make_row (int col, int row);
//static void          insp_show_pgs (int col, int row, int max);
//static int           insp_view_choice (proc_t *obj);
//static void          inspection_utility (int pid);
/*------  Startup routines  ----------------------------------------------*/
//static void          before (char *me);
//static int           config_cvt (WIN_t *q);
//static void          configs_read (void);
//static void          parse_args (char **args);
//static void          whack_terminal (void);
/*------  Windows/Field Groups support  ----------------------------------*/
//static void          win_names (WIN_t *q, const char *name);
//static void          win_reset (WIN_t *q);
//static WIN_t        *win_select (int ch);
//static int           win_warn (int what);
//static void          wins_clrhlp (WIN_t *q, int save);
//static void          wins_colors (void);
//static void          wins_reflag (int what, int flg);
//static void          wins_stage_1 (void);
//static void          wins_stage_2 (void);
/*------  Interactive Input Tertiary support  ----------------------------*/
//static inline int    find_ofs (const WIN_t *q, const char *buf);
//static void          find_string (int ch);
//static void          help_view (void);
//static void          other_selection (int ch);
//static void          write_rcfile (void);
/*------  Interactive Input Secondary support (do_key helpers)  ----------*/
//static void          keys_global (int ch);
//static void          keys_summary (int ch);
//static void          keys_task (int ch);
//static void          keys_window (int ch);
//static void          keys_xtra (int ch);
/*------  Forest View support  -------------------------------------------*/
//static void          forest_adds (const int self, const int level);
//static int           forest_based (const proc_t **x, const proc_t **y);
//static void          forest_create (WIN_t *q);
//static inline const char *forest_display (const WIN_t *q, const proc_t *p);
/*------  Main Screen routines  ------------------------------------------*/
//static void          do_key (int ch);
//static void          summary_hlp (CPU_t *cpu, const char *pfx);
//static void          summary_show (void);
//static const char   *task_show (const WIN_t *q, const proc_t *p);
//static int           window_show (WIN_t *q, int wmax);
/*------  Entry point plus two  ------------------------------------------*/
//static void          frame_hlp (int wix, int max);
//static void          frame_make (void);
//     int           main (int dont_care_argc, char **argv);

#endif /* _IStock_top */
