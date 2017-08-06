/*
 * top.c - Source file:         stock hunter or show Linux processes 
 *
 * Copyright (c) 2015-2016, by: Sun Qijiang 
 *    All rights reserved.      
 *                             
 */
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>

#include <ctype.h>
#include <curses.h>
#ifndef NUMA_DISABLE
#include <dlfcn.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <term.h>       // foul sob, defines all sorts of stuff...
#undef    tab
#undef    TTY
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <values.h>

#include "../include/fileutils.h"
#include "../include/nls.h"

#include "../proc/devname.h"
#include "../proc/procps.h"
#include "../proc/readproc.h"
#include "../proc/sig.h"
#include "../proc/sysinfo.h"
#include "../proc/version.h"
#include "../proc/wchan.h"
#include "../proc/whattime.h"

#include "top.h"
#include "top_nls.h"

#ifdef BUILD_4_STOCK
#endif

static int FACTOR = 1000;
/*
#include <stdio.h> 
#include <SDL/SDL.h> 
int testsdl() 
{ 
    //The images 
    SDL_Surface* hello = NULL; 
    SDL_Surface* screen = NULL; 
    SDL_Init( SDL_INIT_EVERYTHING ); 
    //Set up screen 
    screen = SDL_SetVideoMode( 640, 480, 32, SDL_SWSURFACE ); 
    //Load image 
    hello = SDL_LoadBMP( "111.bmp" ); 
    //Apply image to screen 
    SDL_BlitSurface( hello, NULL, screen, NULL ); 
    //Update Screen 
    SDL_Flip( screen ); 
    //Pause 
    SDL_Delay( 15000 );     
    //Quit SDL 
    SDL_Quit(); 
    //Free memory 
    SDL_FreeSurface( hello ); 
    //Quit SDL 
    SDL_Quit(); 
    return 0; 
} 
*/

/*######  Miscellaneous global stuff  ####################################             */
static proc_t **global_proc_t_ptr_table;  // our base proc_t ptr table
// the memery structure like this, the global_proc_t_ptr_table[0] ... [n] is continouse.
// we just need to free global_proc_t_ptr_table at end.
//
// &global_proc_t_ptr_table = 6624672
// global_proc_t_ptr_table = 21304256
//                         |
//                         ----> 21304256 &global_proc_t_ptr_table[0] --> 21125072 proc_t
//                               21304264 &global_proc_t_ptr_table[1] --> 21138368 proc_t
//                               21304272 &global_proc_t_ptr_table[2] --> 21139168 proc_t
//                               21304280 &global_proc_t_ptr_table[3] --> 21139968 proc_t

        /* The original and new terminal definitions
           (only set when not in 'Batch' mode) */
static struct termios Tty_original,    // our inherited terminal definition
#ifdef TERMIOS_ONLY
                      Tty_tweaked,     // for interactive 'line' input
#endif
                      Tty_raw;         // for unsolicited input
static int Ttychanged = 0;

#ifdef LOG2STDOUT

FILE *LogPtr = 0;
static char *Log_name = "log";

#endif

        /* Last established cursor state/shape */
static const char *Cursor_state = "";

        /* Program name used in error messages and local 'rc' file name */
static char *Myname;

        /* Our constant sigset, so we need initialize it but once */
static sigset_t Sigwinch_set;

        /* The 'local' config file support */
static char  Rc_name [OURPATHSZ];
static RCFull_t Rc = DEF_RCFILE;
static int   Rc_questions;

        /* The run-time acquired page stuff */
static unsigned Page_size;
static unsigned Pg2K_shft = 0;

        /* SMP, Irix/Solaris mode, Linux 2.5.xx support */
static int         Cpu_faux_tot;
static float       Cpu_pmax;
static const char *Cpu_States_fmts;

        /* Specific process id monitoring support */
static pid_t Monpids [MONPIDMAX] = { 0 };
static int   Monpidsidx = 0;

        /* Current screen dimensions.
           note: the number of processes displayed is tracked on a per window
                 basis (see the WIN_t).  Max_lines is the total number of
                 screen rows after deducting summary information overhead. */
        /* Current terminal screen size. */
static int Screen_cols, Screen_rows, Max_lines;

        /* This is really the number of lines needed to display the summary
           information (0 - nn), but is used as the relative row where we
           stick the cursor between frames. */
static int Msg_row;

        /* The nearly complete scroll coordinates message for the current
           window, built at the time column headers are constructed */
static char Scroll_fmts [SMLBUFSIZ];

        /* Global/Non-windows mode stuff that is NOT persistent */
static int No_ksyms = -1,       // set to '0' if ksym avail, '1' otherwise
           PSDBopen = 0,        // set to '1' if psdb opened (now postponed)
           Batch = 0,           // batch mode, collect no input, dumb output
           Loops = -1,          // number of iterations, -1 loops forever
           Secure_mode = 0,     // set if some functionality restricted
           Thread_mode = 0,     // set w/ 'H' - show threads via readeither()
           Width_mode = 0;      // set w/ 'w' - potential output override

        /* Unchangeable cap's stuff built just once (if at all) and
           thus NOT saved in a WIN_t's RCWindow_t.  To accommodate 'Batch'
           mode, they begin life as empty strings so the overlying
           logic need not change ! */
static char  Cap_clr_eol     [CAPBUFSIZ] = "",    // global and/or static vars
             Cap_nl_clreos   [CAPBUFSIZ] = "",    // are initialized to zeros!
             Cap_clr_scr     [CAPBUFSIZ] = "",    // the assignments used here
             Cap_curs_norm   [CAPBUFSIZ] = "",    // cost nothing but DO serve
             Cap_curs_huge   [CAPBUFSIZ] = "",    // to remind people of those
             Cap_curs_hide   [CAPBUFSIZ] = "",    // batch requirements!
             Cap_clr_eos     [CAPBUFSIZ] = "",
             Cap_home        [CAPBUFSIZ] = "",
             Cap_norm        [CAPBUFSIZ] = "",
             Cap_reverse     [CAPBUFSIZ] = "",
             Caps_off        [CAPBUFSIZ] = "",
             Caps_endline    [CAPBUFSIZ] = "";

static char  Caps_sort_header[CLRBUFSIZ] = "";
static char  Caps_yellow     [CLRBUFSIZ] = "";
static char  Caps_magenta    [CLRBUFSIZ] = "";
static char  Caps_red        [CLRBUFSIZ] = "";
static char  Caps_green      [CLRBUFSIZ] = "";
static char  Caps_normal     [CLRBUFSIZ] = "";

#ifndef RMAN_IGNORED
static char  Cap_rmam       [CAPBUFSIZ] = "",
             Cap_smam       [CAPBUFSIZ] = "";
        /* set to 1 if writing to the last column would be troublesome
           (we don't distinguish the lowermost row from the other rows) */
static int   Cap_avoid_eol = 0;
#endif
static int   Cap_can_goto = 0;

        /* Some optimization stuff, to reduce output demands...
           The Pseudo_ guys are managed by adj_geometry and frame_make.  They
           are exploited in a macro and represent 90% of our optimization.
           The Stdout_buf is transparent to our code and regardless of whose
           buffer is used, stdout is flushed at frame end or if interactive. */
static char  *Pseudo_screen;
static int    Pseudo_row = PROC_XTRA;
static size_t Pseudo_size;
#ifndef OFF_STDIOLBF
        // less than stdout's normal buffer but with luck mostly '\n' anyway
static char  Stdout_buf[2048];
#endif

        /* Our four WIN_t's, and which of those is considered the 'current'
           window (ie. which window is associated with any summ info displayed
           and to which window commands are directed) */
static WIN_t  Winstk [GROUPSMAX];
static WIN_t *Curwin;

        /* Frame oriented stuff that can't remain local to any 1 function
           and/or that would be too cumbersome managed as parms,
           and/or that are simply more efficiently handled as globals
           [ 'Frames_...' (plural) stuff persists beyond 1 frame ]
           [ or are used in response to async signals received ! ] */
static volatile int Frames_signal;     // time to rebuild all column headers
static          int Frames_libflags;   // PROC_FILLxxx flags
static int          Frame_maxtask;     // last known number of active tasks
                                       // ie. current 'size' of proc table
static float        Frame_etscale;     // so we can '*' vs. '/' WHEN 'pcpu'
static unsigned     Frame_running,     // state categories for this frame
                    Frame_sleepin,
                    Frame_stopped,
                    Frame_zombied;
#ifdef BUILD_4_STOCK	
static unsigned     stock_ups,     // state categories for this frame
                    stock_draws,
                    stock_downs,
                    Frame_zombied;
#endif
static int          Frame_srtflg,      // the subject window's sort direction
                    Frame_ctimes,      // the subject window's ctimes flag
                    Frame_cmdlin;      // the subject window's cmdlin flag

        /* Support for 'history' processing so we can calculate %cpu */
static int          HHistory_size;     // max number of HISTORY_t structs
static HISTORY_t *  PtrHistory_saved;  // alternating 'old/new' HISTORY_t anchors
static HISTORY_t *  PtrHistory_new;

#ifndef DISABLE_HISTORY_HASH
#define       HHASH_SIZ  4096
static int    HHash_one [HHASH_SIZ],   // actual hash tables ( hereafter known
              HHash_two [HHASH_SIZ],   // as PHash_sav/PHash_new )
              HHash_nul [HHASH_SIZ];   // 'empty' hash table image
static int   *PHash_sav = HHash_one,   // alternating 'old/new' hash tables
             *PHash_new = HHash_two;
#endif

        /* Support for automatically sized fixed-width column expansions.
         * (hopefully, the macros help clarify/document our new 'feature') */
static int Autox_array [P_MAXPFLAGS],
           Autox_found;
#define AUTOX_NO      P_MAXPFLAGS
#define AUTOX_COL(f)  if (P_MAXPFLAGS > f) Autox_array[f] = Autox_found = 1
#define AUTOX_MODE   (0 > Rc.fixed_widest)

        /* Support for scale_mem and scale_num (to avoid duplication. */
#ifdef CASEUP_SUFIX                                                // nls_maybe
   static char Scaled_sfxtab[] =  { 'K', 'M', 'G', 'T', 'P', 'E', 0 };
#else                                                              // nls_maybe
   static char Scaled_sfxtab[] =  { 'k', 'm', 'g', 't', 'p', 'e', 0 };
#endif

        /* Support for NUMA Node display, node expansion/targeting and
           run-time dynamic linking with libnuma.so treated as a plugin */
static int Numa_node_tot;
static int Numa_node_sel = -1;
#ifndef NUMA_DISABLE
static void *Libnuma_handle;
static int Stderr_save = -1;
#if defined(PRETEND_NUMA) || defined(PRETEND8CPUS)
static int Numa_max_node(void) { return 3; }
static int Numa_node_of_cpu(int num) { return (num % 4); }
#else
static int (*Numa_max_node)(void);
static int (*Numa_node_of_cpu)(int num);
#endif
#endif

/*######  Sort callbacks  ################################################*/

        /*
         * These happen to be coded in the enum identifier alphabetic order,
         * not the order of the enum 'pflgs' value.  Also note that a callback
         * routine may serve more than one column.
         */

SCB_STRS(CGR, cgroup[0])
SCB_STRV(CMD, Frame_cmdlin, cmdline, cmd)
SCB_NUM1(COD, trs)
SCB_NUMx(CPN, processor)
SCB_NUM1(CPU, pcpu)
SCB_NUM1(DAT, drs)
SCB_NUM1(DRT, dt)
SCB_STRS(ENV, environ[0])
SCB_NUM1(FL1, maj_flt)
SCB_NUM1(FL2, min_flt)
SCB_NUM1(FLG, flags)
SCB_NUM1(FV1, maj_delta)
SCB_NUM1(FV2, min_delta)
SCB_NUMx(GID, egid)
SCB_STRS(GRP, egroup)
SCB_NUMx(NCE, nice)
SCB_NUM1(NS1, ns[IPCNS])
SCB_NUM1(NS2, ns[MNTNS])
SCB_NUM1(NS3, ns[NETNS])
SCB_NUM1(NS4, ns[PIDNS])
SCB_NUM1(NS5, ns[USERNS])
SCB_NUM1(NS6, ns[UTSNS])
#ifdef OOMEM_ENABLE
SCB_NUM1(OOA, oom_adj)
SCB_NUM1(OOM, oom_score)
#endif
SCB_NUMx(PGD, pgrp)
SCB_NUMx(PID, tid)
#ifdef BUILD_4_STOCK
SCB_NUMx(S_StockID, stockid)
SCB_NUMx(S_TIME, sort_time)
SCB_NUMx(S_DAY, sort_date)
SCB_STRS(S_StockName, stockname)
SCB_NUMx(S_OPEN, open_today)
SCB_NUMx(S_CLOSE_YESTODAY, pre_close)
SCB_NUMx(S_MIN, min)
SCB_NUMx(S_MAX, max)
SCB_NUMx(S_CURRENT, current_price)
SCB_NUMx(S_VOLUME, volume)
SCB_NUMx(S_RMB, sort_RMB)
SCB_NUMx(S_PERCENT, up_percent)
SCB_NUMx(S_TURNOVER_RATE, turn_over_rate)
SCB_NUMx(S_DDR, ddr)
SCB_NUMx(S_MNR, mnr)
SCB_NUMx(S_LIUTONGA, sort_liutongA)
SCB_NUMx(S_TOTALS, sort_totals)
SCB_NUMx(S_VALUE_LIUTONGA, sort_value_liutongA)
SCB_NUMx(S_VALUE_TOTALS, sort_value_totals)
SCB_NUMx(S_AVERAGE_PRICE, average_price)
SCB_NUMx(S_SWEIGHT, sweight)
SCB_NUMx(S_DOWNSHADOW, downshadow)
SCB_NUMx(S_MA5, ma5)
SCB_NUMx(S_MA10, ma10)
SCB_NUMx(S_MA20, ma20)
SCB_NUMx(S_MA30, ma30)
SCB_NUMx(S_MA60, ma60)
SCB_NUMx(S_VRATIO, volume_ratio)
SCB_NUMx(S_SWING, swing)
SCB_NUMx(S_OUTER, outer)
SCB_NUMx(S_INNER, inner)
SCB_NUMx(S_PE_RATIO, pe_ratio)
#endif
SCB_NUMx(PPD, ppid)
SCB_NUMx(PRI, priority)
SCB_NUM1(RES, resident)                // also serves MEM !
SCB_STRX(SGD, supgid)
SCB_STRS(SGN, supgrp)
SCB_NUM1(SHR, share)
SCB_NUM1(SID, session)
SCB_NUMx(STA, state)
SCB_NUM1(SWP, vm_swap)
SCB_NUMx(TGD, tgid)
SCB_NUMx(THD, nlwp)
                                       // also serves TM2 !
static int SCB_NAME(TME) (const proc_t **P, const proc_t **Q) 
{
   if (Frame_ctimes) 
   {
      if (((*P)->cutime + (*P)->cstime + (*P)->utime + (*P)->stime)
        < ((*Q)->cutime + (*Q)->cstime + (*Q)->utime + (*Q)->stime))
           return SORT_lt;
      if (((*P)->cutime + (*P)->cstime + (*P)->utime + (*P)->stime)
        > ((*Q)->cutime + (*Q)->cstime + (*Q)->utime + (*Q)->stime))
           return SORT_gt;
   } 
   else 
   {
      if (((*P)->utime + (*P)->stime) < ((*Q)->utime + (*Q)->stime))
         return SORT_lt;
      if (((*P)->utime + (*P)->stime) > ((*Q)->utime + (*Q)->stime))
         return SORT_gt;
   }
   return SORT_eq;
}
SCB_NUM1(TPG, tpgid)
SCB_NUMx(TTY, tty)
SCB_NUMx(UED, euid)
SCB_STRS(UEN, euser)
SCB_NUMx(URD, ruid)
SCB_STRS(URN, ruser)
SCB_NUMx(USD, suid)
SCB_NUM2(USE, resident, vm_swap)
SCB_STRS(USN, suser)
SCB_NUM1(VRT, size)
SCB_NUM1(WCH, wchan)

#ifdef DISABLE_HISTORY_HASH
        /* special sort for procs_statistic() ! ------------------------ */
static int sort_HISTORY_t (const HISTORY_t *P, const HISTORY_t *Q) 
{
   return P->pid - Q->pid;
}
#endif

/*######  Tiny useful routine(s)  ########################################*/

        /*
         * This routine simply formats whatever the caller wants and
         * returns a pointer to the resulting 'const char' string... */
static const char *fmtmk (const char *fmts, ...) __attribute__((format(printf,1,2)));
static const char *fmtmk (const char *fmts, ...) 
{
   static char buf[BIGBUFSIZ];          // with help stuff, our buffer
   va_list va;                          // requirements now exceed 1k

   va_start(va, fmts);
   vsnprintf(buf, sizeof(buf), fmts, va);
   va_end(va);
   return (const char *)buf;
} // end: fmtmk


        /*
         * This guy is just our way of avoiding the overhead of the standard
         * strcat function (should the caller choose to participate) */
static inline char *scat (char *dst, const char *src) 
{
   while (*dst) dst++;
   while ((*(dst++) = *(src++)));
   return --dst;
} // end: scat


        /*
         * This guy just facilitates Batch and protects against dumb ttys
         * -- we'd 'inline' him but he's only called twice per frame,
         * yet used in many other locations. */
static const char *tg2 (int x, int y) 
{
   // it's entirely possible we're trying for an invalid row...
   return Cap_can_goto ? tgoto(cursor_address, x, y) : "";
} // end: tg2

/*######  Exit/Interrput routines  #######################################*/

        /*
         * Reset the tty, if necessary */
static void at_eoj (void) 
{
   if (Ttychanged) 
   {
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &Tty_original);
      if (keypad_local) putp(keypad_local);
      putp(tg2(0, Screen_rows));
      putp("\n");
#ifdef OFF_SCROLLBK
      if (exit_ca_mode) 
      {
         // this next will also replace top's most recent screen with the
         // original display contents that were visible at our invocation
         putp(exit_ca_mode);
      }
#endif
      putp(Cap_curs_norm);
      putp(Cap_clr_eol);
#ifndef RMAN_IGNORED
      putp(Cap_smam);
#endif
   }
   fflush(stdout);
} // end: at_eoj


        /*
         * The real program end */
static void bye_bye (const char *str) NORETURN;
static void bye_bye (const char *str) 
{
   at_eoj();                 // restore tty in preparation for exit
#ifdef AT_END_OF_JOB_REPORT_STD
   proc_t *p;
   if (!str && !Frames_signal && Ttychanged) 
   { 
      fprintf(stderr,
      "\n%s's Summary report:"
      "\n\tProgram"
      "\n\t   Linux version = %u.%u.%u, %s"
      "\n\t   Hertz = %u (%u bytes, %u-bit time)"
      "\n\t   Page_size = %d, Cpu_faux_tot = %d, smp_num_cpus = %d"
      "\n\t   sizeof(CPU_t) = %u, sizeof(HISTORY_t) = %u (%u HISTORY_t's/Page), HHistory_size = %u"
      "\n\t   sizeof(proc_t) = %u, sizeof(proc_t.cmd) = %u, sizeof(proc_t*) = %u"
      "\n\t   Frames_libflags = %08lX"
      "\n\t   SCREENMAX = %u, ROWMINSIZ = %u, ROWMAXSIZ = %u"
      "\n\t   PACKAGE = '%s', LOCALEDIR = '%s'"
      "\n\tTerminal: %s"
      "\n\t   device = %s, ncurses = v%s"
      "\n\t   max_colors = %d, max_pairs = %d"
      "\n\t   Cap_can_goto = %s"
      "\n\t   Screen_cols = %d, Screen_rows = %d"
      "\n\t   Max_lines = %d, most recent Pseudo_size = %u"
#ifndef OFF_STDIOLBF
      "\n\t   Stdout_buf = %u, BUFSIZ = %u"
#endif
      "\n\tWindows and Curwin->"
      "\n\t   sizeof(WIN_t) = %u, GROUPSMAX = %d"
      "\n\t   winname = %s, grpname = %s"
#ifdef CASEUP_HEXES
      "\n\t   winflags = %08X, max_displayable_pflags = %d"
#else
      "\n\t   winflags = %08x, max_displayable_pflags = %d"
#endif
      "\n\t   sortindex = %d, fieldscur = %s"
      "\n\t   maxtasks = %d, variable_column_size = %d, winlines = %d"
      "\n\t   columnheader = \"%s\""
      "\n\t   %s"
      "\n\t   strlen(columnheader) = %d"
      "\n"
      , __func__
      , LINUX_VERSION_MAJOR(linux_version_code)
      , LINUX_VERSION_MINOR(linux_version_code)
      , LINUX_VERSION_PATCH(linux_version_code)
      , procps_version
      , (unsigned)Hertz, (unsigned)sizeof(Hertz), (unsigned)sizeof(Hertz) * 8
      , Page_size, Cpu_faux_tot, (int)smp_num_cpus, (unsigned)sizeof(CPU_t)
      , (unsigned)sizeof(HISTORY_t), Page_size / (unsigned)sizeof(HISTORY_t), HHistory_size
      , (unsigned)sizeof(proc_t), (unsigned)sizeof(p->cmd), (unsigned)sizeof(proc_t*)
      , (long)Frames_libflags
      , (unsigned)SCREENMAX, (unsigned)ROWMINSIZ, (unsigned)ROWMAXSIZ
      , PACKAGE_NAME, LOCALEDIR
#ifdef PRETENDNOCAP
      , "dumb"
#else
      , termname()
#endif
      , ttyname(STDOUT_FILENO), NCURSES_VERSION
      , max_colors, max_pairs
      , Cap_can_goto ? "yes" : "No!"
      , Screen_cols, Screen_rows
      , Max_lines, (unsigned)Pseudo_size
#ifndef OFF_STDIOLBF
      , (unsigned)sizeof(Stdout_buf), (unsigned)BUFSIZ
#endif
      , (unsigned)sizeof(WIN_t), GROUPSMAX
      , Curwin->rc.winname, Curwin->grpname
      , Curwin->rc.winflags, Curwin->max_displayable_pflags
      , Curwin->rc.sortindex, Curwin->rc.fieldscur
      , Curwin->rc.maxtasks, Curwin->variable_column_size, Curwin->winlines
      , Curwin->columnheader
      , Caps_off
      , (int)strlen(Curwin->columnheader)
      );
   }
#endif // end: AT_END_OF_JOB_REPORT_STD

#ifndef DISABLE_HISTORY_HASH
#ifdef AT_END_OF_JOB_REPORT_HASH
   if (!str && !Frames_signal && Ttychanged) 
   {
      int i, j, pop, total_occupied, maxdepth, maxdepth_sav, numdepth
         , cross_foot, sz = HHASH_SIZ * (unsigned)sizeof(int);
      int depths[HHASH_SIZ];

      for (i = 0, total_occupied = 0, maxdepth = 0; i < HHASH_SIZ; i++) 
      {
         int V = PHash_new[i];
         j = 0;
         if (-1 < V) 
         {
            ++total_occupied;
            while (-1 < V) 
            {
               V = PtrHistory_new[V].lnk;
               if (-1 < V) j++;
            }
         }
         depths[i] = j;
         if (maxdepth < j) maxdepth = j;
      }
      maxdepth_sav = maxdepth;

      fprintf(stderr,
         "\n%s's Supplementary HASH report:"
         "\n\tTwo Tables providing for %d entries each + 1 extra for 'empty' image"
         "\n\t%dk (%d bytes) per table, %d total bytes (including 'empty' image)"
         "\n\tResults from latest hash (PHash_new + PtrHistory_new)..."
         "\n"
         "\n\tTotal hashed = %d"
         "\n\tLevel-0 hash entries = %d (%d%% occupied)"
         "\n\tMax Depth = %d"
         "\n\n"
         , __func__
         , HHASH_SIZ, sz / 1024, sz, sz * 3
         , Frame_maxtask
         , total_occupied, (total_occupied * 100) / HHASH_SIZ
         , maxdepth + 1);

      if (total_occupied) 
      {
         for (pop = total_occupied, cross_foot = 0; maxdepth; maxdepth--) 
         {
            for (i = 0, numdepth = 0; i < HHASH_SIZ; i++)
               if (depths[i] == maxdepth) ++numdepth;
            fprintf(stderr,
               "\t %5d (%3d%%) hash table entries at depth %d\n"
               , numdepth, (numdepth * 100) / total_occupied, maxdepth + 1);
            pop -= numdepth;
            cross_foot += numdepth;
            if (0 == pop && cross_foot == total_occupied) break;
         }
         if (pop) 
         {
            fprintf(stderr, "\t %5d (%3d%%) unchained hash table entries\n"
               , pop, (pop * 100) / total_occupied);
            cross_foot += pop;
         }
         fprintf(stderr,
            "\t -----\n"
            "\t %5d total entries occupied\n", cross_foot);

         if (maxdepth_sav) 
         {
            fprintf(stderr, "\nPIDs at max depth: ");
            for (i = 0; i < HHASH_SIZ; i++)
            {
               if (depths[i] == maxdepth_sav) 
               {
                  j = PHash_new[i];
                  fprintf(stderr, "\n\tpos %4d:  %05d", i, PtrHistory_new[j].pid);
                  while (-1 < j) 
                  {
                     j = PtrHistory_new[j].lnk;
                     if (-1 < j) fprintf(stderr, ", %05d", PtrHistory_new[j].pid);
                  }
               }
            }
            fprintf(stderr, "\n");
         }
      }
   }
#endif // end: AT_END_OF_JOB_REPORT_HASH
#endif // end: DISABLE_HISTORY_HASH
#ifdef LOG2STDOUT
   fprintf(LogPtr, "\n\nbefore free global_proc_t_ptr_table\n");
   fprintf(LogPtr, "&global_proc_t_ptr_table = %d\n", &global_proc_t_ptr_table);
   fprintf(LogPtr, "global_proc_t_ptr_table = %d\n", global_proc_t_ptr_table);
   fprintf(LogPtr, "&global_proc_t_ptr_table[0] = %d\n", &global_proc_t_ptr_table[0]);
   fflush(LogPtr);
#endif
   if (global_proc_t_ptr_table)
   {
     free(global_proc_t_ptr_table);// free(0) would be OK!!!
     global_proc_t_ptr_table = 0;
   }
   if (Pseudo_screen)
   {
      free(Pseudo_screen); 
      Pseudo_screen = 0; 
   }

#ifdef LOG2STDOUT
   if (LogPtr)
   {
      fclose(LogPtr);
      LogPtr = 0;
   }
#endif

#ifndef NUMA_DISABLE
  if (Libnuma_handle) dlclose(Libnuma_handle);
#endif
   if (str) 
   {
      fputs(str, stderr);
      exit(EXIT_FAILURE);
   }
   if (Batch) putp("\n");
   exit(EXIT_SUCCESS);
} // end: bye_bye


        /*
         * Standard error handler to normalize the look of all err output */
static void error_exit (const char *str) NORETURN;
static void error_exit (const char *str) 
{
   static char buf[MEDBUFSIZ];

   /* we'll use our own buffer so callers can still use fmtmk() and, after
      twelve long years, 2013 was the year we finally eliminated the leading
      tab character -- now our message can get lost in screen clutter too! */
   snprintf(buf, sizeof(buf), "%s: %s\n", Myname, str);
   bye_bye(buf);
} // end: error_exit


        /*
         * Handle library errors ourselves rather than accept a default
         * fprintf to stderr (since we've mucked with the termios struct) */
static void library_err (const char *fmts, ...) NORETURN;
static void library_err (const char *fmts, ...) 
{
   static char tmp[MEDBUFSIZ];
   va_list va;

   va_start(va, fmts);
   vsnprintf(tmp, sizeof(tmp), fmts, va);
   va_end(va);
   error_exit(tmp);
} // end: library_err


        /*
         * Catches all remaining signals not otherwise handled */
static void sig_abexit (int sig) 
{
   sigset_t ss;

// POSIX.1-2004 async-signal-safe: sigfillset, sigprocmask, signal, raise
   sigfillset(&ss);
   sigprocmask(SIG_BLOCK, &ss, NULL);
   at_eoj();                 // restore tty in preparation for exit
   fprintf(stderr, N_fmt_Norm_tab(EXIT_signals_fmt)
      , sig, signal_number_to_name(sig), Myname);
   signal(sig, SIG_DFL);     // allow core dumps, if applicable
   raise(sig);               // ( plus set proper return code )
} // end: sig_abexit


        /*
         * Catches:
         *    SIGALRM, SIGHUP, SIGINT, SIGPIPE, SIGQUIT, SIGTERM,
         *    SIGUSR1 and SIGUSR2 */
static void sig_endpgm (int dont_care_sig) NORETURN;
static void sig_endpgm (int dont_care_sig) 
{
   sigset_t ss;

// POSIX.1-2004 async-signal-safe: sigfillset, sigprocmask
   sigfillset(&ss);
   sigprocmask(SIG_BLOCK, &ss, NULL);
   Frames_signal = BREAK_signal;
   bye_bye(NULL);
   (void)dont_care_sig;
} // end: sig_endpgm


        /*
         * Catches:
         *    SIGTSTP, SIGTTIN and SIGTTOU */
static void sig_paused (int dont_care_sig) 
{
// POSIX.1-2004 async-signal-safe: tcsetattr, tcdrain, raise
   if (-1 == tcsetattr(STDIN_FILENO, TCSAFLUSH, &Tty_original))
      error_exit(fmtmk(N_fmt_Norm_tab(FAIL_tty_set_fmt), strerror(errno)));
   if (keypad_local) putp(keypad_local);
   putp(tg2(0, Screen_rows));
   putp(Cap_curs_norm);
#ifndef RMAN_IGNORED
   putp(Cap_smam);
#endif
   // tcdrain(STDOUT_FILENO) was not reliable prior to ncurses-5.9.20121017,
   // so we'll risk POSIX's wrath with good ol' fflush, lest 'Stopped' gets
   // co-mingled with our most recent output...
   fflush(stdout);
   raise(SIGSTOP);
   // later, after SIGCONT...
   if (-1 == tcsetattr(STDIN_FILENO, TCSAFLUSH, &Tty_raw))
      error_exit(fmtmk(N_fmt_Norm_tab(FAIL_tty_set_fmt), strerror(errno)));
#ifndef RMAN_IGNORED
   putp(Cap_rmam);
#endif
   if (keypad_xmit) putp(keypad_xmit);
   putp(Cursor_state);
   Frames_signal = BREAK_signal;
   (void)dont_care_sig;
} // end: sig_paused


        /*
         * Catches:
         *    SIGCONT and SIGWINCH */
static void sig_resize (int dont_care_sig) 
{
// POSIX.1-2004 async-signal-safe: tcdrain
   tcdrain(STDOUT_FILENO);
   Frames_signal = BREAK_signal;
   (void)dont_care_sig;
} // end: sig_resize

/*######  Misc Color/Display support  ####################################*/

        /*
         * Make the appropriate caps/color strings for a window/field group.
         * note: we avoid the use of background color so as to maximize
         *       compatibility with the user's xterm settings */
static void capsmk (WIN_t *w) 
{
   /* macro to test if a basic (non-color) capability is valid
         thanks: Floyd Davidson <floyd@ptialaska.net> */
 #define tIF(s)  s ? s : ""
   static int capsdone = 0;

   // we must NOT disturb our 'empty' terminfo strings!
   if (Batch) return;

   // these are the unchangeable puppies, so we only do 'em once
   if (!capsdone) 
   {
      // defined in /usr/local/termh
      STRLCPY(Cap_clr_eol, tIF(clr_eol))
      STRLCPY(Cap_clr_eos, tIF(clr_eos))
      STRLCPY(Cap_clr_scr, tIF(clear_screen))
      // due to the leading newline, the following must be used with care
      snprintf(Cap_nl_clreos, sizeof(Cap_nl_clreos), "\n%s", tIF(clr_eos));
      STRLCPY(Cap_curs_huge, tIF(cursor_visible))
      STRLCPY(Cap_curs_norm, tIF(cursor_normal))
      STRLCPY(Cap_curs_hide, tIF(cursor_invisible))
      STRLCPY(Cap_home, tIF(cursor_home))
      STRLCPY(Cap_norm, tIF(exit_attribute_mode))
      STRLCPY(Cap_reverse, tIF(enter_reverse_mode))
#ifndef RMAN_IGNORED
      if (!eat_newline_glitch) 
      {
         STRLCPY(Cap_rmam, tIF(exit_am_mode))
         STRLCPY(Cap_smam, tIF(enter_am_mode))
         if (!*Cap_rmam || !*Cap_smam) 
         {
            *Cap_rmam = '\0';
            *Cap_smam = '\0';
            if (auto_right_margin)
               Cap_avoid_eol = 1;
         }
         putp(Cap_rmam);
      }
#endif
      snprintf(Caps_off, sizeof(Caps_off), "%s%s", Cap_norm, tIF(orig_pair));
      snprintf(Caps_endline, sizeof(Caps_endline), "%s%s", Caps_off, Cap_clr_eol);

      if (tgoto(cursor_address, 1, 1)) Cap_can_goto = 1;

      snprintf(Caps_sort_header,  sizeof(Caps_sort_header),  "%s%s"
               , tparm(set_a_foreground, COLOR_CYAN),   Cap_reverse);

      snprintf(Caps_yellow,  sizeof(Caps_yellow),  "%s%s"
               , tparm(set_a_foreground, COLOR_YELLOW),   Cap_reverse);


      snprintf(Caps_magenta, sizeof(Caps_magenta), "%s"
               , tparm(set_a_foreground, COLOR_MAGENTA));
      snprintf(Caps_red,     sizeof(Caps_red),     "%s"
               , tparm(set_a_foreground, COLOR_RED));
      snprintf(Caps_green,   sizeof(Caps_green),   "%s"
               , tparm(set_a_foreground, COLOR_GREEN));
/*
      snprintf(Caps_magenta, sizeof(Caps_magenta), "%s%s"
               , tparm(set_a_foreground, COLOR_MAGENTA),  Cap_reverse);
      snprintf(Caps_red,     sizeof(Caps_red),     "%s%s"
               , tparm(set_a_foreground, COLOR_RED),      Cap_reverse);
      snprintf(Caps_green,   sizeof(Caps_green),   "%s%s"
               , tparm(set_a_foreground, COLOR_GREEN),    Cap_reverse);
*/
      snprintf(Caps_normal,  sizeof(Caps_normal),  "%s%s"
               , Caps_off, tparm(set_a_foreground, w->rc.taskcolor));

      capsdone = 1;
   }

   /* the key to NO run-time costs for configurable colors -- we spend a
      little time with the user now setting up our terminfo strings, and
      the job's done until he/she/it has a change-of-heart */
   STRLCPY(w->cap_bold, CHKwinflags(w, View_NOBOLD) ? Cap_norm : tIF(enter_bold_mode))
   if (CHKwinflags(w, Show_COLORS) && max_colors > 0) 
   {
      STRLCPY(w->capclr_sum, tparm(set_a_foreground, w->rc.summcolor))
      snprintf(w->capclr_msg, sizeof(w->capclr_msg), "%s%s"
         , tparm(set_a_foreground, w->rc.msgscolor), Cap_reverse);
      snprintf(w->capclr_pmt, sizeof(w->capclr_pmt), "%s%s"
         , tparm(set_a_foreground, w->rc.msgscolor), w->cap_bold);
      snprintf(w->capclr_hdr, sizeof(w->capclr_hdr), "%s%s"
         , tparm(set_a_foreground, w->rc.headcolor), Cap_reverse);
      snprintf(w->capclr_rownorm, sizeof(w->capclr_rownorm), "%s%s"
         , Caps_off, tparm(set_a_foreground, w->rc.taskcolor));
   } 
   else 
   {
      w->capclr_sum[0] = '\0';
#ifdef USE_X_COLHDR
      snprintf(w->capclr_msg, sizeof(w->capclr_pmt), "%s%s"
         , Cap_reverse, w->cap_bold);
#else
      STRLCPY(w->capclr_msg, Cap_reverse)
#endif
      STRLCPY(w->capclr_pmt, w->cap_bold)
      STRLCPY(w->capclr_hdr, Cap_reverse)
      STRLCPY(w->capclr_rownorm, Cap_norm)
   }

   // composite(s), so we do 'em outside and after the if
   snprintf(w->capclr_rowhigh, sizeof(w->capclr_rowhigh), "%s%s"
      , w->capclr_rownorm, CHKwinflags(w, Show_Highlight_BOLD) ? w->cap_bold : Cap_reverse);
   
 #undef tIF
} // end: capsmk


        /*
         * Show an error message (caller may include '\a' for sound) */
static void show_msg (const char *str) 
{
   PUTT("%s%s %.*s %s%s%s"
      , tg2(0, Msg_row)
      , Curwin->capclr_msg
      , Screen_cols - 2
      , str
      , Cap_curs_hide
      , Caps_off
      , Cap_clr_eol);
   fflush(stdout);
   usleep(MSG_USLEEP);
} // end: show_msg


        /*
         * Show an input prompt + larger cursor (if possible) */
static int show_pmt (const char *str) 
{
   int rc;

   PUTT("%s%s%.*s %s%s%s"
      , tg2(0, Msg_row)
      , Curwin->capclr_pmt
      , Screen_cols - 2
      , str
      , Cap_curs_huge
      , Caps_off
      , Cap_clr_eol);
   fflush(stdout);
   // +1 for the space we added or -1 for the cursor...
   return ((rc = (int)strlen(str)+1) < Screen_cols) ? rc : Screen_cols-1;
} // end: show_pmt


        /*
         * Show lines with specially formatted elements, but only output
         * what will fit within the current screen width.
         *    Our special formatting consists of:
         *       "some text <_delimiter_> some more text <_delimiter_>...\n"
         *    Where <_delimiter_> is a two byte combination consisting of a
         *    tilde followed by an ascii digit in the the range of 1 - 8.
         *       examples: ~1,  ~5,  ~8, etc.
         *    The tilde is effectively stripped and the next digit
         *    converted to an index which is then used to select an
         *    'attribute' from a capabilities table.  That attribute
         *    is then applied to the *preceding* substring.
         * Once recognized, the delimiter is replaced with a null character
         * and viola, we've got a substring ready to output!  Strings or
         * substrings without delimiters will receive the Cap_norm attribute.
         *
         * Caution:
         *    This routine treats all non-delimiter bytes as displayable
         *    data subject to our screen width marching orders.  If callers
         *    embed non-display data like tabs or terminfo strings in our
         *    glob, a line will truncate incorrectly at best.  Worse case
         *    would be truncation of an embedded tty escape sequence.
         *
         *    Tabs must always be avoided or our efforts are wasted and
         *    lines will wrap.  To lessen but not eliminate the risk of
         *    terminfo string truncation, such non-display stuff should
         *    be placed at the beginning of a "short" line. */
static void show_special (int interact, const char *glob) 
{
  /* note: the following is for documentation only,
           the real captab is now found in a group's WIN_t !
     +------------------------------------------------------+
     | char *captab[] = {                 :   Cap's/Delim's |
     |   Cap_norm, Cap_norm,              =   \000, \001,   |
     |   cap_bold, capclr_sum,            =   \002, \003,   |
     |   capclr_msg, capclr_pmt,          =   \004, \005,   |
     |   capclr_hdr,                      =   \006,         |
     |   capclr_rowhigh,                  =   \007,         |
     |   capclr_rownorm  };               =   \010 [octal!] |
     +------------------------------------------------------+ */
  /* ( Pssst, after adding the termcap transitions, row may )
     ( exceed 300+ bytes, even in an 80x24 terminal window! )
     ( And if we're no longer guaranteed lines created only )
     ( by top, we'll need larger buffs plus some protection )
     ( against overrunning them with this 'lin_end - glob'. ) */
   char tmp[LRGBUFSIZ], lin[LRGBUFSIZ], row[ROWMAXSIZ];
   char *rp, *lin_end, *sub_beg, *sub_end;
   int room;

   // handle multiple lines passed in a bunch
   while ((lin_end = strchr(glob, '\n'))) 
   {
     #define myMIN(a,b) (((a) < (b)) ? (a) : (b))
      size_t lessor = myMIN((size_t)(lin_end - glob), sizeof(lin) -1);

      // create a local copy we can extend and otherwise abuse
      memcpy(lin, glob, lessor);
      // zero terminate this part and prepare to parse substrings
      lin[lessor] = '\0';
      room = Screen_cols;
      sub_beg = sub_end = lin;
      *(rp = row) = '\0';

      while (*sub_beg) 
      {
         int ch = *sub_end;
         if ('~' == ch) ch = *(sub_end + 1) - '0';
         switch (ch) {
            case 0:                    // no end delim, captab makes normal
               *(sub_end + 1) = '\0';  // extend str end, then fall through
               *(sub_end + 2) = '\0';  // ( +1 optimization for usual path )
            case 1: case 2: case 3: case 4:
            case 5: case 6: case 7: case 8:
               *sub_end = '\0';
               snprintf(tmp, sizeof(tmp), "%s%.*s%s"
                  , Curwin->captab[ch], room, sub_beg, Caps_off);
               rp = scat(rp, tmp);
               room -= (sub_end - sub_beg);
               sub_beg = (sub_end += 2);
               break;
            default:                   // nothin' special, just text
               ++sub_end;
         }
         if (0 >= room) break;         // skip substrings that won't fit
      }

      if (interact) PUTT("%s%s\n", row, Cap_clr_eol);
      else PUFF("%s%s\n", row, Caps_endline);
      glob = ++lin_end;                // point to next line (maybe)

     #undef myMIN
   } // end: while 'lines'

   /* If there's anything left in the glob (by virtue of no trailing '\n'),
      it probably means caller wants to retain cursor position on this final
      line.  That, in turn, means we're interactive and so we'll just do our
      'fit-to-screen' thingy while also leaving room for the cursor... */
   if (*glob) PUTT("%.*s", Screen_cols -1, glob);
} // end: show_special


        /*
         * Create a nearly complete scroll coordinates message, but still
         * a format string since we'll be missing the current total tasks. */
static void updt_scroll_msg (void) 
{
   char tmp1[SMLBUFSIZ], tmp2[SMLBUFSIZ];
   int totalpflags = Curwin->totalpflags;
   int begin_column_flags = Curwin->begin_column_flag + 1;

#ifndef USE_X_COLHDR
   if (CHKwinflags(Curwin, Show_Highlight_COLS)) 
   {
      totalpflags -= 2;
      if (ENUpos(Curwin, Curwin->rc.sortindex) < Curwin->begin_column_flag) begin_column_flags -= 2;
   }
#endif
   if (1 > totalpflags) totalpflags = 1;
   if (1 > begin_column_flags) begin_column_flags = 1;
   snprintf(tmp1, sizeof(tmp1)
      , N_fmt_Norm_tab(SCROLL_coord_fmt), Curwin->begintask + 1, begin_column_flags, totalpflags);
   strcpy(tmp2, tmp1);
#ifndef SCROLLVAR_NO
   if (Curwin->variable_column_begin)
      snprintf(tmp2, sizeof(tmp2), "%s + %d", tmp1, Curwin->variable_column_begin);
#endif
   // this Scroll_fmts string no longer provides for termcap tgoto so that
   // the usage timing is critical -- see frame_make() for additional info
   snprintf(Scroll_fmts, sizeof(Scroll_fmts)
      , "%s  %.*s%s", Caps_off, Screen_cols - 3, tmp2, Cap_clr_eol);
} // end: updt_scroll_msg

/*######  Low Level Memory/Keyboard/File I/O support  ####################*/

        /*
         * Handle our own memory stuff without the risk of leaving the
         * user's terminal in an ugly state should things go sour. */

static void *alloc_c (size_t num) MALLOC;
static void *alloc_c (size_t num) 
{
   void *pv;

   if (!num) ++num;
   if (!(pv = calloc(1, num)))
      error_exit(N_txt_Norm_tab(FAIL_alloc_c_txt));
   return pv;
} // end: alloc_c


static void *alloc_r (void *ptr, size_t num) MALLOC;
static void *alloc_r (void *ptr, size_t num) 
{
   void *pv;

   if (!num) ++num;
//C语言函数realloc
//函数简介
//原型：extern void *realloc(void *mem_address, unsigned int newsize);
//语法：指针名=（数据类型*）realloc（要改变内存大小的指针名，新的大小）。//新的大小一定要大于原来的大小，不然的话会导致数据丢失！
//功能：先判断当前的指针是否有足够的连续空间，如果有，扩大mem_address指向的地址，并且将mem_address返回，如果空间不够，先按照newsize指定的大小分配空间，将原有数据从头到尾拷贝到新分配的内存区域，而后释放原来mem_address所指内存区域
//（注意：原来指针是自动释放，不需要使用free），同时返回新分配的内存区域的首地址。即重新分配存储器块的地址。
//
//// for realloc the pv address maybe different from the original ptr, that means the ptr address may be changed.
   if (!(pv = realloc(ptr, num)))
      error_exit(N_txt_Norm_tab(FAIL_alloc_r_txt));
   return pv;
} // end: alloc_r


static char *alloc_s (const char *str) MALLOC;
static char *alloc_s (const char *str) 
{
   return strcpy(alloc_c(strlen(str) +1), str);
} // end: alloc_s


        /*
         * This function is used in connection with raw single byte
         * unsolicited keyboard input that's susceptible to SIGWINCH
         * interrupts (or any other signal).  He also supports timout
         * in the absence of user keystrokes or some signal interrupt. */
static inline int ioa (struct timespec *ts) 
{
   fd_set fs;
   int rc;

   FD_ZERO(&fs);
   FD_SET(STDIN_FILENO, &fs);

#ifdef SIGNALS_LESS // conditional comments are silly, but help in documenting
   // hold here until we've got keyboard input, any signal except SIGWINCH
   // or (optionally) we timeout with nanosecond granularity
#else
   // hold here until we've got keyboard input, any signal (including SIGWINCH)
   // or (optionally) we timeout with nanosecond granularity
#endif
   rc = pselect(STDIN_FILENO + 1, &fs, NULL, NULL, ts, &Sigwinch_set);

   if (rc < 0) rc = 0;
   return rc;
} // end: ioa


        /*
         * This routine isolates ALL user INPUT and ensures that we
         * wont be mixing I/O from stdio and low-level read() requests */
static int ioch (int ech, char *buf, unsigned cnt) 
{
   int rc = -1;

#ifdef TERMIOS_ONLY
   if (ech) 
   {
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &Tty_tweaked);
      rc = read(STDIN_FILENO, buf, cnt);
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &Tty_raw);
   } 
   else 
   {
      if (ioa(NULL))
         rc = read(STDIN_FILENO, buf, cnt);
   }
#else
   (void)ech;
   if (ioa(NULL))
   {
      rc = read(STDIN_FILENO, buf, cnt);
   }
#endif

   // zero means EOF, might happen if we erroneously get detached from terminal
   if (0 == rc) bye_bye(NULL);

   // it may have been the beginning of a lengthy escape sequence
   tcflush(STDIN_FILENO, TCIFLUSH);

   // note: we do NOT produce a vaid 'string'
   return rc;
} // end: ioch


        /*
         * Support for single or multiple keystroke input AND
         * escaped cursor motion keys.
         * note: we support more keys than we currently need, in case
         *       we attract new consumers in the future */
static int iokey (int action) 
{
   static char buf12[CAPBUFSIZ]; 
   static char buf13[CAPBUFSIZ];
   static char buf14[CAPBUFSIZ]; 
   static char buf15[CAPBUFSIZ];
   static struct {
      const char *str;
      int key;
   } tinfo_tab[] = {
      { "\033\n",kbd_ENTER }, { NULL, kbd_UP       }, { NULL, kbd_DOWN     },
      { NULL, kbd_LEFT     }, { NULL, kbd_RIGHT    }, { NULL, kbd_PGUP     },
      { NULL, kbd_PGDN     }, { NULL, kbd_HOME     }, { NULL, kbd_END      },
      { NULL, kbd_BKSP     }, { NULL, kbd_INS      }, { NULL, kbd_DEL      },
         // next 4 destined to be meta + arrow keys...
      { buf12, kbd_PGUP    }, { buf13, kbd_PGDN    },
      { buf14, kbd_HOME    }, { buf15, kbd_END     },
         // remainder are alternatives for above, just in case...
         // ( the k,j,l,h entries are the vim cursor motion keys )
      { "\033\\",   kbd_UP    }, { "\033/",    kbd_DOWN  }, /* meta+      \,/ */
      { "\033<",    kbd_LEFT  }, { "\033>",    kbd_RIGHT }, /* meta+      <,> */
      { "\033k",    kbd_UP    }, { "\033j",    kbd_DOWN  }, /* meta+      k,j */
      { "\033h",    kbd_LEFT  }, { "\033l",    kbd_RIGHT }, /* meta+      h,l */
      { "\033\013", kbd_PGUP  }, { "\033\012", kbd_PGDN  }, /* ctrl+meta+ k,j */
      { "\033\010", kbd_HOME  }, { "\033\014", kbd_END   }  /* ctrl+meta+ h,l */
   };
#ifdef TERMIOS_ONLY
   char buf[SMLBUFSIZ], *pb;
#else
   static char buf[SMLBUFSIZ];
   static int pos, len;
   char *pb;
#endif
   int i;

   if (action == 0) 
   {
    #define tOk(s)  s ? s : ""
      tinfo_tab[1].str  = tOk(key_up);
      tinfo_tab[2].str  = tOk(key_down);
      tinfo_tab[3].str  = tOk(key_left);
      tinfo_tab[4].str  = tOk(key_right);
      tinfo_tab[5].str  = tOk(key_ppage);
      tinfo_tab[6].str  = tOk(key_npage);
      tinfo_tab[7].str  = tOk(key_home);
      tinfo_tab[8].str  = tOk(key_end);
      tinfo_tab[9].str  = tOk(key_backspace);
      tinfo_tab[10].str = tOk(key_ic);
      tinfo_tab[11].str = tOk(key_dc);
      STRLCPY(buf12, fmtmk("\033%s", tOk(key_up)));
      STRLCPY(buf13, fmtmk("\033%s", tOk(key_down)));
      STRLCPY(buf14, fmtmk("\033%s", tOk(key_left)));
      STRLCPY(buf15, fmtmk("\033%s", tOk(key_right)));
      // next is critical so returned results match bound terminfo keys
      putp(tOk(keypad_xmit));
      // ( converse keypad_local issued at pause/pgm end, just in case )
#ifdef LOG2STDOUT
      fprintf(LogPtr, "tOk(key_up)         tinfo_tab[1].str  = %s\n", tinfo_tab[1].str);
      fprintf(LogPtr, "tOk(key_down)       tinfo_tab[2].str  = %s\n", tinfo_tab[2].str);
      fprintf(LogPtr, "tOk(key_left)       tinfo_tab[3].str  = %s\n", tinfo_tab[3].str);
      fprintf(LogPtr, "tOk(key_right)      tinfo_tab[4].str  = %s\n", tinfo_tab[4].str);
      fprintf(LogPtr, "tOk(key_ppage)      tinfo_tab[5].str  = %s\n", tinfo_tab[5].str);
      fprintf(LogPtr, "tOk(key_npage)      tinfo_tab[6].str  = %s\n", tinfo_tab[6].str);
      fprintf(LogPtr, "tOk(key_home)       tinfo_tab[7].str  = %s\n", tinfo_tab[7].str);
      fprintf(LogPtr, "tOk(key_end)        tinfo_tab[8].str  = %s\n", tinfo_tab[8].str);
      fprintf(LogPtr, "tOk(key_backspace)  tinfo_tab[9].str  = %s\n", tinfo_tab[9].str);
      fprintf(LogPtr, "tOk(key_ic)         tinfo_tab[10].str = %s\n", tinfo_tab[10].str);
      fprintf(LogPtr, "tOk(key_dc)         tinfo_tab[11].str = %s\n", tinfo_tab[11].str);
      fflush(LogPtr);
#endif
      return 0;
    #undef tOk
   }

   if (action == 1) 
   {
      memset(buf, '\0', sizeof(buf));
      if (1 > ioch(0, buf, sizeof(buf)-1)) return 0;
   }

#ifndef TERMIOS_ONLY
   if (action == 2) 
   {
      if (pos < len)
         return buf[pos++];            // exhaust prior keystrokes
      pos = len = 0;
      memset(buf, '\0', sizeof(buf));
      int ll = ioch(0, buf, sizeof(buf)-1);
      if (1 > ll) return 0;
      if (isprint(buf[0])) 
      {           // no need for translation
         len = strlen(buf);
         pos = 1;
         return buf[0];
      }
   }
#endif

   /* some emulators implement 'key repeat' too well and we get duplicate
      key sequences -- so we'll focus on the last escaped sequence, while
      also allowing use of the meta key... */
   if (!(pb = strrchr(buf, '\033'))) pb = buf;
   else if (pb > buf && '\033' == *(pb - 1)) --pb;

   for (i = 0; i < MAXTBL(tinfo_tab); i++)
   {
      if (!strcmp(tinfo_tab[i].str, pb))
      {
         return tinfo_tab[i].key;
      }
   }

   // no match, so we'll return single non-escaped keystrokes only
   if (buf[0] == '\033' && buf[1]) return 0;

   // backspace always return 127 in buf[0], i don't know why.
   if (buf[0] == 127) return kbd_BKSP;

   return buf[0];
} // end: iokey


#ifdef TERMIOS_ONLY
        /*
         * Get line oriented interactive input from the user,
         * using native tty support */
static char *ioline (const char *prompt) 
{
   static const char ws[] = "\b\f\n\r\t\v\x1b\x9b";  // 0x1b + 0x9b are escape
   static char buf[MEDBUFSIZ];
   char *p;

   show_pmt(prompt);
   memset(buf, '\0', sizeof(buf));
   ioch(1, buf, sizeof(buf)-1);

   if ((p = strpbrk(buf, ws))) *p = '\0';
   // note: we DO produce a vaid 'string'
   return buf;
} // end: ioline

#else
        /*
         * Get line oriented interactive input from the user,
         * going way beyond native tty support by providing:
         * . true line editing, not just destructive backspace
         * . an input limit sensitive to current screen dimensions
         * . ability to recall prior strings for re-input/re-editing */
static char *ioline (const char *prompt) 
{
 #define savMAX  50
    // thank goodness memmove allows the two strings to overlap
 #define sqzSTR  { memmove(&buf[pos], &buf[pos+1], bufMAX-pos); \
       buf[sizeof(buf)-1] = '\0'; }
 #define expSTR  if (len+1 < bufMAX && len+beg+1 < Screen_cols) { \
       memmove(&buf[pos+1], &buf[pos], bufMAX-pos); buf[pos] = ' '; }
 #define logCOL  (pos+1)
 #define phyCOL  (beg+pos+1)
 #define bufMAX  ((int)sizeof(buf)-2)  // -1 for '\0' string delimeter
   static char buf[MEDBUFSIZ+1];       // +1 for '\0' string delimeter
   static int ovt;
   int beg, pos, len, key, i;
   struct lin_s {
      struct lin_s *bkw;               // ptr to older saved strs
      struct lin_s *fwd;               // ptr to newer saved strs
      char *str;                       // the saved string
   };
   static struct lin_s *anchor, *plin;

   if (!anchor) 
   {
      anchor = alloc_c(sizeof(struct lin_s));
      anchor->str = alloc_s("");       // top-of-stack == empty str
   }
   plin = anchor;
   pos = 0;
   beg = show_pmt(prompt);
   memset(buf, '\0', sizeof(buf));
   putp(ovt ? Cap_curs_huge : Cap_curs_norm);

   do 
   {
      fflush(stdout);
      len = strlen(buf);
      key = iokey(2);
      switch (key) {
         case 0:
         case kbd_ESC:
            buf[0] = '\0';             // fall through !
         case kbd_ENTER:
            continue;
         case kbd_INS:
            ovt = !ovt;
            putp(ovt ? Cap_curs_huge : Cap_curs_norm);
            break;
         case kbd_DEL:
            sqzSTR
            break;
         case kbd_BKSP :
            if (0 < pos) { --pos; sqzSTR }
            break;
         case kbd_LEFT:
            if (0 < pos) --pos;
            break;
         case kbd_RIGHT:
            if (pos < len) ++pos;
            break;
         case kbd_HOME:
            pos = 0;
            break;
         case kbd_END:
            pos = len;
            break;
         case kbd_UP:
            if (plin->bkw) 
            {
               plin = plin->bkw;
               memset(buf, '\0', sizeof(buf));
               pos = snprintf(buf, sizeof(buf), "%s", plin->str);
            }
            break;
         case kbd_DOWN:
            memset(buf, '\0', sizeof(buf));
            if (plin->fwd) plin = plin->fwd;
            pos = snprintf(buf, sizeof(buf), "%s", plin->str);
            break;
         default:                      // what we REALLY wanted (maybe)
            if (isprint(key) && logCOL < bufMAX && phyCOL < Screen_cols) 
            {
               if (!ovt) expSTR
               buf[pos++] = key;
            }
            break;
      }
      putp(fmtmk("%s%s%s", tg2(beg, Msg_row), Cap_clr_eol, buf));
      putp(tg2(beg+pos, Msg_row));
   } while (key && key != kbd_ENTER && key != kbd_ESC);

   // weed out duplicates, including empty strings (top-of-stack)...
   for (i = 0, plin = anchor; ; i++) 
   {
#ifdef RECALL_FIXED
      if (!STRCMP(plin->str, buf))     // if matched, retain original order
         return buf;
#else
      if (!STRCMP(plin->str, buf)) 
      {   // if matched, rearrange stack order
         if (i > 1) 
         {                  // but not null str or if already #2
            if (plin->bkw)             // splice around this matched string
               plin->bkw->fwd = plin->fwd; // if older exists link to newer
            plin->fwd->bkw = plin->bkw;    // newer linked to older or NULL
            anchor->bkw->fwd = plin;   // stick matched on top of former #2
            plin->bkw = anchor->bkw;   // keep empty string at top-of-stack
            plin->fwd = anchor;        // then prepare to be the 2nd banana
            anchor->bkw = plin;        // by sliding us in below the anchor
         }
         return buf;
      }
#endif
      if (!plin->bkw) break;           // let i equal total stacked strings
      plin = plin->bkw;                // ( with plin representing bottom )
   }
   if (i < savMAX)
   {
      plin = alloc_c(sizeof(struct lin_s));
   }
   else 
   {                              // when a new string causes overflow
      plin->fwd->bkw = NULL;           // make next-to-last string new last
      free(plin->str);                 // and toss copy but keep the struct
   }
   plin->str = alloc_s(buf);           // copy user's new unique input line
   plin->bkw = anchor->bkw;            // keep empty string as top-of-stack
   if (plin->bkw)                      // did we have some already stacked?
      plin->bkw->fwd = plin;           // yep, so point prior to new string
   plin->fwd = anchor;                 // and prepare to be a second banana
   anchor->bkw = plin;                 // by sliding it in as new number 2!

   return buf;                         // protect our copy, return original
 #undef savMAX
 #undef sqzSTR
 #undef expSTR
 #undef logCOL
 #undef phyCOL
 #undef bufMAX
} // end: ioline
#endif


        /*
         * This routine provides the i/o in support of files whose size
         * cannot be determined in advance.  Given a stream pointer, he'll
         * try to slurp in the whole thing and return a dynamically acquired
         * buffer supporting that single string glob.
         *
         * He always creates a buffer at least READMINSZ big, possibly
         * all zeros (an empty string), even if the file wasn't read. */
static int readfile (FILE *fp, char **baddr, size_t *bsize, size_t *bread) 
{
   char chunk[4096*16];
   size_t num;

   *bread = 0;
   *bsize = READMINSZ;
   *baddr = alloc_c(READMINSZ);
   if (fp) 
   {
      while (0 < (num = fread(chunk, 1, sizeof(chunk), fp))) 
      {
         *baddr = alloc_r(*baddr, num + *bsize);
         memcpy(*baddr + *bread, chunk, num);
         *bread += num;
         *bsize += num;
      };
      *(*baddr + *bread) = '\0';
      return ferror(fp);
   }
   return ENOENT;
} // end: readfile

/*######  Small Utility routines  ########################################*/

        /*
         * Get a float from the user */
static float get_float (const char *prompt) 
{
   char *line;
   float f;

   line = ioline(prompt);
   if (!line[0] || Frames_signal) return -1.0;
   // note: we're not allowing negative floats
   if (strcspn(line, "+,.0123456789")) 
   {
      show_msg(N_txt_Norm_tab(BAD_numfloat_txt));
      return -1.0;
   }
   sscanf(line, "%f", &f);
   return f;
} // end: get_float


#define GET_INT_BAD  INT_MIN
#define GET_INTNONE (INT_MIN + 1)

        /*
         * Get an integer from the user, returning INT_MIN for error */
static int get_int (const char *prompt) 
{
   char *line;
   int n;

   line = ioline(prompt);
   if (Frames_signal) return GET_INT_BAD;
   if (!line[0]) return GET_INTNONE;
   // note: we've got to allow negative ints (renice)
   if (strcspn(line, "-+0123456789")) 
   {
      show_msg(N_txt_Norm_tab(BAD_integers_txt));
      return GET_INT_BAD;
   }
   sscanf(line, "%d", &n);
   return n;
} // end: get_int


        /*
         * Make a hex value, and maybe suppress zeroes. */
static inline const char *hex_make (KLONG num, int noz) 
{
   static char buf[SMLBUFSIZ];
   int i;

#ifdef CASEUP_HEXES
   snprintf(buf, sizeof(buf), "%08" KLF "X", num);
#else
   snprintf(buf, sizeof(buf), "%08" KLF "x", num);
#endif
   if (noz)
      for (i = 0; buf[i]; i++)
         if ('0' == buf[i])
            buf[i] = '.';
   return buf;
} // end: hex_make


        /*
         * This sructure is hung from a WIN_t when other filtering is active */
struct osel_s {
   struct osel_s *nxt;                         // the next criteria or NULL.
   int (*rel)(const char *, const char *);     // relational strings compare
   char *(*sel)(const char *, const char *);   // for selection str compares
   char *raw;                                  // raw user input (dup check)
   char *val;                                  // value included or excluded
   int   ops;                                  // filter delimiter/operation
   int   inc;                                  // include == 1, exclude == 0
   int   enu;                                  // field (procflag) to filter
};


        /*
         * A function to turn off entire other filtering in the given window */
static void osel_clear (WIN_t *w) 
{
   struct osel_s *osel = w->osel_1st;

   while (osel) 
   {
      struct osel_s *nxt = osel->nxt;
      free(osel->val);
      free(osel->raw);
      free(osel);
      osel = nxt;
   }
   w->osel_tot = 0;
   w->osel_1st = NULL;
   free (w->osel_prt);
   w->osel_prt = NULL;
#ifndef USE_X_COLHDR
   OFFwinflags(Curwin, NOHISEL_xxx);
#endif
} // end: osel_clear


        /*
         * Determine if there is a matching value or releationship among the
         * other criteria in this passed window -- it's called from only one
         * place, and likely inlined even without the directive */
static inline int osel_matched (const WIN_t *w, unsigned char enu, const char *str) 
{
   struct osel_s *osel = w->osel_1st;

   while (osel) 
   {
      if (osel->enu == enu) 
      {
         int r;
         switch (osel->ops) {
            case '<':                          // '<' needs the r < 0 unless
               r = osel->rel(str, osel->val);  // '!' which needs an inverse
               if ((r >= 0 && osel->inc) || (r < 0 && !osel->inc)) return 0;
               break;
            case '>':                          // '>' needs the r > 0 unless
               r = osel->rel(str, osel->val);  // '!' which needs an inverse
#ifdef LOG2STDOUT
      fprintf(LogPtr, "strr = %s\n",str);
      fprintf(LogPtr, "vall = %s\n",osel->val);
      fprintf(LogPtr, "incc = %d\n",osel->inc);
      fprintf(LogPtr, "r = %d\n",r);
#endif
               if ((r <= 0 && osel->inc) || (r > 0 && !osel->inc)) 
               {
#ifdef LOG2STDOUT
                  fprintf(LogPtr, "filtered\n");
#endif
                  return 0;
               }
#ifdef LOG2STDOUT
      fflush(LogPtr);
#endif
               break;
            default:
            {  char *p = osel->sel(str, osel->val);
               if ((!p && osel->inc) || (p && !osel->inc)) return 0;
            }
               break;
         }
      }
      osel = osel->nxt;
   }
   return 1;
} // end: osel_matched

#ifdef BUILD_4_STOCK

static const char *stock_certify (WIN_t *w, const char *str, char typ) 
{
   struct passwd *pwd;
   char *endp;
   uid_t num;

   if (strlen(str) > 6) return N_txt_Norm_tab(BAD_stockid_txt);

   w->user_select_type = 0;
   w->stock_select_flags = 1;

   Monpidsidx = 0;

   if (*str) 
   {
      if ('!' == *str) { ++str; w->stock_select_flags = 0; }
      num = (uid_t)strtoul(str, &endp, 0);
      strcpy(w->filter_stock_prefix, str);
      w->user_select_type = typ;
   }
   else
   {
      strcpy(w->filter_stock_prefix, "");
   }
   return NULL;
} // end: 

#endif

        /*
         * Validate the passed string as a user name or number,
         * and/or update the window's 'u/U' selection stuff. */
static const char *user_certify (WIN_t *w, const char *str, char typ) 
{
   struct passwd *pwd;
   char *endp;
   uid_t num;

   w->user_select_type = 0;
   w->usrselflg = 1;
   Monpidsidx = 0;
   if (*str) 
   {
      if ('!' == *str) { ++str; w->usrselflg = 0; }
      num = (uid_t)strtoul(str, &endp, 0);
      if ('\0' == *endp) 
      {
         pwd = getpwuid(num);
         if (!pwd) 
         {
         /* allow foreign users, from e.g within chroot
          ( thanks Dr. Werner Fink <werner@suse.de> ) */
            w->usrseluid = num;
            w->user_select_type = typ;
            return NULL;
         }
      } 
      else
      {
         pwd = getpwnam(str);
      }
      if (!pwd) return N_txt_Norm_tab(BAD_username_txt);
      w->usrseluid = pwd->pw_uid;
      w->user_select_type = typ;
   }
   return NULL;
} // end: user_certify

#ifdef BUILD_4_STOCK

int strstarts(const char *source, const char *exp)
{
   char *p1, *p2;
   p1 = source;
   p2 = exp;
   while(*p1 && *p2)
   {
      if (*p1 == *p2)
      {
         p1++;
         p2++;
      }
      else
      {
         return 0;
      }
   }
   if (*p1 == '\0' && *p2 == '\0') return 1;
   if (*p2 == '\0') return 1;
   return 0;
}

static inline int filter_stock(const WIN_t *w, const proc_t *p)
{
   int ret = 0;
// strategy 1  ------------  bbi golden 
   if (
       (p->current_price > p->boll_daily_lower) && 
//       (p->current_price < p->boll_daily_middle) &&
       (p->current_price > p->bbi_daily) &&
       (p->pre_close < p->bbi_daily) 
       && p->macd_daily > 0 
       && ((p->dzx_G_daily || p->macd_G_daily ) || (p->kdj_G_daily && p->rsi_G_daily))
      )
   {
      
//      p->sweight = 100*sum/4;
      return 1;
   }

// strategy 2  ------------  boll golden 
   if ((p->current_price > p->boll_daily_lower) && 
       (p->pre_close < p->boll_daily_lower) &&
       (p->current_price < p->bbi_daily) && 
       (p->dzx_G_daily)// || p->macd_G_daily )//p->kdj_G_daily || p->rsi_G_daily)
      )
      return 1;
   return 0;

//   if (
//       p->current_price > 1*FACTOR && p->current_price < 29*FACTOR && 
//       (p->current_price > p->boll_daily_lower) && 
//       (p->current_price < p->boll_daily_middle) &&
//       (p->current_price < p->bbi_daily) &&
//       (p->current_price > p->bbi_60_min) &&
//       p->current_price > p->ma5 &&
//       p->current_price < p->ma10 &&
//       p->volume_ratio > 988 && 
//       //(p->kdj_G_daily) &&
//       p->diff_daily < p->dea_daily &&
//       p->dea_daily < p->macd_daily &&
//       p->macd_daily < 0 &&
//       p->current_price > p->boll_60_middle
//       //(p->current_price > p->boll_60_lower) && 
//       //(p->pre_close < p->boll_60_lower) &&  p->kdj_G_60 &&
//       //(p->dzx_G_60 || p->macd_G_60 || p->kdj_G_60 || p->rsi_G_60)
//      )
//      return 1; 
//   if (
//       (p->current_price > p->boll_daily_lower) && 
//       (p->pre_close < p->boll_daily_lower) &&
//       (p->dzx_G_daily || p->macd_G_daily || p->kdj_G_daily || p->rsi_G_daily)
//       //(p->current_price > p->boll_60_lower) && 
//       //(p->pre_close < p->boll_60_lower) &&  p->kdj_G_60 &&
//       //(p->dzx_G_60 || p->macd_G_60 || p->kdj_G_60 || p->rsi_G_60)
//      )
//      return 1; 
//   if ((p->current_price >= p->bbi_60_min) && 
//       (p->pre_close < p->bbi_60_min) &&
//       (p->dzx_G_60 && p->kdj_G_60)// || p->rsi_G_60)
//      )
////       (p->dzx_G_60))// && p->macd_G_60 ))//&& p->kdj_G_60))// || p->rsi_G_60))
//      return 1;
//   if ((p->current_price >= p->bbi_daily) &&
//        //p->current_price > p->ma5 &&
//        p->current_price <= p->ma10 &&
//      //  p->current_price <= p->pre_close &&
////        p->ma5 < p->ma10 &&
////        p->kdj_daily_j > p->kdj_daily_k &&
//        p->volume_ratio > 1580 && 
//        p->turn_over_rate > 2822 && p->turn_over_rate < 10000
//      ) 
//      return 1;
   if (
        p->current_price > 1*FACTOR && p->current_price < 29*FACTOR && 
        //p->ddbuy > p->ddsell &&
        //p->ddr > 12000 &&
//        ((p->macd_daily > 0 && p->macd_daily < 0.05) && (p->dzx_G_daily))
        p->current_price > p->ma5 &&
        
//        && p->kdj_daily_j > p->kdj_daily_k
       (
        p->current_price > p->average_price &&
        p->current_price > p->ma5 &&
//        p->pre_close < p->ma5 && //golden cross ma5
        p->current_price > p->ma10 &&
        //p->ma5 < p->ma10 &&
        //p->ma10 < p->ma20 &&
        //p->ma30 > p->ma60 &&
        p->outer > p->inner &&
        p->turn_over_rate > 2822 && p->turn_over_rate < 10000 && 
        p->value_liutongA < 100 &&
        //p->swing < 5000 &&
        p->volume_ratio > 1580
        )
        //&& (p->rsi_G_daily && p->kdj_G_daily)
        && (p->current_price > p->bbi_daily)
        //&& (p->pre_close < p->bbi_daily)
        && p->main_in_cash > p->main_out_cash     
       &&((p->macd_daily > 0 && p->macd_daily < 0.08) && (p->rsi_G_daily || p->kdj_G_daily || p->macd_G_daily))
        //&& p->main_net_rate > 10
        //&& p->main_in_cash > p->private_in_cash     
        //&& p->main_net_cash    
        //&& p->main_net_rate    
        //&& p->private_in_cash  
        //&& p->private_out_cash 
        //&& p->private_net_cash 
        //&& p->private_net_rate 
        //&& p->total_cash       
 // for funds
/*
       || 
       (p->stockid == 2336 || p->stockid == 717 || p->stockid == 537 || p->stockid == 600251 || p->stockid == 600814 ||
        p->stockid == 809 || p->stockid == 2263 || p->stockid == 600831 || p->stockid == 600586 || p->stockid == 600616 ||
        p->stockid == 600581 || p->stockid == 721) 
*/
       ) 
      ret = 1;
   return ret; 
}
        /*
         * Determine if this proc_t matches the 'u/U' selection criteria
         * for a given window -- it's called from only one place, and
         * likely inlined even without the directive */
static inline int stock_matched (const WIN_t *w, const proc_t *p) 
{
   if (!CHKwinflags(w, Show_IDLEPS))
      return filter_stock(w, p);

   switch(w->user_select_type) 
   {
      case 0:                                    // uid selection inactive
         return 1;
      case 'U':                                  // match any uid
//         if (p->ruid == w->filter_stock_prefix) return w->stock_select_flags;
//         if (p->suid == w->filter_stock_prefix) return w->stock_select_flags;
//         if (p->fuid == w->filter_stock_prefix) return w->stock_select_flags;
      // fall through...
      case 'u':                                  // match effective uid
//         if (p->euid == w->filter_stock_prefix) return w->stock_select_flags;
//         if (p->stockid == w->filter_stock_prefix) return w->stock_select_flags;
         if (strstarts(p->str_stockid, w->filter_stock_prefix)) 
         {
            return w->stock_select_flags;
         }
      
      // fall through...
      default:                                   // no match...
         ;
   }
   return !w->stock_select_flags;
} // end: stock_matched
#endif

        /*
         * Determine if this proc_t matches the 'u/U' selection criteria
         * for a given window -- it's called from only one place, and
         * likely inlined even without the directive */
static inline int user_matched (const WIN_t *w, const proc_t *p) 
{
   switch(w->user_select_type) 
   {
      case 0:                                    // uid selection inactive
         return 1;
      case 'U':                                  // match any uid
         if (p->ruid == w->usrseluid) return w->usrselflg;
         if (p->suid == w->usrseluid) return w->usrselflg;
         if (p->fuid == w->usrseluid) return w->usrselflg;
      // fall through...
      case 'u':                                  // match effective uid
         if (p->euid == w->usrseluid) return w->usrselflg;
      // fall through...
      default:                                   // no match...
         ;
   }
   return !w->usrselflg;
} // end: user_matched

/*######  Basic Formatting support  ######################################*/

        /*
         * Just do some justify stuff, then add post column padding. */
static inline const char *justify_pad (const char *str, int width, int justr) 
{
   static char l_fmt[]  = "%-*.*s%s", r_fmt[] = "%*.*s%s";
   static char buf[SCREENMAX];

   snprintf(buf, sizeof(buf), justr ? r_fmt : l_fmt, width, width, str, COLPADSTR);
   return buf;
} // end: justify_pad


        /*
         * Make and then justify a single character. */
static inline const char *make_char (const char ch, int width, int justr) 
{
   static char buf[SMLBUFSIZ];

   snprintf(buf, sizeof(buf), "%c", ch);
   return justify_pad(buf, width, justr);
} // end: make_char

#ifdef BUILD_4_STOCK
static inline const char *make_time (int hour, int minute, int second, int width, int justr, int col) 
{
   static char buf[SMLBUFSIZ];

   if (width < snprintf(buf, sizeof(buf), "%d:%d:%d", hour, minute, second)) 
   {
      buf[width-1] = COLPLUSCH;
      AUTOX_COL(col);
   }
   return justify_pad(buf, width, justr);
} // end: make_number

static inline const char *make_day (int year, int month, int day, int width, int justr, int col) 
{
   static char buf[SMLBUFSIZ];

   if (width < snprintf(buf, sizeof(buf), "%d-%d-%d", year, month, day)) 
   {
      buf[width-1] = COLPLUSCH;
      AUTOX_COL(col);
   }
   return justify_pad(buf, width, justr);
} // end: make_number

static inline const char *make_stockid (long num, int width, int justr, int col) 
{
   static char buf[SMLBUFSIZ];
  
   if (num < 10)
   {
      if (width < snprintf(buf, sizeof(buf), "00000%ld", num)) 
      {
         buf[width-1] = COLPLUSCH;
         AUTOX_COL(col);
      }
   }
   else if (num < 100)
   {
      if (width < snprintf(buf, sizeof(buf), "0000%ld", num)) 
      {
         buf[width-1] = COLPLUSCH;
         AUTOX_COL(col);
      }
   }
   else if (num < 1000)
   {
      if (width < snprintf(buf, sizeof(buf), "000%ld", num)) 
      {
         buf[width-1] = COLPLUSCH;
         AUTOX_COL(col);
      }
   }
   else if (num < 10000)
   {
      if (width < snprintf(buf, sizeof(buf), "00%ld", num)) 
      {
         buf[width-1] = COLPLUSCH;
         AUTOX_COL(col);
      }
   }
   else if (num < 100000)
   {
      if (width < snprintf(buf, sizeof(buf), "0%ld", num)) 
      {
         buf[width-1] = COLPLUSCH;
         AUTOX_COL(col);
      }
   }
   else
   {
      if (width < snprintf(buf, sizeof(buf), "%ld", num)) 
      {
         buf[width-1] = COLPLUSCH;
         AUTOX_COL(col);
      }
   }

   return justify_pad(buf, width, justr);
} // end: 

static inline const char *make_float2 (float num, int width, int justr, int col) 
{
   static char buf[SMLBUFSIZ];

   if (width < snprintf(buf, sizeof(buf), "%.2f", num)) 
   {
      buf[width-1] = COLPLUSCH;
      AUTOX_COL(col);
   }
   return justify_pad(buf, width, justr);
} // end: make_float

static inline const char *make_float (float num, int width, int justr, int col) 
{
   static char buf[SMLBUFSIZ];

   if (width < snprintf(buf, sizeof(buf), "%.3f", num)) 
   {
      buf[width-1] = COLPLUSCH;
      AUTOX_COL(col);
   }
   return justify_pad(buf, width, justr);
} // end: make_float

static inline const char *make_volume_number (int scale, long long num, int width, int justr, int col) 
{
   static char buf[SMLBUFSIZ];
   if (scale == VE_shou)
   {
      num /= 100;
   }

   if (width < snprintf(buf, sizeof(buf), "%lld", num)) 
   {
      buf[width-1] = COLPLUSCH;
      AUTOX_COL(col);
   }
   return justify_pad(buf, width, justr);
} // end: make_number

        /*
         * Make and then justify an integer NOT subject to scaling,
         * and include a visual clue should tuncation be necessary. */
static inline const char *make_rmb_number (int scale, long long num, int width, int justr, int col) 
{
   static char buf[SMLBUFSIZ];
   if (scale == rmb_wan)
   {
      float f = (float)num/10000;
      num /= 10000;
      {
         if (width < snprintf(buf, sizeof(buf), "%lld", num)) 
         {
            buf[width-1] = COLPLUSCH;
            AUTOX_COL(col);
         }
         return justify_pad(buf, width, justr);
      }
   }
   else
   {
      if (width < snprintf(buf, sizeof(buf), "%lld", num)) 
      {
         buf[width-1] = COLPLUSCH;
         AUTOX_COL(col);
      }
      return justify_pad(buf, width, justr);
   }
} // end: make_rmb_number

#endif

        /*
         * Make and then justify an integer NOT subject to scaling,
         * and include a visual clue should tuncation be necessary. */
static inline const char *make_number (long num, int width, int justr, int col) 
{
   static char buf[SMLBUFSIZ];

#ifdef BUILD_4_STOCK
   if (col == S_INDEX)
   {
      if (width < snprintf(buf, sizeof(buf), " %ld", num)) 
      {
         buf[width-1] = COLPLUSCH;
         AUTOX_COL(col);
      }
      return justify_pad(buf, width, justr);
   }
#endif

   if (width < snprintf(buf, sizeof(buf), "%ld", num)) 
   {
      buf[width-1] = COLPLUSCH;
      AUTOX_COL(col);
   }
   return justify_pad(buf, width, justr);
} // end: make_number


        /*
         * Make and then justify a character string,
         * and include a visual clue should tuncation be necessary. */

static inline const char *make_string (const char *str, int width, int justr, int col) 
{
   static char buf[SCREENMAX];

   if (width < snprintf(buf, sizeof(buf), "%s", str)) 
   {
      buf[width-1] = COLPLUSCH;
      AUTOX_COL(col);
   }
   return justify_pad(buf, width, justr);
} // end: make_string

#ifdef BUILD_4_STOCK
size_t GetUtf8StrLen(const char* pStr)
{
   size_t nLen = 0;
   char c = '\0';
   while ('\0' != (c = *pStr))
   {
        // This char is ascii
      if (0 == (0x80 & c))
      {
         nLen++;
         pStr++;
         continue;
      }
      else
      {
       // This is NOT a utf-8 header char
         if (0 == (0x40 & c))
         {
            pStr++;
            continue;
         }
         // Parse the utf-8 header char to parse the char length
         unsigned char l = ((0xF0 & c) >> 4);
         switch (l)
         { 
         case 0xF:// utf-8 char is 4 bytes
            pStr += 4;
            break; 
         case 0xE:// utf-8 char is 3 bytes
            pStr += 3;
            break;
         case 0xC:// utf-8 char is 2 bytes
            pStr += 2;
            break;
         }
         nLen++;
      }
   }
   return nLen;
} 

int IsUTF8String(const char* str, int length)
{
    int i = 0;
    int nBytes = 0;//UTF8可用1-6个字节编码,ASCII用一个字节
    unsigned char chr = 0;
    bool bAllAscii = 1;//如果全部都是ASCII,说明不是UTF-8
 
    while (i < length)
    {
        chr = *(str + i);
        if ((chr & 0x80) != 0)
            bAllAscii = 0;
        if (nBytes == 0)//计算字节数
        {
            if ((chr & 0x80) != 0)
            {
                while ((chr & 0x80) != 0)
                {
                    chr <<= 1;
                    nBytes++;
                }
                if (nBytes < 2 || nBytes > 6)
                    return 0;//第一个字节最少为110x xxxx
                nBytes--;//减去自身占的一个字节
            }
        }
        else//多字节除了第一个字节外剩下的字节
        {
            if ((chr & 0xc0) != 0x80)
                return 0;//剩下的字节都是10xx xxxx的形式
            nBytes--;
        }
        ++i;
    }
    if (bAllAscii)
        return 0;
    return nBytes == 0;
}

static inline const char *make_utf8_string (const char *str, int width, int justr, int col) 
{
   static char buf[SCREENMAX];

   int lenof_null_terminated_string = strlen(str);
   if (!IsUTF8String(str, lenof_null_terminated_string)) // is not utf8 string
   {
      if (width < snprintf(buf, sizeof(buf), "%s", str)) 
      {
         buf[width-1] = COLPLUSCH;
         AUTOX_COL(col);
      }
      return justify_pad(buf, width, justr);
   }
   else
   {
      snprintf(buf, sizeof(buf), "%s", str);
      int utf8len = GetUtf8StrLen(str);
      int utf8CharacterCount = (lenof_null_terminated_string - utf8len) / 2;
#ifdef LOG2STDOUT
      fprintf(LogPtr, "utf8 str = %s, strlen = %d, isutf8 = %d, utf8len = %d\n", str, lenof_null_terminated_string, IsUTF8String(str, lenof_null_terminated_string), utf8len); 
      fflush(LogPtr);
#endif
      strncat(buf, " ", utf8CharacterCount); 
      if (width + utf8CharacterCount > 13)
      {
         buf[12] = COLPLUSCH;
         buf[13] = '\0';
         AUTOX_COL(col);
      }
      return justify_pad(buf, (width + utf8CharacterCount) > 13 ? 13 : (width + utf8CharacterCount), justr);  
   }
} // end: 

static const char *float_colored_scale_percent (float num, float old, int width, int justr, int colored) 
{
   static char buf[SMLBUFSIZ];
   static char buf2[SCREENMAX];
   char * ptr = 0;

   buf[0] = '\0';
   if (Rc.zero_suppress && 0 >= num)
      goto end_justifies;
#ifdef BOOST_PERCNT
   if (width >= snprintf(buf, sizeof(buf), "%#.3f", num))
      goto end_justifies;
   if (width >= snprintf(buf, sizeof(buf), "%#.2f", num))
      goto end_justifies;
#endif
   if (width >= snprintf(buf, sizeof(buf), "%#.2f", num))
      goto end_justifies;
   if (width >= snprintf(buf, sizeof(buf), "%*.0f", width, num))
      goto end_justifies;

   // well shoot, this outta' fit...
   snprintf(buf, sizeof(buf), "?");
end_justifies:
   ptr = justify_pad(buf, width, justr);
   if (colored == 1)
   {
      if (num > old)
      {
         strcpy(buf2, Caps_red);
         strcat(buf2, ptr);
         strcat(buf2, Caps_normal);
      }
      else if (num == old)
      {
         strcpy(buf2, Caps_magenta);
         strcat(buf2, ptr);
         strcat(buf2, Caps_normal);
      }
      else
      {
         strcpy(buf2, Caps_green);
         strcat(buf2, ptr);
         strcat(buf2, Caps_normal);
      }
   }
   else
   {
      strcpy(buf2, Caps_yellow);
      strcat(buf2, ptr);
   }
   return buf2;
} // end: float_scale_percent

static const char *float_scale_percent (float num, int width, int justr) 
{
   static char buf[SMLBUFSIZ];

   buf[0] = '\0';
   if (Rc.zero_suppress && 0 >= num)
      goto end_justifies;
#ifdef BOOST_PERCNT
   if (width >= snprintf(buf, sizeof(buf), "%#.3f", num))
      goto end_justifies;
   if (width >= snprintf(buf, sizeof(buf), "%#.2f", num))
      goto end_justifies;
#endif
   if (width >= snprintf(buf, sizeof(buf), "%#.2f", num))
      goto end_justifies;
   if (width >= snprintf(buf, sizeof(buf), "%*.0f", width, num))
      goto end_justifies;

   // well shoot, this outta' fit...
   snprintf(buf, sizeof(buf), "?");
end_justifies:
   return justify_pad(buf, width, justr);
} // end: float_scale_percent

static const char *percent_float_colored_string (float num, int width, int justr, int colored) 
{
   static char buf[SMLBUFSIZ];
   static char percent_buf[SCREENMAX];
   char * ptr = 0;

   buf[0] = '\0';
   if (Rc.zero_suppress && 0 >= num)
      goto end_justifies;
#ifdef BOOST_PERCNT
   if (width >= snprintf(buf, sizeof(buf), "%#.3f%s", num, "%"))
      goto end_justifies;
   if (width >= snprintf(buf, sizeof(buf), "%#.2f%s", num, "%"))
      goto end_justifies;
#endif
   if (width >= snprintf(buf, sizeof(buf), "%#.2f%s", num, "%"))
      goto end_justifies;
   if (width >= snprintf(buf, sizeof(buf), "%*.0f%s", width, num, "%"))
      goto end_justifies;

   // well shoot, this outta' fit...
   snprintf(buf, sizeof(buf), "?");
end_justifies:
   if (!colored)
      return justify_pad(buf, width, justr);
   // cp = (X_XON == i ? Caps_green : w->capclr_rownorm);
   ptr = justify_pad(buf, width, justr);
   if (colored == 1)
   {
      if (num > 0)
      {
         strcpy(percent_buf, Caps_red);
         strcat(percent_buf, ptr);
         strcat(percent_buf, Caps_normal);
      }
      else if (num == 0)
      {
         strcpy(percent_buf, Caps_magenta);
         strcat(percent_buf, ptr);
         strcat(percent_buf, Caps_normal);
      }
      else
      {
         strcpy(percent_buf, Caps_green);
         strcat(percent_buf, ptr);
         strcat(percent_buf, Caps_normal);
      }
      return percent_buf;
   }
   else
   {
//      if (num > 0)
      {
         strcpy(percent_buf, Caps_yellow);
         strcat(percent_buf, ptr);
//         strcat(percent_buf, Caps_normal);
      }
/*
      else if (num == 0)
      {
         strcpy(percent_buf, Caps_magenta);
         strcat(percent_buf, ptr);
         strcat(percent_buf, Caps_normal);
      }
      else
      {
         strcpy(percent_buf, Caps_green);
         strcat(percent_buf, ptr);
         strcat(percent_buf, Caps_normal);
      }
*/
      return percent_buf;
   }
} // end: percent_float_colored_string

#endif

        /*
         * Do some scaling then justify stuff.
         * We'll interpret 'num' as a kibibytes quantity and try to
         * format it to reach 'target' while also fitting 'width'. */
static const char *scale_mem (int target, unsigned long num, int width, int justr) 
{
#ifndef NOBOOST_MEMS
   //                               SK_Kb   SK_Mb      SK_Gb      SK_Tb      SK_Pb      SK_Eb
   static const char *fmttab[] =  { "%.0f", "%#.1f%c", "%#.3f%c", "%#.3f%c", "%#.3f%c", NULL };
#else
   static const char *fmttab[] =  { "%.0f", "%.0f%c",  "%.0f%c",  "%.0f%c",  "%.0f%c",  NULL };
#endif
   static char buf[SMLBUFSIZ];
   float scaled_num;
   char *psfx;
   int i;

   buf[0] = '\0';
   if (Rc.zero_suppress && 0 >= num)
      goto end_justifies;

   scaled_num = num;
   for (i = SK_Kb, psfx = Scaled_sfxtab; i < SK_Eb; psfx++, i++) 
   {
      if (i >= target
      && (width >= snprintf(buf, sizeof(buf), fmttab[i], scaled_num, *psfx)))
         goto end_justifies;
      scaled_num /= 1024.0;
   }

   // well shoot, this outta' fit...
   snprintf(buf, sizeof(buf), "?");
end_justifies:
   return justify_pad(buf, width, justr);
} // end: scale_mem


        /*
         * Do some scaling then justify stuff. */
static const char *scale_num (unsigned long num, int width, int justr) 
{
   static char buf[SMLBUFSIZ];
   float scaled_num;
   char *psfx;

   buf[0] = '\0';
   if (Rc.zero_suppress && 0 >= num)
      goto end_justifies;
   if (width >= snprintf(buf, sizeof(buf), "%lu", num))
      goto end_justifies;

   scaled_num = num;
   for (psfx = Scaled_sfxtab; 0 < *psfx; psfx++) 
   {
      scaled_num /= 1024.0;
      if (width >= snprintf(buf, sizeof(buf), "%.1f%c", scaled_num, *psfx))
         goto end_justifies;
      if (width >= snprintf(buf, sizeof(buf), "%.0f%c", scaled_num, *psfx))
         goto end_justifies;
   }

   // well shoot, this outta' fit...
   snprintf(buf, sizeof(buf), "?");
end_justifies:
   return justify_pad(buf, width, justr);
} // end: scale_num


        /*
         * Make and then justify a percentage, with decreasing precision. */
static const char *scale_percent (float num, int width, int justr) 
{
   static char buf[SMLBUFSIZ];

   buf[0] = '\0';
   if (Rc.zero_suppress && 0 >= num)
      goto end_justifies;
#ifdef BOOST_PERCNT
   if (width >= snprintf(buf, sizeof(buf), "%#.3f", num))
      goto end_justifies;
   if (width >= snprintf(buf, sizeof(buf), "%#.2f", num))
      goto end_justifies;
#endif
   if (width >= snprintf(buf, sizeof(buf), "%#.1f", num))
      goto end_justifies;
   if (width >= snprintf(buf, sizeof(buf), "%*.0f", width, num))
      goto end_justifies;

   // well shoot, this outta' fit...
   snprintf(buf, sizeof(buf), "?");
end_justifies:
   return justify_pad(buf, width, justr);
} // end: scale_percent


        /*
         * Do some scaling stuff.
         * Format 'tics' to fit 'width', then justify it. */
static const char *scale_tics (TIC_t tics, int width, int justr) 
{
#ifdef CASEUP_SUFIX
 #define HH "%uH"                                                  // nls_maybe
 #define DD "%uD"
 #define WW "%uW"
#else
 #define HH "%uh"                                                  // nls_maybe
 #define DD "%ud"
 #define WW "%uw"
#endif
   static char buf[SMLBUFSIZ];
   unsigned long nt;    // narrow time, for speed on 32-bit
   unsigned cc;         // centiseconds
   unsigned nn;         // multi-purpose whatever

   buf[0] = '\0';
   nt  = (tics * 100ull) / Hertz;               // up to 68 weeks of cpu time
   if (Rc.zero_suppress && 0 >= nt)
      goto end_justifies;
   cc  = nt % 100;                              // centiseconds past second
   nt /= 100;                                   // total seconds
   nn  = nt % 60;                               // seconds past the minute
   nt /= 60;                                    // total minutes
   if (width >= snprintf(buf, sizeof(buf), "%lu:%02u.%02u", nt, nn, cc))
      goto end_justifies;
   if (width >= snprintf(buf, sizeof(buf), "%lu:%02u", nt, nn))
      goto end_justifies;
   nn  = nt % 60;                               // minutes past the hour
   nt /= 60;                                    // total hours
   if (width >= snprintf(buf, sizeof(buf), "%lu,%02u", nt, nn))
      goto end_justifies;
   nn = nt;                                     // now also hours
   if (width >= snprintf(buf, sizeof(buf), HH, nn))
      goto end_justifies;
   nn /= 24;                                    // now days
   if (width >= snprintf(buf, sizeof(buf), DD, nn))
      goto end_justifies;
   nn /= 7;                                     // now weeks
   if (width >= snprintf(buf, sizeof(buf), WW, nn))
      goto end_justifies;

   // well shoot, this outta' fit...
   snprintf(buf, sizeof(buf), "?");
end_justifies:
   return justify_pad(buf, width, justr);
 #undef HH
 #undef DD
 #undef WW
} // end: scale_tics

/*######  Fields Management support  #####################################*/

   /* These are the Fieldstab.fillflag values used here and in calibrate_fields.
      (own identifiers as documentation and protection against changes) */
#define L_stat     PROC_FILLSTAT
#define L_statm    PROC_FILLMEM
#define L_status   PROC_FILLSTATUS
#define L_CGROUP   PROC_EDITCGRPCVT | PROC_FILLCGROUP
#define L_CMDLINE  PROC_EDITCMDLCVT | PROC_FILLARG
#define L_ENVIRON  PROC_EDITENVRCVT | PROC_FILLENV
#define L_EUSER    PROC_FILLUSR
#define L_OUSER    PROC_FILLSTATUS | PROC_FILLUSR
#define L_EGROUP   PROC_FILLSTATUS | PROC_FILLGRP
#define L_SUPGRP   PROC_FILLSTATUS | PROC_FILLSUPGRP
#define L_USED     PROC_FILLSTATUS | PROC_FILLMEM
#define L_NS       PROC_FILLNS
   // make 'none' non-zero (used to be important to Frames_libflags)
#define L_NONE     PROC_SPARE_1
   // from either 'stat' or 'status' (preferred), via bits not otherwise used
#define L_EITHER   PROC_SPARE_2
   // for calibrate_fields and summary_show 1st pass
#define L_DEFAULT  PROC_FILLSTAT

        /* These are our gosh darn 'Fields' !
           They MUST be kept in sync with pflags !! */
static Field_t Fieldstab[] = 
{
   // a temporary macro, soon to be undef'd...
 #define SF(f) (QFunc_Sort_Cb)SCB_NAME(f)
   // these identifiers reflect the default column alignment but they really
   // contain the WIN_t flag used to check/change justification at run-time!
 #define A_right Show_JRNUMS       /* toggled with upper case 'J' */
 #define A_left  Show_JRSTRS       /* toggled with lower case 'j' */

/* .width anomalies:
        a -1 width represents variable width columns
        a  0 width represents columns set once at startup (see zap_fieldstab)
   .fillflag anomalies:
        P_UED, L_NONE  - natural outgrowth of 'stat()' in readproc        (euid)
        P_CPU, L_stat  - never filled by libproc, but requires times      (pcpu)
        P_CMD, L_stat  - may yet require L_CMDLINE in calibrate_fields    (cmd/cmdline)
        L_EITHER       - must L_status, else L_stat == 64-bit math (__udivdi3) on 32-bit !

     .width  .scale  .align    .sort     .fillflag
     ------  ------  --------  --------  --------  */
#ifdef BUILD_4_STOCK
   {     6,     -1,  A_left,   NULL,                  	L_NONE}, // S_INDEX
   {     9,     -1,  A_left,   NULL,	   	        L_NONE}, // S_StockName
   {     0,     -1,  A_left,   SF(S_StockID),  		L_NONE},
   {     6,     -1,  A_left,   SF(S_OPEN),     		L_NONE},
   {     7,     -1,  A_left,   SF(S_CLOSE_YESTODAY),	L_NONE},
   {     6,     -1,  A_left,   SF(S_MAX),    		L_NONE},
   {     6,     -1,  A_left,   SF(S_MIN),    		L_NONE},
   {     7,     -1,  A_left,   SF(S_CURRENT),		L_NONE},
   {     10, VE_gu,  A_left,   SF(S_VOLUME), 		L_NONE},
   {     12,rmb_yuan,A_left,   SF(S_RMB),    		L_NONE},
   {     7,     -1,  A_left,   SF(S_PERCENT),		L_NONE},
   {     7,     -1,  A_left,   SF(S_TURNOVER_RATE),	L_NONE},
   {     11,    -1,  A_left,   SF(S_LIUTONGA), 		L_NONE},
   {     10,    -1,  A_left,   SF(S_VALUE_LIUTONGA), 	L_NONE},
   {     10,    -1,  A_left,   SF(S_DAY), 		L_NONE}, // S_DAY
   {     8,     -1,  A_left,   NULL,    		L_NONE}, // S_TIME
   {     6,     -1,  A_left,   SF(S_VRATIO), 		L_NONE}, 
   {     7,     -1,  A_left,   SF(S_AVERAGE_PRICE), 	L_NONE}, 
   {     7,     -1,  A_left,   SF(S_SWING),		L_NONE},
   {     9,     -1,  A_left,   SF(S_OUTER), 		L_NONE},
   {     9,     -1,  A_left,   SF(S_INNER), 		L_NONE},
   {     8,     -1,  A_left,   SF(S_PE_RATIO), 		L_NONE},
   {     7,     -1,  A_left,   SF(S_MA5), 		L_NONE},
   {     7,     -1,  A_left,   SF(S_MA10), 		L_NONE},
   {     7,     -1,  A_left,   SF(S_MA20), 		L_NONE},
   {     7,     -1,  A_left,   SF(S_MA30), 		L_NONE},
   {     7,     -1,  A_left,   SF(S_MA60), 		L_NONE},
   {     7,     -1,  A_left,   SF(S_DDR),		L_NONE},
   {     7,     -1,  A_left,   SF(S_MNR),		L_NONE},
   {     11,    -1,  A_left,   SF(S_TOTALS), 		L_NONE},
   {     10,    -1,  A_left,   SF(S_VALUE_TOTALS), 	L_NONE},
   {     7,     -1,  A_left,   SF(S_SWEIGHT), 	        L_NONE}, 
   {     7,     -1,  A_left,   SF(S_DOWNSHADOW),        L_NONE}, 
#endif
   {     0,     -1,  A_right,  SF(PID),  L_NONE    },
   {     0,     -1,  A_right,  SF(PPD),  L_EITHER  },
   {     5,     -1,  A_right,  SF(UED),  L_NONE    },
   {     8,     -1,  A_left,   SF(UEN),  L_EUSER   },
   {     5,     -1,  A_right,  SF(URD),  L_status  },
   {     8,     -1,  A_left,   SF(URN),  L_OUSER   },
   {     5,     -1,  A_right,  SF(USD),  L_status  },
   {     8,     -1,  A_left,   SF(USN),  L_OUSER   },
   {     5,     -1,  A_right,  SF(GID),  L_NONE    },
   {     8,     -1,  A_left,   SF(GRP),  L_EGROUP  },
   {     0,     -1,  A_right,  SF(PGD),  L_stat    },
   {     8,     -1,  A_left,   SF(TTY),  L_stat    },
   {     0,     -1,  A_right,  SF(TPG),  L_stat    },
   {     0,     -1,  A_right,  SF(SID),  L_stat    },
   {     3,     -1,  A_right,  SF(PRI),  L_stat    },
   {     3,     -1,  A_right,  SF(NCE),  L_stat    },
   {     3,     -1,  A_right,  SF(THD),  L_EITHER  },
   {     0,     -1,  A_right,  SF(CPN),  L_stat    },
   {     0,     -1,  A_right,  SF(CPU),  L_stat    },
   {     6,     -1,  A_right,  SF(TME),  L_stat    },
   {     9,     -1,  A_right,  SF(TME),  L_stat    }, // P_TM2 slot
#ifdef BOOST_PERCNT
   {     5,     -1,  A_right,  SF(RES),  L_statm   }, // P_MEM slot
#else
   {     4,     -1,  A_right,  SF(RES),  L_statm   }, // P_MEM slot
#endif
#ifndef NOBOOST_MEMS
   {     7,  SK_Kb,  A_right,  SF(VRT),  L_statm   },
   {     6,  SK_Kb,  A_right,  SF(SWP),  L_status  },
   {     6,  SK_Kb,  A_right,  SF(RES),  L_statm   },
   {     6,  SK_Kb,  A_right,  SF(COD),  L_statm   },
   {     7,  SK_Kb,  A_right,  SF(DAT),  L_statm   },
   {     6,  SK_Kb,  A_right,  SF(SHR),  L_statm   },
#else
   {     5,  SK_Kb,  A_right,  SF(VRT),  L_statm   },
   {     4,  SK_Kb,  A_right,  SF(SWP),  L_status  },
   {     4,  SK_Kb,  A_right,  SF(RES),  L_statm   },
   {     4,  SK_Kb,  A_right,  SF(COD),  L_statm   },
   {     5,  SK_Kb,  A_right,  SF(DAT),  L_statm   },
   {     4,  SK_Kb,  A_right,  SF(SHR),  L_statm   },
#endif
   {     4,     -1,  A_right,  SF(FL1),  L_stat    },
   {     4,     -1,  A_right,  SF(FL2),  L_stat    },
   {     4,     -1,  A_right,  SF(DRT),  L_statm   },
   {     1,     -1,  A_right,  SF(STA),  L_EITHER  },
   {    -1,     -1,  A_left,   SF(CMD),  L_EITHER  },
   {    10,     -1,  A_left,   SF(WCH),  L_stat    },
   {     8,     -1,  A_left,   SF(FLG),  L_stat    },
   {    -1,     -1,  A_left,   SF(CGR),  L_CGROUP  },
   {    -1,     -1,  A_left,   SF(SGD),  L_status  },
   {    -1,     -1,  A_left,   SF(SGN),  L_SUPGRP  },
   {     0,     -1,  A_right,  SF(TGD),  L_status  },
#ifdef OOMEM_ENABLE
#define L_oom      PROC_FILLOOM
   {     3,     -1,  A_right,  SF(OOA),  L_oom     },
   {     8,     -1,  A_right,  SF(OOM),  L_oom     },
#undef L_oom
#endif
   {    -1,     -1,  A_left,   SF(ENV),  L_ENVIRON },
   {     3,     -1,  A_right,  SF(FV1),  L_stat    },
   {     3,     -1,  A_right,  SF(FV2),  L_stat    },
#ifndef NOBOOST_MEMS
   {     6,  SK_Kb,  A_right,  SF(USE),  L_USED    },
#else
   {     4,  SK_Kb,  A_right,  SF(USE),  L_USED    },
#endif
   {    10,     -1,  A_right,  SF(NS1),  L_NS      }, // IPCNS
   {    10,     -1,  A_right,  SF(NS2),  L_NS      }, // MNTNS
   {    10,     -1,  A_right,  SF(NS3),  L_NS      }, // NETNS
   {    10,     -1,  A_right,  SF(NS4),  L_NS      }, // PIDNS
   {    10,     -1,  A_right,  SF(NS5),  L_NS      }, // USERNS
   {    10,     -1,  A_right,  SF(NS6),  L_NS      }  // UTSNS
 #undef SF
 #undef A_left
 #undef A_right
};


        /*
         * A calibrate_fields() *Helper* function to refresh the
         * cached screen geometry and related variables */
static void adj_geometry (void) 
{
   static size_t pseudo_max = 0;
   static int w_set = 0, w_cols = 0, w_rows = 0;

       //
       //    defined in asm-generic/termios.h
       //   
       //    struct winsize {
       //            unsigned short ws_row;
       //            unsigned short ws_col;
       //            unsigned short ws_xpixel;
       //            unsigned short ws_ypixel;
       //    };

   struct winsize wz;

   Screen_cols = columns;    // <term.h>
   Screen_rows = lines;      // <term.h>

   if (-1 != ioctl(STDOUT_FILENO, TIOCGWINSZ, &wz)
       && 0 < wz.ws_col && 0 < wz.ws_row) 
   {
      Screen_cols = wz.ws_col;
      Screen_rows = wz.ws_row;
   }

#ifndef RMAN_IGNORED
   // be crudely tolerant of crude tty emulators
   if (Cap_avoid_eol) Screen_cols--;
#endif

   // we might disappoint some folks (but they'll deserve it)
   if (SCREENMAX < Screen_cols) Screen_cols = SCREENMAX;
   if (!w_set) 
   {
      if (Width_mode > 0)              // -w with arg, we'll try to honor
         w_cols = Width_mode;
      else
      if (Width_mode < 0)              // -w without arg, try environment 
      {            // -w without arg, try environment
         char *env_columns = getenv("COLUMNS"),
              *env_lines = getenv("LINES"),
              *ep;
         if (env_columns && *env_columns) 
         {
            long t, tc = 0;
            t = strtol(env_columns, &ep, 0);
            if (!*ep && (t > 0) && (t <= 0x7fffffffL)) tc = t;
            if (0 < tc) w_cols = (int)tc;
         }
         if (env_lines && *env_lines) 
         {
            long t, tr = 0;
            t = strtol(env_lines, &ep, 0);
            if (!*ep && (t > 0) && (t <= 0x7fffffffL)) tr = t;
            if (0 < tr) w_rows = (int)tr;
         }
         if (!w_cols) w_cols = SCREENMAX;
         if (w_cols && w_cols < W_MIN_COL) w_cols = W_MIN_COL;
         if (w_rows && w_rows < W_MIN_ROW) w_rows = W_MIN_ROW;
      }
      if (w_cols > SCREENMAX) w_cols = SCREENMAX;
      w_set = 1;
   }

   /* keep our support for output optimization in sync with current reality
      note: when we're in Batch mode, we don't really need a Pseudo_screen
            and when not Batch, our buffer will contain 1 extra 'line' since
            Msg_row is never represented -- but it's nice to have some space
            between us and the great-beyond... */
   if (Batch) 
   {
      if (w_cols) Screen_cols = w_cols;
      Screen_rows = w_rows ? w_rows : MAXINT;
      Pseudo_size = (sizeof(*Pseudo_screen) * ROWMAXSIZ);
   } 
   else 
   {
      if (w_cols && w_cols < Screen_cols) Screen_cols = w_cols;
      if (w_rows && w_rows < Screen_rows) Screen_rows = w_rows;
      Pseudo_size = (sizeof(*Pseudo_screen) * ROWMAXSIZ) * Screen_rows;
   }
   // we'll only grow our Pseudo_screen, never shrink it
   if (pseudo_max < Pseudo_size) 
   {
      pseudo_max = Pseudo_size;
      Pseudo_screen = alloc_r(Pseudo_screen, pseudo_max);
#ifdef LOG2STDOUT
      fprintf(LogPtr, "alloc_r Pseudo_screen = %p, ROWMAXSIZ = %d, Screen_rows = %d, Pseudo_size = %d\n", Pseudo_screen, ROWMAXSIZ, Screen_rows, pseudo_max);
#endif
   }
   // ensure each row is repainted (just in case)
   PSU_CLEARSCREEN(0);

   fflush(stdout);

   //reset Frames_signal to BREAK_off
   Frames_signal = BREAK_off;
} // end: adj_geometry


        /*
         * A calibrate_fields() *Helper* function to build the
         * actual column headers and required library flags */
static void build_headers (void) 
{
   unsigned char f;
   char *s;
   WIN_t *w = Curwin;
#ifdef EQUCOLHDRYES
   int x, hdrmax = 0;
#endif
   int i, needpsdb = 0;

   Frames_libflags = 0; // initialize Frames_libflags

   do 
   {
      if (VIZISw(w)) 
      {
         memset((s = w->columnheader), 0, sizeof(w->columnheader));
#ifdef BUILD_4_STOCK
         //if (Rc.mode_altscreen) s = scat(s, " ");
         if (Rc.mode_altscreen) s = scat(s, fmtmk("%d", w->winnum));
#else
         if (Rc.mode_altscreen) s = scat(s, fmtmk("%d", w->winnum));
#endif
         for (i = 0; i < w->max_displayable_pflags; i++) 
         {
            f = w->proc_flags[i];
            //modify header highlight sort column
            if (CHKwinflags(w, Show_Highlight_COLS) && f == w->rc.sortindex) 
            {
               s = scat(s, fmtmk("%s%s", Caps_off, Caps_sort_header));
               w->hdrcaplen += strlen(Caps_off) + strlen(Caps_sort_header);
            //   s = scat(s, fmtmk("%s%s", Caps_off, w->capclr_msg));
             //  w->hdrcaplen += strlen(Caps_off) + strlen(w->capclr_msg);
            }
#ifdef USE_X_COLHDR
            if (CHKwinflags(w, Show_Highlight_COLS) && f == w->rc.sortindex) 
            {
               s = scat(s, fmtmk("%s%s", Caps_off, w->capclr_msg));
               w->hdrcaplen += strlen(Caps_off) + strlen(w->capclr_msg);
            }
#else
            if (P_MAXPFLAGS <= f) continue;
#endif
            if (P_WCH == f) needpsdb = 1;
            if (P_CMD == f && CHKwinflags(w, Show_CMDLIN)) Frames_libflags |= L_CMDLINE;
            Frames_libflags |= Fieldstab[w->proc_flags[i]].fillflag;
            s = scat(s, justify_pad(N_col_Head_tab(f)
               , IsVariableColumn(f) ? w->variable_column_size : Fieldstab[f].width
               , CHKwinflags(w, Fieldstab[f].align)));
            //modify header highlight sort column
            if (CHKwinflags(w, Show_Highlight_COLS) && f == w->rc.sortindex) 
            {
#ifdef BUILD_4_STOCK
               if (f == S_StockID)
               {
               s = scat(s, fmtmk("%s%s%s", Caps_off, w->capclr_hdr, " "));
               w->hdrcaplen += strlen(Caps_off) + strlen(w->capclr_hdr) + 1;
               }
               else
#endif
               {
               s = scat(s, fmtmk("%s%s", Caps_off, w->capclr_hdr));
               w->hdrcaplen += strlen(Caps_off) + strlen(w->capclr_hdr);
               }
            }
#ifdef USE_X_COLHDR
            if (CHKwinflags(w, Show_Highlight_COLS) && f == w->rc.sortindex) 
            {
               s = scat(s, fmtmk("%s%s", Caps_off, w->capclr_hdr));
               w->hdrcaplen += strlen(Caps_off) + strlen(w->capclr_hdr);
            }
#endif
         }
#ifdef EQUCOLHDRYES
         // prepare to even out column header lengths...
         if (hdrmax + w->hdrcaplen < (x = strlen(w->columnheader))) hdrmax = x - w->hdrcaplen;
#endif
         // with forest view mode, we'll need tgid, ppid & start_time...
         if (CHKwinflags(w, Show_FOREST)) Frames_libflags |= (L_status | L_stat);
         // for 'busy' only processes, we'll need pcpu (utime & stime)...
         if (!CHKwinflags(w, Show_IDLEPS)) Frames_libflags |= L_stat;
         // we must also accommodate an out of view sort field...
         f = w->rc.sortindex;
         Frames_libflags |= Fieldstab[f].fillflag;
         if (P_CMD == f && CHKwinflags(w, Show_CMDLIN)) Frames_libflags |= L_CMDLINE;
      } // end: VIZISw(w)

      if (Rc.mode_altscreen) w = w->next;
   } while (w != Curwin);

#ifdef EQUCOLHDRYES
   /* now we can finally even out column header lengths
      (we're assuming entire columnheader was memset to '\0') */
   if (Rc.mode_altscreen && SCREENMAX > Screen_cols)
   {
      for (i = 0; i < GROUPSMAX; i++) 
      {
         w = &Winstk[i];
         if (CHKwinflags(w, Show_TASKON))
         {
            if (hdrmax + w->hdrcaplen > (x = strlen(w->columnheader)))
            {
               memset(&w->columnheader[x], ' ', hdrmax + w->hdrcaplen - x);
            }
         }
      }
   }
#endif

   // do we need the kernel symbol table (and is it already open?)
   if (needpsdb) 
   {
      if (-1 == No_ksyms) 
      {
         No_ksyms = 0;
         if (open_psdb_message(NULL, library_err))
         {
            No_ksyms = 1;
         }
         else
         {
            PSDBopen = 1;
         }
      }
   }
   // finalize/touchup the libproc PROC_FILLxxx flags for current config...
   if ((Frames_libflags & L_EITHER) && !(Frames_libflags & L_stat))
   {
      Frames_libflags |= L_status;
   }
   if (!Frames_libflags) Frames_libflags = L_DEFAULT;
   if (Monpidsidx) Frames_libflags |= PROC_PID;
#ifdef LOG2STDOUT
   fprintf(LogPtr, "build_headers w->columnheader = %s\n", w->columnheader);
   fflush(LogPtr);
#endif
} // end: build_headers


        /* 校准
         * This guy coordinates the activities surrounding the maintenance
         * of each visible window's columns headers and the library flags
         * required for the openproc interface. */
static void calibrate_fields (void) 
{
   unsigned char f;
   char *s;
   const char *h;
   WIN_t *w = Curwin;
   int i, varcolcnt, len;
 
   adj_geometry();
 
   do 
   {
      if (VIZISw(w)) 
      {
         w->hdrcaplen = 0;   
   // really only used with USE_X_COLHDR
   // build window's pflagsall array, establish upper bounds for max_displayable_pflags
         for (i = 0, w->totalpflags = 0; i < P_MAXPFLAGS; i++) 
         {
 #ifdef LOG2STDOUT
            unsigned char fff = w->rc.fieldscur[i];
            int bit1 = fff & 0x80;
            int bit2 = fff & 0x40;
            int bit3 = fff & 0x20;
            int bit4 = fff & 0x10;
            int bit5 = fff & 0x08;
            int bit6 = fff & 0x04;
            int bit7 = fff & 0x02;
            int bit8 = fff & 0x01;
            char barry[9];
            barry[0] = bit1?'1':'0';
            barry[1] = bit2?'1':'0';
            barry[2] = bit3?'1':'0';
            barry[3] = bit4?'1':'0';
            barry[4] = bit5?'1':'0';
            barry[5] = bit6?'1':'0';
            barry[6] = bit7?'1':'0';
            barry[7] = bit8?'1':'0';
            barry[8] = '\0';
            
          // very important it will cause the log bad encoded
          //fprintf(LogPtr, "rc.fieldscur[%d] = %x, c = %c, ss = %s,  FieldGet = %d, FLD_OFFSET = %d\n", i, w->rc.fieldscur[i], w->rc.fieldscur[i], barry, FieldGetValue(w,i), FLD_OFFSET);
            fflush(LogPtr);
 #endif
            // check if the filed is visible 
            if (FieldIsVisible(w, i)) 
            {
               f = FieldGetValue(w, i);
 #ifdef LOG2STDOUT
               fprintf(LogPtr, "  FieldGetValue(w,%d) = %d, Head = %s\n", i, f, N_col_Head_tab(f));
               fflush(LogPtr);
 #endif
 #ifdef USE_X_COLHDR
               w->pflagsall[w->totalpflags++] = f;
 #else
               if (CHKwinflags(w, Show_Highlight_COLS) && f == w->rc.sortindex) 
               {
                  w->pflagsall[w->totalpflags++] = X_XON;
                  w->pflagsall[w->totalpflags++] = f;
                  w->pflagsall[w->totalpflags++] = X_XOFF;
               } 
               else
               {
                  w->pflagsall[w->totalpflags++] = f;
               }
 #endif
            }
         }
 #ifdef LOG2STDOUT
         fprintf(LogPtr, "w->totalpflags = %d\n", w->totalpflags);
         for (int i = 0; i < w->totalpflags; i++)
         {
            fprintf(LogPtr, "w->pflagsall[%d] = %d\n", i, w->pflagsall[i]);
         }
         fflush(LogPtr);
 #endif
        /* build a preliminary columns header not to exceed screen width
           while accounting for a possible leading window number */
         w->variable_column_size = varcolcnt = 0;
         // initial w->columnheader with null
         *(s = w->columnheader) = '\0';
         if (Rc.mode_altscreen) s = scat(s, " ");
 #ifdef LOG2STDOUT
         fprintf(LogPtr, "w->columnheader = %s\n", w->columnheader);
         fprintf(LogPtr, "w->begin_column_flag = %d\n", w->begin_column_flag);
         fflush(LogPtr);
 #endif

#ifdef BUILD_4_STOCK
   // fixed columns for follwing fields (not scrolling horizontally)
   //    S_INDEX = 0,
   //    S_StockName,
   //    S_StockID,
         for (i = S_INDEX; i <= w->totalpflags ; ) 
         {
            f = w->pflagsall[i];
            w->proc_flags[i] = f;
            i++;
 #ifndef USE_X_COLHDR
            // ignore P_MAXPFLAGS, X_XON, X_XOFF
            if (P_MAXPFLAGS <= f) continue;
 #endif
            h = N_col_Head_tab(f);
            len = (IsVariableColumn(f) ? (int)strlen(h) : Fieldstab[f].width) + COLPADSIZ;
           // add another seperator ' '
            if ((f == S_StockID) && (w->rc.sortindex == S_StockID)) len += 1;
           // oops, won't fit -- we're outta here...
            if (Screen_cols < ((int)(s - w->columnheader) + len)) break;
            if (IsVariableColumn(f)) 
            {  
               ++varcolcnt; 
               w->variable_column_size += strlen(h); 
            }
            s = scat(s, fmtmk("%*.*s", len, len, h));
 #ifdef LOG2STDOUT
            fprintf(LogPtr, "len = %d\n", len);
            fprintf(LogPtr, "ff = %d\n", f);
            fprintf(LogPtr, "header = %s\n", h);
 #endif
            if (f == S_StockID) break; 
         }
#endif

 #ifdef LOG2STDOUT
         fprintf(LogPtr, "sort = %d i = %d, begin_column_flag = %d\n", w->rc.sortindex, i, w->begin_column_flag);
 #endif

#ifdef BUILD_4_STOCK
         if (w->rc.sortindex == S_StockID)  
         {
            // copy the XOFF
            f = w->pflagsall[i];
            w->proc_flags[i] = f;
            i++;
         }

         for (; (i + w->begin_column_flag) < w->totalpflags; i++) 
#else
         for (i = 0; (i + w->begin_column_flag) < w->totalpflags; i++) 
#endif
         {
            f = w->pflagsall[i + w->begin_column_flag];
            w->proc_flags[i] = f;
 #ifndef USE_X_COLHDR
            // ignore P_MAXPFLAGS, X_XON, X_XOFF
            if (P_MAXPFLAGS <= f) continue;
 #endif
            h = N_col_Head_tab(f);
            len = (IsVariableColumn(f) ? (int)strlen(h) : Fieldstab[f].width) + COLPADSIZ;
           // oops, won't fit -- we're outta here...
            if (Screen_cols < ((int)(s - w->columnheader) + len)) break;
            if (IsVariableColumn(f)) 
            {  
               ++varcolcnt; 
               w->variable_column_size += strlen(h); 
            }
            s = scat(s, fmtmk("%*.*s", len, len, h));
 #ifdef LOG2STDOUT
            fprintf(LogPtr, "len = %d\n", len);
            fprintf(LogPtr, "ff = %d\n", f);
            fprintf(LogPtr, "header = %s\n", h);
 #endif
         }
 #ifndef USE_X_COLHDR
         if (X_XON == w->proc_flags[i - 1])
         {
            --i;
         }
 #endif
 
        /* establish the final max_displayable_pflags and prepare to grow the variable column
           heading(s) via variable_column_size - it may be a fib if their pflags weren't
           encountered, but that's ok because they won't be displayed anyway */
         w->max_displayable_pflags = i;
 #ifdef LOG2STDOUT
         fprintf(LogPtr, "w->columnheader = %s\n", w->columnheader);
         fprintf(LogPtr, "w->max_displayable_pflags = %d\n", i);
 #endif
         w->variable_column_size += Screen_cols - strlen(w->columnheader);
         if (varcolcnt) w->variable_column_size /= varcolcnt;
 
        /* establish the field where all remaining fields would still
           fit within screen width, including a leading window number */
         *(s = w->columnheader) = '\0';
 
         if (Rc.mode_altscreen) s = scat(s, " ");
         for (i = w->totalpflags - 1; -1 < i; i--) 
         {
            f = w->pflagsall[i];
 #ifndef USE_X_COLHDR
            if (P_MAXPFLAGS <= f) { w->end_column_flag = i; continue; }
 #endif
            h = N_col_Head_tab(f);
            len = (IsVariableColumn(f) ? (int)strlen(h) : Fieldstab[f].width) + COLPADSIZ;
            if (Screen_cols < ((int)(s - w->columnheader) + len)) break;
            s = scat(s, fmtmk("%*.*s", len, len, h));
            w->end_column_flag = i;
         }
 #ifdef LOG2STDOUT
         fprintf(LogPtr, "w->end_column_flag = %d\n", w->end_column_flag);
         fprintf(LogPtr, "w->begin_column_flag = %d\n", w->begin_column_flag);
         fprintf(LogPtr, "w->columnheader = %s\n", w->columnheader);
         fflush(LogPtr);
 #endif
 #ifndef USE_X_COLHDR
         if (X_XOFF == w->pflagsall[w->end_column_flag]) ++w->end_column_flag;
 #endif
      } // end: if (VIZISw(w))
 
      if (Rc.mode_altscreen) w = w->next;
   } while (w != Curwin);
 
   build_headers();
   if (CHKwinflags(Curwin, View_SCROLL))
   {
      updt_scroll_msg();
   }
} // end: calibrate_fields


        /*
         * Display each field represented in the current window's fieldscur
         * array along with its description.  Mark with bold and a leading
         * asterisk those fields associated with the "on" or "active" state.
         *
         * Special highlighting will be accorded the "focus" field with such
         * highlighting potentially extended to include the description.
         *
         * Below is the current Fieldstab space requirement and how
         * we apportion it.  The xSUFX is considered sacrificial,
         * something we can reduce or do without.
         *            0        1         2         3
         *            12345678901234567890123456789012
         *            * HEADING = Longest Description!
         *      xPRFX ----------______________________ xSUFX
         *    ( xPRFX has pos 2 & 10 for 'extending' when at minimums )
         *
         * The first 4 screen rows are reserved for explanatory text, and
         * the maximum number of columns is Screen_cols / xPRFX + 1 space
         * between columns.  Thus, for example, with 42 fields a tty will
         * still remain useable under these extremes:
         *       rows       columns     what's
         *       tty  top   tty  top    displayed
         *       ---  ---   ---  ---    ------------------
         *        46   42    10    1    xPRFX only
         *        46   42    32    1    full xPRFX + xSUFX
         *         6    2   231   21    xPRFX only
         *        10    6   231    7    full xPRFX + xSUFX
         */
static void display_fields (int focus, int extend) 
{
 #define mkERR { putp("\n"); putp(N_txt_Norm_tab(XTRA_winsize_txt)); return; }
 #define mxCOL ( (Screen_cols / 11) > 0 ? (Screen_cols / 11) : 1 )
 #define yRSVD  4
 #define xSUFX  22
 #define xPRFX (10 + xadd)
 #define xTOTL (xPRFX + xSUFX)
   WIN_t *w = Curwin;                  // avoid gcc bloat with a local copy
   int i;                              // utility int (a row, tot cols, ix)
   int smax;                           // printable width of xSUFX
   int xadd = 0;                       // spacing between data columns
   int cmax = Screen_cols;             // total data column width
   int rmax = Screen_rows - yRSVD;     // total useable rows
   static int col_sav, row_sav;

   i = (P_MAXPFLAGS % mxCOL) ? 1 : 0;
   if (rmax < i + (P_MAXPFLAGS / mxCOL)) mkERR;
   i = P_MAXPFLAGS / rmax;
   if (P_MAXPFLAGS % rmax) ++i;
   if (i > 1) { cmax /= i; xadd = 1; }
   if (cmax > xTOTL) cmax = xTOTL;
   smax = cmax - xPRFX;
   if (smax < 0) mkERR;

   /* we'll go the extra distance to avoid any potential screen flicker
      which occurs under some terminal emulators (but it was our fault) */
   if (col_sav != Screen_cols || row_sav != Screen_rows) 
   {
      col_sav = Screen_cols;
      row_sav = Screen_rows;
      putp(Cap_clr_eos);
   }
   fflush(stdout);

   for (i = 0; i < P_MAXPFLAGS; ++i) 
   {
      int b = FieldIsVisible(w, i), x = (i / rmax) * cmax, y = (i % rmax) + yRSVD;
      const char *e = (i == focus && extend) ? w->capclr_hdr : "";
      unsigned char f = FieldGetValue(w, i);
#ifdef LOG2STDOUT
      fprintf(LogPtr, "display i = %d, f=%d\n",i, f);
#endif
      char sbuf[xSUFX+1];

      // prep sacrificial suffix
      snprintf(sbuf, sizeof(sbuf), "= %s", N_fld_Desc_tab(f));
#ifdef BUILD_4_STOCK
      if (i <= S_StockID)
      {
         PUTT("%s%c%s%s %s%-7.7s%s%s%s %-*.*s%s"
            , tg2(x, y)
            , b ? '*' : ' '
            //, b ? w->cap_bold : Cap_norm
            , b ? Caps_red : Caps_magenta
            , e
            , i == focus ? w->capclr_hdr : ""
            , N_col_Head_tab(f)
            , Cap_norm
            , b ? w->cap_bold : ""
            , e
            , smax, smax
            , sbuf
            , Cap_norm);
      }
      else
      {
         PUTT("%s%c%s%s %s%-7.7s%s%s%s %-*.*s%s"
            , tg2(x, y)
            , b ? '*' : ' '
            //, b ? w->cap_bold : Cap_norm
            , b ? Caps_green : Caps_magenta
            , e
            , i == focus ? w->capclr_hdr : ""
            , N_col_Head_tab(f)
            , Cap_norm
            , b ? w->cap_bold : ""
            , e
            , smax, smax
            , sbuf
            , Cap_norm);
      }
#else
      PUTT("%s%c%s%s %s%-7.7s%s%s%s %-*.*s%s"
         , tg2(x, y)
         , b ? '*' : ' '
         , b ? w->cap_bold : Cap_norm
         , e
         , i == focus ? w->capclr_hdr : ""
         , N_col_Head_tab(f)
         , Cap_norm
         , b ? w->cap_bold : ""
         , e
         , smax, smax
         , sbuf
         , Cap_norm);
#endif
   }

   putp(Caps_off);
 #undef mkERR
 #undef mxCOL
 #undef yRSVD
 #undef xSUFX
 #undef xPRFX
 #undef xTOTL
} // end: display_fields


        /*
         * Manage all fields aspects (order/toggle/sort), for all windows. */
static void fields_utility (void) 
{
#ifndef SCROLLVAR_NO
 #define unSCRL  { w->begin_column_flag = w->variable_column_begin = 0; OFFwinflags(w, Show_Highlight_COLS); }
#else
 #define unSCRL  { w->begin_column_flag = 0; OFFwinflags(w, Show_Highlight_COLS); }
#endif
 #define swapEM  { char c; unSCRL; c = w->rc.fieldscur[i]; \
       w->rc.fieldscur[i] = *p; *p = c; p = &w->rc.fieldscur[i]; }
 #define spewFI  { char *t; f = w->rc.sortindex; t = strchr(w->rc.fieldscur, f + FLD_OFFSET); \
       if (!t) t = strchr(w->rc.fieldscur, (f + FLD_OFFSET) | 0x80); \
       i = (t) ? (int)(t - w->rc.fieldscur) : 0; }
   WIN_t *w = Curwin;             // avoid gcc bloat with a local copy
   const char *h = NULL;
   char *p = NULL;
   int i, key;
   unsigned char f;

   spewFI
signify_that:
   putp(Cap_clr_scr);
   adj_geometry();

   do 
   {
      if (!h) h = N_col_Head_tab(f);
      putp(Cap_home);
      show_special(1, fmtmk(N_unq_Uniq_tab(FIELD_header_fmt)
         , w->grpname, CHKwinflags(w, Show_FOREST) ? N_txt_Norm_tab(FOREST_views_txt) : h));

      display_fields(i, (p != NULL));

      fflush(stdout);

      if (Frames_signal) goto signify_that;
      key = iokey(1);
#ifdef LOG2STDOUT
      fprintf(LogPtr, "key = %d\n", key);
      fflush(LogPtr);
#endif
      if (key < 1) goto signify_that;

      switch (key) {
         case 'k':
         case kbd_UP:
#ifdef BUILD_4_STOCK
            if (p && (i > (S_StockID + 1))) { --i; if (p) swapEM }
            if (!p && (i > 0)) { --i; if(p) swapEM }
//            if (i > (S_StockID + 1)) { --i; if (p) swapEM }
#else
            if (i > 0) { --i; if (p) swapEM }
#endif
            break;
         case 'j':
         case kbd_DOWN:
            if (i + 1 < P_MAXPFLAGS) { ++i; if (p) swapEM }
            break;
         case 'h':
         case kbd_LEFT:
         case kbd_ENTER:
            p = NULL;
            break;
         case 'l':
         case kbd_RIGHT:
#ifdef BUILD_4_STOCK
            if (i <= S_StockID)
            {
               break;
            }
            else
            {
               p = &w->rc.fieldscur[i];
               break;
            }
#else
            p = &w->rc.fieldscur[i];
            break;
#endif
         case kbd_HOME:
         case kbd_PGUP:
            if (!p) i = 0;
            break;
         case kbd_END:
         case kbd_PGDN:
            if (!p) i = P_MAXPFLAGS - 1;
            break;
         case kbd_SPACE:
         case 'd':
#ifdef BUILD_4_STOCK
            if (!p && (i > S_StockID)) { FieldToggle(w, i); unSCRL }
#else
            if (!p) { FieldToggle(w, i); unSCRL }
#endif
            break;
         case 's':
#ifdef TREE_NORESET
            if (!p && !CHKwinflags(w, Show_FOREST)) { w->rc.sortindex = f = FieldGetValue(w, i); h = NULL; unSCRL }
#else
            if (!p) { w->rc.sortindex = f = FieldGetValue(w, i); h = NULL; unSCRL; OFFwinflags(w, Show_FOREST); }
#endif
            break;
         case 'a':
         case 'w':
            Curwin = w = ('a' == key) ? w->next : w->prev;
            spewFI
            h = p = NULL;
            break;
         default:                 // keep gcc happy
            break;
      }
   } while (key != 'q' && key != kbd_ESC);
 #undef unSCRL
 #undef swapEM
 #undef spewFI
} // end: fields_utility


        /*
         * This routine takes care of auto sizing field widths
         * if/when the user sets Rc.fixed_widest to -1.  Along the
         * way he reinitializes some things for the next frame. */
static inline void widths_resize (void) 
{
   int i;

   // next var may also be set by the guys that actually truncate stuff
   Autox_found = 0;
   for (i = 0; i < P_MAXPFLAGS; i++) 
   {
      if (Autox_array[i]) 
      {
         Fieldstab[i].width++;
         Autox_array[i] = 0;
         Autox_found = 1;
      }
   }
   if (Autox_found) calibrate_fields();
} // end: widths_resize

#ifdef BUILD_4_STOCK

static void update_header(void)
{
   if (Rc.task_unit_scale == VE_gu)
   {
      Head_nlstab[S_VOLUME] = "Volume(G)";
      Head_nlstab[S_RMB] = "Amount(CNY)";
   }
   else
   {
      Head_nlstab[S_VOLUME] = "Volume(S)";
      Head_nlstab[S_RMB] = "Amount(CNW)";
   }
}

#endif
        /*
         * This routine exists just to consolidate most of the messin'
         * around with the Fieldstab array and some related stuff. */
static void zap_fieldstab (void) 
{
   static int once;
   unsigned digits;
   char buf[8];
#ifdef BUILD_4_STOCK
#ifdef CHINESE
   Fieldstab[S_StockID].width = 8;
#else
   Fieldstab[S_StockID].width = 7;
#endif
#endif
   if (!once) 
   {
      Fieldstab[P_PID].width = Fieldstab[P_PPD].width
         = Fieldstab[P_PGD].width = Fieldstab[P_SID].width
         = Fieldstab[P_TGD].width = Fieldstab[P_TPG].width = 5;
      if (5 < (digits = get_pid_digits())) 
      {
         if (10 < digits) error_exit(N_txt_Norm_tab(FAIL_widepid_txt));
         Fieldstab[P_PID].width = Fieldstab[P_PPD].width
            = Fieldstab[P_PGD].width = Fieldstab[P_SID].width
            = Fieldstab[P_TGD].width = Fieldstab[P_TPG].width = digits;
      }
      once = 1;
   }

   /*** hotplug_acclimated ***/

   Fieldstab[P_CPN].width = 1;
   if (1 < (digits = (unsigned)snprintf(buf, sizeof(buf), "%u", (unsigned)smp_num_cpus)))   {
      if (5 < digits) error_exit(N_txt_Norm_tab(FAIL_widecpu_txt));
      Fieldstab[P_CPN].width = digits;
   }

#ifdef BOOST_PERCNT
   Cpu_pmax = 99.9;
   Fieldstab[P_CPU].width = 5;
   if (Rc.mode_irixps && smp_num_cpus > 1 && !Thread_mode) 
   {
      Cpu_pmax = 100.0 * smp_num_cpus;
      if (smp_num_cpus > 10) 
      {
         if (Cpu_pmax > 99999.0) Cpu_pmax = 99999.0;
      } 
      else 
      {
         if (Cpu_pmax > 999.9) Cpu_pmax = 999.9;
      }
   }
#else
   Cpu_pmax = 99.9;
   Fieldstab[P_CPU].width = 4;
   if (Rc.mode_irixps && smp_num_cpus > 1 && !Thread_mode) 
   {
      Cpu_pmax = 100.0 * smp_num_cpus;
      if (smp_num_cpus > 10) 
      {
         if (Cpu_pmax > 99999.0) Cpu_pmax = 99999.0;
      } 
      else 
      {
         if (Cpu_pmax > 999.9) Cpu_pmax = 999.9;
      }
      Fieldstab[P_CPU].width = 5;
   }
#endif

   /* and accommodate optional wider non-scalable columns (maybe) */
   if (!AUTOX_MODE) 
   {
      int i;
      Fieldstab[P_UED].width = Fieldstab[P_URD].width
         = Fieldstab[P_USD].width = Fieldstab[P_GID].width
         = Rc.fixed_widest ? 5 + Rc.fixed_widest : 5;
      Fieldstab[P_UEN].width = Fieldstab[P_URN].width
         = Fieldstab[P_USN].width = Fieldstab[P_GRP].width
         = Rc.fixed_widest ? 8 + Rc.fixed_widest : 8;
#ifdef BUILD_4_STOCK
      Fieldstab[S_StockName].width = Rc.fixed_widest ? 9 + Rc.fixed_widest : 9;
      Fieldstab[S_VOLUME].scale = Fieldstab[S_RMB].scale = Rc.task_unit_scale;
      update_header();
#endif
      Fieldstab[P_TTY].width = Rc.fixed_widest ? 8 + Rc.fixed_widest : 8;
      Fieldstab[P_WCH].width = Rc.fixed_widest ? 10 + Rc.fixed_widest : 10;
      for (i = P_NS1; i < P_NS1 + NUM_NS; i++)
         Fieldstab[i].width = Rc.fixed_widest ? 10 + Rc.fixed_widest : 10;
   }

   /* plus user selectable scaling */
   Fieldstab[P_VRT].scale = Fieldstab[P_SWP].scale
      = Fieldstab[P_RES].scale = Fieldstab[P_COD].scale
      = Fieldstab[P_DAT].scale = Fieldstab[P_SHR].scale
      = Fieldstab[P_USE].scale = Rc.task_unit_scale;

#ifdef LOG2STDOUT
   fprintf(LogPtr, "[%s]:%d Rc.task_unit_scale = %d\n", __FUNCTION__, __LINE__, Rc.task_unit_scale);
   fflush(LogPtr);
#endif

   // lastly, ensure we've got proper column headers...
   calibrate_fields();
} // end: zap_fieldstab

/*######  Library Interface  #############################################*/

        /*
         * This guy's modeled on libproc's 'eight_cpu_numbers' function except
         * we preserve all cpu data in our CPU_t array which is organized
         * as follows:
         *    cpus[0] thru cpus[n] == tics for each separate cpu
         *    cpus[sumSLOT]        == tics from the 1st /proc/stat line
         *  [ and beyond sumSLOT   == tics for each cpu NUMA node ] */
static CPU_t *cpus_refresh (CPU_t *cpus) 
{
 #define sumSLOT ( smp_num_cpus )
 #define totSLOT ( 1 + smp_num_cpus + Numa_node_tot)
   static FILE *fp = NULL;
   static int siz, sav_slot = -1;
   static char *buf;
   CPU_t *sum_ptr;                               // avoid gcc subscript bloat
   int i, num, tot_read;
#ifndef NUMA_DISABLE
   int node;
#endif
   char *bp;

   /*** hotplug_acclimated ***/
   if (sav_slot != sumSLOT) 
   {
      sav_slot = sumSLOT;
      zap_fieldstab();
      if (fp) { fclose(fp); fp = NULL; }
      if (cpus) { free(cpus); cpus = NULL; }
   }

   /* by opening this file once, we'll avoid the hit on minor page faults
      (sorry Linux, but you'll have to close it for us) */
   if (!fp) 
   {
      if (!(fp = fopen("/proc/stat", "r")))
         error_exit(fmtmk(N_fmt_Norm_tab(FAIL_statopn_fmt), strerror(errno)));
      /* note: we allocate one more CPU_t via totSLOT than 'cpus' so that a
               slot can hold tics representing the /proc/stat cpu summary */
      cpus = alloc_c(totSLOT * sizeof(CPU_t));
   }
   rewind(fp);
   fflush(fp);

 #define buffGRW 1024
   /* we slurp in the entire directory thus avoiding repeated calls to fgets,
      especially in a massively parallel environment.  additionally, each cpu
      line is then frozen in time rather than changing until we get around to
      accessing it.  this helps to minimize (not eliminate) most distortions. */
   tot_read = 0;
   if (buf) buf[0] = '\0';
   else buf = alloc_c((siz = buffGRW));
   while (0 < (num = fread(buf + tot_read, 1, (siz - tot_read), fp))) 
   {
      tot_read += num;
      if (tot_read < siz) break;
      buf = alloc_r(buf, (siz += buffGRW));
   };
   buf[tot_read] = '\0';
   bp = buf;
 #undef buffGRW

   // remember from last time around
   sum_ptr = &cpus[sumSLOT];
   memcpy(&sum_ptr->sav, &sum_ptr->cur, sizeof(CT_t));
   // then value the last slot with the cpu summary line
   if (4 > sscanf(bp, "cpu %Lu %Lu %Lu %Lu %Lu %Lu %Lu %Lu"
      , &sum_ptr->cur.u, &sum_ptr->cur.n, &sum_ptr->cur.s
      , &sum_ptr->cur.i, &sum_ptr->cur.w, &sum_ptr->cur.x
      , &sum_ptr->cur.y, &sum_ptr->cur.z))
         error_exit(N_txt_Norm_tab(FAIL_statget_txt));
#ifndef CPU_ZEROTICS
   sum_ptr->cur.tot = sum_ptr->cur.u + sum_ptr->cur.s
      + sum_ptr->cur.n + sum_ptr->cur.i + sum_ptr->cur.w
      + sum_ptr->cur.x + sum_ptr->cur.y + sum_ptr->cur.z;
   /* if a cpu has registered substantially fewer tics than those expected,
      we'll force it to be treated as 'idle' so as not to present misleading
      percentages. */
   sum_ptr->edge =
      ((sum_ptr->cur.tot - sum_ptr->sav.tot) / smp_num_cpus) / (100 / TICS_EDGE);
#endif

#ifndef NUMA_DISABLE
   // forget all of the prior node statistics (maybe)
   if (CHKwinflags(Curwin, View_CPUNOD))
      memset(sum_ptr + 1, 0, Numa_node_tot * sizeof(CPU_t));
#endif

   // now value each separate cpu's tics...
   for (i = 0; i < sumSLOT; i++) 
   {
      CPU_t *cpu_ptr = &cpus[i];               // avoid gcc subscript bloat
#ifdef PRETEND8CPUS
      bp = buf;
#endif
      bp = 1 + strchr(bp, '\n');
      // remember from last time around
      memcpy(&cpu_ptr->sav, &cpu_ptr->cur, sizeof(CT_t));
      if (4 > sscanf(bp, "cpu%d %Lu %Lu %Lu %Lu %Lu %Lu %Lu %Lu", &cpu_ptr->id
         , &cpu_ptr->cur.u, &cpu_ptr->cur.n, &cpu_ptr->cur.s
         , &cpu_ptr->cur.i, &cpu_ptr->cur.w, &cpu_ptr->cur.x
         , &cpu_ptr->cur.y, &cpu_ptr->cur.z)) 
      {
            memmove(cpu_ptr, sum_ptr, sizeof(CPU_t));
            break;        // tolerate cpus taken offline
      }

#ifndef CPU_ZEROTICS
      cpu_ptr->edge = sum_ptr->edge;
#endif
#ifdef PRETEND8CPUS
      cpu_ptr->id = i;
#endif
#ifndef NUMA_DISABLE
      /* henceforth, with just a little more arithmetic we can avoid
         maintaining *any* node stats unless they're actually needed */
      if (CHKwinflags(Curwin, View_CPUNOD)
          && Numa_node_tot
          && -1 < (node = Numa_node_of_cpu(cpu_ptr->id))) 
      {
         // use our own pointer to avoid gcc subscript bloat
         CPU_t *nod_ptr = sum_ptr + 1 + node;
         nod_ptr->cur.u += cpu_ptr->cur.u; nod_ptr->sav.u += cpu_ptr->sav.u;
         nod_ptr->cur.n += cpu_ptr->cur.n; nod_ptr->sav.n += cpu_ptr->sav.n;
         nod_ptr->cur.s += cpu_ptr->cur.s; nod_ptr->sav.s += cpu_ptr->sav.s;
         nod_ptr->cur.i += cpu_ptr->cur.i; nod_ptr->sav.i += cpu_ptr->sav.i;
         nod_ptr->cur.w += cpu_ptr->cur.w; nod_ptr->sav.w += cpu_ptr->sav.w;
         nod_ptr->cur.x += cpu_ptr->cur.x; nod_ptr->sav.x += cpu_ptr->sav.x;
         nod_ptr->cur.y += cpu_ptr->cur.y; nod_ptr->sav.y += cpu_ptr->sav.y;
         nod_ptr->cur.z += cpu_ptr->cur.z; nod_ptr->sav.z += cpu_ptr->sav.z;
#ifndef CPU_ZEROTICS
         /* yep, we re-value this repeatedly for each cpu encountered, but we
            can then avoid a prior loop to selectively initialize each node */
         nod_ptr->edge = sum_ptr->edge;
#endif
         cpu_ptr->node = node;
      }
#endif
   } // end: for each cpu

   Cpu_faux_tot = i;      // tolerate cpus taken offline

   return cpus;
 #undef sumSLOT
 #undef totSLOT
} // end: cpus_refresh


#ifdef DISABLE_HISTORY_HASH

        /*
         * Binary Search for HISTORY_t's put/get support */

static inline HISTORY_t *history_bsearch (HISTORY_t *hst, int max, int pid) 
{
   int mid, min_index = 0;

   while (min_index <= max) 
   {
      mid = (min_index + max) / 2;
      if (pid < hst[mid].pid) max = mid - 1;
      else if (pid > hst[mid].pid) min_index = mid + 1;
      else return &hst[mid];
   }
   return NULL;
} // end: history_bsearch

#else
        /*
         * Hashing functions for HISTORY_t's put/get support
         * (not your normal 'chaining', those damn HISTORY_t's might move!) */

#define _HASH_(K) (K & (HHASH_SIZ - 1))

static inline HISTORY_t *hstget (int pid) 
{
   int V = PHash_sav[_HASH_(pid)];
//   printf("hstget pid = %d, key = %d\n", pid, V);

   while (-1 < V) 
   {
      if (PtrHistory_saved[V].pid == pid) return &PtrHistory_saved[V];
      V = PtrHistory_saved[V].lnk; 
   }
   return NULL;
} // end: hstget


static inline void hstput (unsigned idx) 
{
   int V = _HASH_(PtrHistory_new[idx].pid);
//   printf("hstput idx = %d, PtrHistory_new[idx].pid = %d, key = %d\n", idx, PtrHistory_new[idx].pid, V);

   //PHash_new and PHash_save is -1 at initialization  
   PtrHistory_new[idx].lnk = PHash_new[V];
 //  printf("\tPtrHistory_new[%d].lnk = PHash_new[%d] = %d\n", idx, V, PtrHistory_new[idx].lnk); 
   PHash_new[V] = idx;
  // printf("\tPHash_new[%d] = %d\n", V, idx);
} // end: hstput

#undef _HASH_
#endif

#ifdef BUILD_4_STOCK
void update_filter_statistic(const proc_t *this)
{ 
   int i;
   for (i = 0; i < GROUPSMAX; i++) 
   {
      WIN_t *w = &Winstk[i];
      if (!CHKwinflags(w, Show_IDLEPS) || (w->user_select_type))
      {
         if (stock_matched(w, this))
         {
            w->max_matched_row++;
   //       w->last_matched_index = i; // cannot update it because not sorted
            if (this->current_price > this->pre_close)
            {
               w->ups++;
            }
            else if (this->current_price == this->pre_close)
            {
               w->draws++;
            }
            else
            {
               w->downs++;
            }
         }
      }
   }
}
#endif
        /*
         * Refresh procs *Helper* function to eliminate yet one more need
         * to loop through our darn proc_t table.  He's responsible for:
         *    1) calculating the elapsed time since the previous frame
         *    2) counting the number of tasks in each state (run, sleep, etc)
         *    3) maintaining the HISTORY_t's and priming the proc_t pcpu field
         *    4) establishing the total number tasks for this frame */
static void procs_statistic (proc_t *this) 
{
#ifdef DISABLE_HISTORY_HASH
   static unsigned maxt_sav = 0;        // prior frame's max tasks
#endif
   TIC_t tics;
   HISTORY_t *h;

   if (!this) 
   {
      static struct timeval oldtimev;
      struct timeval timev;
      struct timezone timez;
      float et;
      void *v;

      gettimeofday(&timev, &timez);
      et = (timev.tv_sec - oldtimev.tv_sec)
         + (float)(timev.tv_usec - oldtimev.tv_usec) / 1000000.0;
      oldtimev.tv_sec = timev.tv_sec;
      oldtimev.tv_usec = timev.tv_usec;

      // if in Solaris mode, adjust our scaling for all cpus
      Frame_etscale = 100.0f / ((float)Hertz * (float)et * (Rc.mode_irixps ? 1 : smp_num_cpus));
#ifdef DISABLE_HISTORY_HASH
      maxt_sav = Frame_maxtask;
#endif
      Frame_maxtask = Frame_running = Frame_sleepin = Frame_stopped = Frame_zombied = 0;
#ifdef BUILD_4_STOCK	
      stock_ups = stock_draws = stock_downs = 0;
#endif

      // prep for saving this frame's HISTORY_t's (and reuse mem each time around)
      v = PtrHistory_saved;
      PtrHistory_saved = PtrHistory_new;
      PtrHistory_new = v;
#ifdef DISABLE_HISTORY_HASH
      // prep for binary search by sorting the last frame's HISTORY_t's
      qsort(PtrHistory_saved, maxt_sav, sizeof(HISTORY_t), (QFunc_Sort_Cb)sort_HISTORY_t);
#else
      v = PHash_sav;
      PHash_sav = PHash_new;
      PHash_new = v;
      memcpy(PHash_new, HHash_nul, sizeof(HHash_nul));
#endif
      return;
   }

   switch (this->state) 
   {
      case 'R':
         Frame_running++;
         break;
      case 'S':
      case 'D':
         Frame_sleepin++;
         break;
      case 'T':
         Frame_stopped++;
         break;
      case 'Z':
         Frame_zombied++;
         break;
      default:                    // keep gcc happy
         break;
   }

   if (Frame_maxtask+1 >= HHistory_size) 
   {
      HHistory_size = HHistory_size * 5 / 4 + 100;
      PtrHistory_saved = alloc_r(PtrHistory_saved, sizeof(HISTORY_t) * HHistory_size);
      PtrHistory_new = alloc_r(PtrHistory_new, sizeof(HISTORY_t) * HHistory_size);
   }

   /* calculate time in this process; the sum of user time (utime) and
      system time (stime) -- but PLEASE dont waste time and effort on
      calcs and saves that go unused, like the old top! */
   PtrHistory_new[Frame_maxtask].pid  = this->tid;
   PtrHistory_new[Frame_maxtask].tics = tics = (this->utime + this->stime);
   // finally, save major/minor fault counts in case the deltas are displayable
   PtrHistory_new[Frame_maxtask].maj = this->maj_flt;
   PtrHistory_new[Frame_maxtask].min = this->min_flt;

#ifdef DISABLE_HISTORY_HASH
   // find matching entry from previous frame and make stuff elapsed
   if ((h = history_bsearch(PtrHistory_saved, maxt_sav - 1, this->tid))) 
   {
      tics -= h->tics;
      this->maj_delta = this->maj_flt - h->maj;
      this->min_delta = this->min_flt - h->min;
   }
#else
   // hash & save for the next frame
   hstput(Frame_maxtask);
   // find matching entry from previous frame and make stuff elapsed
   if ((h = hstget(this->tid))) 
   {
      
      tics -= h->tics;
      this->maj_delta = this->maj_flt - h->maj;
      this->min_delta = this->min_flt - h->min;
   }
#endif

   /* we're just saving elapsed tics, to be converted into %cpu if
      this task wins it's displayable screen row lottery... */
   this->pcpu = tics;
#ifdef BUILD_4_STOCK
   if (this->current_price == this->pre_close)
   {
      stock_draws++;
   }
   else if (this->current_price > this->pre_close)
   {
      stock_ups++;
   }
   else if (this->current_price < this->pre_close)
   {
      stock_downs++;
   }

   if (this->current_price > 0)
   {
      this->up_percent = (int)(((float)this->current_price - (float)this->pre_close)*100000 / (float)(this->pre_close));
   }
   else
   {
      this->up_percent = 0;
   }
   int sum = this->dzx_G_daily + this->macd_G_daily + this->kdj_G_daily + this->rsi_G_daily;
   this->sweight = 100*((float)sum/4);
   this->downshadow = 0;
   this->downshadow = 10000*((float)(this->current_price>this->open_today?this->open_today:this->current_price) - (float)this->min) / ((float)this->max - (float)this->min);
   if (this->volume > 0)
   {
      this->ddr = 100000*(this->ddbuy)/(this->volume);
   }
   else
   {
      this->ddr = 0;
   }
   this->mnr = 100*(this->main_net_rate);
   this->sort_date = this->year + this->month + this->day;
   this->sort_time = this->hour + this->minute + this->second;
   if (this->liutongA > 0)
   {
      
      this->turn_over_rate = 100000*(this->volume) / ((long long) (this->liutongA * 10000)); 
      this->value_liutongA = (this->liutongA * this->current_price) / FACTOR / 10000;
      this->sort_value_liutongA = this->value_liutongA*1000;
   }
   else
   {
      this->turn_over_rate = 0;
      this->value_liutongA = 0;
      this->sort_value_liutongA = 0; 
   }

   if (this->totals > 0)
   {
      this->value_totals = (this->totals * this->current_price) / FACTOR / 10000;
      this->sort_value_totals = this->value_totals*1000;
   }
   else
   {
      this->value_totals = 0;
      this->sort_value_totals = 0;
   }

   // must do it while all values are updated
   update_filter_statistic(this);
#endif

   // shout this to the world with the final call (or us the next time in)
   Frame_maxtask++;
} // end: procs_statistic


        /*
         * This guy's modeled on libproc's 'readproctab' function except
         * we reuse and extend any prior proc_t's.  He's been customized
         * for our specific needs and to avoid the use of <stdarg.h> */
static void procs_refresh (void) 
{
 #ifdef LOG2STDOUT
   fprintf(LogPtr, "procs_refresh started\n");
   fflush(LogPtr);
 #endif

 #define n_used  Frame_maxtask                   // maintained by procs_statistic()
   // the private_proc_t_ptr_table address maybe changed due to calling realloc within alloc_r
   static proc_t **private_proc_t_ptr_table;     // our base proc_t ptr table
   static int n_alloc = 0;                       // size of our private_proc_t_ptr_table
   static int n_saved = 0;                       // last window ppt size
   proc_t *ptask;
   PROCTAB* PT;
   int i;
   proc_t*(*read_something)(PROCTAB*, proc_t*);

   procs_statistic(NULL);                              // prep for a new frame

   if (NULL == (PT = openproc(Frames_libflags, Monpids)))
   {
      error_exit(fmtmk(N_fmt_Norm_tab(FAIL_openlib_fmt), strerror(errno)));
   }
   read_something = Thread_mode ? readeither : readproc;

#ifdef BUILD_4_STOCK
   int j;
   WIN_t * w;
   for (j = 0; j < GROUPSMAX; j++) 
   {
      w = &Winstk[j];
      if (!CHKwinflags(w, Show_IDLEPS) || (w->user_select_type))
      {
         w->stock_select_flags = 1;
         w->max_matched_row = 0;
         w->ups = 0;
         w->draws = 0;
         w->downs = 0;
      }
   }
#endif

   for (;;) 
   {
#ifdef LOG2STDOUT
         fprintf(LogPtr, "n_alloc = %d\n", n_alloc);
         fprintf(LogPtr, "n_used = %d\n", n_used);
         fprintf(LogPtr, "private_proc_t_ptr_table = %p\n", private_proc_t_ptr_table);
#endif
      if (n_used == n_alloc) 
      {
         n_alloc = 10 + ((n_alloc * 5) / 4);     // grow by over 25%

         private_proc_t_ptr_table = alloc_r(private_proc_t_ptr_table, sizeof(proc_t*) * n_alloc);
#ifdef LOG2STDOUT 
         if (global_proc_t_ptr_table != private_proc_t_ptr_table)
         {
            fprintf(LogPtr, "alloc_r n_alloc = %d\n", n_alloc);
            fprintf(LogPtr, "new private_proc_t_ptr_table = %p\n", private_proc_t_ptr_table);
            fprintf(LogPtr, "global_proc_t_ptr_table = %p\n", global_proc_t_ptr_table);
            fprintf(LogPtr, "private_proc_t_ptr_table changed due to realloc\n");
         }
#endif
         global_proc_t_ptr_table = private_proc_t_ptr_table;  // our base proc_t ptr table
         // ensure NULL pointers for the additional memory just acquired
         memset(private_proc_t_ptr_table + n_used, 0, sizeof(proc_t*) * (n_alloc - n_used));
      }
#ifdef LOG2STDOUT
      fprintf(LogPtr, "private_proc_t_ptr_table = %p\n", private_proc_t_ptr_table);
      fprintf(LogPtr, "sizeof(private_proc_t_ptr_table) = %d\n", sizeof(private_proc_t_ptr_table));
      fprintf(LogPtr, "sizeof(proc_t*) = %d\n", sizeof(proc_t*));
      fflush(LogPtr);
#endif
      // on the way to n_alloc, the library will allocate the underlying
      // proc_t storage whenever our private_proc_t_ptr_table[] pointer is NULL...
      if (!(ptask = read_something(PT, private_proc_t_ptr_table[n_used]))) break;
      // save ptask in private_proc_t_ptr_table[n_used]
      procs_statistic((private_proc_t_ptr_table[n_used] = ptask));  // tally this proc_t
   }

   closeproc(PT);
   PT = 0;

   // lastly, refresh each window's proc pointers table...
   if (n_saved == n_alloc)
   {
      for (i = 0; i < GROUPSMAX; i++)
      {
         memcpy(Winstk[i].ppt, private_proc_t_ptr_table, sizeof(proc_t*) * n_used);
      }
   }
   else 
   {
      n_saved = n_alloc;
      for (i = 0; i < GROUPSMAX; i++) 
      {
         Winstk[i].ppt = alloc_r(Winstk[i].ppt, sizeof(proc_t*) * n_alloc);
         memcpy(Winstk[i].ppt, private_proc_t_ptr_table, sizeof(proc_t*) * n_used);
      }
   }
 #undef n_used

#if defined(LOG2STDOUT) && defined(BUILD_4_STOCK)
   for (j = 0; j < GROUPSMAX; j++) 
   {
      w = &Winstk[j];
      fprintf(LogPtr, "kkkkkk Winstk[%d] = %d\n", j, w->max_matched_row);
   }
#endif

#ifdef LOG2STDOUT
   fprintf(LogPtr, "procs_refresh ended\n");
   fflush(LogPtr);
#endif
} // end: procs_refresh


        /*
         * This serves as our interface to the memory & cpu count (sysinfo)
         * portion of libproc.  In support of those hotpluggable resources,
         * the sampling frequencies are reduced so as to minimize overhead.
         * We'll strive to verify the number of cpus every 5 minutes and the
         * memory availability/usage every 3 seconds. */
static void sysinfo_refresh (int forced) 
{
   static time_t mem_secs, cpu_secs;
   time_t cur_secs;

   if (forced)
      mem_secs = cpu_secs = 0;
   time(&cur_secs);

   /*** hotplug_acclimated ***/
   if (3 <= cur_secs - mem_secs) 
   {
      meminfo();
      mem_secs = cur_secs;
   }
#ifndef PRETEND8CPUS
   /*** hotplug_acclimated ***/
   if (300 <= cur_secs - cpu_secs) 
   {
      cpuinfo();
      Cpu_faux_tot = smp_num_cpus;
      cpu_secs = cur_secs;
#ifndef NUMA_DISABLE
      if (Libnuma_handle)
         Numa_node_tot = Numa_max_node() + 1;
#endif
   }
#endif
} // end: sysinfo_refresh

/*######  Inspect Other Output  ##########################################*/

        /*
         * HOWTO Extend the top 'inspect' functionality:
         *
         * To exploit the 'Y' interactive command, one must add entries to
         * the top personal configuration file.  Such entries simply reflect
         * a file to be read or command/pipeline to be executed whose results
         * will then be displayed in a separate scrollable window.
         *
         * Entries beginning with a '#' character are ignored, regardless of
         * content.  Otherwise they consist of the following 3 elements, each
         * of which must be separated by a tab character (thus 2 '\t' total):
         *     type:  literal 'file' or 'pipe'
         *     name:  selection shown on the Inspect screen
         *     fmts:  string representing a path or command
         *
         * The two types of Inspect entries are not interchangeable.
         * Those designated 'file' will be accessed using fopen/fread and must
         * reference a single file in the 'fmts' element.  Entries specifying
         * 'pipe' will employ popen/fread, their 'fmts' element could contain
         * many pipelined commands and, none can be interactive.
         *
         * Here are some examples of both types of inspection entries.
         * The first entry will be ignored due to the initial '#' character.
         * For clarity, the pseudo tab depictions (^I) are surrounded by an
         * extra space but the actual tabs would not be.
         *
         *     # pipe ^I Sockets ^I lsof -n -P -i 2>&1
         *     pipe ^I Open Files ^I lsof -P -p %d 2>&1
         *     file ^I NUMA Info ^I /proc/%d/numa_maps
         *     pipe ^I Log ^I tail -n100 /var/log/syslog | sort -Mr
         *
         * Caution:  If the output contains unprintable characters they will
         * be displayed in either the ^I notation or hexidecimal <FF> form.
         * This applies to tab characters as well.  So if one wants a more
         * accurate display, any tabs should be expanded within the 'fmts'.
         *
         * The following example takes what could have been a 'file' entry
         * but employs a 'pipe' instead so as to expand the tabs.
         *
         *     # next would have contained '\t' ...
         *     # file ^I <your_name> ^I /proc/%d/status
         *     # but this will eliminate embedded '\t' ...
         *     pipe ^I <your_name> ^I cat /proc/%d/status | expand -
         */

        /*
         * Our driving table support, the basis for generalized inspection,
         * built at startup (if at all) from rcfile or demo entries. */
struct I_ent {
   void (*func)(char *, int);     // a pointer to file/pipe/demo function
   char *type;                    // the type of entry ('file' or 'pipe')
   char *name;                    // the selection label for display
   char *fmts;                    // format string to build path or command
   int   farg;                    // 1 = '%d' in fmts, 0 = not (future use)
   const char *caps;              // not really caps, show_special() delim's
   char *fstr;                    // entry's current/active search string
   int   flen;                    // above's strlen, without call overhead
};
struct I_struc {
   int demo;                      // do NOT save table entries in rcfile
   int total;                     // total I_ent table entries
   char *raw;                     // all entries for 'W', incl '#' & blank
   struct I_ent *tab;
};
static struct I_struc Inspect;

static char   **Insp_p;           // pointers to each line start
static int      Insp_nl;          // total lines, total Insp_p entries
static char    *Insp_buf;         // the results from insp_do_file/pipe
static size_t   Insp_bufsz;       // allocated size of Insp_buf
static size_t   Insp_bufrd;       // bytes actually in Insp_buf
static struct I_ent *Insp_sel;    // currently selected Inspect entry

        // Our 'make status line' macro
#define INSP_MKSL(big,txt) { int _sz = big ? Screen_cols : 80; \
   putp(tg2(0, (Msg_row = 3))); \
   PUTT("%s%.*s", Curwin->capclr_hdr, Screen_cols -1 \
      , fmtmk("%-*.*s%s", _sz, _sz, txt, Cap_clr_eol)); \
   putp(Caps_off); fflush(stdout); }

        // Our 'row length' macro, equivalent to a strlen() call
#define INSP_RLEN(idx) (int)(Insp_p[idx +1] - Insp_p[idx] -1)

        // Our 'busy' (wait please) macro
#define INSP_BUSY  { INSP_MKSL(0, N_txt_Norm_tab(YINSP_workin_txt)); }


        /*
         * Establish the number of lines present in the Insp_buf glob plus
         * build the all important row start array.  It is that array that
         * others will rely on since we dare not try to use strlen() on what
         * is potentially raw binary data.  Who knows what some user might
         * name as a file or include in a pipeline (scary, ain't it?). */
static void insp_cnt_nl (void) 
{
   char *beg = Insp_buf;
   char *cur = Insp_buf;
   char *end = Insp_buf + Insp_bufrd + 1;

#ifdef INSP_SAVEBUF
{
   static int n = 1;
   char fn[SMLBUFSIZ];
   FILE *fd;
   snprintf(fn, sizeof(fn), "%s.Insp_buf.%02d.txt", Myname, n++);
   fd = fopen(fn, "w");
   if (fd) 
   {
      fwrite(Insp_buf, 1, Insp_bufrd, fd);
      fclose(fd);
   }
}
#endif
   Insp_p = alloc_c(sizeof(char*) * 2);

   for (Insp_nl = 0; beg < end; beg++) 
   {
      if (*beg == '\n') 
      {
         Insp_p[Insp_nl++] = cur;
         // keep our array ahead of next potential need (plus the 2 above)
         Insp_p = alloc_r(Insp_p, (sizeof(char*) * (Insp_nl +3)));
         cur = beg +1;
      }
   }
   Insp_p[0] = Insp_buf;
   Insp_p[Insp_nl++] = cur;
   Insp_p[Insp_nl] = end;
   if ((end - cur) == 1)          // if there's an eof null delimiter,
      --Insp_nl;                  // don't count it as a new line
} // end: insp_cnt_nl


#ifndef INSP_OFFDEMO
        /*
         * The pseudo output DEMO utility. */
static void insp_do_demo (char *fmts, int pid) 
{
   (void)fmts; (void)pid;
   /* next will put us on a par with the real file/pipe read buffers
    ( and also avoid a harmless, but evil sounding, valgrind warning ) */
   Insp_bufsz = READMINSZ + strlen(N_txt_Norm_tab(YINSP_dstory_txt));
   Insp_buf   = alloc_c(Insp_bufsz);
   Insp_bufrd = snprintf(Insp_buf, Insp_bufsz, "%s", N_txt_Norm_tab(YINSP_dstory_txt));
   insp_cnt_nl();
} // end: insp_do_demo
#endif


        /*
         * The generalized FILE utility. */
static void insp_do_file (char *fmts, int pid) 
{
   char buf[LRGBUFSIZ];
   FILE *fp;
   int rc;

   snprintf(buf, sizeof(buf), fmts, pid);
   fp = fopen(buf, "r");
   rc = readfile(fp, &Insp_buf, &Insp_bufsz, &Insp_bufrd);
   if (fp) fclose(fp);
   if (rc) Insp_bufrd = snprintf(Insp_buf, Insp_bufsz, "%s"
      , fmtmk(N_fmt_Norm_tab(YINSP_failed_fmt), strerror(errno)));
   insp_cnt_nl();
} // end: insp_do_file


        /*
         * The generalized PIPE utility. */
static void insp_do_pipe (char *fmts, int pid) 
{
   char buf[LRGBUFSIZ];
   FILE *fp;
   int rc;

   snprintf(buf, sizeof(buf), fmts, pid);
   fp = popen(buf, "r");
   rc = readfile(fp, &Insp_buf, &Insp_bufsz, &Insp_bufrd);
   if (fp) pclose(fp);
   if (rc) Insp_bufrd = snprintf(Insp_buf, Insp_bufsz, "%s"
      , fmtmk(N_fmt_Norm_tab(YINSP_failed_fmt), strerror(errno)));
   insp_cnt_nl();
} // end: insp_do_pipe


        /*
         * This guy is a *Helper* function serving the following two masters:
         *   insp_find_str() - find the next Insp_sel->fstr match
         *   insp_make_row() - highlight any Insp_sel->fstr matches in-view
         * If Insp_sel->fstr is found in the designated row, he returns the
         * offset from the start of the row, otherwise he returns a huge
         * integer so traditional fencepost usage can be employed. */
static inline int insp_find_ofs (int col, int row) 
{
 #define begFS (int)(fnd - Insp_p[row])
   char *p, *fnd = NULL;

   if (Insp_sel->fstr[0]) 
   {
      // skip this row, if there's no chance of a match
      if (memchr(Insp_p[row], Insp_sel->fstr[0], INSP_RLEN(row))) 
      {
         for ( ; col < INSP_RLEN(row); col++) 
         {
            if (!*(p = Insp_p[row] + col))       // skip any empty strings
               continue;
            fnd = STRSTR(p, Insp_sel->fstr);     // with binary data, each
            if (fnd)                             // row may have '\0'.  so
               break;                            // our scans must be done
            col += strlen(p);                    // as individual strings.
         }
         if (fnd && fnd < Insp_p[row + 1])       // and, we must watch out
            return begFS;                        // for potential overrun!
      }
   }
   return INT_MAX;
 #undef begFS
} // end: insp_find_ofs


        /*
         * This guy supports the inspect 'L' and '&' search provisions
         * and returns the row and *optimal* column for viewing any match
         * ( we'll always opt for left column justification since any )
         * ( preceding ctrl chars appropriate an unpredictable amount ) */
static void insp_find_str (int ch, int *col, int *row) 
{
 #define reDUX (found) ? N_txt_Norm_tab(WORD_another_txt) : ""
   static int found;

   if ((ch == '&' || ch == 'n') && !Insp_sel->fstr[0]) 
   {
      show_msg(N_txt_Norm_tab(FIND_no_next_txt));
      return;
   }
   if (ch == 'L' || ch == '/') 
   {
      snprintf(Insp_sel->fstr, FNDBUFSIZ, "%s", ioline(N_txt_Norm_tab(GET_find_str_txt)));
      Insp_sel->flen = strlen(Insp_sel->fstr);
      found = 0;
   }
   if (Insp_sel->fstr[0]) 
   {
      int xx, yy;

      INSP_BUSY;
      for (xx = *col, yy = *row; yy < Insp_nl; ) 
      {
         xx = insp_find_ofs(xx, yy);
         if (xx < INSP_RLEN(yy)) 
         {
            found = 1;
            if (xx == *col &&  yy == *row) 
            {                                    // matched where we were!
               ++xx;                             // ( was the user maybe )
               continue;                         // ( trying to fool us? )
            }
            *col = xx;
            *row = yy;
            return;
         }
         xx = 0;
         ++yy;
      }
      show_msg(fmtmk(N_fmt_Norm_tab(FIND_no_find_fmt), reDUX, Insp_sel->fstr));
   }
 #undef reDUX
} // end: insp_find_str


        /*
         * This guy is a *Helper* function responsible for positioning a
         * single row in the current 'X axis', then displaying the results.
         * Along the way, he makes sure control characters and/or unprintable
         * characters display in a less-like fashion:
         *    '^A'    for control chars
         *    '<BC>'  for other unprintable stuff
         * Those will be highlighted with the current windows's capclr_msg,
         * while visible search matches display with capclr_hdr for emphasis.
         * ( we hide ugly plumbing in macros to concentrate on the algorithm ) */
static inline void insp_make_row (int col, int row) 
{
 #define maxSZ ( Screen_cols - (to + 1) )
 #define capNO { if (hicap) { putp(Caps_off); hicap = 0; } }
 #define mkFND { PUTT("%s%.*s%s", Curwin->capclr_hdr, maxSZ, Insp_sel->fstr, Caps_off); \
    fr += Insp_sel->flen -1; to += Insp_sel->flen; hicap = 0; }
#ifndef INSP_JUSTNOT
 #define mkCTL { int x = maxSZ; const char *p = fmtmk("^%c", uch + '@'); \
    PUTT("%s%.*s", (!hicap) ? Curwin->capclr_msg : "", x, p); to += 2; hicap = 1; }
 #define mkUNP { int x = maxSZ; const char *p = fmtmk("<%02X>", uch); \
    PUTT("%s%.*s", (!hicap) ? Curwin->capclr_msg : "", x, p); to += 4; hicap = 1; }
#else
 #define mkCTL { if ((to += 2) <= Screen_cols) \
    PUTT("%s^%c", (!hicap) ? Curwin->capclr_msg : "", uch + '@'); hicap = 1; }
 #define mkUNP { if ((to += 4) <= Screen_cols) \
    PUTT("%s<%02X>", (!hicap) ? Curwin->capclr_msg : "", uch); hicap = 1; }
#endif
 #define mkSTD { capNO; if (++to <= Screen_cols) { static char _str[2]; \
    _str[0] = uch; putp(_str); } }
   char tline[SCREENMAX];
   int fr, to, ofs;
   int hicap = 0;

   capNO;
   if (col < INSP_RLEN(row))
      memcpy(tline, Insp_p[row] + col, sizeof(tline));
   else tline[0] = '\n';

   for (fr = 0, to = 0, ofs = 0; to < Screen_cols -1; fr++) 
   {
      if (!ofs)
         ofs = insp_find_ofs(col + fr, row);
      if (col + fr < ofs) 
      {
         unsigned char uch = tline[fr];
         if (uch == '\n')   break;     // a no show  (he,he)
         if (uch > 126)     mkUNP      // show as: '<AB>'
         else if (uch < 32) mkCTL      // show as:  '^C'
         else               mkSTD      // a show off (he,he)
      } else {              mkFND      // a big show (he,he)
         ofs = 0;
      }
      if (col + fr >= INSP_RLEN(row)) break;
   }
   capNO;
   putp(Cap_clr_eol);

 #undef maxSZ
 #undef capNO
 #undef mkFND
 #undef mkCTL
 #undef mkUNP
 #undef mkSTD
} // end: insp_make_row


        /*
         * This guy is an insp_view_choice() *Helper* function who displays
         * a page worth of of the user's damages.  He also creates a status
         * line based on maximum digits for the current selection's lines and
         * hozizontal position (so it serves to inform, not distract, by
         * otherwise being jumpy). */
static inline void insp_show_pgs (int col, int row, int max) 
{
   char buf[SMLBUFSIZ];
   int r = snprintf(buf, sizeof(buf), "%d", Insp_nl);
   int c = snprintf(buf, sizeof(buf), "%d", col +Screen_cols);
   int l = row +1, ls = Insp_nl;;

   if (!Insp_bufrd)
      l = ls = 0;
   snprintf(buf, sizeof(buf), N_fmt_Norm_tab(YINSP_status_fmt)
      , Insp_sel->name
      , r, l, r, ls
      , c, col + 1, c, col + Screen_cols
      , (unsigned long)Insp_bufrd);
   INSP_MKSL(0, buf);

   for ( ; max && row < Insp_nl; row++) 
   {
      putp("\n");
      insp_make_row(col, row);
      --max;
   }

   if (max)
      putp(Cap_nl_clreos);
} // end: insp_show_pgs


        /*
         * This guy is responsible for displaying the Insp_buf contents and
         * managing all scrolling/locate requests until the user gives up. */
static int insp_view_choice (proc_t *obj) 
{
#ifdef INSP_SLIDE_1
 #define hzAMT  1
#else
 #define hzAMT  8
#endif
 #define maxLN (Screen_rows - (Msg_row +1))
 #define makHD(b1,b2,b3) { \
    snprintf(b1, sizeof(b1), "%s", make_number(obj->tid,   5, 1, AUTOX_NO)); \
    snprintf(b2, sizeof(b2), "%s", make_string(obj->cmd,   8, 1, AUTOX_NO)); \
    snprintf(b3, sizeof(b3), "%s", make_string(obj->euser, 8, 1, AUTOX_NO)); }
 #define makFS(dst) { if (Insp_sel->flen < 22) \
       snprintf(dst, sizeof(dst), "%s", Insp_sel->fstr); \
    else snprintf(dst, sizeof(dst), "%.19s...", Insp_sel->fstr); }
   char buf[SMLBUFSIZ];
   int key, curlin = 0, curcol = 0;

signify_that:
   putp(Cap_clr_scr);
   adj_geometry();

   for (;;) {
      char pid[6], cmd[9], usr[9];

      if (curcol < 0) curcol = 0;
      if (curlin >= Insp_nl) curlin = Insp_nl -1;
      if (curlin < 0) curlin = 0;

      makFS(buf)
      makHD(pid,cmd,usr)
      putp(Cap_home);
      show_special(1, fmtmk(N_unq_Uniq_tab(INSP_hdrview_fmt)
         , pid, cmd, usr, (Insp_sel->fstr[0]) ? buf : " N/A "));   // nls_maybe
      insp_show_pgs(curcol, curlin, maxLN);
      fflush(stdout);
      /* fflush(stdin) didn't do the trick, so we'll just dip a little deeper
         lest repeated <Enter> keys produce immediate re-selection in caller */
      tcflush(STDIN_FILENO, TCIFLUSH);

      if (Frames_signal) goto signify_that;
      key = iokey(1);
      if (key < 1) goto signify_that;

      switch (key) {
         case kbd_ENTER:          // must force new iokey()
            key = INT_MAX;        // fall through !
         case kbd_ESC:
         case 'q':
            putp(Cap_clr_scr);
            return key;
         case kbd_LEFT:
            curcol -= hzAMT;
            break;
         case kbd_RIGHT:
            curcol += hzAMT;
            break;
         case kbd_UP:
            --curlin;
            break;
         case kbd_DOWN:
            ++curlin;
            break;
         case kbd_PGUP:
         case 'b':
            curlin -= maxLN -1;   // keep 1 line for reference
            break;
         case kbd_PGDN:
         case kbd_SPACE:
            curlin += maxLN -1;   // ditto
            break;
         case kbd_HOME:
         case 'g':
            curcol = curlin = 0;
            break;
         case kbd_END:
         case 'G':
            curcol = 0;
            curlin = Insp_nl - maxLN;
            break;
         case 'L':
         case '&':
         case '/':
         case 'n':
            insp_find_str(key, &curcol, &curlin);
            // must re-hide cursor in case a prompt for a string makes it huge
            putp((Cursor_state = Cap_curs_hide));
            break;
         case '=':
            snprintf(buf, sizeof(buf), "%s: %s", Insp_sel->type, Insp_sel->fmts);
            INSP_MKSL(1, buf);    // show an extended SL
            if (iokey(1) < 1)
               goto signify_that;
            break;
         default:                 // keep gcc happy
            break;
      }
   }
 #undef hzAMT
 #undef maxLN
 #undef makHD
 #undef makFS
} // end: insp_view_choice


        /*
         * This is the main Inspect routine, responsible for:
         *   1) validating the passed pid (required, but not always used)
         *   2) presenting/establishing the target selection
         *   3) arranging to fill Insp_buf (via the Inspect.tab[?].func)
         *   4) invoking insp_view_choice for viewing/scrolling/searching
         *   5) cleaning up the dynamically acquired memory afterwards */
static void inspection_utility (int pid) 
{
 #define mkSEL(dst) { for (i = 0; i < Inspect.total; i++) Inspect.tab[i].caps = "~1"; \
      Inspect.tab[sel].caps = "~4"; dst[0] = '\0'; \
      for (i = 0; i < Inspect.total; i++) { char _s[SMLBUFSIZ]; \
         snprintf(_s, sizeof(_s), " %s %s", Inspect.tab[i].name, Inspect.tab[i].caps); \
         strcat(dst, _s); } }
   char sels[MEDBUFSIZ];
   static int sel;
   int i, key;
   proc_t *p;

   for (i = 0, p = NULL; i < Frame_maxtask; i++)
   {
      if (pid == Curwin->ppt[i]->tid) 
      {
         p = Curwin->ppt[i];
         break;
      }
   }
   if (!p) 
   {
      show_msg(fmtmk(N_fmt_Norm_tab(YINSP_pidbad_fmt), pid));
      return;
   }
   // must re-hide cursor since the prompt for a pid made it huge
   putp((Cursor_state = Cap_curs_hide));
signify_that:
   putp(Cap_clr_scr);
   adj_geometry();

   key = INT_MAX;
   do 
   {
      mkSEL(sels);
      putp(Cap_home);
      show_special(1, fmtmk(N_unq_Uniq_tab(INSP_hdrsels_fmt)
         , pid, p->cmd, p->euser, sels));
      INSP_MKSL(0, " ");

      if (Frames_signal) goto signify_that;
      if (key == INT_MAX) key = iokey(1);
      if (key < 1) goto signify_that;

      switch (key) {
         case 'q':
         case kbd_ESC:
            break;
         case kbd_END:
            sel = 0;              // fall through !
         case kbd_LEFT:
            if (--sel < 0) sel = Inspect.total -1;
            key = INT_MAX;
            break;
         case kbd_HOME:
            sel = Inspect.total;  // fall through !
         case kbd_RIGHT:
            if (++sel >= Inspect.total) sel = 0;
            key = INT_MAX;
            break;
         case kbd_ENTER:
            INSP_BUSY;
            Insp_sel = &Inspect.tab[sel];
            Inspect.tab[sel].func(Inspect.tab[sel].fmts, pid);
            key = insp_view_choice(p);
            free(Insp_buf);
            free(Insp_p);
            break;
         default:
            goto signify_that;
      }
   } while (key != 'q' && key != kbd_ESC);

 #undef mkSEL
} // end: inspection_utility
#undef INSP_MKSL
#undef INSP_RLEN
#undef INSP_BUSY

/*######  Startup routines  ##############################################*/

        /*
         * No matter what *they* say, we handle the really really BIG and
         * IMPORTANT stuff upon which all those lessor functions depend! */
static void before (char *me) 
{
   struct sigaction sa;
   proc_t p;
   int i;

   atexit(close_stdout);

#ifndef BUILD_4_STOCK
   // is /proc mounted?
   look_up_our_self(&p);
#endif

   // setup our program name
   Myname = strrchr(me, '/');
   if (Myname) ++Myname; else Myname = me;

   // accommodate nls/gettext potential translations
   initialize_nls();

   // establish cpu particulars
#ifdef PRETEND8CPUS
   smp_num_cpus = 8;
#endif
   Cpu_faux_tot = smp_num_cpus;
   Cpu_States_fmts = N_unq_Uniq_tab(STATE_lin2x4_fmt);
   if (linux_version_code > LINUX_VERSION(2, 5, 41))
      Cpu_States_fmts = N_unq_Uniq_tab(STATE_lin2x5_fmt);
   if (linux_version_code >= LINUX_VERSION(2, 6, 0))
      Cpu_States_fmts = N_unq_Uniq_tab(STATE_lin2x6_fmt);
   if (linux_version_code >= LINUX_VERSION(2, 6, 11))
      Cpu_States_fmts = N_unq_Uniq_tab(STATE_lin2x7_fmt);

   // get virtual page stuff
   Page_size = getpagesize();

   i = Page_size;
   while(i > 1024) { i >>= 1; Pg2K_shft++; }

#ifndef DISABLE_HISTORY_HASH
   // prep for HISTORY_t's put/get hashing optimizations
   for (i = 0; i < HHASH_SIZ; i++) HHash_nul[i] = -1;

   memcpy(HHash_one, HHash_nul, sizeof(HHash_nul));
   memcpy(HHash_two, HHash_nul, sizeof(HHash_nul));
#endif

#ifndef NUMA_DISABLE
#if defined(PRETEND_NUMA) || defined(PRETEND8CPUS)
   Numa_node_tot = Numa_max_node() + 1;
#else
   // we'll try for the most recent version, then a version we know works...
   if ((Libnuma_handle = dlopen("libnuma.so", RTLD_LAZY)) || 
       (Libnuma_handle = dlopen("libnuma.so.1", RTLD_LAZY))) 
   {
      Numa_max_node = dlsym(Libnuma_handle, "numa_max_node");
      Numa_node_of_cpu = dlsym(Libnuma_handle, "numa_node_of_cpu");
      if (Numa_max_node && Numa_node_of_cpu)
      {
         Numa_node_tot = Numa_max_node() + 1;
      }
      else 
      {
         dlclose(Libnuma_handle);
         Libnuma_handle = NULL;
      }
   }
#endif
#endif

#ifndef SIGRTMAX       // not available on hurd, maybe others too
#define SIGRTMAX 32
#endif
   // lastly, establish a robust signals environment
   sigemptyset(&sa.sa_mask);
   // with user position preserved through SIGWINCH, we must avoid SA_RESTART
   sa.sa_flags = 0;
   for (i = SIGRTMAX; i; i--) 
   {
      switch (i) 
      {
         case SIGALRM: case SIGHUP:  case SIGINT:
         case SIGPIPE: case SIGQUIT: case SIGTERM:
         case SIGUSR1: case SIGUSR2:
            sa.sa_handler = sig_endpgm;
            break;
         case SIGTSTP: case SIGTTIN: case SIGTTOU:
            sa.sa_handler = sig_paused;
            break;
         case SIGCONT: case SIGWINCH:
            sa.sa_handler = sig_resize;
            break;
         default:
            sa.sa_handler = sig_abexit;
            break;
         case SIGCHLD: // we can't catch this
            continue;  // when opening a pipe
      }
      sigaction(i, &sa, NULL);
   }
} // end: before


        /*
         * A configs_read *Helper* function responsible for converting
         * a single window's old rc stuff into a new style rcfile entry */
static int config_cvt (WIN_t *w) 
{
   static struct {
      int old, new;
   } flags_tab[] = {
    #define old_View_NOBOLD  0x000001
    #define old_VISIBLE_tsk  0x000008
    #define old_Qsrt_NORMAL  0x000010
    #define old_Show_Highlight_COLS  0x000200
    #define old_Show_THREAD  0x010000
      { old_View_NOBOLD, View_NOBOLD },
      { old_VISIBLE_tsk, Show_TASKON },
      { old_Qsrt_NORMAL, Qsrt_NORMAL },
      { old_Show_Highlight_COLS, Show_Highlight_COLS },
      { old_Show_THREAD, 0           }
    #undef old_View_NOBOLD
    #undef old_VISIBLE_tsk
    #undef old_Qsrt_NORMAL
    #undef old_Show_Highlight_COLS
    #undef old_Show_THREAD
   };
   static const char fields_src[] = CVT_FIELDS;
#ifdef OOMEM_ENABLE
   char fields_dst[PFLAGSSIZ], *p1, *p2;
#else
   char fields_dst[PFLAGSSIZ];
#endif
   int i, j, x;

   // first we'll touch up this window's winflags...
   x = w->rc.winflags;
   w->rc.winflags = 0;
   for (i = 0; i < MAXTBL(flags_tab); i++) 
   {
      if (x & flags_tab[i].old) 
      {
         x &= ~flags_tab[i].old;
         w->rc.winflags |= flags_tab[i].new;
      }
   }
   w->rc.winflags |= x;

   // now let's convert old top's more limited fields...
   j = strlen(w->rc.fieldscur);
   if (j > CVT_FLDMAX)
      return 1;
   strcpy(fields_dst, fields_src);
#ifdef OOMEM_ENABLE
   /* all other fields represent the 'on' state with a capitalized version
      of a particular qwerty key.  for the 2 additional suse out-of-memory
      fields it makes perfect sense to do the exact opposite, doesn't it?
      in any case, we must turn them 'off' temporarily... */
   if ((p1 = strchr(w->rc.fieldscur, '[')))  *p1 = '{';
   if ((p2 = strchr(w->rc.fieldscur, '\\'))) *p2 = '|';
#endif
   for (i = 0; i < j; i++) 
   {
      int c = w->rc.fieldscur[i];
      x = tolower(c) - 'a';
      if (x < 0 || x >= CVT_FLDMAX)
         return 1;
      fields_dst[i] = fields_src[x];
      if (isupper(c))
         FieldOn(fields_dst[i]);
   }
#ifdef OOMEM_ENABLE
   // if we turned any suse only fields off, turn 'em back on OUR way...
   if (p1) FieldOn(fields_dst[p1 - w->rc.fieldscur]);
   if (p2) FieldOn(fields_dst[p2 - w->rc.fieldscur]);
#endif
   strcpy(w->rc.fieldscur, fields_dst);

   // lastly, we must adjust the old sort field enum...
   x = w->rc.sortindex;
   w->rc.sortindex = fields_src[x] - FLD_OFFSET;

   Rc_questions = 1;
   return 0;
} // end: config_cvt


        /*
         * Build the local RC file name then try to read both of 'em.
         * 'SYS_RCFILESPEC' contains two lines consisting of the secure
         *   mode switch and an update interval.  It's presence limits what
         *   ordinary users are allowed to do.
         * 'Rc_name' contains multiple lines - 3 global + 3 per window.
         *   line 1  : an eyecatcher and creating program/alias name
         *   line 2  : an id, Mode_altcsr, Mode_irixps, Delay_time, Curwin.
         *   For each of the 4 windows:
         *     line a: contains w->winname, fieldscur
         *     line b: contains w->winflags, sortindex, maxtasks
         *     line c: contains w->summcolor, msgscolor, headcolor, taskcolor
         *   line 15 : Fixed_widest, Summary_unit_scale, Task_unit_scale, Zero_suppress */
static void configs_read (void) 
{
   float tmp_delay = DEF_DELAY;
   char fbuf[LRGBUFSIZ];
   const char *ptmp;
   FILE *fp;
   int i;

   ptmp = getenv("HOME");
#ifdef LOG2STDOUT
   fprintf(LogPtr, "getenv(\"HOME\") = %s\n", ptmp);
#endif
   snprintf(Rc_name, sizeof(Rc_name), "%s/.%src", (ptmp && *ptmp) ? ptmp : ".", Myname);
#ifdef LOG2STDOUT
   fprintf(LogPtr,"Rc_name = %s\n", Rc_name);
   fflush(LogPtr);
#endif

   fp = fopen(SYS_RCFILESPEC, "r");
   if (fp) 
   {
      if (fgets(fbuf, sizeof(fbuf), fp)) 
      {     // sys rc file, line 1
         Secure_mode = 1;
         if (fgets(fbuf, sizeof(fbuf), fp))    // sys rc file, line 2
            sscanf(fbuf, "%f", &Rc.delay_time);
      }
      fclose(fp);
   }
#ifdef LOG2STDOUT
   else
   {
      fprintf(LogPtr, "failed to open SYS_RCFILESPEC = %s\n", SYS_RCFILESPEC);
      fflush(LogPtr);
   }
#endif

   fp = fopen(Rc_name, "r");
   if (fp) 
   {
      int tmp_whole, tmp_fract;
      if (fgets(fbuf, sizeof(fbuf), fp))       // ignore eyecatcher
         ;                                     // avoid -Wunused-result
      if (6 != fscanf(fp
         , "Id:%c, Mode_altscr=%d, Mode_irixps=%d, Delay_time=%d.%d, Curwin=%d\n"
         , &Rc.id, &Rc.mode_altscreen, &Rc.mode_irixps, &tmp_whole, &tmp_fract, &i)) 
      {
            ptmp = fmtmk(N_fmt_Norm_tab(RC_bad_files_fmt), Rc_name);
            Rc_questions = -1;
            goto try_inspect_entries;          // maybe a faulty 'inspect' echo
      }
      // you saw that, right?  (fscanf stickin' it to 'i')
      Curwin = &Winstk[i];
      // this may be ugly, but it keeps us locale independent...
      tmp_delay = (float)tmp_whole + (float)tmp_fract / 1000;

      for (i = 0 ; i < GROUPSMAX; i++) 
      {
         int x;
         WIN_t *w = &Winstk[i];
         ptmp = fmtmk(N_fmt_Norm_tab(RC_bad_entry_fmt), i+1, Rc_name);

         // note: "fieldscur=%__s" on next line should equal PFLAGSSIZ !
         if (2 != fscanf(fp, "%3s\tfieldscur=%83s\n", w->rc.winname, w->rc.fieldscur))
               goto default_or_error;
#if PFLAGSSIZ > 88
 // too bad fscanf is not as flexible with his format string as snprintf
 # error Hey, fix the above fscanf 'PFLAGSSIZ' dependency !
#endif
         if (3 != fscanf(fp, "\twinflags=%d, sortindex=%d, maxtasks=%d\n"
            , &w->rc.winflags, &w->rc.sortindex, &w->rc.maxtasks))
               goto default_or_error;
         if (4 != fscanf(fp, "\tsummcolor=%d, msgscolor=%d, headcolor=%d, taskcolor=%d\n"
            , &w->rc.summcolor, &w->rc.msgscolor
            , &w->rc.headcolor, &w->rc.taskcolor))
               goto default_or_error;
#ifdef BUILD_4_STOCK
         if (2 != fscanf(fp, "\tustype=%d, filter_prefix=%6s\n"
            , &w->rc.ustype, w->rc.filter_prefix))
               goto default_or_error;
#endif

#ifdef LOG2STDOUT
        // very important it will cause the log bad encoded
        //fprintf(LogPtr, "w[%d]->rc.fieldscur = %s\n", i, w->rc.fieldscur);
#ifdef BUILD_4_STOCK
        fprintf(LogPtr, "Winstk[%d]=%s, ustype=%d, filter_prefix=%s\n", i, w->rc.winname, w->rc.ustype, w->rc.filter_prefix);
#endif
        fflush(LogPtr);
#endif
         switch (Rc.id) 
         {
            case 'a':                          // 3.2.8 (former procps)
               if (config_cvt(w))
                  goto default_or_error;          // fall through !
            case 'f':                          // 3.3.0 thru 3.3.3 (procps-ng)
               SETwinflags(w, Show_JRNUMS);              // fall through !
            case 'g':                          // 3.3.4 thru 3.3.8
               scat(w->rc.fieldscur, RCF_PLUS_H); // fall through !
            case 'h':                          // current RCF_VERSION_ID
            default:                           // and future versions?
               if (strlen(w->rc.fieldscur) != sizeof(DEF_FIELDS) - 1)
                  goto default_or_error;
               for (x = 0; x < P_MAXPFLAGS; ++x)
                  if (P_MAXPFLAGS <= FieldGetValue(w, x))
                     goto default_or_error;
               break;
         }
#ifndef USE_X_COLHDR
         OFFwinflags(w, NOHIFND_xxx | NOHISEL_xxx);
#endif
      } // end: for (GROUPSMAX)

      // any new addition(s) last, for older rcfiles compatibility...
      if (fscanf(fp, "Fixed_widest=%d, Summary_unit_scale=%d, Task_unit_scale=%d, Zero_suppress=%d\n"
         , &Rc.fixed_widest, &Rc.summary_unit_scale, &Rc.task_unit_scale, &Rc.zero_suppress))
            ;                                  // avoid -Wunused-result

try_inspect_entries:
      // we'll start off Inspect stuff with 1 'potential' blank line
      // ( only realized if we end up with Inspect.total > 0 )
      for (i = 0, Inspect.raw = alloc_s("\n");;) 
      {
       #define iT(element) Inspect.tab[i].element
         size_t lraw = strlen(Inspect.raw) +1;
         char *s;

         if (!fgets(fbuf, sizeof(fbuf), fp)) break;
         lraw += strlen(fbuf) +1;
         Inspect.raw = alloc_r(Inspect.raw, lraw);
         strcat(Inspect.raw, fbuf);

         if (fbuf[0] == '#' || fbuf[0] == '\n') continue;
         Inspect.tab = alloc_r(Inspect.tab, sizeof(struct I_ent) * (i + 1));
         ptmp = fmtmk(N_fmt_Norm_tab(YINSP_rcfile_fmt), i +1);

         if (!(s = strtok(fbuf, "\t\n"))) { Rc_questions = 1; continue; }
         iT(type) = alloc_s(s);
         if (!(s = strtok(NULL, "\t\n"))) { Rc_questions = 1; continue; }
         iT(name) = alloc_s(s);
         if (!(s = strtok(NULL, "\t\n"))) { Rc_questions = 1; continue; }
         iT(fmts) = alloc_s(s);

         switch (toupper(fbuf[0])) 
         {
            case 'F':
               iT(func) = insp_do_file;
               break;
            case 'P':
               iT(func) = insp_do_pipe;
               break;
            default:
               Rc_questions = 1;
               continue;
         }

         iT(farg) = (strstr(iT(fmts), "%d")) ? 1 : 0;
         iT(fstr) = alloc_c(FNDBUFSIZ);
         iT(flen) = 0;

         if (Rc_questions < 0) Rc_questions = 1;
         ++i;
       #undef iT
      } // end: for ('inspect' entries)

      Inspect.total = i;
#ifndef INSP_OFFDEMO
      if (!Inspect.total) 
      {
       #define mkS(n) N_txt_Norm_tab(YINSP_demo ## n ## _txt)
         const char *sels[] = { mkS(01), mkS(02), mkS(03) };
         Inspect.total = Inspect.demo = MAXTBL(sels);
         Inspect.tab = alloc_c(sizeof(struct I_ent) * Inspect.total);
         for (i = 0; i < Inspect.total; i++) 
         {
            Inspect.tab[i].type = alloc_s(N_txt_Norm_tab(YINSP_deqtyp_txt));
            Inspect.tab[i].name = alloc_s(sels[i]);
            Inspect.tab[i].func = insp_do_demo;
            Inspect.tab[i].fmts = alloc_s(N_txt_Norm_tab(YINSP_deqfmt_txt));
            Inspect.tab[i].fstr = alloc_c(FNDBUFSIZ);
         }
       #undef mkS
      }
#endif
      if (Rc_questions < 0) 
      {
         ptmp = fmtmk(N_fmt_Norm_tab(RC_bad_files_fmt), Rc_name);
         goto default_or_error;
      }
      fclose(fp);
   } // end: if (fp)

   // lastly, establish the true runtime secure mode and delay time
   if (!getuid()) Secure_mode = 0;
   if (!Secure_mode) Rc.delay_time = tmp_delay;
   return;

default_or_error:
#ifdef RCFILE_NOERR
{  RCFull_t rcdef = DEF_RCFILE;
   fclose(fp);
   Rc = rcdef;
   for (i = 0 ; i < GROUPSMAX; i++)
      Winstk[i].rc  = Rc.win[i];
   Rc_questions = 1;
}
#else
   error_exit(ptmp);
#endif
} // end: configs_read


        /*
         * Parse command line arguments.
         * Note: it's assumed that the rc file(s) have already been read
         *       and our job is to see if any of those options are to be
         *       overridden -- we'll force some on and negate others in our
         *       best effort to honor the loser's (oops, user's) wishes... */
static void parse_args (char **args) 
{
   /* differences between us and the former top:
      -C (separate CPU states for SMP) is left to an rcfile
      -u (user monitoring) added to compliment interactive 'u'
      -p (pid monitoring) allows a comma delimited list
      -q (zero delay) eliminated as redundant, incomplete and inappropriate
            use: "nice -n-10 top -d0" to achieve what was only claimed
      .  most switches act as toggles (not 'on' sw) for more user flexibility
      .  no deprecated/illegal use of 'breakargv:' with goto
      .  bunched args are actually handled properly and none are ignored
      .  we tolerate NO whitespace and NO switches -- maybe too tolerant? */
   static const char numbs_str[] = "+,-.0123456789";
   float tmp_delay = MAXFLOAT;
   char *p;
   int i;

   while (*args) 
   {
      const char *cp = *(args++);

      while (*cp) 
      {
         char ch;
         switch ((ch = *cp)) 
         {
            case '\0':
               break;
            case '-':
               if (cp[1]) ++cp;
               else if (*args) cp = *args++;
               if (strspn(cp, numbs_str))
                  error_exit(fmtmk(N_fmt_Norm_tab(WRONG_switch_fmt)
                     , cp, Myname, N_txt_Norm_tab(USAGE_abbrev_txt)));
               continue;
            case 'b':
               Batch = 1;
               break;
            case 'c':
               TOGGLEwinflags(Curwin, Show_CMDLIN);
               break;
            case 'd':
               if (cp[1]) ++cp;
               else if (*args) cp = *args++;
               else error_exit(fmtmk(N_fmt_Norm_tab(MISSING_args_fmt), ch));
                  /* a negative delay will be dealt with shortly... */
               if (1 != sscanf(cp, "%f", &tmp_delay))
                  error_exit(fmtmk(N_fmt_Norm_tab(BAD_delayint_fmt), cp));
               break;
            case 'H':
               Thread_mode = 1;
               break;
            case 'h':
            case 'v':
               puts(fmtmk(N_fmt_Norm_tab(HELP_cmdline_fmt)
                  , procps_version, Myname, N_txt_Norm_tab(USAGE_abbrev_txt)));
               bye_bye(NULL);
            case 'i':
               TOGGLEwinflags(Curwin, Show_IDLEPS);
               Curwin->rc.maxtasks = 0;
               break;
            case 'n':
               if (cp[1]) cp++;
               else if (*args) cp = *args++;
               else error_exit(fmtmk(N_fmt_Norm_tab(MISSING_args_fmt), ch));
               if (1 != sscanf(cp, "%d", &Loops) || 1 > Loops)
                  error_exit(fmtmk(N_fmt_Norm_tab(BAD_niterate_fmt), cp));
               break;
            case 'o':
               if (cp[1]) cp++;
               else if (*args) cp = *args++;
               else error_exit(fmtmk(N_fmt_Norm_tab(MISSING_args_fmt), ch));
               if (*cp == '+') { SETwinflags(Curwin, Qsrt_NORMAL); ++cp; }
               else if (*cp == '-') { OFFwinflags(Curwin, Qsrt_NORMAL); ++cp; }
               for (i = 0; i < P_MAXPFLAGS; i++)
                  if (!STRCMP(cp, N_col_Head_tab(i))) break;
               if (i == P_MAXPFLAGS)
                  error_exit(fmtmk(N_fmt_Norm_tab(XTRA_badflds_fmt), cp));
               OFFwinflags(Curwin, Show_FOREST);
               Curwin->rc.sortindex = i;
               cp += strlen(cp);
               break;
            case 'O':
               puts("Descriptions of Fields");
               for (i = 0; i < P_MAXPFLAGS; i++)
                  puts(fmtmk("%d \t %s\t--\t%s", i+1, N_col_Head_tab(i), N_fld_Desc_tab(i)));
               bye_bye(NULL);
            case 'p':
               if (Curwin->user_select_type) error_exit(N_txt_Norm_tab(SELECT_clash_txt));
               do 
               { 
                  int pid;
                  if (cp[1]) cp++;
                  else if (*args) cp = *args++;
                  else error_exit(fmtmk(N_fmt_Norm_tab(MISSING_args_fmt), ch));
                  if (Monpidsidx >= MONPIDMAX)
                     error_exit(fmtmk(N_fmt_Norm_tab(LIMIT_exceed_fmt), MONPIDMAX));
                  if (1 != sscanf(cp, "%d", &pid) || 0 > pid)
                     error_exit(fmtmk(N_fmt_Norm_tab(BAD_mon_pids_fmt), cp));
                  if (!pid) pid = getpid();
                  for (i = 0; i < Monpidsidx; i++)
                     if (Monpids[i] == pid) goto next_pid;
                     Monpids[Monpidsidx++] = pid;
               next_pid:
                  if (!(p = strchr(cp, ','))) break;
                  cp = p;
               } while (*cp);
               break;
            case 's':
               Secure_mode = 1;
               break;
            case 'S':
               TOGGLEwinflags(Curwin, Show_CTIMES);
               break;
            case 'u':
            case 'U':
            {  const char *errmsg;
               if (Monpidsidx || Curwin->user_select_type) error_exit(N_txt_Norm_tab(SELECT_clash_txt));
               if (cp[1]) cp++;
               else if (*args) cp = *args++;
               else error_exit(fmtmk(N_fmt_Norm_tab(MISSING_args_fmt), ch));
               if ((errmsg = user_certify(Curwin, cp, ch))) error_exit(errmsg);
               cp += strlen(cp);
               break;
            }
            case 'w':
            {  const char *pn = NULL;
               int ai = 0, ci = 0;
               Width_mode = -1;
               if (cp[1]) pn = &cp[1];
               else if (*args) { pn = *args; ai = 1; }
               if (pn && !(ci = strspn(pn, "0123456789"))) { ai = 0; pn = NULL; }
               if (pn && (1 != sscanf(pn, "%d", &Width_mode)
               || Width_mode < W_MIN_COL))
                  error_exit(fmtmk(N_fmt_Norm_tab(BAD_widtharg_fmt), pn, W_MIN_COL-1));
               cp++;
               args += ai;
               if (pn) cp = pn + ci;
               continue;
            }
            default :
               error_exit(fmtmk(N_fmt_Norm_tab(UNKNOWN_opts_fmt)
                  , *cp, Myname, N_txt_Norm_tab(USAGE_abbrev_txt)));

         } // end: switch (*cp)

         // advance cp and jump over any numerical args used above
         if (*cp) cp += strspn(&cp[1], numbs_str) + 1;

      } // end: while (*cp)
   } // end: while (*args)

   // fixup delay time, maybe...
   if (MAXFLOAT > tmp_delay) 
   {
      if (Secure_mode)
         error_exit(N_txt_Norm_tab(DELAY_secure_txt));
      if (0 > tmp_delay)
         error_exit(N_txt_Norm_tab(DELAY_badarg_txt));
      Rc.delay_time = tmp_delay;
   }
} // end: parse_args


/*
typedef unsigned char   cc_t;
typedef unsigned int    speed_t;
typedef unsigned int    tcflag_t;

#define NCCS 32
struct termios
  {
//    tcflag_t c_iflag;            input mode flags 
//    tcflag_t c_oflag;            output mode flags 
//    tcflag_t c_cflag;            control mode flags 
//    tcflag_t c_lflag;            local mode flags 
//    cc_t c_line;                        line discipline 
//    cc_t c_cc[NCCS];             control characters 
//    speed_t c_ispeed;            input speed 
//    speed_t c_ospeed;            output speed 
//#define _HAVE_STRUCT_TERMIOS_C_ISPEED 1
//#define _HAVE_STRUCT_TERMIOS_C_OSPEED 1
//  };
*/
        /*
         * Set up the terminal attributes */
static void whack_terminal (void) 
{
   static char dummy[] = "dumb";
   struct termios tmptty;

#ifdef LOG2STDOUT
   fprintf(LogPtr, "STDOUT_FILENO = %d\n",STDOUT_FILENO);
   fflush(LogPtr);
#endif
   // the curses part...
   if (Batch) 
   {
      setupterm(dummy, STDOUT_FILENO, NULL);
      return;
   }
#ifdef PRETENDNOCAP
   setupterm(dummy, STDOUT_FILENO, NULL);
#else
   setupterm(NULL, STDOUT_FILENO, NULL);
#endif
   // our part...
   if (-1 == tcgetattr(STDIN_FILENO, &Tty_original))
      error_exit(N_txt_Norm_tab(FAIL_tty_get_txt));
   // ok, haven't really changed anything but we do have our snapshot
   Ttychanged = 1;

   // first, a consistent canonical mode for interactive line input
   tmptty = Tty_original;
   tmptty.c_lflag |= (ECHO | ECHOCTL | ECHOE | ICANON | ISIG);
   tmptty.c_lflag &= ~NOFLSH;
   tmptty.c_oflag &= ~TAB3;
   tmptty.c_iflag |= BRKINT;
   tmptty.c_iflag &= ~IGNBRK;
   if (key_backspace && 1 == strlen(key_backspace))
   {
      tmptty.c_cc[VERASE] = *key_backspace;
   }
#ifdef TERMIOS_ONLY
   if (-1 == tcsetattr(STDIN_FILENO, TCSAFLUSH, &tmptty))
      error_exit(fmtmk(N_fmt_Norm_tab(FAIL_tty_set_fmt), strerror(errno)));
   tcgetattr(STDIN_FILENO, &Tty_tweaked);
#endif
   // lastly, a nearly raw mode for unsolicited single keystrokes
   // ECHO
   // 回显输入字符。
   //
   // ECHOCTL
   // (不属于 POSIX) 如果同时设置了 ECHO，除了 TAB, NL, START, 和 STOP 之外的 ASCII控制信号被回显为 ^X, 这里 X 是比控制信号大 0x40 的 ASCII 码。例如，字符 0x08 (BS) 被回显为 ^H。
   //
   // ECHOE
   // 如果同时设置了 ICANON，字符 ERASE 擦除前一个输入字符，WERASE 擦除前一个词。
   //
   // ICANON
   // 启用标准模式 (canonical mode)。允许使用特殊字符 EOF, EOL, EOL2, ERASE, KILL, LNEXT, REPRINT, STATUS, 和 WERASE，以及按行的缓冲。
   tmptty.c_lflag &= ~(ECHO | ECHOCTL | ECHOE | ICANON);
   tmptty.c_cc[VMIN] = 1;
   tmptty.c_cc[VTIME] = 0;
   if (-1 == tcsetattr(STDIN_FILENO, TCSAFLUSH, &tmptty))
   {
      error_exit(fmtmk(N_fmt_Norm_tab(FAIL_tty_set_fmt), strerror(errno)));
   }
   tcgetattr(STDIN_FILENO, &Tty_raw);

#ifndef OFF_STDIOLBF
   // thanks anyway stdio, but we'll manage buffering at the frame level...
   setbuffer(stdout, Stdout_buf, sizeof(Stdout_buf));
#endif
#ifdef OFF_SCROLLBK
   // this has the effect of disabling any troublesome scrollback buffer...
   if (enter_ca_mode) putp(enter_ca_mode);
#endif
   // and don't forget to ask iokey to initialize his tinfo_tab
   iokey(0);
} // end: whack_terminal

/*######  Windows/Field Groups support  #################################*/

        /*
         * Value a window's name and make the associated group name. */
static void win_names (WIN_t *w, const char *name) 
{
   /* note: sprintf/snprintf results are "undefined" when src==dst,
            according to C99 & POSIX.1-2001 (thanks adc) */
   if (w->rc.winname != name)
   {
      snprintf(w->rc.winname, sizeof(w->rc.winname), "%s", name);
   }
   snprintf(w->grpname, sizeof(w->grpname), "%d:%s", w->winnum, name);
} // end: win_names


        /*
         * This guy just resets (normalizes) a single window
         * and he ensures pid monitoring is no longer active. */
static void win_reset (WIN_t *w) 
{
//   SETwinflags(w, Show_IDLEPS | Show_TASKON);
#ifndef SCROLLVAR_NO
   w->rc.maxtasks = w->user_select_type = w->begin_column_flag = w->begintask = w->variable_column_begin = 0;
#else
   w->rc.maxtasks = w->user_select_type = w->begin_column_flag = w->begintask = 0;
#endif
#ifdef BUILD_4_STOCK
   w->current_index = 0;
#endif
   Monpidsidx = 0;
   osel_clear(w);
} // end: win_reset


        /*
         * Display a window/field group (ie. make it "current"). */
static WIN_t *win_select (int ch) 
{
   WIN_t *w = Curwin;             // avoid gcc bloat with a local copy

   /* if there's no ch, it means we're supporting the external interface,
      so we must try to get our own darn ch by begging the user... */
   if (!ch) 
   {
      show_pmt(N_txt_Norm_tab(CHOOSE_group_txt));
      if (1 > (ch = iokey(1))) return w;
   }
   switch (ch) {
      case 'a':                   // we don't carry 'a' / 'w' in our
         w = w->next;             // pmt - they're here for a good
         break;                   // friend of ours -- wins_colors.
      case 'w':                   // (however those letters work via
         w = w->prev;             // the pmt too but gee, end-loser
         break;                   // should just press the darn key)
      case '1': case '2' : case '3': case '4':
         w = &Winstk[ch - '1'];
         break;
      default:                    // keep gcc happy
         break;
   }
   return Curwin = w;
} // end: win_select


        /*
         * Just warn the user when a command can't be honored. */
static int win_warn (int what) 
{
   switch (what) 
   {
      case Warn_ALT:
         show_msg(N_txt_Norm_tab(DISABLED_cmd_txt));
         break;
      case Warn_VIZ:
         show_msg(fmtmk(N_fmt_Norm_tab(DISABLED_win_fmt), Curwin->grpname));
         break;
      default:                    // keep gcc happy
         break;
   }
   /* we gotta' return false 'cause we're somewhat well known within
      macro society, by way of that sassy little tertiary operator... */
   return 0;
} // end: win_warn


        /*
         * Change colors *Helper* function to save/restore settings;
         * ensure colors will show; and rebuild the terminfo strings. */
static void wins_clrhlp (WIN_t *w, int save) 
{
   static int flgssav, summsav, msgssav, headsav, tasksav;

   if (save) 
   {
      flgssav = w->rc.winflags; summsav = w->rc.summcolor;
      msgssav = w->rc.msgscolor;  headsav = w->rc.headcolor; tasksav = w->rc.taskcolor;
      SETwinflags(w, Show_COLORS);
   } 
   else 
   {
      w->rc.winflags = flgssav; w->rc.summcolor = summsav;
      w->rc.msgscolor = msgssav;  w->rc.headcolor = headsav; w->rc.taskcolor = tasksav;
   }
   capsmk(w);
} // end: wins_clrhlp


        /*
         * Change colors used in display */
static void wins_colors (void) 
{
 #define kbdABORT  'q'
 #define kbdAPPLY  kbd_ENTER
   WIN_t *w = Curwin;             // avoid gcc bloat with a local copy
   int clr = w->rc.taskcolor, *pclr = &w->rc.taskcolor;
   char tgt = 'T';
   int key;

   if (0 >= max_colors) 
   {
      show_msg(N_txt_Norm_tab(COLORS_nomap_txt));
      return;
   }
   wins_clrhlp(w, 1);
   putp((Cursor_state = Cap_curs_huge));
signify_that:
   putp(Cap_clr_scr);
   adj_geometry();

   do {
      putp(Cap_home);
      // this string is well above ISO C89's minimum requirements!
      show_special(1, fmtmk(N_unq_Uniq_tab(COLOR_custom_fmt)
         , procps_version, w->grpname
         , CHKwinflags(w, View_NOBOLD) ? N_txt_Norm_tab(ON_word_only_txt) : N_txt_Norm_tab(OFF_one_word_txt)
         , CHKwinflags(w, Show_COLORS) ? N_txt_Norm_tab(ON_word_only_txt) : N_txt_Norm_tab(OFF_one_word_txt)
         , CHKwinflags(w, Show_Highlight_BOLD) ? N_txt_Norm_tab(ON_word_only_txt) : N_txt_Norm_tab(OFF_one_word_txt)
         , tgt, clr, w->grpname));
      putp(Cap_clr_eos);
      fflush(stdout);

      if (Frames_signal) goto signify_that;
      key = iokey(1);
      if (key < 1) goto signify_that;

      switch (key) {
         case 'S':
            pclr = &w->rc.summcolor;
            clr = *pclr;
            tgt = key;
            break;
         case 'M':
            pclr = &w->rc.msgscolor;
            clr = *pclr;
            tgt = key;
            break;
         case 'H':
            pclr = &w->rc.headcolor;
            clr = *pclr;
            tgt = key;
            break;
         case 'T':
            pclr = &w->rc.taskcolor;
            clr = *pclr;
            tgt = key;
            break;
         case '0': case '1': case '2': case '3':
         case '4': case '5': case '6': case '7':
            clr = key - '0';
            *pclr = clr;
            break;
         case 'B':
            TOGGLEwinflags(w, View_NOBOLD);
            break;
         case 'b':
            TOGGLEwinflags(w, Show_Highlight_BOLD);
            break;
         case 'z':
            TOGGLEwinflags(w, Show_COLORS);
            break;
         case 'a':
         case 'w':
            wins_clrhlp((w = win_select(key)), 1);
            clr = w->rc.taskcolor, pclr = &w->rc.taskcolor;
            tgt = 'T';
            break;
         default:
            break;                // keep gcc happy
      }
      capsmk(w);
   } while (key != kbdAPPLY && key != kbdABORT);

   if (key == kbdABORT) wins_clrhlp(w, 0);

 #undef kbdABORT
 #undef kbdAPPLY
} // end: wins_colors


        /*
         * Manipulate flag(s) for all our windows. */
static void wins_reflag (int what, int flg) 
{
   WIN_t *w = Curwin;             // avoid gcc bloat with a local copy

   do {
      switch (what) {
         case Flags_TOG:
            TOGGLEwinflags(w, flg);
            break;
         case Flags_SET:          // Ummmm, i can't find anybody
            SETwinflags(w, flg);         // who uses Flags_set ...
            break;
         case Flags_OFF:
            OFFwinflags(w, flg);
            break;
         default:                 // keep gcc happy
            break;
      }
         /* a flag with special significance -- user wants to rebalance
            display so we gotta' off some stuff then force on two flags... */
      if (EQUWINS_xxx == flg)
         win_reset(w);

      w = w->next;
   } while (w != Curwin);
} // end: wins_reflag


        /*
         * Set up the raw/incomplete field group windows --
         * they'll be finished off after startup completes.
         * [ and very likely that will override most/all of our efforts ]
         * [               --- life-is-NOT-fair ---                     ] */
static void wins_stage_1 (void) 
{
   WIN_t *w;
   int i;

   for (i = 0; i < GROUPSMAX; i++) 
   {
      w = &Winstk[i];
      w->winnum = i + 1;
      w->rc = Rc.win[i];
      w->captab[0] = Cap_norm;
      w->captab[1] = Cap_norm;
      w->captab[2] = w->cap_bold;
      w->captab[3] = w->capclr_sum;
      w->captab[4] = w->capclr_msg;
      w->captab[5] = w->capclr_pmt;
      w->captab[6] = w->capclr_hdr;
      w->captab[7] = w->capclr_rowhigh;
      w->captab[8] = w->capclr_rownorm;
      w->next = w + 1;
      w->prev = w - 1;
   }

   // fixup the circular chains...
   Winstk[GROUPSMAX - 1].next = &Winstk[0];
   Winstk[0].prev = &Winstk[GROUPSMAX - 1];
   Curwin = Winstk;
} // end: wins_stage_1


        /*
         * This guy just completes the field group windows after the
         * rcfiles have been read and command line arguments parsed.
         * And since he's the cabose of startup, he'll also tidy up
         * a few final things... */
static void wins_stage_2 (void) 
{
   int i;

   for (i = 0; i < GROUPSMAX; i++) 
   {
      win_names(&Winstk[i], Winstk[i].rc.winname);
      capsmk(&Winstk[i]);
      Winstk[i].findstr = alloc_c(FNDBUFSIZ);
      Winstk[i].findlen = 0;
#ifdef BUILD_4_STOCK
      Winstk[i].user_select_type = Winstk[i].rc.ustype;
      if (strcmp(Winstk[i].rc.filter_prefix, "N"))
      {
         strcpy(Winstk[i].filter_stock_prefix, Winstk[i].rc.filter_prefix);
      }
      else
      {
         strcpy(Winstk[i].filter_stock_prefix, "");
      }
#endif
   }

   if (!Batch)
   {
      putp((Cursor_state = Cap_curs_hide));
   }
   else 
   {
      OFFwinflags(Curwin, View_SCROLL);
      signal(SIGHUP, SIG_IGN);    // allow running under nohup
   }
   // fill in missing Fieldstab members and build each window's columnheader
   zap_fieldstab();

#ifndef NUMA_DISABLE
   /* there's a chance that damn libnuma may spew to stderr so we gotta
      make sure he does not corrupt poor ol' top's first output screen!
      Yes, he provides some overridable 'weak' functions to change such
      behavior but we can't exploit that since we don't follow a normal
      ld route to symbol resolution (we use that dlopen() guy instead)! */
   Stderr_save = dup(fileno(stderr));
   if (-1 < Stderr_save && freopen("/dev/null", "w", stderr))
      ;                           // avoid -Wunused-result
#endif

   // lastly, initialize a signal set used to throttle one troublesome signal
   sigemptyset(&Sigwinch_set);
#ifdef SIGNALS_LESS
   sigaddset(&Sigwinch_set, SIGWINCH);
#endif
} // end: wins_stage_2

/*######  Interactive Input Tertiary support  ############################*/

  /*
   * This section exists so as to offer some function naming freedom
   * while also maintaining the strict alphabetical order protocol
   * within each section. */

        /*
         * This guy is a *Helper* function serving the following two masters:
         *   find_string() - find the next match in a given window
         *   task_show()   - highlight all matches currently in-view
         * If w->findstr is found in the designated buffer, he returns the
         * offset from the start of the buffer, otherwise he returns -1. */
static inline int find_ofs (const WIN_t *w, const char *buf) 
{
   char *fnd;

   if (w->findstr[0] && (fnd = STRSTR(buf, w->findstr)))
      return (int)(fnd - buf);
   return -1;
} // end: find_ofs



   /* This is currently the one true prototype require by top.
      It is placed here, instead of top.h, so as to avoid a compiler
      warning when top_nls.c is compiled. */
static const char *task_show (const WIN_t *w, const proc_t *p);

static void find_string (int ch) 
{
 #define reDUX (found) ? N_txt_Norm_tab(WORD_another_txt) : ""
   static int found;
   int i;

   if ('&' == ch && !Curwin->findstr[0]) 
   {
      show_msg(N_txt_Norm_tab(FIND_no_next_txt));
      return;
   }
   if ('L' == ch) 
   {
      snprintf(Curwin->findstr, FNDBUFSIZ, "%s", ioline(N_txt_Norm_tab(GET_find_str_txt)));
      Curwin->findlen = strlen(Curwin->findstr);
      found = 0;
#ifndef USE_X_COLHDR
      if (Curwin->findstr[0]) SETwinflags(Curwin, NOHIFND_xxx);
      else OFFwinflags(Curwin, NOHIFND_xxx);
#endif
   }
   if (Curwin->findstr[0]) 
   {
      SETwinflags(Curwin, INFINDS_xxx);
      for (i = Curwin->begintask; i < Frame_maxtask; i++) 
      {
         const char *row = task_show(Curwin, Curwin->ppt[i]);
         if (*row && -1 < find_ofs(Curwin, row)) 
         {
            found = 1;
            if (i == Curwin->begintask) continue;
            Curwin->begintask = i;
            return;
         }
      }
      show_msg(fmtmk(N_fmt_Norm_tab(FIND_no_find_fmt), reDUX, Curwin->findstr));
   }
 #undef reDUX
} // end: find_string


static void help_view (void) 
{
   WIN_t *w = Curwin;             // avoid gcc bloat with a local copy
   char key = 1;

   putp((Cursor_state = Cap_curs_huge));
signify_that:
   putp(Cap_clr_scr);
   adj_geometry();

   show_special(1, fmtmk(N_unq_Uniq_tab(KEYS_helpbas_fmt)
      , procps_version
      , w->grpname
      , CHKwinflags(w, Show_CTIMES) ? N_txt_Norm_tab(ON_word_only_txt) : N_txt_Norm_tab(OFF_one_word_txt)
      , Rc.delay_time
      , Secure_mode ? N_txt_Norm_tab(ON_word_only_txt) : N_txt_Norm_tab(OFF_one_word_txt)
      , Secure_mode ? "" : N_unq_Uniq_tab(KEYS_helpext_fmt)));
   putp(Cap_clr_eos);
   fflush(stdout);

   if (Frames_signal) goto signify_that;
   key = iokey(1);
   if (key < 1) goto signify_that;

   switch (key) {
      case kbd_ESC: case 'q':
         break;
      case '?': case 'h': case 'H':
         do 
         {
            putp(Cap_home);
            show_special(1, fmtmk(N_unq_Uniq_tab(WINDOWS_help_fmt)
               , w->grpname
               , Winstk[0].rc.winname, Winstk[1].rc.winname
               , Winstk[2].rc.winname, Winstk[3].rc.winname));
            putp(Cap_clr_eos);
            fflush(stdout);
            if (Frames_signal || (key = iokey(1)) < 1) 
            {
               adj_geometry();
               putp(Cap_clr_scr);
            } 
            else 
            {
               w = win_select(key);
            }
         } while (key != kbd_ENTER && key != kbd_ESC);
         break;
      default:
         goto signify_that;
   }
} // end: help_view


static void other_selection (int ch) 
{
   int (*rel)(const char *, const char *);
   char *(*sel)(const char *, const char *);
   char raw[MEDBUFSIZ], ops, *glob, *pval;
   struct osel_s *osel;
   const char *typ;
   int inc, enu;

   if (ch == 'o') 
   {
      typ   = N_txt_Norm_tab(OSEL_casenot_txt);
      rel   = strcasecmp;
      sel   = strcasestr;
   } 
   else 
   {
      typ   = N_txt_Norm_tab(OSEL_caseyes_txt);
      rel   = strcmp;
      sel   = strstr;
   }
   glob = ioline(fmtmk(N_fmt_Norm_tab(OSEL_prompts_fmt), Curwin->osel_tot + 1, typ));
   if (!snprintf(raw, sizeof(raw), "%s", glob)) return;
   for (osel = Curwin->osel_1st; osel; ) 
   {
      if (!strcmp(osel->raw, glob))            // #1: is criteria duplicate?
      {
         show_msg(N_txt_Norm_tab(OSEL_errdups_txt));
         return;
      }
      osel = osel->nxt;
   }
   if (*glob != '!') inc = 1;                  // #2: is it include/exclude?
   else { ++glob; inc = 0; }
   if (!(pval = strpbrk(glob, "<=>")))         // #3: do we see a delimiter?
   {
      show_msg(fmtmk(N_fmt_Norm_tab(OSEL_errdelm_fmt)
         , inc ? N_txt_Norm_tab(WORD_include_txt) : N_txt_Norm_tab(WORD_exclude_txt)));
      return;
   }
   ops = *(pval);
   *(pval++) = '\0';
   for (enu = 0; enu < P_MAXPFLAGS; enu++)      // #4: is this a valid field?
      if (!STRCMP(N_col_Head_tab(enu), glob)) break;
   if (enu == P_MAXPFLAGS) 
   {
      show_msg(fmtmk(N_fmt_Norm_tab(XTRA_badflds_fmt), glob));
      return;
   }
   if (!(*pval))                               // #5: did we get some value?
   {
      show_msg(fmtmk(N_fmt_Norm_tab(OSEL_errvalu_fmt)
         , inc ? N_txt_Norm_tab(WORD_include_txt) : N_txt_Norm_tab(WORD_exclude_txt)));
      return;
   }
   osel = alloc_c(sizeof(struct osel_s));
   osel->inc = inc;
   osel->enu = enu;
   osel->ops = ops;
   if (ops == '=') osel->val = alloc_s(pval);
   else osel->val = alloc_s(justify_pad(pval, Fieldstab[enu].width, Fieldstab[enu].align));
   osel->rel = rel;
   osel->sel = sel;
   osel->raw = alloc_s(raw);
   osel->nxt = Curwin->osel_1st;
   Curwin->osel_1st = osel;
   Curwin->osel_tot += 1;
   if (!Curwin->osel_prt) Curwin->osel_prt = alloc_c(strlen(raw) + 3);
   else Curwin->osel_prt = alloc_r(Curwin->osel_prt, strlen(Curwin->osel_prt) + strlen(raw) + 6);
   strcat(Curwin->osel_prt, fmtmk("%s'%s'", (Curwin->osel_tot > 1) ? " + " : "", raw));
#ifndef USE_X_COLHDR
   SETwinflags(Curwin, NOHISEL_xxx);
#endif
} // end: other_selection

#ifdef LOG2STDOUT

static void init_log (void) 
{
   if (!(LogPtr = fopen(Log_name, "w"))) 
   {
      show_msg(fmtmk(N_fmt_Norm_tab(FAIL_rc_open_fmt), Log_name, strerror(errno)));
      return;
   }
} // end: init_log

#endif

static void write_rcfile (void) 
{
   FILE *fp;
   int i;

   if (Rc_questions) 
   {
      show_pmt(N_txt_Norm_tab(XTRA_warncfg_txt));
      if ('y' != tolower(iokey(1)))
         return;
      Rc_questions = 0;
   }
   if (!(fp = fopen(Rc_name, "w"))) 
   {
      show_msg(fmtmk(N_fmt_Norm_tab(FAIL_rc_open_fmt), Rc_name, strerror(errno)));
      return;
   }
   fprintf(fp, "%s's " RCF_EYECATCHER, Myname);
   fprintf(fp, "Id:%c, Mode_altscr=%d, Mode_irixps=%d, Delay_time=%d.%d, Curwin=%d\n"
      , RCF_VERSION_ID
      , Rc.mode_altscreen, Rc.mode_irixps
        // this may be ugly, but it keeps us locale independent...
      , (int)Rc.delay_time, (int)((Rc.delay_time - (int)Rc.delay_time) * 1000)
      , (int)(Curwin - Winstk));

   for (i = 0 ; i < GROUPSMAX; i++) 
   {
      fprintf(fp, "%s\tfieldscur=%s\n"
         , Winstk[i].rc.winname, Winstk[i].rc.fieldscur);
      fprintf(fp, "\twinflags=%d, sortindex=%d, maxtasks=%d\n"
         , Winstk[i].rc.winflags, Winstk[i].rc.sortindex
         , Winstk[i].rc.maxtasks);
      fprintf(fp, "\tsummcolor=%d, msgscolor=%d, headcolor=%d, taskcolor=%d\n"
         , Winstk[i].rc.summcolor, Winstk[i].rc.msgscolor
         , Winstk[i].rc.headcolor, Winstk[i].rc.taskcolor);
#ifdef BUILD_4_STOCK
      fprintf(fp, "\tustype=%d, filter_prefix=%s\n"
         , Winstk[i].user_select_type, strlen(Winstk[i].filter_stock_prefix) ? Winstk[i].filter_stock_prefix : "N");
#endif
   }

   // any new addition(s) last, for older rcfiles compatibility...
   fprintf(fp, "Fixed_widest=%d, Summary_unit_scale=%d, Task_unit_scale=%d, Zero_suppress=%d\n"
      , Rc.fixed_widest, Rc.summary_unit_scale, Rc.task_unit_scale, Rc.zero_suppress);

   if (Inspect.raw)
      fputs(Inspect.raw, fp);

   fclose(fp);
   show_msg(fmtmk(N_fmt_Norm_tab(WRITE_rcfile_fmt), Rc_name));
} // end: write_rcfile

/*######  Interactive Input Secondary support (do_key helpers)  ##########*/

  /*
   *  These routines exist just to keep the do_key() function
   *  a reasonably modest size. */

static void keys_global (int ch) 
{
   WIN_t *w = Curwin;             // avoid gcc bloat with a local copy

   switch (ch) {
      case '?':
      case 'h':
         help_view();
         break;
      case 'B':
         TOGGLEwinflags(w, View_NOBOLD);
         capsmk(w);
         break;
      case 'd':
      case 's':
         if (Secure_mode)
         {
            show_msg(N_txt_Norm_tab(NOT_onsecure_txt));
         }
         else 
         {
            float tmp =
               get_float(fmtmk(N_fmt_Norm_tab(DELAY_change_fmt), Rc.delay_time));
            if (-1 < tmp) Rc.delay_time = tmp;
         }
         break;
      case 'E':
         if (++Rc.summary_unit_scale > SK_Eb) Rc.summary_unit_scale = SK_Kb;
         break;
      case 'e':
#ifdef BUILD_4_STOCK
         if (++Rc.task_unit_scale > VE_shou) Rc.task_unit_scale = VE_gu;
         update_header();
         break;
#else
         if (++Rc.task_unit_scale > SK_Pb) Rc.task_unit_scale = SK_Kb;
         break;
#endif
      case 'F':
      case 'f':
         fields_utility();
         break;
      case 'g':
         win_select(0);
         break;
      case 'H':
         Thread_mode = !Thread_mode;
         if (!CHKwinflags(w, View_TASKS))
            show_msg(fmtmk(N_fmt_Norm_tab(THREADS_show_fmt)
               , Thread_mode ? N_txt_Norm_tab(ON_word_only_txt) : N_txt_Norm_tab(OFF_one_word_txt)));
         // force an extra procs refresh to avoid %cpu distortions...
         Pseudo_row = PROC_XTRA;
         break;
      case 'I':
         if (Cpu_faux_tot > 1) 
         {
            Rc.mode_irixps = !Rc.mode_irixps;
            show_msg(fmtmk(N_fmt_Norm_tab(IRIX_curmode_fmt)
               , Rc.mode_irixps ? N_txt_Norm_tab(ON_word_only_txt) : N_txt_Norm_tab(OFF_one_word_txt)));
         } 
         else
         {
            show_msg(N_txt_Norm_tab(NOT_smp_cpus_txt));
         }
         break;
      case 'k':
         if (Secure_mode) 
         {
            show_msg(N_txt_Norm_tab(NOT_onsecure_txt));
         } 
         else 
         {
            int pid, sig = SIGTERM, def = w->ppt[w->begintask]->tid;
            if (GET_INT_BAD < (pid = get_int(fmtmk(N_txt_Norm_tab(GET_pid2kill_fmt), def)))) 
            {
               char *str;
               if (0 > pid) pid = def;
               str = ioline(fmtmk(N_fmt_Norm_tab(GET_sigs_num_fmt), pid, SIGTERM));
               if (*str) sig = signal_name_to_number(str);
               if (Frames_signal) break;
               if (0 < sig && kill(pid, sig))
               {
                  show_msg(fmtmk(N_fmt_Norm_tab(FAIL_signals_fmt)
                     , pid, sig, strerror(errno)));
               }
               else if (0 > sig) show_msg(N_txt_Norm_tab(BAD_signalid_txt));
            }
         }
         break;
      case 'r':
         if (Secure_mode)
         {
            show_msg(N_txt_Norm_tab(NOT_onsecure_txt));
         }
         else 
         {
            int val, pid, def = w->ppt[w->begintask]->tid;
            if (GET_INT_BAD < (pid = get_int(fmtmk(N_txt_Norm_tab(GET_pid2nice_fmt), def)))) 
            {
               if (0 > pid) pid = def;
               if (GET_INTNONE < (val = get_int(fmtmk(N_fmt_Norm_tab(GET_nice_num_fmt), pid))))
               {
                  if (setpriority(PRIO_PROCESS, (unsigned)pid, val))
                  {
                     show_msg(fmtmk(N_fmt_Norm_tab(FAIL_re_nice_fmt)
                        , pid, val, strerror(errno)));
                  }
               }
            }
         }
         break;
      case 'X':
      {  int wide = get_int(fmtmk(N_fmt_Norm_tab(XTRA_fixwide_fmt), Rc.fixed_widest));
         if (GET_INTNONE < wide) 
         {
            if (-1 < wide) Rc.fixed_widest = wide;
            else if (INT_MIN < wide) Rc.fixed_widest = -1;
         }
      }
         break;
      case 'Y':
         if (!Inspect.total)
         {
            ioline(N_txt_Norm_tab(YINSP_noents_txt));
         }
         else 
         {
            int pid, def = w->ppt[w->begintask]->tid;
            if (GET_INT_BAD < (pid = get_int(fmtmk(N_fmt_Norm_tab(YINSP_pidsee_fmt), def)))) 
            {
               if (0 > pid) pid = def;
               if (pid) inspection_utility(pid);
            }
         }
         break;
      case 'Z':
         wins_colors();
         break;
      case '0':
#ifndef BUILD_4_STOCK
         Rc.zero_suppress = !Rc.zero_suppress;
#endif
         break;
      case kbd_ENTER:        // these two have the effect of waking us
      case kbd_SPACE:        // from 'select()', updating hotplugged
         sysinfo_refresh(1); // resources and refreshing the display
         break;
      default:                    // keep gcc happy
         break;
   }
} // end: keys_global


static void keys_summary (int ch) 
{
   WIN_t *w = Curwin;             // avoid gcc bloat with a local copy

   switch (ch) 
   {
#ifndef BUILD_4_STOCK
      case '1':
         if (CHKwinflags(w, View_CPUNOD)) OFFwinflags(w, View_CPUSUM);
         else TOGGLEwinflags(w, View_CPUSUM);
         OFFwinflags(w, View_CPUNOD);
         SETwinflags(w, View_TASKS);
         break;
      case '2':
         if (!Numa_node_tot)
         {
            show_msg(N_txt_Norm_tab(NUMA_nodenot_txt));
         }
         else 
         {
            if (Numa_node_sel < 0) TOGGLEwinflags(w, View_CPUNOD);
            if (!CHKwinflags(w, View_CPUNOD)) SETwinflags(w, View_CPUSUM);
            SETwinflags(w, View_TASKS);
            Numa_node_sel = -1;
         }
         break;
      case '3':
         if (!Numa_node_tot)
         {
            show_msg(N_txt_Norm_tab(NUMA_nodenot_txt));
         }
         else 
         {
            int num = get_int(fmtmk(N_fmt_Norm_tab(NUMA_nodeget_fmt), Numa_node_tot -1));
            if (GET_INTNONE < num) 
            {
               if (num >= 0 && num < Numa_node_tot) 
               {
                  Numa_node_sel = num;
                  SETwinflags(w, View_CPUNOD | View_TASKS);
                  OFFwinflags(w, View_CPUSUM);
               } 
               else
               {
                  show_msg(N_txt_Norm_tab(NUMA_nodebad_txt));
               } 
            }
         }
         break;
#endif
      case 'C':
         VIZTOGGLEwinflags(w, View_SCROLL);
         break;
      case 'l':
         TOGGLEwinflags(w, View_LOADAV);
         break;
      case 'm':
         TOGGLEwinflags(w, View_MEMORY);
         break;
      case 't':
         TOGGLEwinflags(w, View_TASKS);
         break;
      default:                    // keep gcc happy
         break;
   }
} // end: keys_summary

#ifdef BUILD_4_STOCK	
void updateIfilter()
{
   WIN_t *w = Curwin;             // avoid gcc bloat with a local copy
   w->current_index = 0;
   w->begintask = 0;
   w->user_select_type = 0;
   w->max_matched_row = 0; // must initialize to 0 
   strcpy(w->filter_stock_prefix, "");

   if (!CHKwinflags(w, Show_IDLEPS))
   {

      int i = w->begintask;

      w->ups = w->draws = w->downs = 0;
      while (i < Frame_maxtask) 
      {
         if (filter_stock(w, w->ppt[i]))
         {
            w->max_matched_row++;
            w->last_matched_index = i;
            if (w->ppt[i]->current_price > w->ppt[i]->pre_close)
            {
               w->ups++;
            }
            else if (w->ppt[i]->current_price == w->ppt[i]->pre_close)
            {
               w->draws++;
            }
            else
            {
               w->downs++;
            }
         }
         i++;
      }
   }
}

void updateUfilter()
{
   WIN_t *w = Curwin;             // avoid gcc bloat with a local copy
   w->current_index = 0;
   w->begintask = 0;

   if (!CHKwinflags(w, Show_IDLEPS))
   {
      TOGGLEwinflags(w, Show_IDLEPS);
   }

   int i = w->begintask;

   w->max_matched_row = w->ups = w->draws = w->downs = 0;
   w->max_matched_row = 0;

   while (i < Frame_maxtask) 
   {
      if (stock_matched(w, w->ppt[i]))
      {
         w->max_matched_row++;
         w->last_matched_index = i;

         if (w->ppt[i]->current_price > w->ppt[i]->pre_close)
         {
            w->ups++;
         }
         else if (w->ppt[i]->current_price == w->ppt[i]->pre_close)
         {
            w->draws++;
         }
         else
         {
            w->downs++;
         }
      }
      i++;
   }
}

#endif

static void keys_task (int ch) 
{
   WIN_t *w = Curwin;             // avoid gcc bloat with a local copy

   switch (ch) 
   {
      case '#':
      case 'n':
         if (VIZCHKwinflags(w)) 
         {
            int num = get_int(fmtmk(N_fmt_Norm_tab(GET_max_task_fmt), w->rc.maxtasks));
            if (GET_INTNONE < num) 
            {
               if (-1 < num ) w->rc.maxtasks = num;
               else show_msg(N_txt_Norm_tab(BAD_max_task_txt));
            }
         }
         break;
      case '<':
#ifdef TREE_NORESET
         if (CHKwinflags(w, Show_FOREST)) break;
#endif
         if (VIZCHKwinflags(w)) 
         {
            unsigned char *p = w->proc_flags + w->max_displayable_pflags - 1;
            while (p > w->proc_flags && *p != w->rc.sortindex) --p;
            if (*p == w->rc.sortindex) 
            {
               --p;
#ifndef USE_X_COLHDR
               if (P_MAXPFLAGS < *p) --p;
#endif
               if (p >= w->proc_flags) 
               {
                  w->rc.sortindex = *p;
#ifndef TREE_NORESET
                  OFFwinflags(w, Show_FOREST);
#endif
               }
            }
#ifdef BUILD_4_STOCK
            w->begintask = 0;
#endif
         }
         break;
      case '>':
#ifdef TREE_NORESET
         if (CHKwinflags(w, Show_FOREST)) break;
#endif
         if (VIZCHKwinflags(w)) 
         {
            unsigned char *p = w->proc_flags + w->max_displayable_pflags - 1;
            while (p > w->proc_flags && *p != w->rc.sortindex) --p;
            if (*p == w->rc.sortindex) 
            {
               ++p;
#ifndef USE_X_COLHDR
               if (P_MAXPFLAGS < *p) ++p;
#endif
               if (p < w->proc_flags + w->max_displayable_pflags) 
               {
                  w->rc.sortindex = *p;
#ifndef TREE_NORESET
                  OFFwinflags(w, Show_FOREST);
#endif
               }
            }
#ifdef BUILD_4_STOCK
            w->begintask = 0;
#endif
         }
         break;
      case 'b':
         if (VIZCHKwinflags(w)) 
         {
#ifdef USE_X_COLHDR
            if (!CHKwinflags(w, Show_Highlight_ROWS))
#else
            if (!CHKwinflags(w, Show_Highlight_COLS | Show_Highlight_ROWS))
#endif
               show_msg(N_txt_Norm_tab(HILIGHT_cant_txt));
            else 
            {
               TOGGLEwinflags(w, Show_Highlight_BOLD);
               capsmk(w);
            }
         }
         break;
      case 'c':
         VIZTOGGLEwinflags(w, Show_CMDLIN);
         break;
#ifdef BUILD_4_STOCK	
      case '.':
         VIZTOGGLEwinflags(w, STOCK_CURRENT_ROW_HIGHLIGHT);
         break;
#endif
      case 'i':
#ifdef BUILD_4_STOCK	
         VIZTOGGLEwinflags(w, Show_IDLEPS);
         updateIfilter();
#else
         VIZTOGGLEwinflags(w, Show_IDLEPS);
#endif
         break;
      case 'J':
         VIZTOGGLEwinflags(w, Show_JRNUMS);
         break;
      case 'j':
         VIZTOGGLEwinflags(w, Show_JRSTRS);
         break;
      case 'R':
#ifdef TREE_NORESET
         if (!CHKwinflags(w, Show_FOREST)) VIZTOGGLEwinflags(w, Qsrt_NORMAL);
#else
         if (VIZCHKwinflags(w)) 
         {
            TOGGLEwinflags(w, Qsrt_NORMAL);
            OFFwinflags(w, Show_FOREST);
         }
#endif
         break;
      case 'S':
         if (VIZCHKwinflags(w)) 
         {
            TOGGLEwinflags(w, Show_CTIMES);
            show_msg(fmtmk(N_fmt_Norm_tab(TIME_accumed_fmt) , CHKwinflags(w, Show_CTIMES)
               ? N_txt_Norm_tab(ON_word_only_txt) : N_txt_Norm_tab(OFF_one_word_txt)));
         }
         break;
      case 'O':
      case 'o':
         if (VIZCHKwinflags(w)) other_selection(ch);
         break;
      case 'U':
      case 'u':
         if (VIZCHKwinflags(w)) 
         {
            const char *errmsg;
#ifdef BUILD_4_STOCK
            if ((errmsg = stock_certify(w, ioline("stock filter : "), ch)))
            {
               show_msg(errmsg);
               break;
            }
            updateUfilter();
#else
            if ((errmsg = user_certify(w, ioline(N_txt_Norm_tab(GET_user_ids_txt)), ch)))
            {
               show_msg(errmsg);
            }
#endif
         }
         break;
      case 'V':
         if (VIZCHKwinflags(w)) 
         {
            TOGGLEwinflags(w, Show_FOREST);
            if (!ENUviz(w, P_CMD))
               show_msg(fmtmk(N_fmt_Norm_tab(FOREST_modes_fmt) , CHKwinflags(w, Show_FOREST)
                  ? N_txt_Norm_tab(ON_word_only_txt) : N_txt_Norm_tab(OFF_one_word_txt)));
         }
         break;
      case 'x':
         if (VIZCHKwinflags(w)) 
         {
#ifdef USE_X_COLHDR
            TOGGLEwinflags(w, Show_Highlight_COLS);
            capsmk(w);
#else
            if (ENUviz(w, w->rc.sortindex)) 
            {
               TOGGLEwinflags(w, Show_Highlight_COLS);
               if (ENUpos(w, w->rc.sortindex) < w->begin_column_flag) 
               {
                  if (CHKwinflags(w, Show_Highlight_COLS)) w->begin_column_flag += 2;
                  else w->begin_column_flag -= 2;
                  if (0 > w->begin_column_flag) w->begin_column_flag = 0;
               }
               capsmk(w);
            }
#endif
         }
         break;
      case 'y':
         if (VIZCHKwinflags(w)) 
         {
            TOGGLEwinflags(w, Show_Highlight_ROWS);
            capsmk(w);
         }
         break;
      case 'z':
         if (VIZCHKwinflags(w)) 
         {
            TOGGLEwinflags(w, Show_COLORS);
            capsmk(w);
         }
         break;
      case kbd_CtrlO:
         if (VIZCHKwinflags(w))
            ioline(fmtmk(N_fmt_Norm_tab(OSEL_statlin_fmt)
               , w->osel_prt ? w->osel_prt : N_txt_Norm_tab(WORD_noneone_txt)));
         break;
      default:                    // keep gcc happy
         break;
   }
} // end: keys_task


static void keys_window (int ch) 
{
   WIN_t *w = Curwin;             // avoid gcc bloat with a local copy

   switch (ch) 
   {
      case '+':
         if (ALTCHKwinflags) wins_reflag(Flags_OFF, EQUWINS_xxx);
         break;
      case '-':
         if (ALTCHKwinflags) TOGGLEwinflags(w, Show_TASKON);
         break;
      case '=':
         win_reset(w);
         break;
      case '_':
         if (ALTCHKwinflags) wins_reflag(Flags_TOG, Show_TASKON);
         break;
      case '&':
      case 'L':
         if (VIZCHKwinflags(w)) find_string(ch);
         break;
      case 'A':
         Rc.mode_altscreen = !Rc.mode_altscreen;
         break;
      case 'a':
      case 'w':
         if (ALTCHKwinflags) win_select(ch);
         break;
      case 'G':
         if (ALTCHKwinflags) 
         {
            char tmp[SMLBUFSIZ];
            STRLCPY(tmp, ioline(fmtmk(N_fmt_Norm_tab(NAME_windows_fmt), w->rc.winname)));
            if (tmp[0]) win_names(w, tmp);
         }
         break;
      case ctrl_u:
      case kbd_UP:
         if (VIZCHKwinflags(w)) 
         {
#ifdef BUILD_4_STOCK
            if (w->user_select_type || !CHKwinflags(w, Show_IDLEPS))  // filter mode           
            {
               if (0 < w->current_index) 
               {
                  w->current_index -= 1;
               }
               else
               {
                  if (0 < w->begintask) w->begintask -= 1;
                  if (!stock_matched(w, w->ppt[w->begintask]))
                  {
                     int j = w->begintask - 1;
                     while (j >= 0) 
                     {
                        if (stock_matched(w, w->ppt[j]))
                        {
                           w->begintask = j;
                           break;
                        }
                        j--;
                     }
                  }
               }
            }
            else
            {
               if (0 < w->current_index) 
               {
                  w->current_index -= 1;
               }
               else
               {
                  if (0 < w->begintask) w->begintask -= 1;
               }
            }
#else
            {
               if (0 < w->begintask) w->begintask -= 1;
            }
#endif
         }
         break;
      case ctrl_d:
      case kbd_DOWN:
#ifdef BUILD_4_STOCK
         if (VIZCHKwinflags(w) && (w->user_select_type || !CHKwinflags(w, Show_IDLEPS)))
         {
#ifdef LOG2STDOUT
            fprintf(LogPtr, "w->current_index = %d\n", w->current_index);
            fprintf(LogPtr, "w->max_matched_row = %d\n", w->max_matched_row);
#endif
            int j = 0;
            int prev = 0;
            while (j < w->begintask) 
            {
               if (stock_matched(w, w->ppt[j]))
               {
                  prev++;
               }
               ++j;
            }
            if ((w->current_index < (Screen_rows - Msg_row) - 3) 
                && (w->current_index < w->max_matched_row - 1))
            {
               if (w->begintask + w->winlines - 1 < Frame_maxtask - 1) 
               {
                  if (w->current_index < (w->max_matched_row - prev - 1))
                  {
                     w->current_index += 1;
                  }
               }
               else
               {
                  if (w->current_index < Frame_maxtask - w->begintask - 1)
                  {
                     if (w->current_index < (w->max_matched_row - prev - 1))
                     w->current_index += 1;
                  }
                  else
                  {
                     w->current_index = Frame_maxtask - w->begintask - 1;
                  }
               }
            } 
            else
            {
#ifdef LOG2STDOUT
               fprintf(LogPtr, "w->begintask = %d\n", w->begintask);
#endif
               if (w->current_index < (w->max_matched_row - prev - 1))
               {
                  if ((w->begintask < Frame_maxtask - 1)
                      && (w->current_index < w->max_matched_row - 1))
                  {
                     w->begintask += 1;
                     if (w->begintask + w->winlines > Frame_maxtask) 
                     {
                        w->current_index -= 1;
                     }
                  }
                  else if (w->current_index < w->max_matched_row - 1)
                  {
                     if (w->begintask + w->winlines > Frame_maxtask) 
                     {
                        w->current_index -= 1;
                     }
                     if (w->current_index > w->begintask + w->winlines - Frame_maxtask)
                     {
                        w->current_index = w->begintask + w->winlines - Frame_maxtask;
                     }
                  }
               }
            }
            int i = w->begintask;
            while (i < Frame_maxtask) 
            {
               if (stock_matched(w, w->ppt[i]))
               {
#ifdef LOG2STDOUT
                  fprintf(LogPtr, "new begintask = %d\n", i);
                  fflush(LogPtr);
#endif
                  w->begintask = i;                       
                  break;
               }
               ++i;
            }
         }
         else
         {
            if (VIZCHKwinflags(w))
            {
               if ((w->current_index < (Screen_rows - Msg_row) - 3))
               {
                  if (w->begintask + w->winlines - 1 < Frame_maxtask - 1) 
                  {
                     w->current_index += 1;
                  }
                  else
                  {
                     if (w->current_index < Frame_maxtask - w->begintask - 1)
                     {
                        w->current_index += 1;
                     }
                     else
                     {
                        w->current_index = Frame_maxtask - w->begintask - 1;
                     }
                  }
               } 
               else
               {
                  if (w->begintask < Frame_maxtask - 1)
                  {
                     w->begintask += 1;
                     if (w->begintask + w->winlines > Frame_maxtask) 
                     {
                        w->current_index -= 1;
                     }
                  }
                  else
                  {
                     if (w->begintask + w->winlines > Frame_maxtask) 
                     {
                        w->current_index -= 1;
                     }
                     if (w->current_index > w->begintask + w->winlines - Frame_maxtask)
                     {
                        w->current_index = w->begintask + w->winlines - Frame_maxtask;
                     }
                  }
               }
            }
         }
#else
         if (VIZCHKwinflags(w)) if (w->begintask < Frame_maxtask - 1) w->begintask += 1;
#endif
         break;
#ifdef USE_X_COLHDR // ------------------------------------
      case kbd_LEFT:
#ifndef SCROLLVAR_NO
         if (VIZCHKwinflags(w)) 
         {
            if (VARleft(w))
            {
               w->variable_column_begin -= SCROLLAMT;
            }
            else if (0 < w->begin_column_flag)
            {
               w->begin_column_flag -= 1;
            }
         }
#else
         if (VIZCHKwinflags(w)) if (0 < w->begin_column_flag) w->begin_column_flag -= 1;
#endif
         break;
      case kbd_RIGHT:
#ifndef SCROLLVAR_NO
         if (VIZCHKwinflags(w)) 
         {
            
            if (w->max_displayable_pflags == 3) break;

            if (VARright(w)) 
            {
               w->variable_column_begin += SCROLLAMT;
               if (0 > w->variable_column_begin) w->variable_column_begin = 0;
            } 
            else if (w->begin_column_flag + 1 < w->totalpflags)
            {
               w->begin_column_flag += 1;
            }
         }
#else
         if (VIZCHKwinflags(w)) if (w->begin_column_flag + 1 < w->totalpflags) w->begin_column_flag += 1;
#endif
         break;
#else  // USE_X_COLHDR ------------------------------------
      case kbd_LEFT:
#ifndef SCROLLVAR_NO
         if (VIZCHKwinflags(w)) 
         {
            if (VARleft(w))
            {
               w->variable_column_begin -= SCROLLAMT;
            }
            else if (0 < w->begin_column_flag) 
            {
               w->begin_column_flag -= 1;
#ifdef BUILD_4_STOCK
               if (P_MAXPFLAGS < w->pflagsall[w->begin_column_flag + S_StockID + 1]) w->begin_column_flag -= 2;
               if (w->begin_column_flag < 0) w->begin_column_flag = 0;
#else
               if (P_MAXPFLAGS < w->pflagsall[w->begin_column_flag]) w->begin_column_flag -= 2;
#endif
            }
         }
#else
         if (VIZCHKwinflags(w))
         {
            w->begin_column_flag -= 1;
            if (P_MAXPFLAGS < w->pflagsall[w->begin_column_flag]) w->begin_column_flag -= 2;
         }
#endif
         break;
      case kbd_RIGHT:
#ifndef SCROLLVAR_NO
         if (VIZCHKwinflags(w)) 
         {
#ifdef BUILD_4_STOCK
            // make sure that first 3 columns are aways displayed
            //      S_INDEX = 0,
            //      S_StockName,
            //      S_StockID,
            if (w->rc.sortindex <= S_StockID && w->max_displayable_pflags == (S_StockID + 4)) break; 
            if (w->max_displayable_pflags == (S_StockID + 2)) break;
#endif

            if (VARright(w)) 
            {
               w->variable_column_begin += SCROLLAMT;
               if (0 > w->variable_column_begin) w->variable_column_begin = 0;
            } 
            else if (w->begin_column_flag + 1 < w->totalpflags) 
            {
#ifdef BUILD_4_STOCK
               if (P_MAXPFLAGS < w->pflagsall[w->begin_column_flag + S_StockID + 1])
#else
               if (P_MAXPFLAGS < w->pflagsall[w->begin_column_flag])
#endif
               {
                  w->begin_column_flag += (w->begin_column_flag + 3 < w->totalpflags) ? 3 : 0;
               }
               else 
               {
                  w->begin_column_flag += 1;
               }
            }
         }
#else
         if (VIZCHKwinflags(w)) if (w->begin_column_flag + 1 < w->totalpflags) 
         {
            if (P_MAXPFLAGS < w->pflagsall[w->begin_column_flag])
               w->begin_column_flag += (w->begin_column_flag + 3 < w->totalpflags) ? 3 : 0;
            else w->begin_column_flag += 1;
         }
#endif
         break;
#endif // USE_X_COLHDR ------------------------------------
      case ctrl_b:
      case kbd_PGUP:
#ifdef BUILD_4_STOCK
         if (VIZCHKwinflags(w) && (!(w->user_select_type)) && CHKwinflags(w, Show_IDLEPS))
         {
               w->begintask -= (w->winlines - 1);
               if (0 > w->begintask) w->begintask = 0;
         }
         else
         {
               int count = w->winlines - 1;
               int j = w->begintask - 1;
               int old = w->begintask;
               while (count && j >= 0)
               {
                  if (stock_matched(w, w->ppt[j])) 
                     count--;
                  j--;
               }
               w->begintask = j + 1;
               if (0 > w->begintask) w->begintask = 0;
         }
#else
         if (VIZCHKwinflags(w))
         {
               w->begintask -= (w->winlines - 1);
               if (0 > w->begintask) w->begintask = 0;
         }
#endif
         break;
      case ctrl_f:
      case kbd_PGDN:
#ifndef BUILD_4_STOCK
         if (VIZCHKwinflags(w))
         {
            w->begintask += (w->winlines - 1);
            if (w->begintask > Frame_maxtask - 1) w->begintask = Frame_maxtask - 1;
            if (0 > w->begintask) w->begintask = 0;
         }
#else 
         if (!w->user_select_type && CHKwinflags(w, Show_IDLEPS))
         {
            if (VIZCHKwinflags(w)) 
            {
               w->begintask += (w->winlines - 1);
               if (w->begintask > Frame_maxtask - 1) w->begintask = Frame_maxtask - 1;
               if (0 > w->begintask) w->begintask = 0;
            }
            if (w->current_index > Frame_maxtask - w->begintask - 1)
            {
               w->current_index = Frame_maxtask - w->begintask - 1;
            }
         }
         else
         {
            if (VIZCHKwinflags(w))
            {
               if (w->max_matched_row > w->winlines)
               {
                  int count = w->winlines - 2;
                  int j = w->begintask + 1;
                  int old = w->begintask;
                  while (count && j < Frame_maxtask)
                  {
                     if (stock_matched(w, w->ppt[j])) 
                        count--;
                     j++;
                  }
                  w->begintask = j;
                  if (w->begintask > w->last_matched_index) w->begintask = old;
               }
               if (w->begintask > Frame_maxtask - 1) w->begintask = Frame_maxtask - 1;
               if (0 > w->begintask) w->begintask = 0;
            }
            int j = w->begintask + 1;
            int remained = 0;
            while (j < Frame_maxtask)
            {
               if (stock_matched(w, w->ppt[j++]))
               {
                  remained++; 
               }
            }
            if (w->current_index > remained)
            {
               w->current_index = remained;
            }
            if (w->current_index > Frame_maxtask - w->begintask - 1)
            {
               w->current_index = Frame_maxtask - w->begintask - 1;
            }
         }
#endif
         break;
      case kbd_HOME:
#ifndef SCROLLVAR_NO
         if (VIZCHKwinflags(w)) w->begintask = w->begin_column_flag = w->variable_column_begin = 0;
#else
         if (VIZCHKwinflags(w)) w->begintask = w->begin_column_flag = 0;
#endif
#ifdef BUILD_4_STOCK
         w->current_index = 0; 
#endif
         break;
      case ctrl_e:
      case kbd_END:
#ifndef BUILD_4_STOCK
         if (VIZCHKwinflags(w)) 
         {
            w->begintask = (Frame_maxtask - w->winlines) + 1;
            if (0 > w->begintask) w->begintask = 0;
            w->begin_column_flag = w->end_column_flag;
       #ifndef SCROLLVAR_NO
            w->variable_column_begin = 0;
       #endif
         }
#else
         if (!w->user_select_type && CHKwinflags(w, Show_IDLEPS))
         {
            if (VIZCHKwinflags(w)) 
            {
               w->begintask = (Frame_maxtask - w->winlines) + 1;
               if (0 > w->begintask) w->begintask = 0;
       #ifndef SCROLLVAR_NO
               w->variable_column_begin = 0;
       #endif
            }
            w->current_index = Frame_maxtask < w->winlines - 2 ? Frame_maxtask - 1 : w->winlines - 2;
         }
         else
         {
            if (VIZCHKwinflags(w)) 
            {
               int j = w->begintask;
               int remained = 0;
               while (j < Frame_maxtask)
               {
                  if (stock_matched(w, w->ppt[j++]))
                  {
                     remained++; 
                  }
               }
               if (remained <= (w->winlines -1))
               {
                  w->current_index = remained - 1;
                  break;
               }
               int count = w->winlines - 2;
               j =  w->last_matched_index;
               if (w->max_matched_row > (w->winlines - 1))
               {
                  while (count > 0)
                  {
                     if (stock_matched(w, w->ppt[j--]))
                     {
                        count--;
                     }
                     if (j < 0) break;
                  }
                  w->begintask = j++;
                  w->current_index = w->winlines - 2;
               }
               else
               {
                  w->current_index = w->max_matched_row - 1;
               }
       #ifndef SCROLLVAR_NO
               w->variable_column_begin = 0;
       #endif
            }
         }
#endif
         break;
      default:                    // keep gcc happy
         break;
   }
} // end: keys_window


static void keys_xtra (int ch) 
{
// const char *xmsg;
   WIN_t *w = Curwin;             // avoid gcc bloat with a local copy

#ifdef TREE_NORESET
   if (CHKwinflags(w, Show_FOREST)) return;
#else
   OFFwinflags(w, Show_FOREST);
#endif
   /* these keys represent old-top compatibility --
      they're grouped here so that if users could ever be weaned,
      we would just whack do_key's key_tab entry and this function... */
   switch (ch) {
      case 'M':
#ifdef BUILD_4_STOCK
         w->rc.sortindex = S_CURRENT;
#else
         w->rc.sortindex = P_MEM;
#endif
//       xmsg = "Memory";
         break;
#ifdef BUILD_4_STOCK
      case 'V':
         w->rc.sortindex = S_VRATIO;
#else
      case 'N':
         w->rc.sortindex = P_PID;
#endif
//       xmsg = "Numerical";
         break;
      case 'P':
#ifdef BUILD_4_STOCK
         w->rc.sortindex = S_PERCENT;
#else
         w->rc.sortindex = P_CPU;
#endif
//       xmsg = "CPU";
         break;
      case 'T':
#ifdef BUILD_4_STOCK
         w->rc.sortindex = S_TURNOVER_RATE;
#else
         w->rc.sortindex = P_TM2;
#endif
//       xmsg = "Time";
         break;
      default:                    // keep gcc happy
         break;
   }
// some have objected to this message, so we'll just keep silent...
// show_msg(fmtmk("%s sort compatibility key honored", xmsg));
} // end: keys_xtra

/*######  Forest View support  ###########################################*/

        /*
         * We try to keep most existing code unaware of our activities
         * ( plus, maintain alphabetical order with carefully chosen )
         * ( function names: forest_a, forest_b, forest_c & forest_d )
         * ( each with exactly one letter more than its predecessor! ) */
static proc_t **Seed_ppt;                   // temporary window ppt ptr
static proc_t **Tree_ppt;                   // resized by forest_create
static int      Tree_idx;                   // frame_make initializes

        /*
         * This little recursive guy is the real forest view workhorse.
         * He fills in the Tree_ppt array and also sets the child indent
         * level which is stored in an unused proc_t padding byte. */
static void forest_adds (const int self, const int level) 
{
   int i;

   Tree_ppt[Tree_idx] = Seed_ppt[self];     // add this as root or child
   Tree_ppt[Tree_idx++]->pad_3 = level;     // borrow 1 byte, 127 levels
   for (i = self + 1; i < Frame_maxtask; i++) 
   {
      if (Seed_ppt[self]->tid == Seed_ppt[i]->tgid
      || (Seed_ppt[self]->tid == Seed_ppt[i]->ppid && Seed_ppt[i]->tid == Seed_ppt[i]->tgid))
         forest_adds(i, level + 1);         // got one child any others?
   }
} // end: forest_adds


        /*
         * Our qsort callback to order a ppt by the non-display start_time
         * which will make us immune from any pid, ppid or tgid anomalies
         * if/when pid values are wrapped by the kernel! */
static int forest_based (const proc_t **x, const proc_t **y) 
{
   if ( (*x)->start_time > (*y)->start_time ) return  1;
   if ( (*x)->start_time < (*y)->start_time ) return -1;
   return 0;
} // end: forest_based


        /*
         * This routine is responsible for preparing the proc_t's for
         * a forest display in the designated window.  Upon completion,
         * he'll replace the original window ppt with our specially
         * ordered forest version. */
static void forest_create (WIN_t *w) 
{
   static int hwmsav;
   int i;

   Seed_ppt = w->ppt;                       // avoid passing WIN_t ptrs
   if (!Tree_idx)                           // do just once per frame
   {
      if (hwmsav < Frame_maxtask)           // grow, but never shrink
      {
         hwmsav = Frame_maxtask;
         Tree_ppt = alloc_r(Tree_ppt, sizeof(proc_t*) * hwmsav);
      }
      qsort(Seed_ppt, Frame_maxtask, sizeof(proc_t*), (QFunc_Sort_Cb)forest_based);
      for (i = 0; i < Frame_maxtask; i++)   // avoid any hidepid distortions
         if (!Seed_ppt[i]->pad_3)           // identify real or pretend trees
            forest_adds(i, 1);              // add as parent plus its children
   }
   memcpy(Seed_ppt, Tree_ppt, sizeof(proc_t*) * Frame_maxtask);
} // end: forest_create


        /*
         * This guy adds the artwork to either p->cmd or p->cmdline
         * when in forest view mode, otherwise he just returns 'em. */
static inline const char *forest_display (const WIN_t *w, const proc_t *p) 
{
#ifndef SCROLLVAR_NO
   static char buf[1024*64*2]; // the same as readproc's MAX_BUFSZ
#else
   static char buf[ROWMINSIZ];
#endif
   const char *which = (CHKwinflags(w, Show_CMDLIN)) ? *p->cmdline : p->cmd;

   if (!CHKwinflags(w, Show_FOREST) || 1 == p->pad_3) return which;
   snprintf(buf, sizeof(buf), "%*s%s", 4 * (p->pad_3 - 1), " `- ", which);
   return buf;
} // end: forest_display

#ifdef BUILD_4_STOCK
void setup_console(int t)
{
   struct termios our_termios;
   static struct termios old_termios;

   if(t)
   {
     tcgetattr(0, &old_termios);
     memcpy(&our_termios, &old_termios, sizeof(struct termios));
    // our_termios.c_lflag &= !(ECHO | ICANON);
    // tcsetattr(0, TCSANOW, &our_termios);
    //  memcpy(&old_termios, &Tty_original, sizeof(struct termios));
   }
   else
   {
/*
   static char dummy[] = "dumb";
   struct termios tmptty;

#ifdef LOG2STDOUT
   fprintf(LogPtr, "STDOUT_FILENO = %d\n",STDOUT_FILENO);
   fflush(LogPtr);
#endif
   // the curses part...
   if (Batch) 
   {
      setupterm(dummy, STDOUT_FILENO, NULL);
      return;
   }
#ifdef PRETENDNOCAP
   setupterm(dummy, STDOUT_FILENO, NULL);
#else
   setupterm(NULL, STDOUT_FILENO, NULL);
#endif
   // our part...
   if (-1 == tcgetattr(STDIN_FILENO, &Tty_original))
      error_exit(N_txt_Norm_tab(FAIL_tty_get_txt));
   // ok, haven't really changed anything but we do have our snapshot
   Ttychanged = 1;

   // first, a consistent canonical mode for interactive line input
   tmptty = Tty_original;
   tmptty.c_lflag |= (ECHO | ECHOCTL | ECHOE | ICANON | ISIG);
   tmptty.c_lflag &= ~NOFLSH;
   tmptty.c_oflag &= ~TAB3;
   tmptty.c_iflag |= BRKINT;
   tmptty.c_iflag &= ~IGNBRK;
   if (key_backspace && 1 == strlen(key_backspace))
   {
      tmptty.c_cc[VERASE] = *key_backspace;
   }
#ifdef TERMIOS_ONLY
   if (-1 == tcsetattr(STDIN_FILENO, TCSAFLUSH, &tmptty))
      error_exit(fmtmk(N_fmt_Norm_tab(FAIL_tty_set_fmt), strerror(errno)));
   tcgetattr(STDIN_FILENO, &Tty_tweaked);
#endif
   // lastly, a nearly raw mode for unsolicited single keystrokes
   // ECHO
   // 回显输入字符。
   //
   // ECHOCTL
   // (不属于 POSIX) 如果同时设置了 ECHO，除了 TAB, NL, START, 和 STOP 之外的 ASCII控制信号被回显为 ^X, 这里 X 是比控制信号大 0x40 的 ASCII 码。例如，字符 0x08 (BS) 被回显为 ^H。
   //
   // ECHOE
   // 如果同时设置了 ICANON，字符 ERASE 擦除前一个输入字符，WERASE 擦除前一个词。
   //
   // ICANON
   // 启用标准模式 (canonical mode)。允许使用特殊字符 EOF, EOL, EOL2, ERASE, KILL, LNEXT, REPRINT, STATUS, 和 WERASE，以及按行的缓冲。
   tmptty.c_lflag &= ~(ECHO | ECHOCTL | ECHOE | ICANON);
   tmptty.c_cc[VMIN] = 1;
   tmptty.c_cc[VTIME] = 0;
   if (-1 == tcsetattr(STDIN_FILENO, TCSAFLUSH, &tmptty))
   {
      error_exit(fmtmk(N_fmt_Norm_tab(FAIL_tty_set_fmt), strerror(errno)));
   }
   tcgetattr(STDIN_FILENO, &Tty_raw);

#ifndef OFF_STDIOLBF
   // thanks anyway stdio, but we'll manage buffering at the frame level...
   setbuffer(stdout, Stdout_buf, sizeof(Stdout_buf));
#endif
#ifdef OFF_SCROLLBK
   // this has the effect of disabling any troublesome scrollback buffer...
   if (enter_ca_mode) putp(enter_ca_mode);
#endif
   // and don't forget to ask iokey to initialize his tinfo_tab
   iokey(0);
*/
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_termios);
   iokey(0);
   }
}
#endif

        /*
         * Process keyboard input during the main loop */
static void do_key (int ch) 
{
   static struct 
   {
      void (*func)(int ch);
      char keys[SMLBUFSIZ];
   } key_tab[] = {
      { keys_global,
         { '?', 'B', 'd', 'E', 'e', 'F', 'f', 'g', 'H', 'h'
         , 'I', 'k', 'r', 's', 'X', 'Y', 'Z', '0'
         , kbd_ENTER, kbd_SPACE, '\0' } },
      { keys_summary,
         { '1', '2', '3', 'C', 'l', 'm', 't', '\0' } },
      { keys_task,
#ifdef BUILD_4_STOCK
         { '#', '<', '>', 'b', 'c', 'i', 'J', 'j', 'n', 'O', 'o'
         , 'R', 'S', 'U', 'u', 'x', 'y', 'z'
         , kbd_CtrlO, '.', '\0' } },
#else
         { '#', '<', '>', 'b', 'c', 'i', 'J', 'j', 'n', 'O', 'o'
         , 'R', 'S', 'U', 'u', 'V', 'x', 'y', 'z'
         , kbd_CtrlO, '.', '\0' } },
#endif
      { keys_window,
         { '+', '-', '=', '_', '&', 'A', 'a', 'G', 'L', 'w'
         , kbd_UP, kbd_DOWN, kbd_LEFT, kbd_RIGHT, kbd_PGUP, kbd_PGDN
         , kbd_HOME, kbd_END, ctrl_e, ctrl_f, ctrl_b, ctrl_u, ctrl_d, '\0' } },
#ifdef BUILD_4_STOCK
      { keys_xtra,
         { 'M', 'V', 'P', 'T', '\0'} }
#else
      { keys_xtra,
         { 'M', 'N', 'P', 'T', '\0'} }
#endif
   };
   int i;

   switch (ch) {
#ifdef BUILD_4_STOCK
      case 'w':                // ignored (always)
         setup_console(1);
         //testsdl();
         //startwin();
         setup_console(0);
         //whack_terminal();                    //                 > onions etc. <
         goto all_done;
#endif
      case 0:                // ignored (always)
         goto all_done;
      case kbd_ESC:          // ignored (sometimes)
         goto all_done;
      case 'q':              // no return from this guy
         bye_bye(NULL);
      case 'W':              // no need for rebuilds
         write_rcfile();
         goto all_done;
      default:               // and now, the real work...
         for (i = 0; i < MAXTBL(key_tab); ++i)
         {
            if (strchr(key_tab[i].keys, ch)) 
            {
               key_tab[i].func(ch);
               Frames_signal = BREAK_keyboard;
               goto all_done;
            }
         }
   };
   /* Frames_signal above will force a rebuild of all column headers and
      the PROC_FILLxxx flags.  It's NOT simply lazy programming.  Here are
      some keys that COULD require new column headers and/or libproc flags:
         'A' - likely
         'c' - likely when !Mode_altscr, maybe when Mode_altscr
         'F' - likely
         'f' - likely
         'g' - likely
         'H' - likely
         'I' - likely
         'J' - always
         'j' - always
         'Z' - likely, if 'Curwin' changed when !Mode_altscr
         '-' - likely (restricted to Mode_altscr)
         '_' - likely (restricted to Mode_altscr)
         '=' - maybe, but only when Mode_altscr
         '+' - likely (restricted to Mode_altscr)
         PLUS, likely for FOUR of the EIGHT cursor motion keys (scrolled)
      ( At this point we have a human being involved and so have all the time )
      ( in the world.  We can afford a few extra cpu cycles every now & then! )
    */

   show_msg(N_txt_Norm_tab(UNKNOWN_cmds_txt));
all_done:
   putp((Cursor_state = Cap_curs_hide));
} // end: do_key


        /*
         * State display *Helper* function to calc and display the state
         * percentages for a single cpu.  In this way, we can support
         * the following environments without the usual code bloat.
         *    1) single cpu machines
         *    2) modest smp boxes with room for each cpu's percentages
         *    3) massive smp guys leaving little or no room for process
         *       display and thus requiring the cpu summary toggle */
static void summary_hlp (CPU_t *cpu, const char *pfx) 
{
   /* we'll trim to zero if we get negative time ticks,
      which has happened with some SMP kernels (pre-2.4?)
      and when cpus are dynamically added or removed */
 #define TRIMz(x)  ((tz = (SIC_t)(x)) < 0 ? 0 : tz)
   SIC_t u_frme, s_frme, n_frme, i_frme, w_frme, x_frme, y_frme, z_frme, tot_frme, tz;
   float scale;

   u_frme = TRIMz(cpu->cur.u - cpu->sav.u);
   s_frme = TRIMz(cpu->cur.s - cpu->sav.s);
   n_frme = TRIMz(cpu->cur.n - cpu->sav.n);
   i_frme = TRIMz(cpu->cur.i - cpu->sav.i);
   w_frme = TRIMz(cpu->cur.w - cpu->sav.w);
   x_frme = TRIMz(cpu->cur.x - cpu->sav.x);
   y_frme = TRIMz(cpu->cur.y - cpu->sav.y);
   z_frme = TRIMz(cpu->cur.z - cpu->sav.z);
   tot_frme = u_frme + s_frme + n_frme + i_frme + w_frme + x_frme + y_frme + z_frme;
#ifdef CPU_ZEROTICS
   if (1 > tot_frme) tot_frme = 1;
#else
   if (tot_frme < cpu->edge)
      tot_frme = u_frme = s_frme = n_frme = i_frme = w_frme = x_frme = y_frme = z_frme = 0;
   if (1 > tot_frme) i_frme = tot_frme = 1;
#endif
   scale = 100.0 / (float)tot_frme;

   /* display some kinda' cpu state percentages
      (who or what is explained by the passed prefix) */
   show_special(0, fmtmk(Cpu_States_fmts, pfx
      , (float)u_frme * scale, (float)s_frme * scale
      , (float)n_frme * scale, (float)i_frme * scale
      , (float)w_frme * scale, (float)x_frme * scale
      , (float)y_frme * scale, (float)z_frme * scale));
 #undef TRIMz
} // end: summary_hlp


        /*
         * In support of a new frame:
         *    1) Display uptime and load average (maybe)
         *    2) Display task/cpu states (maybe)
         *    3) Display memory & swap usage (maybe) */
static void summary_show (void) 
{
 #define isROOM(f,n) (CHKwinflags(w, f) && Msg_row + (n) < Screen_rows - 1)
 #define anyFLG 0xffffff
   static CPU_t *smpcpu = NULL;
   WIN_t *w = Curwin;             // avoid gcc bloat with a local copy
   char tmp[MEDBUFSIZ];
   int i;

   // Display Uptime and Loadavg
   if (isROOM(View_LOADAV, 1)) 
   {
      if (!Rc.mode_altscreen)
      {
#ifdef BUILD_4_STOCK
         show_special(0, fmtmk(CHKwinflags(w, Show_TASKON)? LOADAV_line_alt : LOADAV_line
            , w->grpname, sprint_uptime(0)));
#else
         show_special(0, fmtmk(LOADAV_line, Myname, sprint_uptime(0)));
#endif
      }
      else
      {
         show_special(0, fmtmk(CHKwinflags(w, Show_TASKON)? LOADAV_line_alt : LOADAV_line
            , w->grpname, sprint_uptime(0)));
      }
      Msg_row += 1;
   } // end: View_LOADAV

   // Display Task and Cpu(s) States
   if (isROOM(View_TASKS, 2)) 
   {
#ifdef BUILD_4_STOCK
      if (CHKwinflags(w, Show_IDLEPS) && !(w->user_select_type))
      {
         show_special(0, fmtmk(_("%s:~3 %3u ~2total,~3 %3u ~2ups,~3 %3u ~2draws,~3 %3u ~2downs~3\n"),"Stock" , Frame_maxtask, stock_ups, stock_draws, stock_downs));
      }
      else if (!CHKwinflags(w, Show_IDLEPS))
      {
         show_special(0, fmtmk(_("%s:~3 %3u ~2total,~3 %3u ~2ups,~3 %3u ~2draws,~3 %3u ~2downs,~3 %s\n"),"Stock" , w->max_matched_row, w->ups, w->draws, w->downs, CHKwinflags(w, Show_IDLEPS)?"ALL":"iFiltered"));
      }
      else if (w->user_select_type)
      {
         show_special(0, fmtmk(_("%s:~3 %3u ~2total,~3 %3u ~2ups,~3 %3u ~2draws,~3 %3u ~2downs,~3 \"%s\" ~2search prefix~3\n"),"Stock" , w->max_matched_row, w->ups, w->draws, w->downs, w->filter_stock_prefix));
      }
#else
      show_special(0, fmtmk(N_unq_Uniq_tab(STATE_line_1_fmt)
         , Thread_mode ? N_txt_Norm_tab(WORD_threads_txt) : N_txt_Norm_tab(WORD_process_txt)
         , Frame_maxtask, Frame_running, Frame_sleepin
         , Frame_stopped, Frame_zombied));
#endif
      Msg_row += 1;

#ifndef BUILD_4_STOCK
      smpcpu = cpus_refresh(smpcpu);

#ifndef NUMA_DISABLE
      if (!Numa_node_tot) goto numa_nope;

      if (CHKwinflags(w, View_CPUNOD)) 
      {
         if (Numa_node_sel < 0) 
         {
            // display the 1st /proc/stat line, then the nodes (if room)
            summary_hlp(&smpcpu[smp_num_cpus], N_txt_Norm_tab(WORD_allcpus_txt));
            Msg_row += 1;
            // display each cpu node's states
            for (i = 0; i < Numa_node_tot; i++) 
            {
               if (!isROOM(anyFLG, 1)) break;
               snprintf(tmp, sizeof(tmp), N_fmt_Norm_tab(NUMA_nodenam_fmt), i);
               summary_hlp(&smpcpu[1 + smp_num_cpus + i], tmp);
               Msg_row += 1;
            }
         } 
         else 
         {
            // display the node summary, then the associated cpus (if room)
            snprintf(tmp, sizeof(tmp), N_fmt_Norm_tab(NUMA_nodenam_fmt), Numa_node_sel);
            summary_hlp(&smpcpu[1 + smp_num_cpus + Numa_node_sel], tmp);
            Msg_row += 1;
            for (i = 0; i < Cpu_faux_tot; i++) 
            {
               if (Numa_node_sel == smpcpu[i].node) 
               {
                  if (!isROOM(anyFLG, 1)) break;
                  snprintf(tmp, sizeof(tmp), N_fmt_Norm_tab(WORD_eachcpu_fmt), smpcpu[i].id);
                  summary_hlp(&smpcpu[i], tmp);
                  Msg_row += 1;
               }
            }
         }
      } else
numa_nope:
#endif
      if (CHKwinflags(w, View_CPUSUM)) 
      {
         // display just the 1st /proc/stat line
         summary_hlp(&smpcpu[Cpu_faux_tot], N_txt_Norm_tab(WORD_allcpus_txt));
         Msg_row += 1;

      } 
      else 
      {
         // display each cpu's states separately, screen height permitting...
         for (i = 0; i < Cpu_faux_tot; i++) 
         {
            snprintf(tmp, sizeof(tmp), N_fmt_Norm_tab(WORD_eachcpu_fmt), smpcpu[i].id);
            summary_hlp(&smpcpu[i], tmp);
            Msg_row += 1;
            if (!isROOM(anyFLG, 1)) break;
         }
      }
#endif
   } // end: View_TASKS

#ifndef BUILD_4_STOCK
   // Display Memory and Swap stats
   if (isROOM(View_MEMORY, 2)) 
   {
    #define bfT(n)  buftab[n].buf
    #define scT(e)  scaletab[Rc.summary_unit_scale]. e
    #define mkM(x) (float)kb_main_ ## x / scT(div)
    #define mkS(x) (float)kb_swap_ ## x / scT(div)
    #define prT(b,z) { if (9 < snprintf(b, 10, scT(fmts), z)) b[8] = '+'; }
      static struct {
         float div;
         const char *fmts;
         const char *label;
      } scaletab[] = {
         { 1, "%8.0f ", NULL },                            // kibibytes
         { 1024.0, "%#4.3f ", NULL },                      // mebibytes
         { 1024.0*1024, "%#4.3f ", NULL },                 // gibibytes
         { 1024.0*1024*1024, "%#4.3f ", NULL },            // tebibytes
         { 1024.0*1024*1024*1024, "%#4.3f ", NULL },       // pebibytes
         { 1024.0*1024*1024*1024*1024, "%#4.3f ", NULL }   // exbibytes
      };
      struct { //                                            0123456789
      // snprintf contents of each buf (after SK_Kb):       'nnnn.nnn 0'
      // and prT macro might replace space at buf[8] with:   ------> +
         char buf[10]; // MEMORY_lines_fmt provides for 8+1 bytes
      } buftab[8];

      if (!scaletab[0].label) 
      {
         scaletab[0].label = N_txt_Norm_tab(AMT_kilobyte_txt);
         scaletab[1].label = N_txt_Norm_tab(AMT_megabyte_txt);
         scaletab[2].label = N_txt_Norm_tab(AMT_gigabyte_txt);
         scaletab[3].label = N_txt_Norm_tab(AMT_terabyte_txt);
         scaletab[4].label = N_txt_Norm_tab(AMT_petabyte_txt);
         scaletab[5].label = N_txt_Norm_tab(AMT_exxabyte_txt);
      }
      prT(bfT(0), mkM(total)); prT(bfT(1), mkM(used));
      prT(bfT(2), mkM(free));  prT(bfT(3), mkM(buffers));
      prT(bfT(4), mkS(total)); prT(bfT(5), mkS(used));
      prT(bfT(6), mkS(free));  prT(bfT(7), mkM(cached));

      show_special(0, fmtmk(N_unq_Uniq_tab(MEMORY_lines_fmt)
         , scT(label), bfT(0), bfT(1), bfT(2), bfT(3)
         , scT(label), bfT(4), bfT(5), bfT(6), bfT(7)));
      Msg_row += 2;
    #undef bfT
    #undef scT
    #undef mkM
    #undef mkS
    #undef prT
   } // end: View_MEMORY
#endif

 #undef isROOM
 #undef anyFLG
} // end: summary_show

#ifdef BUILD_4_STOCK
        /*
         * Build the information for a single task row and
         * display the results or return them to the caller. */
static const char *stock_show (int index, int current, const WIN_t *w, const proc_t *p) 
{
#ifndef SCROLLVAR_NO
 #define makeVAR(v)  { const char *pv = v; \
    if (!w->variable_column_begin) cp = make_string(pv, w->variable_column_size, Justify_String_Right, AUTOX_NO); \
    else cp = make_string(w->variable_column_begin < (int)strlen(pv) ? pv + w->variable_column_begin : "", w->variable_column_size, Justify_String_Right, AUTOX_NO); }
#else
 #define makeVAR(v) cp = make_string(v, w->variable_column_size, Justify_String_Right, AUTOX_NO)
#endif
 #define pages2K(n)  (unsigned long)( (n) << Pg2K_shft )
   static char rbuf[ROWMINSIZ];
   static char stock[7];
   static char tmpstock[7];
   char *rp;
   int x;

   // we must begin a row with a possible window number in mind...
   *(rp = rbuf) = '\0';
   if (Rc.mode_altscreen) rp = scat(rp, " ");

   for (x = 0; x < w->max_displayable_pflags; x++) 
   {
      const char *cp;
      unsigned char i = w->proc_flags[x];
   #define SCALE                 Fieldstab[i].scale// these used to be variables
   #define FieldWidth            Fieldstab[i].width// but it's much better if we
   #define Justify_String_Right  CHKwinflags(w, Show_JRSTRS)// represent them as #defines
   #define Justify_Num_Right     CHKwinflags(w, Show_JRNUMS)// and only exec code if used

      switch (i) 
      {
#ifndef USE_X_COLHDR
         // these 2 aren't real proc_flags, they're used in column highlighting!
         case X_XON:
         case X_XOFF:
            cp = NULL;
            if (!CHKwinflags(w, INFINDS_xxx | NOHIFND_xxx | NOHISEL_xxx)) 
            {
               /* treat running tasks specially - entire row may get highlighted
                  so we needn't turn it on and we MUST NOT turn it off */
               if (!('R' == p->state && CHKwinflags(w, Show_Highlight_ROWS)))
               {
                  cp = (X_XON == i ? w->capclr_rowhigh : w->capclr_rownorm);
                 // cp = (X_XON == i ? Caps_green : w->capclr_rownorm);
               }
             
#ifdef BUILD_4_STOCK
               if ((current - 1 == w->current_index))// && X_XOFF == i)
               {
                  if (CHKwinflags(Curwin, STOCK_CURRENT_ROW_HIGHLIGHT))
                     cp = Caps_yellow; 
               }
#endif
            }
            break;
#endif
         case P_CGR:
            makeVAR(p->cgroup[0]);
            break;
         case P_CMD:
            makeVAR(forest_display(w, p));
            break;
         case P_COD:
            cp = scale_mem(SCALE, pages2K(p->trs), FieldWidth, Justify_Num_Right);
            break;
         case P_CPN:
            cp = make_number(p->processor, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
         case P_DAT:
            cp = scale_mem(SCALE, pages2K(p->drs), FieldWidth, Justify_Num_Right);
            break;
         case P_DRT:
            cp = scale_num(p->dt, FieldWidth, Justify_Num_Right);
            break;
         case P_ENV:
            makeVAR(p->environ[0]);
            break;
         case P_FL1:
            cp = scale_num(p->maj_flt, FieldWidth, Justify_Num_Right);
            break;
         case P_FL2:
            cp = scale_num(p->min_flt, FieldWidth, Justify_Num_Right);
            break;
         case P_FLG:
            cp = make_string(hex_make(p->flags, 1), FieldWidth, Justify_String_Right, AUTOX_NO);
            break;
         case P_FV1:
            cp = scale_num(p->maj_delta, FieldWidth, Justify_Num_Right);
            break;
         case P_FV2:
            cp = scale_num(p->min_delta, FieldWidth, Justify_Num_Right);
            break;
         case P_GID:
            cp = make_number(p->egid, FieldWidth, Justify_Num_Right, P_GID);
            break;
         case P_GRP:
            cp = make_string(p->egroup, FieldWidth, Justify_String_Right, P_GRP);
            break;
         case P_MEM:
            cp = scale_percent((float)pages2K(p->resident) * 100 / kb_main_total, FieldWidth, Justify_Num_Right);
            break;
         case P_CPU:
         {  float u = (float)p->pcpu * Frame_etscale;
            /* process can't use more %cpu than number of threads it has
             ( thanks Jaromir Capik <jcapik@redhat.com> ) */
            if (u > 100.0 * p->nlwp) u = 100.0 * p->nlwp;
            if (u > Cpu_pmax) u = Cpu_pmax;
            cp = scale_percent(u, FieldWidth, Justify_Num_Right);
         }
            break;
         case P_NCE:
            cp = make_number(p->nice, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
         case P_NS1:   // IPCNS
         case P_NS2:   // MNTNS
         case P_NS3:   // NETNS
         case P_NS4:   // PIDNS
         case P_NS5:   // USERNS
         case P_NS6:   // UTSNS
         {  long ino = p->ns[i - P_NS1];
            if (ino > 0) cp = make_number(ino, FieldWidth, Justify_Num_Right, i);
            else cp = make_string("-", FieldWidth, Justify_String_Right, i);
         }
            break;
#ifdef OOMEM_ENABLE
         case P_OOA:
            cp = make_number(p->oom_adj, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
         case P_OOM:
            cp = make_number(p->oom_score, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
#endif
         case P_PGD:
            cp = make_number(p->pgrp, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
#ifdef BUILD_4_STOCK
         case S_DAY:
            cp = make_day(p->year, p->month, p->day, FieldWidth, Justify_Num_Right, S_DAY);
            break;
         case S_TIME:
            cp = make_time(p->hour, p->minute, p->second, FieldWidth, Justify_Num_Right, S_TIME);
            break;
         case S_INDEX:
            cp = make_number(index, FieldWidth, Justify_Num_Right, S_INDEX);
            break;
         case S_StockID:
            //cp = make_stockid(p->stockid, FieldWidth, Justify_Num_Right, S_StockID);
            cp = make_string(p->str_stockid, FieldWidth, Justify_Num_Right, S_StockID);
            break;
         case S_StockName:
            cp = make_utf8_string(p->stockname, FieldWidth, Justify_String_Right, S_StockName);
            break;
         case S_OPEN:
            cp = float_scale_percent((float)p->open_today/FACTOR, FieldWidth, Justify_Num_Right);
            break;
         case S_CLOSE_YESTODAY:
            cp = float_scale_percent((float)p->pre_close/FACTOR, FieldWidth, Justify_Num_Right);
            break;
         case S_MIN:
            cp = float_scale_percent((float)p->min/FACTOR, FieldWidth, Justify_Num_Right);
            break;
         case S_MAX:
            cp = float_scale_percent((float)p->max/FACTOR, FieldWidth, Justify_Num_Right);
            break;
         case S_AVERAGE_PRICE:
            cp = float_scale_percent((float)p->average_price/FACTOR, FieldWidth, Justify_Num_Right);
            break;
         case S_SWEIGHT:
            cp = float_scale_percent((float)p->sweight, FieldWidth, Justify_Num_Right);
            break;
         case S_DOWNSHADOW:
            cp = float_scale_percent(((float)p->downshadow)/100, FieldWidth, Justify_Num_Right);
            break;
         case S_MA5:
            cp = float_scale_percent((float)p->ma5/FACTOR, FieldWidth, Justify_Num_Right);
            break;
         case S_MA10:
            cp = float_scale_percent((float)p->ma10/FACTOR, FieldWidth, Justify_Num_Right);
            break;
         case S_MA20:
            cp = float_scale_percent((float)p->ma20/FACTOR, FieldWidth, Justify_Num_Right);
            break;
         case S_MA30:
            cp = float_scale_percent((float)p->ma30/FACTOR, FieldWidth, Justify_Num_Right);
            break;
         case S_MA60:
            cp = float_scale_percent((float)p->ma60/FACTOR, FieldWidth, Justify_Num_Right);
            break;
         case S_VRATIO:
            cp = float_scale_percent((float)p->volume_ratio/1000, FieldWidth, Justify_Num_Right);
            break;
         case S_SWING:
            cp = percent_float_colored_string((float)p->swing/1000, FieldWidth, Justify_Num_Right, 0);
            break;
         case S_OUTER:
            cp = make_number(p->outer, FieldWidth, Justify_Num_Right, S_OUTER);
            break;
         case S_INNER:
            cp = make_number(p->inner, FieldWidth, Justify_Num_Right, S_INNER);
            break;
         case S_PE_RATIO:
            cp = make_float2((float)p->pe_ratio/1000, FieldWidth, Justify_Num_Right, S_PE_RATIO);
            break;
         case S_CURRENT:
            if ((current - 1 == w->current_index) && CHKwinflags(Curwin, STOCK_CURRENT_ROW_HIGHLIGHT))
            {
               cp = float_colored_scale_percent((float)p->current_price/FACTOR, (float)p->pre_close/FACTOR, FieldWidth, Justify_Num_Right, 2);
            }
            else
            {
               cp = float_colored_scale_percent((float)p->current_price/FACTOR, (float)p->pre_close/FACTOR, FieldWidth, Justify_Num_Right, 1);
            }
            break;
         case S_VOLUME:
            cp = make_volume_number(SCALE, p->volume, FieldWidth, Justify_Num_Right, S_VOLUME);
            break;
         case S_LIUTONGA:
            cp = make_float(p->liutongA, FieldWidth, Justify_Num_Right, S_LIUTONGA);
            break;
         case S_TOTALS:
            cp = make_float(p->totals, FieldWidth, Justify_Num_Right, S_TOTALS);
            break;
         case S_VALUE_LIUTONGA:
            cp = make_float(p->value_liutongA, FieldWidth, Justify_Num_Right, S_VALUE_LIUTONGA);
            break;
         case S_VALUE_TOTALS:
            cp = make_float(p->value_totals, FieldWidth, Justify_Num_Right, S_VALUE_TOTALS);
            break;
         case S_RMB:
            cp = make_rmb_number(SCALE, p->RMB, FieldWidth, Justify_Num_Right, S_RMB);
            break;
         case S_PERCENT:
         {  
            if ((current - 1 == w->current_index) && CHKwinflags(Curwin, STOCK_CURRENT_ROW_HIGHLIGHT))
            {
               cp = percent_float_colored_string((float)p->up_percent/1000, FieldWidth, Justify_Num_Right, 2);
            }
            else
            {
               cp = percent_float_colored_string((float)p->up_percent/1000, FieldWidth, Justify_Num_Right, 1);
            }
         }
            break;
         case S_TURNOVER_RATE:
         {  
            cp = percent_float_colored_string((float)p->turn_over_rate/1000, FieldWidth, Justify_Num_Right, 0);
         }
            break;
         case S_DDR:
         {  
            cp = percent_float_colored_string((float)p->ddr/1000, FieldWidth, Justify_Num_Right, 0);
         }
            break;
         case S_MNR:
         {  
            cp = percent_float_colored_string((float)p->mnr/100, FieldWidth, Justify_Num_Right, 0);
         }
            break;
#endif
         case P_PID:
            cp = make_number(p->tid, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
         case P_PPD:
            cp = make_number(p->ppid, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
         case P_PRI:
            if (-99 > p->priority || 999 < p->priority) 
            {
               cp = make_string("rt", FieldWidth, Justify_Num_Right, AUTOX_NO);
            } 
            else
            {
               cp = make_number(p->priority, FieldWidth, Justify_Num_Right, AUTOX_NO);
            }
            break;
         case P_RES:
            cp = scale_mem(SCALE, pages2K(p->resident), FieldWidth, Justify_Num_Right);
            break;
         case P_SGD:
            makeVAR(p->supgid);
            break;
         case P_SGN:
            makeVAR(p->supgrp);
            break;
         case P_SHR:
            cp = scale_mem(SCALE, pages2K(p->share), FieldWidth, Justify_Num_Right);
            break;
         case P_SID:
            cp = make_number(p->session, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
         case P_STA:
            cp = make_char(p->state, FieldWidth, Justify_String_Right);
            break;
         case P_SWP:
            cp = scale_mem(SCALE, p->vm_swap, FieldWidth, Justify_Num_Right);
            break;
         case P_TGD:
            cp = make_number(p->tgid, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
         case P_THD:
            cp = make_number(p->nlwp, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
         case P_TM2:
         case P_TME:
         {  TIC_t t = p->utime + p->stime;
            if (CHKwinflags(w, Show_CTIMES)) t += (p->cutime + p->cstime);
            cp = scale_tics(t, FieldWidth, Justify_Num_Right);
         }
            break;
         case P_TPG:
            cp = make_number(p->tpgid, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
         case P_TTY:
         {  char tmp[SMLBUFSIZ];
            dev_to_tty(tmp, FieldWidth, p->tty, p->tid, ABBREV_DEV);
            cp = make_string(tmp, FieldWidth, Justify_String_Right, P_TTY);
         }
            break;
         case P_UED:
            cp = make_number(p->euid, FieldWidth, Justify_Num_Right, P_UED);
            break;
         case P_UEN:
            cp = make_string(p->euser, FieldWidth, Justify_String_Right, P_UEN);
            break;
         case P_URD:
            cp = make_number(p->ruid, FieldWidth, Justify_Num_Right, P_URD);
            break;
         case P_URN:
            cp = make_string(p->ruser, FieldWidth, Justify_String_Right, P_URN);
            break;
         case P_USD:
            cp = make_number(p->suid, FieldWidth, Justify_Num_Right, P_USD);
            break;
         case P_USE:
            cp = scale_mem(SCALE, (p->vm_swap + pages2K(p->resident)), FieldWidth, Justify_Num_Right);
            break;
         case P_USN:
            cp = make_string(p->suser, FieldWidth, Justify_String_Right, P_USN);
            break;
         case P_VRT:
            cp = scale_mem(SCALE, pages2K(p->size), FieldWidth, Justify_Num_Right);
            break;
         case P_WCH:
         {  const char *u;
            if (No_ksyms)
               u = hex_make(p->wchan, 0);
            else
               u = lookup_wchan(p->wchan, p->tid);
            cp = make_string(u, FieldWidth, Justify_String_Right, P_WCH);
         }
            break;
         default:                 // keep gcc happy
            continue;

      } // end: switch 'procflag'

      if (cp) 
      {
         if (w->osel_tot && !osel_matched(w, i, cp)) return "";
         rp = scat(rp, cp);
#ifdef BUILD_4_STOCK
         if ((i == S_StockID) && (w->rc.sortindex == S_StockID))  
         {
            rp = scat(rp, " ");
         }
         if ((i == S_PERCENT || i == S_CURRENT) && (current - 1 == w->current_index))
         {
            if (CHKwinflags(Curwin, STOCK_CURRENT_ROW_HIGHLIGHT))
               rp = scat(rp, Caps_yellow); 
         }
#endif
      }
      #undef SCALE
      #undef FieldWidth
      #undef Justify_String_Right
      #undef Justify_Num_Right
   } // end: for 'max_displayable_pflags'

   if (!CHKwinflags(w, INFINDS_xxx)) 
   {
      // highlight the row if it's state is running
      const char *cap = ((CHKwinflags(w, Show_Highlight_ROWS) && 'R' == p->state))
         ? w->capclr_rowhigh : w->capclr_rownorm;
#ifdef BUILD_4_STOCK
      if ( current - 1 == w->current_index )
      {
         if (CHKwinflags(Curwin, STOCK_CURRENT_ROW_HIGHLIGHT))
            cap = Caps_yellow; 
      }
#endif
      char *row = rbuf;
      int ofs;
      /* since we can't predict what the search string will be and,
         considering what a single space search request would do to
         potential buffer needs, when any matches are found we skip
         normal output routing and send all of the results directly
         to the terminal (and we sound asthmatic: poof, putt, puff) */
      if (-1 < (ofs = find_ofs(w, row))) 
      {
         POOF("\n", cap);
         do {
            row[ofs] = '\0';
            PUTT("%s%s%s%s", row, w->capclr_hdr, w->findstr, cap);
            row += (ofs + w->findlen);
            ofs = find_ofs(w, row);
         } while (-1 < ofs);
         PUTT("%s%s", row, Caps_endline);
         // with a corrupted rbuf, ensure row is 'counted' by window_show
         rbuf[0] = '!';
      } 
      else
      {
#if defined(LOG2STDOUT) && defined(BUILD_4_STOCK)
         fprintf(LogPtr, "processing stock row = %s\n", row);
         fflush(LogPtr);
#endif
//         printf("\n%s%s%s", cap, row, Caps_endline);
         PUFF("\n%s%s%s", cap, row, Caps_endline);
      }
   }
   return rbuf;
 #undef makeVAR
 #undef pages2K
} // end: stock_show

#endif

        /*
         * Build the information for a single task row and
         * display the results or return them to the caller. */
static const char *task_show (const WIN_t *w, const proc_t *p) 
{
#ifndef SCROLLVAR_NO
 #define makeVAR(v)  { const char *pv = v; \
    if (!w->variable_column_begin) cp = make_string(pv, w->variable_column_size, Justify_String_Right, AUTOX_NO); \
    else cp = make_string(w->variable_column_begin < (int)strlen(pv) ? pv + w->variable_column_begin : "", w->variable_column_size, Justify_String_Right, AUTOX_NO); }
#else
 #define makeVAR(v) cp = make_string(v, w->variable_column_size, Justify_String_Right, AUTOX_NO)
#endif
 #define pages2K(n)  (unsigned long)( (n) << Pg2K_shft )
   static char rbuf[ROWMINSIZ];
   static char stock[7];
   static char tmpstock[7];
   char *rp;
   int x;

   // we must begin a row with a possible window number in mind...
   *(rp = rbuf) = '\0';
   if (Rc.mode_altscreen) rp = scat(rp, " ");

   for (x = 0; x < w->max_displayable_pflags; x++) 
   {
      const char *cp;
      unsigned char i = w->proc_flags[x];
   #define SCALE                 Fieldstab[i].scale// these used to be variables
   #define FieldWidth            Fieldstab[i].width// but it's much better if we
   #define Justify_String_Right  CHKwinflags(w, Show_JRSTRS)// represent them as #defines
   #define Justify_Num_Right     CHKwinflags(w, Show_JRNUMS)// and only exec code if used

      switch (i) 
      {
#ifndef USE_X_COLHDR
         // these 2 aren't real proc_flags, they're used in column highlighting!
         case X_XON:
         case X_XOFF:
            cp = NULL;
            if (!CHKwinflags(w, INFINDS_xxx | NOHIFND_xxx | NOHISEL_xxx)) 
            {
               /* treat running tasks specially - entire row may get highlighted
                  so we needn't turn it on and we MUST NOT turn it off */
               if (!('R' == p->state && CHKwinflags(w, Show_Highlight_ROWS)))
               {
                  cp = (X_XON == i ? w->capclr_rowhigh : w->capclr_rownorm);
                 // cp = (X_XON == i ? Caps_green : w->capclr_rownorm);
               }
            }
            break;
#endif
         case P_CGR:
            makeVAR(p->cgroup[0]);
            break;
         case P_CMD:
            makeVAR(forest_display(w, p));
            break;
         case P_COD:
            cp = scale_mem(SCALE, pages2K(p->trs), FieldWidth, Justify_Num_Right);
            break;
         case P_CPN:
            cp = make_number(p->processor, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
         case P_DAT:
            cp = scale_mem(SCALE, pages2K(p->drs), FieldWidth, Justify_Num_Right);
            break;
         case P_DRT:
            cp = scale_num(p->dt, FieldWidth, Justify_Num_Right);
            break;
         case P_ENV:
            makeVAR(p->environ[0]);
            break;
         case P_FL1:
            cp = scale_num(p->maj_flt, FieldWidth, Justify_Num_Right);
            break;
         case P_FL2:
            cp = scale_num(p->min_flt, FieldWidth, Justify_Num_Right);
            break;
         case P_FLG:
            cp = make_string(hex_make(p->flags, 1), FieldWidth, Justify_String_Right, AUTOX_NO);
            break;
         case P_FV1:
            cp = scale_num(p->maj_delta, FieldWidth, Justify_Num_Right);
            break;
         case P_FV2:
            cp = scale_num(p->min_delta, FieldWidth, Justify_Num_Right);
            break;
         case P_GID:
            cp = make_number(p->egid, FieldWidth, Justify_Num_Right, P_GID);
            break;
         case P_GRP:
            cp = make_string(p->egroup, FieldWidth, Justify_String_Right, P_GRP);
            break;
         case P_MEM:
            cp = scale_percent((float)pages2K(p->resident) * 100 / kb_main_total, FieldWidth, Justify_Num_Right);
            break;
         case P_CPU:
         {  float u = (float)p->pcpu * Frame_etscale;
            /* process can't use more %cpu than number of threads it has
             ( thanks Jaromir Capik <jcapik@redhat.com> ) */
            if (u > 100.0 * p->nlwp) u = 100.0 * p->nlwp;
            if (u > Cpu_pmax) u = Cpu_pmax;
            cp = scale_percent(u, FieldWidth, Justify_Num_Right);
         }
            break;
         case P_NCE:
            cp = make_number(p->nice, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
         case P_NS1:   // IPCNS
         case P_NS2:   // MNTNS
         case P_NS3:   // NETNS
         case P_NS4:   // PIDNS
         case P_NS5:   // USERNS
         case P_NS6:   // UTSNS
         {  long ino = p->ns[i - P_NS1];
            if (ino > 0) cp = make_number(ino, FieldWidth, Justify_Num_Right, i);
            else cp = make_string("-", FieldWidth, Justify_String_Right, i);
         }
            break;
#ifdef OOMEM_ENABLE
         case P_OOA:
            cp = make_number(p->oom_adj, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
         case P_OOM:
            cp = make_number(p->oom_score, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
#endif
         case P_PGD:
            cp = make_number(p->pgrp, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
         case P_PID:
            cp = make_number(p->tid, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
         case P_PPD:
            cp = make_number(p->ppid, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
         case P_PRI:
            if (-99 > p->priority || 999 < p->priority) 
            {
               cp = make_string("rt", FieldWidth, Justify_Num_Right, AUTOX_NO);
            } 
            else
            {
               cp = make_number(p->priority, FieldWidth, Justify_Num_Right, AUTOX_NO);
            }
            break;
         case P_RES:
            cp = scale_mem(SCALE, pages2K(p->resident), FieldWidth, Justify_Num_Right);
            break;
         case P_SGD:
            makeVAR(p->supgid);
            break;
         case P_SGN:
            makeVAR(p->supgrp);
            break;
         case P_SHR:
            cp = scale_mem(SCALE, pages2K(p->share), FieldWidth, Justify_Num_Right);
            break;
         case P_SID:
            cp = make_number(p->session, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
         case P_STA:
            cp = make_char(p->state, FieldWidth, Justify_String_Right);
            break;
         case P_SWP:
            cp = scale_mem(SCALE, p->vm_swap, FieldWidth, Justify_Num_Right);
            break;
         case P_TGD:
            cp = make_number(p->tgid, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
         case P_THD:
            cp = make_number(p->nlwp, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
         case P_TM2:
         case P_TME:
         {  TIC_t t = p->utime + p->stime;
            if (CHKwinflags(w, Show_CTIMES)) t += (p->cutime + p->cstime);
            cp = scale_tics(t, FieldWidth, Justify_Num_Right);
         }
            break;
         case P_TPG:
            cp = make_number(p->tpgid, FieldWidth, Justify_Num_Right, AUTOX_NO);
            break;
         case P_TTY:
         {  char tmp[SMLBUFSIZ];
            dev_to_tty(tmp, FieldWidth, p->tty, p->tid, ABBREV_DEV);
            cp = make_string(tmp, FieldWidth, Justify_String_Right, P_TTY);
         }
            break;
         case P_UED:
            cp = make_number(p->euid, FieldWidth, Justify_Num_Right, P_UED);
            break;
         case P_UEN:
            cp = make_string(p->euser, FieldWidth, Justify_String_Right, P_UEN);
            break;
         case P_URD:
            cp = make_number(p->ruid, FieldWidth, Justify_Num_Right, P_URD);
            break;
         case P_URN:
            cp = make_string(p->ruser, FieldWidth, Justify_String_Right, P_URN);
            break;
         case P_USD:
            cp = make_number(p->suid, FieldWidth, Justify_Num_Right, P_USD);
            break;
         case P_USE:
            cp = scale_mem(SCALE, (p->vm_swap + pages2K(p->resident)), FieldWidth, Justify_Num_Right);
            break;
         case P_USN:
            cp = make_string(p->suser, FieldWidth, Justify_String_Right, P_USN);
            break;
         case P_VRT:
            cp = scale_mem(SCALE, pages2K(p->size), FieldWidth, Justify_Num_Right);
            break;
         case P_WCH:
         {  const char *u;
            if (No_ksyms)
               u = hex_make(p->wchan, 0);
            else
               u = lookup_wchan(p->wchan, p->tid);
            cp = make_string(u, FieldWidth, Justify_String_Right, P_WCH);
         }
            break;
         default:                 // keep gcc happy
            continue;

      } // end: switch 'procflag'

      if (cp) 
      {
         if (w->osel_tot && !osel_matched(w, i, cp)) return "";
         rp = scat(rp, cp);
      }
      #undef SCALE
      #undef FieldWidth
      #undef Justify_String_Right
      #undef Justify_Num_Right
   } // end: for 'max_displayable_pflags'

   if (!CHKwinflags(w, INFINDS_xxx)) 
   {
      // highlight the row if it's state is running
      const char *cap = ((CHKwinflags(w, Show_Highlight_ROWS) && 'R' == p->state))
         ? w->capclr_rowhigh : w->capclr_rownorm;
      char *row = rbuf;
      int ofs;
      /* since we can't predict what the search string will be and,
         considering what a single space search request would do to
         potential buffer needs, when any matches are found we skip
         normal output routing and send all of the results directly
         to the terminal (and we sound asthmatic: poof, putt, puff) */
      if (-1 < (ofs = find_ofs(w, row))) 
      {
         POOF("\n", cap);
         do 
         {
            row[ofs] = '\0';
            PUTT("%s%s%s%s", row, w->capclr_hdr, w->findstr, cap);
            row += (ofs + w->findlen);
            ofs = find_ofs(w, row);
         } while (-1 < ofs);
         PUTT("%s%s", row, Caps_endline);
         // with a corrupted rbuf, ensure row is 'counted' by window_show
         rbuf[0] = '!';
      } 
      else
      {
#if defined(LOG2STDOUT) && defined(BUILD_4_STOCK)
         fprintf(LogPtr, "processing stock row = %s\n", row);
         fflush(LogPtr);
#endif
//         printf("\n%s%s%s", cap, row, Caps_endline);
         PUFF("\n%s%s%s", cap, row, Caps_endline);
      }
   }
   return rbuf;
 #undef makeVAR
 #undef pages2K
} // end: task_show


        /*
         * Squeeze as many tasks as we can into a single window,
         * after sorting the passed proc table. */
static int window_show (WIN_t *w, int wmax) 
{
 /* the isBUSY macro determines if a task is 'active' --
    it returns true if some cpu was used since the last sample.
    ( actual 'running' tasks will be a subset of those selected ) */
 #define isBUSY(x)   (0 < x->pcpu)
 #define winMIN(a,b) ((a < b) ? a : b)
#ifdef BUILD_4_STOCK
 #define isTransaction(x) ((0 < x->current_price) && (0 < x->volume) && (0 < x->RMB)) 
 #define isTingpan(x) ((0 == x->current_price) || (0 == x->volume) || (0 == x->RMB)) 
#endif
   int i, lwin;

// with USE_X_COLHDR
// columnheader =   "StockI   PID  PPID   UID   TIME    VIRT   GID   SWAP ^[[m^O^[[39;49m^[[38;5;1m^[[7m %CPU ^[[m^O^[[39;49m^[[38;5;3m^[[7mGROUP    %MEM COMMAND"
#ifdef LOG2STDOUT
   fprintf(LogPtr,"Cap_clr_eol   = %s\n", Cap_clr_eol   ); 
   fprintf(LogPtr,"Cap_nl_clreos = %s\n", Cap_nl_clreos ); 
   fprintf(LogPtr,"Cap_clr_scr   = %s\n", Cap_clr_scr   ); 
   fprintf(LogPtr,"Cap_curs_norm = %s\n", Cap_curs_norm ); 
   fprintf(LogPtr,"Cap_curs_huge = %s\n", Cap_curs_huge ); 
   fprintf(LogPtr,"Cap_curs_hide = %s\n", Cap_curs_hide ); 
   fprintf(LogPtr,"Cap_clr_eos   = %s\n", Cap_clr_eos   ); 
   fprintf(LogPtr,"Cap_home      = %s\n", Cap_home      ); 
   fprintf(LogPtr,"Cap_norm      = %s\n", Cap_norm      ); 
   fprintf(LogPtr,"Cap_reverse   = %s\n", Cap_reverse   ); 
   fprintf(LogPtr,"Caps_off      = %s\n", Caps_off      ); 
   fprintf(LogPtr,"Caps_endline  = %s\n", Caps_endline  ); 
   fprintf(LogPtr,"Cap_rmam      = %s\n", Cap_rmam      );    
   fprintf(LogPtr,"Cap_smam      = %s\n", Cap_smam      );     
   fprintf(LogPtr,"w->captab[0] = Cap_norm; = %s\n",          w->captab[0]);
   fprintf(LogPtr,"w->captab[1] = Cap_norm; = %s\n",          w->captab[1]);
   fprintf(LogPtr,"w->captab[2] = cap_bold;       = %s\n", w->captab[2]);
   fprintf(LogPtr,"w->captab[3] = capclr_sum; = %s\n",     w->captab[3]);
   fprintf(LogPtr,"w->captab[4] = capclr_msg; = %s\n",     w->captab[4]);
   fprintf(LogPtr,"w->captab[5] = capclr_pmt; = %s\n",     w->captab[5]);
   fprintf(LogPtr,"w->captab[6] = capclr_hdr; = %s\n",     w->captab[6]);
   fprintf(LogPtr,"w->captab[7] = capclr_rowhigh; = %s\n", w->captab[7]);
   fprintf(LogPtr,"w->captab[8] = capclr_rownorm; = %s\n", w->captab[8]);

   fprintf(LogPtr, "window_show w->capclr_hdr = %s, w->columnheader = %s, Caps_endline = %s\n", w->capclr_hdr, w->columnheader, Caps_endline);
   char aa[300];
   strcpy(aa, w->capclr_hdr);
   strcat(aa, "StockI   PID  PPID   UID   TIME    VIRT   GID   SWAP ");
   strcat(aa, Caps_off);
   strcat(aa, w->capclr_sum);
   strcat(aa, Cap_reverse);
   strcat(aa, " %CPU ");
   strcat(aa, Caps_off);
   strcat(aa, w->capclr_hdr);
   strcat(aa, "GROUP    %MEM COMMAND             ");
   strcat(aa, Caps_endline);
   fprintf(LogPtr, "aaaaa = %s\n", aa);
   fflush(LogPtr);
#endif
   //printf("\n%s%s%s", w->capclr_hdr, aa, Caps_endline);
   //printf("\n%s", aa);
   //PUFF("\n%s%s%s", w->capclr_hdr, aa, Caps_endline);
   // Display Column Headings -- and distract 'em while we sort (maybe)
   PUFF("\n%s%s%s", w->capclr_hdr, w->columnheader, Caps_endline);

   if (CHKwinflags(w, Show_FOREST))
   {
      forest_create(w);
   }
   else 
   {
      if (CHKwinflags(w, Qsrt_NORMAL)) Frame_srtflg = 1;   // this is always needed!
      else Frame_srtflg = -1;
      Frame_ctimes = CHKwinflags(w, Show_CTIMES);          // this & next, only maybe
      Frame_cmdlin = CHKwinflags(w, Show_CMDLIN);
      if (Fieldstab[w->rc.sortindex].sort)
      {
         qsort(w->ppt, Frame_maxtask, sizeof(proc_t*), Fieldstab[w->rc.sortindex].sort);
      }
   }

#ifdef BUILD_4_STOCK
   int d = w->begintask;
   // update w->last_matched_index only
   if (!CHKwinflags(w, Show_IDLEPS))
   {
      while (d < Frame_maxtask)
      {
         if (filter_stock(w, w->ppt[d]))
         {
            w->last_matched_index = d;
         }
         d++;
      }
   }

   d = w->begintask;
   if (w->user_select_type)
   {
      while (d < Frame_maxtask)
      {
         if (stock_matched(w, w->ppt[d]))
         {
            w->last_matched_index = d;
         }
         d++;
      }
   }
#endif

   i = w->begintask;
   lwin = 1;                                        // 1 for the column header
   wmax = winMIN(wmax, w->winlines + 1);            // ditto for winlines, too

#ifdef BUILD_4_STOCK
   if (CHKwinflags(w, Show_IDLEPS) && !w->user_select_type)
   {
      while (i < Frame_maxtask && lwin < wmax) 
      {
         { 
            // the i passed to stock_show is i++;
            if (w->max_matched_row == 1)
               lwin = -1;
            if (*stock_show(i, lwin, w, w->ppt[i++]))
            {
               ++lwin;
            }
         }
      }
   }
   else
   {
      int j = 0;
      int indexcolumn = 1;
      while (j < i)
      {
         if (stock_matched(w, w->ppt[j++]))
         {
            indexcolumn++;
         }
      }
#ifdef LOG2STDOUT
      fprintf(LogPtr, "begintask = %d\n", w->begintask);
      fprintf(LogPtr, "filter_stock_prefix = %s\n", w->filter_stock_prefix);
      fprintf(LogPtr, "w->max_matched_row = %d\n", w->max_matched_row);
      fflush(LogPtr);
#endif
      while (i < Frame_maxtask && lwin < wmax) 
      {
         if (w->max_matched_row == 1)
            lwin = -1;
         if ((CHKwinflags(w, Show_IDLEPS))
             && stock_matched(w, w->ppt[i])
             && *stock_show(indexcolumn++, lwin, w, w->ppt[i]))
         {
#ifdef LOG2STDOUT
            fprintf(LogPtr, "lwin = %d\n", lwin);
            fflush(LogPtr);
#endif
            ++lwin;
         }
        
         // i command pressed
         if (!(CHKwinflags(w, Show_IDLEPS))
             && filter_stock(w, w->ppt[i])
             && *stock_show(indexcolumn++, lwin, w, w->ppt[i]))
         {
            ++lwin;
         }
        
         ++i;
      }
   }
#else
   /* the least likely scenario is also the most costly, so we'll try to avoid
      checking some stuff with each iteration and check it just once... */
   if (CHKwinflags(w, Show_IDLEPS) && !w->user_select_type)
   {
      while (i < Frame_maxtask && lwin < wmax) 
      {
         if (*task_show(w, w->ppt[i++]))
            ++lwin;
      }
   }
   else
   {
      while (i < Frame_maxtask && lwin < wmax) 
      {
         if ((CHKwinflags(w, Show_IDLEPS) || isBUSY(w->ppt[i]))
         && user_matched(w, w->ppt[i])
         && *task_show(w, w->ppt[i]))
         {
            ++lwin;
         }
         ++i;
      }
   }
#endif

   return lwin;
 #undef winMIN
 #undef isBUSY
} // end: window_show

/*######  Entry point plus two  ##########################################*/

        /*
         * This guy's just a *Helper* function who apportions the
         * remaining amount of screen real estate under multiple windows */
static void frame_hlp (int wix, int max) 
{
   int i, size, wins;

   // calc remaining number of visible windows
   for (i = wix, wins = 0; i < GROUPSMAX; i++)
   {
      if (CHKwinflags(&Winstk[i], Show_TASKON))
      {
         ++wins;
      }
   }

   if (!wins) wins = 1;
   // deduct 1 line/window for the columns heading
   size = (max - wins) / wins;

   /* for subject window, set WIN_t winlines to either the user's
      maxtask (1st choice) or our 'foxized' size calculation
      (foxized  adj. -  'fair and balanced') */
   Winstk[wix].winlines =
      Winstk[wix].rc.maxtasks ? Winstk[wix].rc.maxtasks : size;
} // end: frame_hlp


        /*
         * Initiate the Frame Display Update cycle at someone's whim!
         * This routine doesn't do much, mostly he just calls others.
         *
         * (Whoa, wait a minute, we DO caretake those row guys, plus)
         * (we CALCULATE that IMPORTANT Max_lines thingy so that the)
         * (*subordinate* functions invoked know WHEN the user's had)
         * (ENOUGH already.  And at Frame End, it SHOULD be apparent)
         * (WE am d'MAN -- clearing UNUSED screen LINES and ensuring)
         * (that those auto-sized columns are addressed, know what I)
         * (mean?  Huh, "doesn't DO MUCH"!  Never, EVER think or say)
         * (THAT about THIS function again, Ok?  Good that's better.)
         *
         * (ps. we ARE the UNEQUALED justification KING of COMMENTS!)
         * (No, I don't mean significance/relevance, only alignment.)
         */
static void frame_make (void) 
{
   WIN_t *w = Curwin;             // avoid gcc bloat with a local copy

   int i, scrlins;

   // deal with potential signal(s) since the last time around...
   if (Frames_signal)
   {
      zap_fieldstab();
   }

   // whoa either first time or thread/task mode change, (re)prime the pump...
   if (Pseudo_row == PROC_XTRA) 
   {
      procs_refresh();
#ifdef LOG2STDOUT
      fprintf(LogPtr, "PROC_XTRA proc_refreshed Frame_maxtask = %d\n", Frame_maxtask);
#endif
      usleep(LIB_USLEEP);
   // this is ncurses function
      putp(Cap_clr_scr);
   } 
   else
   {
   // this is ncurses function
      putp(Batch ? "\n\n" : Cap_home);
   }

   procs_refresh();

#ifdef LOG2STDOUT
   fprintf(LogPtr, "proc_refreshed Frame_maxtask = %d\n", Frame_maxtask);
#endif

   sysinfo_refresh(0);

   Tree_idx = Pseudo_row = Msg_row = scrlins = 0;

#ifdef BUILD_4_STOCK
   // just run for only once at startup
   // for the startup while Show_IDLEPS not set in .toprc
   if (!CHKwinflags(w, Show_IDLEPS) && !w->ifilter)
   {
      updateIfilter();
      w->ifilter = 1;
   }
   
   // just run for only once at startup for user_select_type
   if (w->user_select_type && !w->ufilter)
   {
      w->stock_select_flags = 1;
      updateUfilter();
      w->ufilter = 1;
   }
#endif

   summary_show();

   Max_lines = (Screen_rows - Msg_row) - 1;

   OFFwinflags(w, INFINDS_xxx);

   /* one way or another, rid us of any prior frame's msg
      [ now that this is positioned after the call to summary_show(), ]
      [ we no longer need or employ tg2(0, Msg_row) since all summary ]
      [ lines end with a newline, and header lines begin with newline ] */
   if (VIZISw(w) && CHKwinflags(w, View_SCROLL)) 
   {
      PUTT(Scroll_fmts, Frame_maxtask);
   }
   else 
   {
      putp(Cap_clr_eol);
   }

   if (!Rc.mode_altscreen) 
   {
      // only 1 window to show so, piece o' cake
      w->winlines = w->rc.maxtasks ? w->rc.maxtasks : Max_lines;
      scrlins = window_show(w, Max_lines);
   } 
   else 
   {
      // maybe NO window is visible but assume, pieces o' cakes
      for (i = 0 ; i < GROUPSMAX; i++) 
      {
         if (CHKwinflags(&Winstk[i], Show_TASKON)) 
         {
            frame_hlp(i, Max_lines - scrlins);
            scrlins += window_show(&Winstk[i], Max_lines - scrlins);
         }
         if (Max_lines <= scrlins) break;
      }
   }

   /* clear to end-of-screen - critical if last window is 'idleps off'
      (main loop must iterate such that we're always called before sleep) */
   if (scrlins < Max_lines) 
   {
      putp(Cap_nl_clreos);
      PSU_CLEARSCREEN(Pseudo_row);
   }
   fflush(stdout);

   /* we'll deem any terminal not supporting tgoto as dumb and disable
      the normal non-interactive output optimization... */
   if (!Cap_can_goto)
   {
      PSU_CLEARSCREEN(0);
   }

#ifndef NUMA_DISABLE
   /* we gotta reverse the stderr redirect which was employed in wins_stage_2
      and needed because the two libnuma 'weak' functions were useless to us! */
   if (-1 < Stderr_save) 
   {
      dup2(Stderr_save, fileno(stderr));
      close(Stderr_save);
      Stderr_save = -1;
   }
#endif

   /* lastly, check auto-sized width needs for the next iteration */
   if (AUTOX_MODE && Autox_found)
   {
      widths_resize();
   }
} // end: frame_make

#ifdef LOG2STDOUT
void printterm()
{
   // Screen_cols = columns;    // <term.h>
   // Screen_rows = lines;      // <term.h>
   fprintf(LogPtr, "Screen_cols = lines = %d\n", lines);
   fprintf(LogPtr,"Screen_rows = columns = %d\n", columns);
   fflush(LogPtr);
}
#endif

void generat_magic_string()
{
  int i = 165; // 0x25 % + 0x80
  unsigned char bytes[100];
  int j = 0;
  for (;i < 256;++i)
  {
    bytes[j] = i;
    if (i > 188) bytes[j] &= 0x7f;
    printf("bytes[%d], hex = %x, c = %c, index = %d\n", j, bytes[j], bytes[j], (bytes[j] & 0x7f)-0x25);
    j++;
  }
  bytes[80]='\0';
  printf("magic string = \"%s\"\n", bytes);
}

struct utlbuf_s 
{
    char *buf;     // dynamically grown buffer
    int   siz;     // current len of the above
} utlbuf_s;

static int file2str(const char *directory, const char *what, struct utlbuf_s *ub) 
{
 #define buffGRW 1024
   char path[PROCPATHLEN];
   int fd, num, tot_read = 0;

    /* on first use we preallocate a buffer of minimum size to emulate
       former 'local static' behavior -- even if this read fails, that
       buffer will likely soon be used for another subdirectory anyway
       ( besides, with this xcalloc we will never need to use memcpy ) */
   if (ub->buf) ub->buf[0] = '\0';
   else ub->buf = xcalloc((ub->siz = buffGRW));
   sprintf(path, "%s/%s", directory, what);
   if (-1 == (fd = open(path, O_RDONLY, 0))) return -1;
   while (0 < (num = read(fd, ub->buf + tot_read, ub->siz - tot_read))) 
   {
      tot_read += num;
      if (tot_read < ub->siz) break;
      ub->buf = xrealloc(ub->buf, (ub->siz += buffGRW));
   };
   ub->buf[tot_read] = '\0';
   close(fd);
   if (unlikely(tot_read < 1)) return -1;
   return tot_read;
 #undef buffGRW
}

static void data2proc(const char* s, proc_t *restrict P) 
{
   int num;
   num = sscanf(s, "%ld %ld %ld %ld %ld %ld %ld",
	   &P->size, &P->resident, &P->share,
	   &P->trs, &P->lrs, &P->drs, &P->dt);
/*    fprintf(stderr, "statm2proc converted %d fields.\n",num); */
}

static int mfile2str(const char *directory, const char *what, struct utlbuf_s *ub) 
{
 #define buffGRW 1024
   char path[PROCPATHLEN];
   int fd, num, tot_read = 0;

   /* on first use we preallocate a buffer of minimum size to emulate
      former 'local static' behavior -- even if this read fails, that
      buffer will likely soon be used for another subdirectory anyway
      ( besides, with this xcalloc we will never need to use memcpy ) */
   if (ub->buf) ub->buf[0] = '\0';
   else ub->buf = xcalloc((ub->siz = buffGRW));
   sprintf(path, "%s/%s", directory, what);
   if (-1 == (fd = open(path, O_RDONLY, 0))) return -1;
   while (0 < (num = read(fd, ub->buf + tot_read, ub->siz - tot_read))) 
   {
      tot_read += num;
      if (tot_read < ub->siz) break;
      ub->buf = xrealloc(ub->buf, (ub->siz += buffGRW));
   };
   ub->buf[tot_read] = '\0';
   close(fd);
   if (unlikely(tot_read < 1)) return -1;
   return tot_read;
 #undef buffGRW
}

/*
static int mytest() 
{
   static struct utlbuf_s ub = { NULL, 0 };    // buf for stat,statm,status
   char *path = "/home/admin/Downloads/stock-1.0.2/stock/000521";

   if (unlikely(mfile2str(path, "data", &ub) == -1))
      printf("error\n");
   fprintf(LogPtr, "testdata = %s\n", ub.buf);
   fflush(LogPtr);
}
*/
/*
struct A {
   int index;
   int *vvv;
   };

int cmp1( const struct A **a , const struct A **b )
{ 
   return (*a)->index > (*b)->index ? 1 : -1; 
}

void test_qsort()
{
    int i;
    struct A **pp = (struct A**)malloc(sizeof(struct A)*3);
    struct A *a1;
    a1 = (struct A*) malloc(sizeof(struct A));
    a1->index = 111;
    int *pa;
    pa = (int *)malloc(sizeof(int));
    *pa = 9; 
    a1->vvv = pa;
    pp[0] = a1;
    a1 = (struct A*) malloc(sizeof(struct A));
    a1->index = 211;
    pa = (int *)malloc(sizeof(int));
    *pa = 431; 
    a1->vvv = pa;
    pp[1] = a1;
    a1 = (struct A*) malloc(sizeof(struct A));
    a1->index = 81;
    pa = (int *)malloc(sizeof(int));
    *pa = 930; 
    a1->vvv = pa;
    pp[2] = a1;
    for(i = 0; i<3; ++i)
    {
      printf("pp[%d]->index = %d, vvv = %d\n", i, pp[i]->index, *(pp[i]->vvv));
    }
    qsort(pp, 3, sizeof(struct A*), cmp1);
    for(i = 0; i<3; ++i)
    {
      printf("pp[%d]->index = %d, vvv = %d\n", i, pp[i]->index, *(pp[i]->vvv));
    }

   return 0;
}
*/

//
// Convert between .c and .h in vim by adding this line within ~/.vimrc
// map <C-h> :e %:p:s,.h$,.X123X,:s,.c$,.h,:s,.X123X$,.c,<CR>
// map <C-h> :e %:p:s,.h$,.X123X,:s,.cpp$,.h,:s,.X123X$,.cpp,<CR>
// map <F5> :!ctags -R --c++-kinds=+p --fields=+iaS --extra=+q .<CR><CR> :TlistUpdate<CR>
// imap <F5> <ESC>:!ctags -R --c++-kinds=+p --fields=+iaS --extra=+q .<CR><CR> :TlistUpdate<CR>
// "set tags=/home/admin/Downloads/stock-1.0.2/tags
// set tags+=/home/admin/Downloads/robomongo-shell/tags
//
int main (int dont_care_argc, char **argv) 
{
   /*
   FILE *fp;
   fp = fopen("data/603011/rsi_akdaily", "r");
   if (fp) 
   {
      char cc[16]; 
      if (1 == fscanf(fp, "%16s" , cc))
      {
         printf("%s\n",cc);
         if (strcmp(cc, "Not") == 0)
            printf("c = %c, Not Golden\n", cc[0]);
         else
            printf("c = %c, Golden\n", cc[0]);
      }
      fclose(fp);
   }
*/
   //system("python sin.py");
   //return 0;
   //printf("\033[1;31;40m iiiii \e[0m jjj\n");
//   printf("%x,%d,%o\n",Show_IDLEPS,Show_IDLEPS,Show_IDLEPS);
   char *p = "price_A2':['14.33','14.37','10192','14.37','0.80','14.55','0.77','14.14','745687','15.81','17.53','12.93','106847','14.50','2.85','1'],'perform':['4.46','859','14.54','2943','14.53','980','14.52','2410','14.51','2412','14.50','463','14.49','3321','14.48','4935','14.47','989','14.46','608','14.45','214','368873','376813'],";
   float vratio;
   float swing;
   long long outer;
   long long inner;
   int num = sscanf(p, "price_A2':[%*[^,],%*[^,],%*[^,],%*[^,],'%f',%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],'%f%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],'%lld','%lld'",&vratio, &swing, &outer, &inner);
   //printf("vratio = %.2f, swing = %.2f\%, outer = %lld, innter = %lld\n", vratio, swing, outer, inner);
   //return 0;

   //initscr();
 //  printf("KLF = %s\n", KLF);
//   printf("Datadir = %s, strlen= %d\n", DATADIR, strlen(DATADIR));
/*
   int fff = 61020;

   if (fff&View_CPUSUM        ) printf("View_CPUSUM\n");
   if (fff&View_CPUNOD        ) printf("View_CPUNOD\n");
   if (fff&View_LOADAV        ) printf("View_LOADAV\n");
   if (fff&View_TASKS         ) printf("View_TASKS \n");
   if (fff&View_MEMORY        ) printf("View_MEMORY\n");
   if (fff&View_NOBOLD        ) printf("View_NOBOLD\n");
   if (fff&View_SCROLL        ) printf("View_SCROLL\n");
   if (fff&Show_COLORS        ) printf("Show_COLORS\n");
   if (fff&Show_Highlight_BOLD) printf("Show_Highlight_BOLD\n");
   if (fff&Show_Highlight_COLS) printf("Show_Highlight_COLS\n");
   if (fff&Show_Highlight_ROWS) printf("Show_Highlight_ROWS\n");
   if (fff&Show_CMDLIN        ) printf("Show_CMDLIN\n");
   if (fff&Show_CTIMES        ) printf("Show_CTIMES\n");
   if (fff&Show_IDLEPS        ) printf("Show_IDLEPS\n");
   if (fff&Show_TASKON        ) printf("Show_TASKON\n");
   if (fff&Show_FOREST        ) printf("Show_FOREST\n");
   if (fff&Qsrt_NORMAL        ) printf("Qsrt_NORMAL\n");
   if (fff&Show_JRSTRS        ) printf("Show_JRSTRS\n");
   if (fff&Show_JRNUMS        ) printf("Show_JRNUMS\n");
   if (fff&INFINDS_xxx        ) printf("INFINDS_xxx\n");
   if (fff&EQUWINS_xxx        ) printf("EQUWINS_xxx\n");
   if (fff&NOHISEL_xxx        ) printf("NOHISEL_xxx\n");
   if (fff&NOHIFND_xxx        ) printf("NOHIFND_xxx\n");
   return 0;
*/

//   generat_magic_string();
   int i=55;
//   for (;i<100;i++)
//      printf("%c",i+'%'); 
//   printf("\n");
//   return 0;
   (void)dont_care_argc;

#ifdef LOG2STDOUT
   init_log();

#ifdef BUILD_4_STOCK
   fprintf(LogPtr, "Build_4_stock\n");
#else
   fprintf(LogPtr, "Build_4_top\n");
#endif

   fprintf(LogPtr, "Datadir = %s, strlen= %d\n", DATADIR, strlen(DATADIR));
//  mytest();
   i = 22;
   fprintf(LogPtr, "mmmmmm\n");
   for (;i<30;++i)
   {
      unsigned char uuu = i + '%';
      uuu |= 0x80;
  //    fprintf(LogPtr,"%c",uuu);
   }
#endif

   before(*argv);
                                        //                 +-------------+
   wins_stage_1();                      //                 top (sic) slice
   configs_read();                      //                 > spread etc, <
   parse_args(&argv[1]);                //                 > lean stuff, <
   whack_terminal();                    //                 > onions etc. <
#ifdef LOG2STDOUT
   printterm();                         //  must called after whack_terminal()
#endif
   wins_stage_2();                      //                 as bottom slice
                                        //                 +-------------+
   for (;;) 
   {
      struct timespec ts;

      frame_make();

      if (0 < Loops) --Loops;
      if (!Loops) bye_bye(NULL);

      ts.tv_sec = Rc.delay_time;
      ts.tv_nsec = (Rc.delay_time - (int)Rc.delay_time) * 1000000000;

      if (Batch)
      {
         pselect(0, NULL, NULL, NULL, &ts, NULL);
      }
      else 
      {
         if (ioa(&ts))
         {
            do_key(iokey(1));
         }
      }
      //system("gnome-screenshot");
      /* note: that above ioa routine exists to consolidate all logic
               which is susceptible to signal interrupt and must then
               produce a screen refresh. in this main loop frame_make
               assumes responsibility for such refreshes. other logic
               in contact with users must deal more obliquely with an
               interrupt/refresh (hint: Frames_signal + return code)!

               (everything is perfectly justified plus right margins)
               (are completely filled, but of course it must be luck)
       */
   }
   return 0;
} // end: main
