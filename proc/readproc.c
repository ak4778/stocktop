/*
 * New Interface to Process Table -- PROCTAB Stream (a la Directory streams)
 * Copyright (C) 1996 Charles L. Blake.
 * Copyright (C) 1998 Michael K. Johnson
 * Copyright 1998-2003 Albert Cahalan
 *
 */

#include "version.h"
#include "readproc.h"
#include "alloc.h"
#include "escape.h"
#include "pwcache.h"
#include "devname.h"
#include "procps.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/dir.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "top.h"

static int FACTOR = 1000;

// sometimes it's easier to do this manually, w/o gcc helping
#ifdef PROF
extern void __cyg_profile_func_enter(void*,void*);
#define ENTER(x) __cyg_profile_func_enter((void*)x,(void*)x)
#define LEAVE(x) __cyg_profile_func_exit((void*)x,(void*)x)
#else
#define ENTER(x)
#define LEAVE(x)
#endif

#ifdef QUICK_THREADS
// used when multi-threaded and some memory must not be freed
#define MK_THREAD(q)   q->pad_1 =  '\xee'
#define IS_THREAD(q) ( q->pad_1 == '\xee' )
#endif

// utility buffers of MAX_BUFSZ bytes each, available to
// any function following an openproc() call
static char *src_buffer,
            *dst_buffer;
#define MAX_BUFSZ 1024*64*2

// dynamic 'utility' buffer support for file2str() calls
struct utlbuf_s {
    char *buf;     // dynamically grown buffer
    int   siz;     // current len of the above
} utlbuf_s;

#ifndef SIGNAL_STRING
// convert hex string to unsigned long long
static unsigned long long unhex(const char *restrict cp){
    unsigned long long ull = 0;
    for(;;){
        char c = *cp++;
        if(unlikely(c<0x30)) break;
        ull = (ull<<4) | (c - (c>0x57) ? 0x57 : 0x30) ;
    }
    return ull;
}
#endif

static int task_dir_missing;

// free any additional dynamically acquired storage associated with a proc_t
// ( and if it's to be reused, refresh it otherwise destroy it )
static inline void free_acquired (proc_t *p, int reuse) {
#ifdef QUICK_THREADS
    if (!IS_THREAD(p)) {
#endif
        if (p->environ) free((void*)*p->environ);
        if (p->cmdline) free((void*)*p->cmdline);
        if (p->cgroup)  free((void*)*p->cgroup);
        if (p->supgid)  free(p->supgid);
        if (p->supgrp)  free(p->supgrp);
#ifdef QUICK_THREADS
    }
#endif
    memset(p, reuse ? '\0' : '\xff', sizeof(*p));
}

///////////////////////////////////////////////////////////////////////////

typedef struct status_table_struct {
    unsigned char name[7];        // /proc/*/status field name
    unsigned char len;            // name length
#ifdef LABEL_OFFSET
    long offset;                  // jump address offset
#else
    void *addr;
#endif
} status_table_struct;

#ifdef LABEL_OFFSET
#define F(x) {#x, sizeof(#x)-1, (long)(&&case_##x-&&base)},
#else
#define F(x) {#x, sizeof(#x)-1, &&case_##x},
#endif
#define NUL  {"", 0, 0},

// Derived from:
// gperf -7 --language=ANSI-C --key-positions=1,3,4 -C -n -c <if-not-piped>
//
// Suggested method:
// Grep this file for "case_", then strip those down to the name.
// Eliminate duplicates (due to #ifs), the '    case_' prefix and
// any c comments.  Leave the colon and newline so that "Pid:\n",
// "Threads:\n", etc. would be lines, but no quote, no escape, etc.
//
// After a pipe through gperf, insert the resulting 'asso_values'
// into our 'asso' array.  Then convert the gperf 'wordlist' array
// into our 'table' array by wrapping the string literals within
// the F macro and replacing empty strings with the NUL define.
//
// In the status_table_struct watch out for name size (grrr, expanding)
// and the number of entries (we mask with 63 for now). The table
// must be padded out to 64 entries, maybe 128 in the future.

static void status2proc(char *S, proc_t *restrict P, int is_proc){
    long Threads = 0;
    long Tgid = 0;
    long Pid = 0;

  // 128 entries because we trust the kernel to use ASCII names
  static const unsigned char asso[] =
    {
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 28, 64,
      64, 64, 64, 64, 64, 64,  8, 25, 23, 25,
       6, 25,  0,  3, 64, 64,  3, 64, 25, 64,
      20,  1,  1,  5,  0, 30,  0,  0, 64, 64,
      64, 64, 64, 64, 64, 64, 64,  3, 64,  0,
       0, 18, 64, 10, 64, 10, 64, 64, 64, 20,
      64, 20,  0, 64, 25, 64,  3, 15, 64,  0,
      30, 64, 64, 64, 64, 64, 64, 64
    };

    static const status_table_struct table[] = {
      F(VmHWM)
      NUL NUL
      F(VmLck)
      NUL
      F(VmSwap)
      F(VmRSS)
      NUL
      F(VmStk)
      NUL
      F(Tgid)
      F(State)
      NUL
      F(VmLib)
      NUL
      F(VmSize)
      F(SigQ)
      NUL
      F(SigIgn)
      NUL
      F(VmPTE)
      F(FDSize)
      NUL
      F(SigBlk)
      NUL
      F(ShdPnd)
      F(VmData)
      NUL
      F(CapInh)
      NUL
      F(PPid)
      NUL NUL
      F(CapBnd)
      NUL
      F(SigPnd)
      NUL NUL
      F(VmPeak)
      NUL
      F(SigCgt)
      NUL NUL
      F(Threads)
      NUL
      F(CapPrm)
      NUL NUL
      F(Pid)
      NUL
      F(CapEff)
      NUL NUL
      F(Gid)
      NUL
      F(VmExe)
      NUL NUL
      F(Uid)
      NUL
      F(Groups)
      NUL NUL
      F(Name)
    };

#undef F
#undef NUL

ENTER(0x220);

    goto base;

    for(;;){
        char *colon;
        status_table_struct entry;

        // advance to next line
        S = strchr(S, '\n');
        if(unlikely(!S)) break;  // if no newline
        S++;

        // examine a field name (hash and compare)
    base:
        if(unlikely(!*S)) break;
        entry = table[63 & (asso[(int)S[3]] + asso[(int)S[2]] + asso[(int)S[0]])];
        colon = strchr(S, ':');
        if(unlikely(!colon)) break;
        if(unlikely(colon[1]!='\t')) break;
        if(unlikely(colon-S != entry.len)) continue;
        if(unlikely(memcmp(entry.name,S,colon-S))) continue;

        S = colon+2; // past the '\t'

#ifdef LABEL_OFFSET
        goto *(&&base + entry.offset);
#else
        goto *entry.addr;
#endif

    case_Name:
    {   unsigned u = 0;
        while(u < sizeof P->cmd - 1u){
            int c = *S++;
            if(unlikely(c=='\n')) break;
            if(unlikely(c=='\0')) break; // should never happen
            if(unlikely(c=='\\')){
                c = *S++;
                if(c=='\n') break; // should never happen
                if(!c)      break; // should never happen
                if(c=='n') c='\n'; // else we assume it is '\\'
            }
            P->cmd[u++] = c;
        }
        P->cmd[u] = '\0';
        S--;   // put back the '\n' or '\0'
        continue;
    }
#ifdef SIGNAL_STRING
    case_ShdPnd:
        memcpy(P->signal, S, 16);
        P->signal[16] = '\0';
        continue;
    case_SigBlk:
        memcpy(P->blocked, S, 16);
        P->blocked[16] = '\0';
        continue;
    case_SigCgt:
        memcpy(P->sigcatch, S, 16);
        P->sigcatch[16] = '\0';
        continue;
    case_SigIgn:
        memcpy(P->sigignore, S, 16);
        P->sigignore[16] = '\0';
        continue;
    case_SigPnd:
        memcpy(P->_sigpnd, S, 16);
        P->_sigpnd[16] = '\0';
        continue;
#else
    case_ShdPnd:
        P->signal = unhex(S);
        continue;
    case_SigBlk:
        P->blocked = unhex(S);
        continue;
    case_SigCgt:
        P->sigcatch = unhex(S);
        continue;
    case_SigIgn:
        P->sigignore = unhex(S);
        continue;
    case_SigPnd:
        P->_sigpnd = unhex(S);
        continue;
#endif
    case_State:
        P->state = *S;
        continue;
    case_Tgid:
        Tgid = strtol(S,&S,10);
        continue;
    case_Pid:
        Pid = strtol(S,&S,10);
        continue;
    case_PPid:
        P->ppid = strtol(S,&S,10);
        continue;
    case_Threads:
        Threads = strtol(S,&S,10);
        continue;
    case_Uid:
        P->ruid = strtol(S,&S,10);
        P->euid = strtol(S,&S,10);
        P->suid = strtol(S,&S,10);
        P->fuid = strtol(S,&S,10);
        continue;
    case_Gid:
        P->rgid = strtol(S,&S,10);
        P->egid = strtol(S,&S,10);
        P->sgid = strtol(S,&S,10);
        P->fgid = strtol(S,&S,10);
        continue;
    case_VmData:
        P->vm_data = strtol(S,&S,10);
        continue;
    case_VmExe:
        P->vm_exe = strtol(S,&S,10);
        continue;
    case_VmLck:
        P->vm_lock = strtol(S,&S,10);
        continue;
    case_VmLib:
        P->vm_lib = strtol(S,&S,10);
        continue;
    case_VmRSS:
        P->vm_rss = strtol(S,&S,10);
        continue;
    case_VmSize:
        P->vm_size = strtol(S,&S,10);
        continue;
    case_VmStk:
        P->vm_stack = strtol(S,&S,10);
        continue;
    case_VmSwap: // Linux 2.6.34
        P->vm_swap = strtol(S,&S,10);
        continue;
    case_Groups:
    {   char *nl = strchr(S, '\n');
        int j = nl ? (nl - S) : strlen(S);

        if (j) {
            P->supgid = xmalloc(j+1);       // +1 in case space disappears
            memcpy(P->supgid, S, j);
            if (unlikely(' ' != P->supgid[--j])) ++j;
            P->supgid[j] = '\0';            // whack the space or the newline
            for ( ; j; j--)
                if (' '  == P->supgid[j])
                    P->supgid[j] = ',';
        }
        continue;
    }
    case_CapBnd:
    case_CapEff:
    case_CapInh:
    case_CapPrm:
    case_FDSize:
    case_SigQ:
    case_VmHWM: // 2005, peak VmRSS unless VmRSS is bigger
    case_VmPTE:
    case_VmPeak: // 2005, peak VmSize unless VmSize is bigger
        continue;
    }

#if 0
    // recent kernels supply per-tgid pending signals
    if(is_proc && *ShdPnd){
        memcpy(P->signal, ShdPnd, 16);
        P->signal[16] = '\0';
    }
#endif

    // recent kernels supply per-tgid pending signals
#ifdef SIGNAL_STRING
    if(!is_proc || !P->signal[0]){
        memcpy(P->signal, P->_sigpnd, 16);
        P->signal[16] = '\0';
    }
#else
    if(!is_proc){
        P->signal = P->_sigpnd;
    }
#endif

    // Linux 2.4.13-pre1 to max 2.4.xx have a useless "Tgid"
    // that is not initialized for built-in kernel tasks.
    // Only 2.6.0 and above have "Threads" (nlwp) info.

    if(Threads){
        P->nlwp = Threads;
        P->tgid = Tgid;     // the POSIX PID value
        P->tid  = Pid;      // the thread ID
    }else{
        P->nlwp = 1;
        P->tgid = Pid;
        P->tid  = Pid;
    }

    if (!P->supgid)
        P->supgid = xstrdup("-");

LEAVE(0x220);
}

static void supgrps_from_supgids (proc_t *p) {
    char *g, *s;
    int t;

    if (!p->supgid || '-' == *p->supgid) {
        p->supgrp = xstrdup("-");
        return;
    }
    s = p->supgid;
    t = 0;
    do {
        if (',' == *s) ++s;
        g = group_from_gid((uid_t)strtol(s, &s, 10));
        p->supgrp = xrealloc(p->supgrp, P_G_SZ+t+2);
        t += snprintf(p->supgrp+t, P_G_SZ+2, "%s%s", t ? "," : "", g);
    } while (*s);
}

///////////////////////////////////////////////////////////////////////
#ifdef OOMEM_ENABLE
static void oomscore2proc(const char* S, proc_t *restrict P)
{
    sscanf(S, "%d", &P->oom_score);
}

static void oomadj2proc(const char* S, proc_t *restrict P)
{
    sscanf(S, "%d", &P->oom_adj);
}
#endif
///////////////////////////////////////////////////////////////////////

static const char *ns_names[] = {
    [IPCNS] = "ipc",
    [MNTNS] = "mnt",
    [NETNS] = "net",
    [PIDNS] = "pid",
    [USERNS] = "user",
    [UTSNS] = "uts",
};

const char *get_ns_name(int id) {
    if (id >= NUM_NS)
        return NULL;
    return ns_names[id];
}

int get_ns_id(const char *name) {
    int i;

    for (i = 0; i < NUM_NS; i++)
        if (!strcmp(ns_names[i], name))
            return i;
    return -1;
}

static void ns2proc(const char *directory, proc_t *restrict p) {
    char path[PROCPATHLEN];
    struct stat sb;
    int i;

    for (i = 0; i < NUM_NS; i++) {
        snprintf(path, sizeof(path), "%s/ns/%s", directory, ns_names[i]);
        if (0 == stat(path, &sb))
            p->ns[i] = (long)sb.st_ino;
#if 0
        else                           // this allows a caller to distinguish
            p->ns[i] = -errno;         // between the ENOENT or EACCES errors
#endif
    }
}
///////////////////////////////////////////////////////////////////////


// Reads /proc/*/stat files, being careful not to trip over processes with
// names like ":-) 1 2 3 4 5 6".
static void stat2proc(const char* S, proc_t *restrict P) 
{
   unsigned num;
   char* tmp;

ENTER(0x160);

    /* fill in default values for older kernels */
   P->processor = 0;
   P->rtprio = -1;
   P->sched = -1;
   P->nlwp = 0;

   S = strchr(S, '(') + 1;
   tmp = strrchr(S, ')');
   num = tmp - S;
   if(unlikely(num >= sizeof P->cmd)) num = sizeof P->cmd - 1;
   memcpy(P->cmd, S, num);
   P->cmd[num] = '\0';
   S = tmp + 2;                 // skip ") "

   num = sscanf(S,
       "%c "
       "%d %d %d %d %d "
       "%lu %lu %lu %lu %lu "
       "%Lu %Lu %Lu %Lu "  /* utime stime cutime cstime */
       "%ld %ld "
       "%d "
       "%ld "
       "%Lu "  /* start_time */
       "%lu "
       "%ld "
       "%lu %"KLF"u %"KLF"u %"KLF"u %"KLF"u %"KLF"u "
       "%*s %*s %*s %*s " /* discard, no RT signals & Linux 2.1 used hex */
       "%"KLF"u %*u %*u "
       "%d %d "
       "%lu %lu",
       &P->state,
       &P->ppid, &P->pgrp, &P->session, &P->tty, &P->tpgid,
       &P->flags, &P->min_flt, &P->cmin_flt, &P->maj_flt, &P->cmaj_flt,
       &P->utime, &P->stime, &P->cutime, &P->cstime,
       &P->priority, &P->nice,
       &P->nlwp,
       &P->alarm,
       &P->start_time,
       &P->vsize,
       &P->rss,
       &P->rss_rlim, &P->start_code, &P->end_code, &P->start_stack, &P->kstk_esp, &P->kstk_eip,
/*     P->signal, P->blocked, P->sigignore, P->sigcatch,   */ /* can't use */
       &P->wchan, /* &P->nswap, &P->cnswap, */  /* nswap and cnswap dead for 2.4.xx and up */
/* -- Linux 2.0.35 ends here -- */
       &P->exit_signal, &P->processor,  /* 2.2.1 ends with "exit_signal" */
/* -- Linux 2.2.8 to 2.5.17 end here -- */
       &P->rtprio, &P->sched  /* both added to 2.5.18 */
    );

   if(!P->nlwp)
   {
      P->nlwp = 1;
   }

LEAVE(0x160);
}

/////////////////////////////////////////////////////////////////////////

static void statm2proc(const char* s, proc_t *restrict P) {
    int num;
    num = sscanf(s, "%ld %ld %ld %ld %ld %ld %ld",
	   &P->size, &P->resident, &P->share,
	   &P->trs, &P->lrs, &P->drs, &P->dt);
/*    fprintf(stderr, "statm2proc converted %d fields.\n",num); */
}

static int lfile2str(const char *directory, const char *what, struct utlbuf_s *ub, int lines) 
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
   int l = 0;
   while ((l++<lines)&&(0 < (num = read(fd, ub->buf + tot_read, ub->siz - tot_read)))) 
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

static char** file2strvec(const char* directory, const char* what) 
{
   char buf[2048];	/* read buf bytes at a time */
   char *p, *rbuf = 0, *endbuf, **q, **ret;
   int fd, tot = 0, n, c, end_of_file = 0;
   int align;

   sprintf(buf, "%s/%s", directory, what);
   fd = open(buf, O_RDONLY, 0);
   if(fd==-1) return NULL;

   /* read whole file into a memory buffer, allocating as we go */
   while ((n = read(fd, buf, sizeof buf - 1)) >= 0) 
   {
       if (n < (int)(sizeof buf - 1))
           end_of_file = 1;
       if (n == 0 && rbuf == 0) 
       {
           close(fd);
           return NULL;	/* process died between our open and read */
       }
       if (n < 0) 
       {
           if (rbuf)
       	free(rbuf);
           close(fd);
           return NULL;	/* read error */
       }
       if (end_of_file && (n == 0 || buf[n-1]))/* last read char not null */
           buf[n++] = '\0';			/* so append null-terminator */
       rbuf = xrealloc(rbuf, tot + n);		/* allocate more memory */
       memcpy(rbuf + tot, buf, n);		/* copy buffer into it */
       tot += n;				/* increment total byte ctr */
       if (end_of_file)
           break;
   }
   close(fd);
   if (n <= 0 && !end_of_file) {
       if (rbuf) free(rbuf);
       return NULL;		/* read error */
   }
   endbuf = rbuf + tot;			/* count space for pointers */
   align = (sizeof(char*)-1) - ((tot + sizeof(char*)-1) & (sizeof(char*)-1));
   for (c = 0, p = rbuf; p < endbuf; p++) {
       if (!*p || *p == '\n')
           c += sizeof(char*);
       if (*p == '\n')
           *p = 0;
   }
   c += sizeof(char*);				/* one extra for NULL term */

   rbuf = xrealloc(rbuf, tot + c + align);	/* make room for ptrs AT END */
   endbuf = rbuf + tot;			/* addr just past data buf */
   q = ret = (char**) (endbuf+align);		/* ==> free(*ret) to dealloc */
   *q++ = p = rbuf;				/* point ptrs to the strings */
   endbuf--;					/* do not traverse final NUL */
   while (++p < endbuf)
   	if (!*p)				/* NUL char implies that */
           *q++ = p+1;				/* next string -> next char */

   *q = 0;					/* null ptr list terminator */
   return ret;
}

    // this is the former under utilized 'read_cmdline', which has been
    // generalized in support of these new libproc flags:
    //     PROC_EDITCGRPCVT, PROC_EDITCMDLCVT and PROC_EDITENVRCVT
static int read_unvectored(char *restrict const dst, unsigned sz, const char* whom, const char *what, char sep) {
    char path[PROCPATHLEN];
    int fd;
    unsigned n = 0;

    snprintf(path, sizeof(path), "%s/%s", whom, what);
    fd = open(path, O_RDONLY);
    if(fd==-1) return 0;

    for(;;){
        ssize_t r = read(fd,dst+n,sz-n);
        if(r==-1){
            if(errno==EINTR) continue;
            break;
        }
        n += r;
        if(n==sz) {      // filled the buffer
            --n;         // make room for '\0'
            break;
        }
        if(r==0) break;  // EOF
    }
    close(fd);
    if(n){
        int i=n;
        while(i--)
            if(dst[i]=='\n' || dst[i]=='\0') dst[i]=sep;
        if(dst[n-1]==' ') dst[n-1]='\0';
    }
    dst[n] = '\0';
    return n;
}

static char** vectorize_this_str (const char* src) {
 #define pSZ  (sizeof(char*))
    char *cpy, **vec;
    int adj, tot;

    tot = strlen(src) + 1;                       // prep for our vectors
    adj = (pSZ-1) - ((tot + pSZ-1) & (pSZ-1));   // calc alignment bytes
    cpy = xcalloc(tot + adj + (2 * pSZ));        // get new larger buffer
    snprintf(cpy, tot, "%s", src);               // duplicate their string
    vec = (char**)(cpy + tot + adj);             // prep pointer to pointers
    *vec = cpy;                                  // point 1st vector to string
    *(vec+1) = NULL;                             // null ptr 'list' delimit
    return vec;                                  // ==> free(*vec) to dealloc
 #undef pSZ
}

    // This routine reads a 'cgroup' for the designated proc_t.
    // It is similar to file2strvec except we filter and concatenate
    // the data into a single string represented as a single vector.
static void fill_cgroup_cvt (const char* directory, proc_t *restrict p) {
 #define vMAX ( MAX_BUFSZ - (int)(dst - dst_buffer) )
    char *src, *dst, *grp, *eob;
    int tot, x, whackable_int = MAX_BUFSZ;

    *(dst = dst_buffer) = '\0';                  // empty destination
    tot = read_unvectored(src_buffer, MAX_BUFSZ, directory, "cgroup", '\0');
    for (src = src_buffer, eob = src_buffer + tot; src < eob; src += x) {
        x = 1;                                   // loop assist
        if (!*src) continue;
        x = strlen((grp = src));
        if ('/' == grp[x - 1]) continue;         // skip empty root cgroups
#if 0
        grp += strspn(grp, "0123456789:");       // jump past group number
#endif
        dst += snprintf(dst, vMAX, "%s", (dst > dst_buffer) ? "," : "");
        dst += escape_str(dst, grp, vMAX, &whackable_int);
    }
    p->cgroup = vectorize_this_str(dst_buffer[0] ? dst_buffer : "-");
 #undef vMAX
}

    // This routine reads a 'cmdline' for the designated proc_t, "escapes"
    // the result into a single string represented as a single vector
    // and guarantees the caller a valid proc_t.cmdline pointer.
static void fill_cmdline_cvt (const char* directory, proc_t *restrict p) {
 #define uFLG ( ESC_BRACKETS | ESC_DEFUNCT )
    int whackable_int = MAX_BUFSZ;

    if (read_unvectored(src_buffer, MAX_BUFSZ, directory, "cmdline", ' '))
        escape_str(dst_buffer, src_buffer, MAX_BUFSZ, &whackable_int);
    else
        escape_command(dst_buffer, p, MAX_BUFSZ, &whackable_int, uFLG);
    p->cmdline = vectorize_this_str(dst_buffer);
 #undef uFLG
}

    // This routine reads an 'environ' for the designated proc_t and
    // guarantees the caller a valid proc_t.environ pointer.
static void fill_environ_cvt (const char* directory, proc_t *restrict p) {
    int whackable_int = MAX_BUFSZ;

    dst_buffer[0] = '\0';
    if (read_unvectored(src_buffer, MAX_BUFSZ, directory, "environ", ' '))
        escape_str(dst_buffer, src_buffer, MAX_BUFSZ, &whackable_int);
    p->environ = vectorize_this_str(dst_buffer[0] ? dst_buffer : "-");
}

// warning: interface may change
int read_cmdline(char *restrict const dst, unsigned sz, unsigned pid) 
{
   char path[PROCPATHLEN];
   snprintf(path, sizeof(path), "/proc/%u", pid);
   return read_unvectored(dst, sz, path, "cmdline", ' ');
}


/* These are some nice GNU C expression subscope "inline" functions.
 * The can be used with arbitrary types and evaluate their arguments
 * exactly once.
 */

/* Test if item X of type T is present in the 0 terminated list L */
#   define XinL(T, X, L) ( {			\
	    T  x = (X), *l = (L);		\
	    while (*l && *l != x) l++;		\
	    *l == x;				\
	} )

/* Test if item X of type T is present in the list L of length N */
#   define XinLN(T, X, L, N) ( {		\
	    T x = (X), *l = (L);		\
	    int i = 0, n = (N);			\
	    while (i < n && l[i] != x) i++;	\
	    i < n && l[i] == x;			\
	} )


#ifdef BUILD_4_STOCK

static void totals2proc(const char* S, proc_t *restrict P) 
{
#ifdef LOG2STDOUT
   fprintf(LogPtr, "totals data = %s\n", S);
#endif
   float totals;
   
   int num;
   num = sscanf(S, "%f", &totals);
#ifdef LOG2STDOUT
   fprintf(LogPtr, "totals = %.3f\n", totals);
   fflush(LogPtr);
#endif

   P->totals = totals;
}

static void liutong2proc(const char* S, proc_t *restrict P) 
{
#ifdef LOG2STDOUT
   fprintf(LogPtr, "liutongA data = %s\n", S);
#endif
   float liutongA;
   
   int num;
   num = sscanf(S, "%f", &liutongA);
#ifdef LOG2STDOUT
   fprintf(LogPtr, "liutongA = %.3f\n", liutongA);
   fflush(LogPtr);
#endif

   P->liutongA = liutongA;
}

static void SMA2toproc(const char* S, proc_t *restrict P)
{
   float ma5, ma10, ma20, ma30, ma60;
#ifdef LOG2STDOUT
   fprintf(LogPtr, "SMA = %s\n", S);
#endif
/*
   int num = sscanf(S, "%*[^,],%f,%f,%f,%f,%f", &ma5, &ma10, &ma20, &ma30, &ma60);

#ifdef LOG2STDOUT
   fprintf(LogPtr, "ma5 = %.2f, ma10 = %.2f, ma20 = %.2f, ma30 = %.2f, ma60 = %.2f\n", ma5, ma10, ma20, ma30, ma60);
   fflush(LogPtr);
#endif
   P->ma5 = ma5*100;
   P->ma10 = ma10*100;
   P->ma20 = ma20*100;
   P->ma30 = ma30*100;
   P->ma60 = ma60*100;
*/
}

static void cash2toproc(const char* S, proc_t *restrict P)
{
   float ma5, ma10, ma20, ma30, ma60;
   float main_in_cash;
   float main_out_cash;
   float main_net_cash;
   float main_net_rate;
   float private_in_cash;
   float private_out_cash;
   float private_net_cash;
   float private_net_rate;
   float total_cash;

#ifdef LOG2STDOUT
   fprintf(LogPtr, "cash = %s\n", S);
#endif
   int num = sscanf(S, "%*[^,],%f,%f,%f,%f,%f,%f,%f,%f,%f", 
                    &main_in_cash, 
                    &main_out_cash, 
                    &main_net_cash, 
                    &main_net_rate,
                    &private_in_cash,
                    &private_out_cash, 
                    &private_net_cash, 
                    &private_net_rate,
                    &total_cash);

#ifdef LOG2STDOUT
   fprintf(LogPtr, "main_in_cash = %.2f, main_out_cash = %.2f, main_net_cash = %.2f, main_net_rate = %.2f, private_in_cash = %.2f, private_out_cash = %.2f, private_net_cash = %.2f, private_net_rate = %.2f, total_cash = %.2f\n", 
                    main_in_cash, 
                    main_out_cash, 
                    main_net_cash, 
                    main_net_rate,
                    private_in_cash,
                    private_out_cash, 
                    private_net_cash, 
                    private_net_rate,
                    total_cash);
   fflush(LogPtr);
#endif
   P->main_in_cash        =       main_in_cash;
   P->main_out_cash       =       main_out_cash;
   P->main_net_cash       =       main_net_cash;
   P->main_net_rate       =       main_net_rate;
   P->private_in_cash     =       private_in_cash;
   P->private_out_cash    =       private_out_cash;
   P->private_net_cash    =       private_net_cash;
   P->private_net_rate    =       private_net_rate;
   P->total_cash          =       total_cash;
}

static void MA2toproc(const char* S, proc_t *restrict P)
{
   float ma5, ma10, ma20, ma30, ma60;
#ifdef LOG2STDOUT
   fprintf(LogPtr, "MAt = %s\n", S);
#endif
   int num = sscanf(S, "%*[^,],%f,%f,%f,%f,%f", &ma5, &ma10, &ma20, &ma30, &ma60);

#ifdef LOG2STDOUT
   fprintf(LogPtr, "ma5 = %.2f, ma10 = %.2f, ma20 = %.2f, ma30 = %.2f, ma60 = %.2f\n", ma5, ma10, ma20, ma30, ma60);
   fflush(LogPtr);
#endif
   P->ma5 = ma5*FACTOR;
   P->ma10 = ma10*FACTOR;
   P->ma20 = ma20*FACTOR;
   P->ma30 = ma30*FACTOR;
   P->ma60 = ma60*FACTOR;
}

static void price_A2toproc(const char* S, proc_t *restrict P)
{
   float vratio;
   float swing;
   long long outer;
   long long inner;
   float average_price;
   float shiyinglv;
#ifdef LOG2STDOUT
   //fprintf(LogPtr, "from sohu : price_A2 = %s\n", S);
#endif
   int num = sscanf(S, "price_A2':['%f',%*[^,],%*[^,],%*[^,],'%f',%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],'%f%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],'%lld','%lld'", &average_price, &vratio, &swing, &outer, &inner);
   num = sscanf(S, "price_A2':[%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],'%f',", &shiyinglv);

#ifdef LOG2STDOUT
   fprintf(LogPtr, "average = %.2f, vratio = %.2f, swing = %.2f\%, ,shiyinglv = %.2f, outer = %lld, innter = %lld\n", average_price, vratio, swing, shiyinglv, outer, inner);
   fflush(LogPtr);
#endif
   P->volume_ratio = vratio*1000;
   P->swing = swing*1000;
   P->outer = outer;
   P->inner = inner;
   P->average_price = average_price*FACTOR;
   P->pe_ratio = shiyinglv*1000;
}

static void ddr2proc(const char* S, proc_t *restrict P) 
{
#ifdef LOG2STDOUT
   fprintf(LogPtr, "ddr data = %s\n", S);
#endif
   long long ddbuy;
   long long ddsell;
   
   int num;
   num = sscanf(S, "%lld,%lld", 
                &ddbuy,
                &ddsell);

   P->ddbuy = ddbuy;
   P->ddsell = ddsell;
#ifdef LOG2STDOUT
   fprintf(LogPtr, "ddbuy = %lld, ddsell = %lld, volume = %lld\n", ddbuy, ddsell, P->volume);
   fflush(LogPtr);
#endif
}

static void realtime_data2proc(const char* S, proc_t *restrict P) 
{
#ifdef LOG2STDOUT
   fprintf(LogPtr, "original data = %s\n", S);
#endif
    //stat2proc(ub.buf, p);  // parse /proc/self/stat
//    char name[100];
//    char *p = strchr(ub.buf,',')+1;
//    printf("p = %s\n", p);
//    int nn = p-ub.buf;
//    printf(" nn = %d\n", nn);
//    memcpy(name, ub.buf, nn-1);
//    name[nn-1] = '\0';
    
//    float close;
//    float current;
//    int num;
//    num = sscanf(ub.buf+nn, "%f,%f",&close,&current);
//    printf("name = %s| close = %2.2f| current = %2.2f\n", name, close, current);
   char stock_id[288];
   char location[10];
   char stock_name[40];
   float pre_close;
   float open_today;
   float current_price;
   float max;
   float min;
   float buy1;
   float sell1;
   long long volume;
   long long RMB;
   
   int num;
   num = sscanf(S, "%*s hq_str_%2s%[^=]=\"%[^,],%f,%f,%f,%f,%f,%f,%f,%lld,%lld", 
                location, 
                stock_id, 
                stock_name, 
                &open_today,
                &pre_close, 
                &current_price,
                &max,
                &min,
                &buy1,
                &sell1,
                &volume,
                &RMB);

   int year, month, day;
   int hour, minute, sec;

   num = sscanf(S, "%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%d-%d-%d,%d:%d:%d", &year, &month, &day, &hour, &minute, &sec);

#ifdef LOG2STDOUT
   fprintf(LogPtr, "year = %d, month = %d, day = %d, hour = %d, min = %d, sec = %d\n", year, month, day, hour, minute, sec);
#endif

#ifdef LOG2STDOUT
   fprintf(LogPtr, "stock_id = %s, location = %s, stock_name = %s, open_today = %3.2f, pre_close = %3.2f, current_price = %3.2f, max = %3.2f, min = %3.2f, volume = %lld, RMB = %lld\n", stock_id, location, stock_name, open_today, pre_close, current_price, max, min, volume, RMB);
   fflush(LogPtr);
#endif
   P->year = 0;
   P->month = 0;
   P->day = 0;
   P->hour = 0;
   P->minute = 0;
   P->second = 0;
   strcpy(P->stockname, stock_name);
   P->open_today = FACTOR*open_today;
   P->pre_close = FACTOR*pre_close;
   P->current_price = FACTOR*current_price;
   P->max = FACTOR*max;
   P->min = FACTOR*min;
   P->volume = volume;
   P->RMB = RMB;
   P->sort_RMB = RMB/88;
   P->year = year;
   P->month = month;
   P->day = day;
   P->hour = hour;
   P->minute = minute;
   P->second = sec;
}

static int fileExist(const char *directory, const char *what) 
{
 #define buffGRW 1024
   char path[PROCPATHLEN];
   int fd;

   sprintf(path, "%s/%s", directory, what);
   if (-1 == (fd = open(path, O_RDONLY, 0))) return -1;
   close(fd);
   return 1;
 #undef buffGRW
}

static proc_t* read_stock_data(PROCTAB *restrict const PT, proc_t *restrict const p) 
{
   static struct utlbuf_s ub = { NULL, 0 };    // buf for stat,statm,status
   static struct stat sb;     // stat() buffer
   char *restrict const path = PT->path;
   unsigned flags = PT->flags;

#ifdef LOG2STDOUT
   fprintf(LogPtr, "%s path = %s, flags = %i\n", __FUNCTION__, path, flags);
   fflush(LogPtr);
#endif

   //printf("loading %s\n", path);
   if (unlikely(stat(path, &sb) == -1))        /* no such dirent (anymore) */
      goto next_proc;

   if ((flags & PROC_UID) && !XinLN(uid_t, sb.st_uid, PT->uids, PT->nuid))
      goto next_proc;                 /* not one of the requested uids */
   if (unlikely(file2str(path, "realtime", &ub) == -1))
      goto next_proc;
   strcpy(p->stockname, "");
   realtime_data2proc(ub.buf, p);

   if (unlikely(file2str(path, "totals", &ub) != -1))
   {
      totals2proc(ub.buf, p);
   }
   else
   {
      p->totals = 0;
   }
   p->sort_totals = (long long) p->totals*10;

   if (unlikely(file2str(path, "liutong", &ub) != -1))
   {
      liutong2proc(ub.buf, p);
   }
   else
   {
      p->liutongA = 0;
   }
   p->sort_liutongA = (long long) p->liutongA*10;

   if (unlikely(file2str(path, "A2", &ub) != -1))
   {
      price_A2toproc(ub.buf, p);   
   }
   if (unlikely(file2str(path, "MAt", &ub) != -1))
   {
      MA2toproc(ub.buf, p);   
   }
   if (unlikely(file2str(path, "cash", &ub) != -1))
   {
      cash2toproc(ub.buf, p);   
   }
   else
   {
      p->main_in_cash        = 0; 
      p->main_out_cash       = 0; 
      p->main_net_cash       = 0; 
      p->main_net_rate       = 0; 
      p->private_in_cash     = 0; 
      p->private_out_cash    = 0; 
      p->private_net_cash    = 0; 
      p->private_net_rate    = 0; 
      p->total_cash          = 0; 
   }
   if (unlikely(file2str(path, "ddr", &ub) != -1))
   {
      ddr2proc(ub.buf, p);   
   }
   else
   {
      p->ddbuy = p->ddsell = 0;
   }

   if (unlikely(fileExist(path, "dzx_akdaily") != -1))
   {
    #define buffGRW 1024
      char tpath[PROCPATHLEN];
      sprintf(tpath, "%s/%s", path, "dzx_akdaily");
      FILE *fp;
      fp = fopen(tpath, "r");
      if (fp) 
      {
         char cc[16]; 
         if (1 == fscanf(fp, "%16s\n" , cc))
         {
#ifdef LOG2STDOUT
            fprintf(LogPtr, "dzx_akdaily = %s\n", cc);
            fflush(LogPtr);
#endif
            if (strcmp(cc, "Not") == 0)
               p->dzx_G_daily = 0;
            else
               p->dzx_G_daily = 1;
         }
         fclose(fp);
      }
    #undef buffGRW
   }
   else
   {
      p->dzx_G_daily = 0;
   }

   if (unlikely(fileExist(path, "dzx_60_minutes") != -1))
   {
    #define buffGRW 1024
      char tpath[PROCPATHLEN];
      sprintf(tpath, "%s/%s", path, "dzx_60_minutes");
      FILE *fp;
      fp = fopen(tpath, "r");
      if (fp) 
      {
         char cc[16]; 
         if (1 == fscanf(fp, "%16s\n" , cc))
         {
#ifdef LOG2STDOUT
            fprintf(LogPtr, "dzx_60_minutes = %s\n", cc);
            fflush(LogPtr);
#endif
            if (strcmp(cc, "Not") == 0)
               p->dzx_G_60 = 0;
            else
               p->dzx_G_60 = 1;
         }
         fclose(fp);
      }
    #undef buffGRW
   }
   else
   {
      p->dzx_G_60 = 0;
   }

   if (unlikely(fileExist(path, "rsi_60_minutes") != -1))
   {
    #define buffGRW 1024
      char tpath[PROCPATHLEN];
      sprintf(tpath, "%s/%s", path, "rsi_60_minutes");
      FILE *fp;
      fp = fopen(tpath, "r");
      if (fp) 
      {
         char cc[16]; 
         if (1 == fscanf(fp, "%16s\n" , cc))
         {
#ifdef LOG2STDOUT
            fprintf(LogPtr, "rsi_60 = %s\n", cc);
            fflush(LogPtr);
#endif
            if (strcmp(cc, "Not") == 0)
               p->rsi_G_60 = 0;
            else
               p->rsi_G_60 = 1;
         }
         fclose(fp);
      }
    #undef buffGRW
   }
   else
   {
      p->rsi_G_60 = 0;
   }

   if (unlikely(fileExist(path, "rsi_akdaily") != -1))
   {
    #define buffGRW 1024
      char tpath[PROCPATHLEN];
      sprintf(tpath, "%s/%s", path, "rsi_akdaily");
      FILE *fp;
      fp = fopen(tpath, "r");
      if (fp) 
      {
         char cc[16]; 
         if (1 == fscanf(fp, "%16s\n" , cc))
         {
#ifdef LOG2STDOUT
            fprintf(LogPtr, "rsi_akdaily = %s\n", cc);
            fflush(LogPtr);
#endif
            if (strcmp(cc, "Not") == 0)
               p->rsi_G_daily = 0;
            else
               p->rsi_G_daily = 1;
         }
         fclose(fp);
      }
    #undef buffGRW
   }
   else
   {
      p->rsi_G_daily = 0;
   }

   if (unlikely(fileExist(path, "kdj_akdaily") != -1))
   {
    #define buffGRW 1024
      char tpath[PROCPATHLEN];
      sprintf(tpath, "%s/%s", path, "kdj_akdaily");
      FILE *fp;
      fp = fopen(tpath, "r");
      if (fp) 
      {
         char cc[16]; 
         if (1 == fscanf(fp, "%16s\n" , cc))
         {
#ifdef LOG2STDOUT
            fprintf(LogPtr, "kdj_akdaily = %s\n", cc);
            fflush(LogPtr);
#endif
            if (strcmp(cc, "Not") == 0)
               p->kdj_G_daily = 0;
            else
               p->kdj_G_daily = 1;
            char ttt[100];
            if (1 == fscanf(fp,"%100s\n",ttt))
            {
               float diff;
               float dea;
               char macds[20];
               float macd;
               int num;
               diff = dea = macd = 0.0;
               p->kdj_daily_j = p->kdj_daily_k = 0.0; 
#ifdef LOG2STDOUT
               fprintf(LogPtr, "ttt = %s\n", ttt);
               fflush(LogPtr);
#endif
           
               num =  sscanf(ttt, "%*[^,],%f,%f", &diff, &dea);
               p->kdj_daily_j = diff;
               p->kdj_daily_k = dea;
/*
               if (strcmp(macds,"nan") == 0)
               {
                  p->macd_daily = (-1)*8.88;
               }
               else
               {
                  num =  sscanf(ttt, "%*[^,],%f,%f,%f", &diff, &dea, &macd);
                  p->macd_daily = macd;
               }
*/
#ifdef LOG2STDOUT
               fprintf(LogPtr, "kdj_daily_j = %f, kdj_daily_k = %f\n", p->kdj_daily_j, p->kdj_daily_k);
               fflush(LogPtr);
#endif
            }
            else
            {
               p->kdj_daily_j = (-1)*1111;
               p->kdj_daily_k = (-1)*1111;
            }
         }
         fclose(fp);
      }
    #undef buffGRW
   }
   else
   {
      p->kdj_G_daily = 0;
   }
   if (unlikely(fileExist(path, "boll_60_minutes") != -1))
   {
    #define buffGRW 1024
      char tpath[PROCPATHLEN];
      sprintf(tpath, "%s/%s", path, "boll_60_minutes");
      FILE *fp;
      fp = fopen(tpath, "r");
      if (fp) 
      {
         char ttt[100];
         if (1 == fscanf(fp,"%100s\n",ttt))
         {
            float diff;
            float dea;
            char macds[20];
            float macd;
            int num;
            diff = dea = macd = 0.0;
#ifdef LOG2STDOUT
            fprintf(LogPtr, "boll_60 ttt = %s\n", ttt);
            fflush(LogPtr);
#endif
            num =  sscanf(ttt, "%*[^,],%f,%f,%s", &diff, &dea, &macds);
            if (strcmp(macds,"nan") == 0)
            {
               p->boll_60_upper = 0;
               p->boll_60_middle = 0;
               p->boll_60_lower = 0;
            }
            else
            {
               num =  sscanf(ttt, "%*[^,],%f,%f,%f", &diff, &dea, &macd);
               p->boll_60_upper = diff*FACTOR;
               p->boll_60_middle = dea*FACTOR;
               p->boll_60_lower = macd*FACTOR;
            }
#ifdef LOG2STDOUT
            fprintf(LogPtr, "boll_60 = %.3f,%.3f,%.3f\n", (float)(p->boll_60_upper)/FACTOR, 
                                                         (float)(p->boll_60_middle)/FACTOR, 
                                                         (float)(p->boll_60_lower)/FACTOR);
            fflush(LogPtr);
#endif
         }
         else
         {
            p->boll_60_upper = 0;
            p->boll_60_middle = 0;
            p->boll_60_lower = 0;
         }
         fclose(fp);
      }
    #undef buffGRW
   }
   else
   {
      p->boll_60_upper = 0;
      p->boll_60_middle = 0;
      p->boll_60_lower = 0;
   }

   if (unlikely(fileExist(path, "boll_akdaily") != -1))
   {
    #define buffGRW 1024
      char tpath[PROCPATHLEN];
      sprintf(tpath, "%s/%s", path, "boll_akdaily");
      FILE *fp;
      fp = fopen(tpath, "r");
      if (fp) 
      {
         char ttt[100];
         if (1 == fscanf(fp,"%100s\n",ttt))
         {
            float diff;
            float dea;
            char macds[20];
            float macd;
            int num;
            diff = dea = macd = 0.0;
#ifdef LOG2STDOUT
            fprintf(LogPtr, "boll_akdaily ttt = %s\n", ttt);
            fflush(LogPtr);
#endif
            num =  sscanf(ttt, "%*[^,],%f,%f,%s", &diff, &dea, &macds);
            if (strcmp(macds,"nan") == 0)
            {
               p->boll_daily_upper = 0;
               p->boll_daily_middle = 0;
               p->boll_daily_lower = 0;
            }
            else
            {
               num =  sscanf(ttt, "%*[^,],%f,%f,%f", &diff, &dea, &macd);
               p->boll_daily_upper = diff*FACTOR;
               p->boll_daily_middle = dea*FACTOR;
               p->boll_daily_lower = macd*FACTOR;
            }
#ifdef LOG2STDOUT
            fprintf(LogPtr, "boll_akdaily = %.3f,%.3f,%.3f\n", (float)(p->boll_daily_upper)/FACTOR, 
                                                         (float)(p->boll_daily_middle)/FACTOR, 
                                                         (float)(p->boll_daily_lower)/FACTOR);
            fflush(LogPtr);
#endif
         }
         else
         {
            p->boll_daily_upper = 0;
            p->boll_daily_middle = 0;
            p->boll_daily_lower = 0;
         }
         fclose(fp);
      }
    #undef buffGRW
   }
   else
   {
      p->boll_daily_upper = 0;
      p->boll_daily_middle = 0;
      p->boll_daily_lower = 0;
   }
   if (unlikely(fileExist(path, "macd_akdaily") != -1))
   {
    #define buffGRW 1024
      char tpath[PROCPATHLEN];
      sprintf(tpath, "%s/%s", path, "macd_akdaily");
      FILE *fp;
      fp = fopen(tpath, "r");
      if (fp) 
      {
         char cc[16]; 
         if (1 == fscanf(fp, "%16s\n" , cc))
         {
#ifdef LOG2STDOUT
            fprintf(LogPtr, "macd_akdaily = %s\n", cc);
            fflush(LogPtr);
#endif
            if (strcmp(cc, "Not") == 0)
               p->macd_G_daily = 0;
            else
               p->macd_G_daily = 1;
            char ttt[100];
            if (1 == fscanf(fp,"%100s\n",ttt))
            {
               float diff;
               float dea;
               char macds[20];
               float macd;
               int num;
               diff = dea = macd = 0.0;
#ifdef LOG2STDOUT
               fprintf(LogPtr, "ttt = %s\n", ttt);
               fflush(LogPtr);
#endif
           
               num =  sscanf(ttt, "%*[^,],%f,%f,%s", &diff, &dea, &macds);
               if (strcmp(macds,"nan") == 0)
               {
                  p->macd_daily = (-1)*8.88;
                  p->diff_daily = -8888888.88;
                  p->dea_daily = -8888888.88;
               }
               else
               {
                  num =  sscanf(ttt, "%*[^,],%f,%f,%f", &diff, &dea, &macd);
                  p->macd_daily = macd;
                  p->diff_daily = diff;
                  p->dea_daily = dea;
               }
#ifdef LOG2STDOUT
               fprintf(LogPtr, "macd_akdaily = %f,%f,%f\n", p->diff_daily, p->dea_daily, p->macd_daily);
               fflush(LogPtr);
#endif
            }
            else
            {
               p->macd_daily = (-1)*8.88;
               p->diff_daily = -8888888.88;
               p->dea_daily = -8888888.88;
            }
         }
         fclose(fp);
      }
    #undef buffGRW
   }
   else
   {
      p->macd_G_daily = 0;
   }
   if (unlikely(fileExist(path, "bbi_akdaily") != -1))
   {
    #define buffGRW 1024
      char tpath[PROCPATHLEN];
      sprintf(tpath, "%s/%s", path, "bbi_akdaily");
      FILE *fp;
      fp = fopen(tpath, "r");
      if (fp) 
      {
         {
            char ttt[100];
            if (1 == fscanf(fp,"%100s\n",ttt))
            {
                float diff;
                float dea;
                char macds[20];
                float macd;
                int num;
                diff = dea = macd = 0.0;
#ifdef LOG2STDOUT
                fprintf(LogPtr, "bbi_daily = %s\n", ttt);
                fflush(LogPtr);
#endif
                num =  sscanf(ttt, "%*[^,],%s", &macds);
                if (strcmp(macds,"nan") == 0)
                {
                   p->bbi_daily = 0;
                }
                else
                {
                   num =  sscanf(ttt, "%*[^,],%f", &macd);
                   p->bbi_daily = macd*FACTOR;
                }
#ifdef LOG2STDOUT
                fprintf(LogPtr, "bbi_daily = %.3f\n", ((float)(p->bbi_daily)/FACTOR));
                fflush(LogPtr);
#endif
            }
            else
            {
               p->bbi_daily = 0;
            }
         }
         fclose(fp);
      }
    #undef buffGRW
   }
   if (unlikely(fileExist(path, "bbi_60_minutes") != -1))
   {
    #define buffGRW 1024
      char tpath[PROCPATHLEN];
      sprintf(tpath, "%s/%s", path, "bbi_60_minutes");
      FILE *fp;
      fp = fopen(tpath, "r");
      if (fp) 
      {
         {
            char ttt[100];
            if (1 == fscanf(fp,"%100s\n",ttt))
            {
                float diff;
                float dea;
                char macds[20];
                float macd;
                int num;
                diff = dea = macd = 0.0;
#ifdef LOG2STDOUT
                fprintf(LogPtr, "bbi_60_min = %s\n", ttt);
                fflush(LogPtr);
#endif
                num =  sscanf(ttt, "%*[^,],%s", &macds);
                if (strcmp(macds,"nan") == 0)
                {
                   p->bbi_60_min = 0;
                }
                else
                {
                   num =  sscanf(ttt, "%*[^,],%f", &macd);
                   p->bbi_60_min = macd*FACTOR;
                }
#ifdef LOG2STDOUT
                fprintf(LogPtr, "bbi_60_min = %.3f\n", ((float)(p->bbi_60_min)/FACTOR));
                fflush(LogPtr);
#endif
            }
            else
            {
               p->bbi_60_min = 0;
            }
         }
         fclose(fp);
      }
    #undef buffGRW
   }
   if (unlikely(fileExist(path, "kdj_60_minutes") != -1))
   {
    #define buffGRW 1024
      char tpath[PROCPATHLEN];
      sprintf(tpath, "%s/%s", path, "kdj_60_minutes");
      FILE *fp;
      fp = fopen(tpath, "r");
      if (fp) 
      {
         char cc[16]; 
         if (1 == fscanf(fp, "%16s\n" , cc))
         {
#ifdef LOG2STDOUT
            fprintf(LogPtr, "kdj_60 = %s\n", cc);
            fflush(LogPtr);
#endif
            if (strcmp(cc, "Not") == 0)
               p->kdj_G_60 = 0;
            else
               p->kdj_G_60 = 1;
            char ttt[100];
            if (1 == fscanf(fp,"%100s\n",ttt))
            {
               float diff;
               float dea;
               char macds[20];
               float macd;
               int num;
               diff = dea = macd = 0.0;
               p->kdj_60_j = p->kdj_60_k = 0.0; 
#ifdef LOG2STDOUT
               fprintf(LogPtr, "ttt = %s\n", ttt);
               fflush(LogPtr);
#endif
           
               num =  sscanf(ttt, "%*[^,],%f,%f", &diff, &dea);
               p->kdj_60_j = diff;
               p->kdj_60_k = dea;
/*
               if (strcmp(macds,"nan") == 0)
               {
                  p->macd_60 = (-1)*8.88;
               }
               else
               {
                  num =  sscanf(ttt, "%*[^,],%f,%f,%f", &diff, &dea, &macd);
                  p->macd_60 = macd;
               }
*/
#ifdef LOG2STDOUT
               fprintf(LogPtr, "kdj_60_j = %f, kdj_60_k = %f\n", p->kdj_60_j, p->kdj_60_k);
               fflush(LogPtr);
#endif
            }
            else
            {
               p->kdj_60_j = (-1)*1111;
               p->kdj_60_k = (-1)*1111;
            }
         }
         fclose(fp);
      }
    #undef buffGRW
   }
   else
   {
      p->kdj_G_60 = 0;
   }
   if (unlikely(fileExist(path, "macd_60_minutes") != -1))
   {
    #define buffGRW 1024
      char tpath[PROCPATHLEN];
      sprintf(tpath, "%s/%s", path, "macd_60_minutes");
      FILE *fp;
      fp = fopen(tpath, "r");
      if (fp) 
      {
         char cc[16]; 
         if (1 == fscanf(fp, "%16s\n" , cc))
         {
#ifdef LOG2STDOUT
            fprintf(LogPtr, "macd_60_minutes = %s\n", cc);
            fflush(LogPtr);
#endif
            if (strcmp(cc, "Not") == 0)
               p->macd_G_60 = 0;
            else
               p->macd_G_60 = 1;
            char ttt[100];
            if (1 == fscanf(fp,"%100s\n",ttt))
            {
               float diff;
               float dea;
               char macds[20];
               float macd;
               int num;
               diff = dea = macd = 0.0;
#ifdef LOG2STDOUT
               fprintf(LogPtr, "ttt = %s\n", ttt);
               fflush(LogPtr);
#endif
           
               num =  sscanf(ttt, "%*[^,],%f,%f,%s", &diff, &dea, &macds);
               if (strcmp(macds,"nan") == 0)
               {
                  p->macd_60 = (-1)*8.88;
                  p->diff_60 = -8888888.88;
                  p->dea_60 = -8888888.88;
               }
               else
               {
                  num =  sscanf(ttt, "%*[^,],%f,%f,%f", &diff, &dea, &macd);
                  p->macd_60 = macd;
                  p->diff_60 = diff;
                  p->dea_60 = dea;
               }
#ifdef LOG2STDOUT
               fprintf(LogPtr, "macd_60_minutes = %f,%f,%f\n", p->diff_60, p->dea_60, p->macd_60);
               fflush(LogPtr);
#endif
            }
            else
            {
               p->macd_60 = (-1)*8.88;
               p->diff_60 = -8888888.88;
               p->dea_60 = -8888888.88;
            }
         }
         fclose(fp);
      }
    #undef buffGRW
   }
   else
   {
      p->macd_G_60 = 0;
   }
//   if (unlikely(file2str(path, "SMA", &ub, 1) != -1))
//   {
//      SMA2toproc(ub.buf, p);   
//   }
   return p;

   p->euid = sb.st_uid;                        /* need a way to get real uid */
   p->egid = sb.st_gid;                        /* need a way to get real gid */

   if (flags & PROC_FILLSTAT) 
   {                // read /proc/#/stat
      if (unlikely(file2str(path, "stat", &ub) == -1))
         goto next_proc;
      stat2proc(ub.buf, p);
   }

   if (flags & PROC_FILLMEM) {                 // read /proc/#/statm
      if (likely(file2str(path, "statm", &ub) != -1))
            statm2proc(ub.buf, p);
   }

   if (flags & PROC_FILLSTATUS) {              // read /proc/#/status
      if (likely(file2str(path, "status", &ub) != -1)){
         status2proc(ub.buf, p, 1);
         if (flags & PROC_FILLSUPGRP)
            supgrps_from_supgids(p);
      }
   }

    // if multithreaded, some values are crap
   if(p->nlwp > 1){
      p->wchan = (KLONG)~0ull;
   }

    /* some number->text resolving which is time consuming */
   if (flags & PROC_FILLUSR){
      memcpy(p->euser, user_from_uid(p->euid), sizeof p->euser);
      if(flags & PROC_FILLSTATUS) {
         memcpy(p->ruser, user_from_uid(p->ruid), sizeof p->ruser);
         memcpy(p->suser, user_from_uid(p->suid), sizeof p->suser);
         memcpy(p->fuser, user_from_uid(p->fuid), sizeof p->fuser);
      }
   }

    /* some number->text resolving which is time consuming */
   if (flags & PROC_FILLGRP){
      memcpy(p->egroup, group_from_gid(p->egid), sizeof p->egroup);
      if(flags & PROC_FILLSTATUS) {
         memcpy(p->rgroup, group_from_gid(p->rgid), sizeof p->rgroup);
         memcpy(p->sgroup, group_from_gid(p->sgid), sizeof p->sgroup);
         memcpy(p->fgroup, group_from_gid(p->fgid), sizeof p->fgroup);
      }
   }

   if (unlikely(flags & PROC_FILLENV)) {       // read /proc/#/environ
      if (flags & PROC_EDITENVRCVT)
         fill_environ_cvt(path, p);
      else
         p->environ = file2strvec(path, "environ");
   } 
   else
      p->environ = NULL;

   if (flags & (PROC_FILLCOM|PROC_FILLARG)) {  // read /proc/#/cmdline
      if (flags & PROC_EDITCMDLCVT)
         fill_cmdline_cvt(path, p);
      else
         p->cmdline = file2strvec(path, "cmdline");
   } else
       p->cmdline = NULL;

   if ((flags & PROC_FILLCGROUP)) {            // read /proc/#/cgroup
      if (flags & PROC_EDITCGRPCVT)
         fill_cgroup_cvt(path, p);
      else
         p->cgroup = file2strvec(path, "cgroup");
   } 
   else
      p->cgroup = NULL;

#ifdef OOMEM_ENABLE
   if (unlikely(flags & PROC_FILLOOM)) {
      if (likely(file2str(path, "oom_score", &ub) != -1))
         oomscore2proc(ub.buf, p);
      if (likely(file2str(path, "oom_adj", &ub) != -1))
         oomadj2proc(ub.buf, p);
   }
#endif

   if (unlikely(flags & PROC_FILLNS))          // read /proc/#/ns/*
      ns2proc(path, p);

   return p;
next_proc:
   return NULL;
}

#endif

//////////////////////////////////////////////////////////////////////////////////
// This reads process info from /proc in the traditional way, for one process.
// The pid (tgid? tid?) is already in p, and a path to it in path, with some
// room to spare.
static proc_t* simple_readproc(PROCTAB *restrict const PT, proc_t *restrict const p) 
{
    static struct utlbuf_s ub = { NULL, 0 };    // buf for stat,statm,status
    static struct stat sb;     // stat() buffer
    char *restrict const path = PT->path;
    unsigned flags = PT->flags;

#ifdef LOG2STDOUT
  fprintf(LogPtr, "%s path = %s, flags = %i\n", __FUNCTION__, path, flags);
  fflush(LogPtr);
#endif

    if (unlikely(stat(path, &sb) == -1))        /* no such dirent (anymore) */
        goto next_proc;

    if ((flags & PROC_UID) && !XinLN(uid_t, sb.st_uid, PT->uids, PT->nuid))
        goto next_proc;                 /* not one of the requested uids */

    p->euid = sb.st_uid;                        /* need a way to get real uid */
    p->egid = sb.st_gid;                        /* need a way to get real gid */

    if (flags & PROC_FILLSTAT) {                // read /proc/#/stat
        if (unlikely(file2str(path, "stat", &ub) == -1))
            goto next_proc;
        stat2proc(ub.buf, p);
    }

    if (flags & PROC_FILLMEM) {                 // read /proc/#/statm
        if (likely(file2str(path, "statm", &ub) != -1))
            statm2proc(ub.buf, p);
    }

    if (flags & PROC_FILLSTATUS) {              // read /proc/#/status
        if (likely(file2str(path, "status", &ub) != -1)){
            status2proc(ub.buf, p, 1);
            if (flags & PROC_FILLSUPGRP)
                supgrps_from_supgids(p);
        }
    }

    // if multithreaded, some values are crap
    if(p->nlwp > 1){
      p->wchan = (KLONG)~0ull;
    }

    /* some number->text resolving which is time consuming */
    if (flags & PROC_FILLUSR){
        memcpy(p->euser, user_from_uid(p->euid), sizeof p->euser);
        if(flags & PROC_FILLSTATUS) {
            memcpy(p->ruser, user_from_uid(p->ruid), sizeof p->ruser);
            memcpy(p->suser, user_from_uid(p->suid), sizeof p->suser);
            memcpy(p->fuser, user_from_uid(p->fuid), sizeof p->fuser);
        }
    }

    /* some number->text resolving which is time consuming */
    if (flags & PROC_FILLGRP){
        memcpy(p->egroup, group_from_gid(p->egid), sizeof p->egroup);
        if(flags & PROC_FILLSTATUS) {
            memcpy(p->rgroup, group_from_gid(p->rgid), sizeof p->rgroup);
            memcpy(p->sgroup, group_from_gid(p->sgid), sizeof p->sgroup);
            memcpy(p->fgroup, group_from_gid(p->fgid), sizeof p->fgroup);
        }
    }

    if (unlikely(flags & PROC_FILLENV)) {       // read /proc/#/environ
        if (flags & PROC_EDITENVRCVT)
            fill_environ_cvt(path, p);
        else
            p->environ = file2strvec(path, "environ");
    } else
        p->environ = NULL;

    if (flags & (PROC_FILLCOM|PROC_FILLARG)) {  // read /proc/#/cmdline
        if (flags & PROC_EDITCMDLCVT)
            fill_cmdline_cvt(path, p);
        else
            p->cmdline = file2strvec(path, "cmdline");
    } else
        p->cmdline = NULL;

    if ((flags & PROC_FILLCGROUP)) {            // read /proc/#/cgroup
        if (flags & PROC_EDITCGRPCVT)
            fill_cgroup_cvt(path, p);
        else
            p->cgroup = file2strvec(path, "cgroup");
    } else
        p->cgroup = NULL;

#ifdef OOMEM_ENABLE
    if (unlikely(flags & PROC_FILLOOM)) {
        if (likely(file2str(path, "oom_score", &ub) != -1))
            oomscore2proc(ub.buf, p);
        if (likely(file2str(path, "oom_adj", &ub) != -1))
            oomadj2proc(ub.buf, p);
    }
#endif

    if (unlikely(flags & PROC_FILLNS))          // read /proc/#/ns/*
        ns2proc(path, p);

    return p;
next_proc:
    return NULL;
}

//////////////////////////////////////////////////////////////////////////////////
// This reads /proc/*/task/* data, for one task.
#ifdef QUICK_THREADS
// p is the POSIX process (task group summary) & source for some copies if !NULL
#else
// p is the POSIX process (task group summary) (not needed by THIS implementation)
#endif
// t is the POSIX thread (task group member, generally not the leader)
// path is a path to the task, with some room to spare.
static proc_t* simple_readtask(PROCTAB *restrict const PT, const proc_t *restrict const p, proc_t *restrict const t, char *restrict const path) {
    static struct utlbuf_s ub = { NULL, 0 };    // buf for stat,statm,status
    static struct stat sb;     // stat() buffer
    unsigned flags = PT->flags;

    if (unlikely(stat(path, &sb) == -1))        /* no such dirent (anymore) */
        goto next_task;

//  if ((flags & PROC_UID) && !XinLN(uid_t, sb.st_uid, PT->uids, PT->nuid))
//      goto next_task;                         /* not one of the requested uids */

    t->euid = sb.st_uid;                        /* need a way to get real uid */
    t->egid = sb.st_gid;                        /* need a way to get real gid */

    if (flags & PROC_FILLSTAT) {                        // read /proc/#/task/#/stat
        if (unlikely(file2str(path, "stat", &ub) == -1))
            goto next_task;
        stat2proc(ub.buf, t);
    }

#ifndef QUICK_THREADS
    if (flags & PROC_FILLMEM)                           // read /proc/#/task/#statm
        if (likely(file2str(path, "statm", &ub) != -1))
            statm2proc(ub.buf, t);
#endif

    if (flags & PROC_FILLSTATUS) {                      // read /proc/#/task/#/status
        if (likely(file2str(path, "status", &ub) != -1)) {
            status2proc(ub.buf, t, 0);
#ifndef QUICK_THREADS
            if (flags & PROC_FILLSUPGRP)
                supgrps_from_supgids(t);
#endif
        }
    }

    /* some number->text resolving which is time consuming */
    if (flags & PROC_FILLUSR){
        memcpy(t->euser, user_from_uid(t->euid), sizeof t->euser);
        if(flags & PROC_FILLSTATUS) {
            memcpy(t->ruser, user_from_uid(t->ruid), sizeof t->ruser);
            memcpy(t->suser, user_from_uid(t->suid), sizeof t->suser);
            memcpy(t->fuser, user_from_uid(t->fuid), sizeof t->fuser);
        }
    }

    /* some number->text resolving which is time consuming */
    if (flags & PROC_FILLGRP){
        memcpy(t->egroup, group_from_gid(t->egid), sizeof t->egroup);
        if(flags & PROC_FILLSTATUS) {
            memcpy(t->rgroup, group_from_gid(t->rgid), sizeof t->rgroup);
            memcpy(t->sgroup, group_from_gid(t->sgid), sizeof t->sgroup);
            memcpy(t->fgroup, group_from_gid(t->fgid), sizeof t->fgroup);
        }
    }

#ifdef QUICK_THREADS
    if (!p) {
        if (flags & PROC_FILLMEM)
            if (likely(file2str(path, "statm", &ub) != -1))
                statm2proc(ub.buf, t);

        if (flags & PROC_FILLSUPGRP)
            supgrps_from_supgids(t);
#endif
        if (unlikely(flags & PROC_FILLENV)) {           // read /proc/#/task/#/environ
            if (flags & PROC_EDITENVRCVT)
                fill_environ_cvt(path, t);
            else
                t->environ = file2strvec(path, "environ");
        } else
            t->environ = NULL;

        if (flags & (PROC_FILLCOM|PROC_FILLARG)) {      // read /proc/#/task/#/cmdline
            if (flags & PROC_EDITCMDLCVT)
                fill_cmdline_cvt(path, t);
            else
                t->cmdline = file2strvec(path, "cmdline");
        } else
            t->cmdline = NULL;

        if ((flags & PROC_FILLCGROUP)) {                // read /proc/#/task/#/cgroup
            if (flags & PROC_EDITCGRPCVT)
                fill_cgroup_cvt(path, t);
            else
                t->cgroup = file2strvec(path, "cgroup");
        } else
            t->cgroup = NULL;

#ifdef QUICK_THREADS
    } else {
        t->size     = p->size;
        t->resident = p->resident;
        t->share    = p->share;
        t->trs      = p->trs;
        t->lrs      = p->lrs;
        t->drs      = p->drs;
        t->dt       = p->dt;
        t->cmdline  = p->cmdline;  // better not free these until done with all threads!
        t->environ  = p->environ;
        t->cgroup   = p->cgroup;
        if (t->supgid) free(t->supgid);
        t->supgid   = p->supgid;
        t->supgrp   = p->supgrp;
        MK_THREAD(t);
    }
#endif

#ifdef OOMEM_ENABLE
    if (unlikely(flags & PROC_FILLOOM)) {
        if (likely(file2str(path, "oom_score", &ub) != -1))
            oomscore2proc(ub.buf, t);
        if (likely(file2str(path, "oom_adj", &ub) != -1))
            oomadj2proc(ub.buf, t);
    }
#endif

    if (unlikely(flags & PROC_FILLNS))                  // read /proc/#/task/#/ns/*
        ns2proc(path, t);

    return t;
next_task:
    return NULL;
#ifndef QUICK_THREADS
    (void)p;
#endif
}

#ifdef BUILD_4_STOCK
static int simple_next_stock_id(PROCTAB *restrict const PT, proc_t *restrict const p) 
{
   static struct direct *ent;		/* dirent handle */

   char *restrict const path = PT->path;
   for (;;) 
   {
      ent = readdir(PT->data_root_fs);
      if(unlikely(unlikely(!ent) || unlikely(!ent->d_name))) return 0;
      if(likely(likely(*ent->d_name >= '0') && likely(*ent->d_name <= '9'))) break;
   }
// unsigned long strtoul(const char *nptr,char **endptr,int base);
// strtoul()会将参数nptr字符串根据参数base来转换成无符号的长整型数。参数base范围从2至36，或0。参数base代表采用的进制方式，如base值为10则采用10进制，若base值为16则采用16进制数等。当base值为0时会根据情况选择用哪种进制：如果第一个字符是'0'，就判断第二字符如果是‘x’则用16进制，否则用8进制；第一个字符不是‘0’，则用10进制。一开始strtoul()会扫描参数nptr字符串，跳过前面的空格字符串，直到遇上数字或正负符号才开始做转换，再遇到非数字或字符串结束时('')结束转换，并将结果返回。若参数endptr不为NULL，则会将遇到不合条件而终止的nptr中的字符指针由endptr返回。
//   p->tgid = strtoul(ent->d_name, NULL, 10);
//   p->tid = p->tgid;

   p->stockid = strtoul(ent->d_name, NULL, 10);
   strcpy(p->str_stockid, ent->d_name);
//   memcpy(path, "/proc/", 6);
//   strcpy(path+6, ent->d_name);  // trust /proc to not contain evil top-level entries
   strcpy(path, DATADIR);
   strcat(path, "/");
   strcat(path, ent->d_name);
#ifdef LOG2STDOUT
   fprintf(LogPtr, "%s ent->d_name = %s\n", __FUNCTION__, ent->d_name);
   fprintf(LogPtr, "%s p->tid = %d\n", __FUNCTION__, p->tid);
   fprintf(LogPtr, "%s path = %s\n", __FUNCTION__, path); 
   fflush(LogPtr);
#endif
   return 1;
}
#endif

//////////////////////////////////////////////////////////////////////////////////
// This finds processes in /proc in the traditional way.
// Return non-zero on success.
static int simple_nextpid(PROCTAB *restrict const PT, proc_t *restrict const p) 
{
   static struct direct *ent;		/* dirent handle */

   char *restrict const path = PT->path;
   for (;;) 
   {
      ent = readdir(PT->data_root_fs);
      if(unlikely(unlikely(!ent) || unlikely(!ent->d_name))) return 0;
      if(likely(likely(*ent->d_name > '0') && likely(*ent->d_name <= '9'))) break;
   }
// unsigned long strtoul(const char *nptr,char **endptr,int base);
// strtoul()会将参数nptr字符串根据参数base来转换成无符号的长整型数。参数base范围从2至36，或0。参数base代表采用的进制方式，如base值为10则采用10进制，若base值为16则采用16进制数等。当base值为0时会根据情况选择用哪种进制：如果第一个字符是'0'，就判断第二字符如果是‘x’则用16进制，否则用8进制；第一个字符不是‘0’，则用10进制。一开始strtoul()会扫描参数nptr字符串，跳过前面的空格字符串，直到遇上数字或正负符号才开始做转换，再遇到非数字或字符串结束时('')结束转换，并将结果返回。若参数endptr不为NULL，则会将遇到不合条件而终止的nptr中的字符指针由endptr返回。
   p->tgid = strtoul(ent->d_name, NULL, 10);
   p->tid = p->tgid;

   memcpy(path, "/proc/", 6);
   strcpy(path+6, ent->d_name);  // trust /proc to not contain evil top-level entries
#ifdef LOG2STDOUT
   fprintf(LogPtr, "%s ent->d_name = %s\n", __FUNCTION__, ent->d_name);
   fprintf(LogPtr, "%s p->tid = %d\n", __FUNCTION__, p->tid);
   fprintf(LogPtr, "%s path = %s\n", __FUNCTION__, path); 
   fflush(LogPtr);
#endif
   return 1;
}

//////////////////////////////////////////////////////////////////////////////////
// This finds tasks in /proc/*/task/ in the traditional way.
// Return non-zero on success.
static int simple_nexttid(PROCTAB *restrict const PT, const proc_t *restrict const p, proc_t *restrict const t, char *restrict const path) {
  static struct direct *ent;		/* dirent handle */
  if(PT->taskdir_user != p->tgid){
    if(PT->taskdir){
      closedir(PT->taskdir);
    }
    // use "path" as some tmp space
    snprintf(path, PROCPATHLEN, "/proc/%d/task", p->tgid);
    PT->taskdir = opendir(path);
    if(!PT->taskdir) return 0;
    PT->taskdir_user = p->tgid;
  }
  for (;;) {
    ent = readdir(PT->taskdir);
    if(unlikely(unlikely(!ent) || unlikely(!ent->d_name))) return 0;
    if(likely(likely(*ent->d_name > '0') && likely(*ent->d_name <= '9'))) break;
  }
  t->tid = strtoul(ent->d_name, NULL, 10);
  t->tgid = p->tgid;
//t->ppid = p->ppid;  // cover for kernel behavior? we want both actually...?
  snprintf(path, PROCPATHLEN, "/proc/%d/task/%s", p->tgid, ent->d_name);
  return 1;
}
   
#ifdef BUILD_4_STOCK
//////////////////////////////////////////////////////////////////////////////////
// This "finds" processes in a list that was given to openproc().
// Return non-zero on success. (tgid was handy)
static int listed_next_stock_id(PROCTAB *restrict const PT, proc_t *restrict const p) 
{
   char *restrict const path = PT->path;
   pid_t tgid = *(PT->pids)++;
   if(likely(tgid))
   {
      if (tgid < 10)
      {
         snprintf(path, PROCPATHLEN, "%s/00000%d", DATADIR, tgid);
      }
      else if (tgid < 100)
      {
         snprintf(path, PROCPATHLEN, "%s/0000%d", DATADIR, tgid);
      }
      else if (tgid < 1000)
      {
         snprintf(path, PROCPATHLEN, "%s/000%d", DATADIR, tgid);
      }
      else if (tgid < 10000)
      {
         snprintf(path, PROCPATHLEN, "%s/00%d", DATADIR, tgid);
      }
      else
      {
         snprintf(path, PROCPATHLEN, "%s/%d", DATADIR, tgid);
      }
      
      p->stockid = tgid;
      strcpy(p->str_stockid, path+strlen(DATADIR)+1); // they match for leaders
   }
   return tgid;
}
#endif

//////////////////////////////////////////////////////////////////////////////////
// This "finds" processes in a list that was given to openproc().
// Return non-zero on success. (tgid was handy)
static int listed_nextpid(PROCTAB *restrict const PT, proc_t *restrict const p) 
{
   char *restrict const path = PT->path;
   pid_t tgid = *(PT->pids)++;
   if(likely(tgid))
   {
      snprintf(path, PROCPATHLEN, "/proc/%d", tgid);
      p->tgid = tgid;
      p->tid = tgid;  // they match for leaders
   }
   return tgid;
}

//////////////////////////////////////////////////////////////////////////////////
/* readproc: return a pointer to a proc_t filled with requested info about the
 * next process available matching the restriction set.  If no more such
 * processes are available, return a null pointer (boolean false).  Use the
 * passed buffer instead of allocating space if it is non-NULL.  */

/* This is optimized so that if a PID list is given, only those files are
 * searched for in /proc.  If other lists are given in addition to the PID list,
 * the same logic can follow through as for the no-PID list case.  This is
 * fairly complex, but it does try to not to do any unnecessary work.
 */
proc_t* readproc(PROCTAB *restrict const PT, proc_t *restrict p) 
{
   proc_t *ret;
   proc_t *saved_p;

   PT->did_fake=0;
//  if (PT->taskdir) {
//    closedir(PT->taskdir);
//    PT->taskdir = NULL;
//    PT->taskdir_user = -1;
//  }

   saved_p = p;
#ifdef LOG2STDOUT
   if (!p) fprintf(LogPtr, "readproc p = NULL\n"); 
#endif
#ifdef LOG2STDOUT
   if (!p) fprintf(LogPtr, "xcalloc sizeof(*p) (proc_t) = %d\n", sizeof(*p)); 
   fflush(LogPtr);
#endif
// equal to sizeof(struct proc_t)
   if (!p) p = xcalloc(sizeof(*p));
   else free_acquired(p, 1);

   for(;;) 
   {
// within openproc function
//   PT->taskfinder = simple_nexttid;
//   PT->taskreader = simple_readtask;

//#ifdef BUILD_4_STOCK
//   PT->reader = read_stock_data;
//#else
//   PT->reader = simple_readproc;
//#endif
//   if (flags & PROC_PID)
//   {
//      PT->data_root_fs = NULL;
//      PT->finder = listed_nextpid;
//   }
//   else
//   {
//#ifdef BUILD_4_STOCK
//      PT->data_root_fs = opendir(DATADIR);
//#else
//      PT->data_root_fs = opendir("/proc");
//#endif
//      if (!PT->data_root_fs) { free(PT); return NULL; }
//#ifdef BUILD_4_STOCK
//      PT->finder = simple_next_stock_id;
//#else
//      PT->finder = simple_nextpid;
//#endif
//   }
    // fills in the path, plus p->tid and p->tgid
      if (unlikely(!PT->finder(PT,p))) goto out;

    // go read the process data
      ret = PT->reader(PT,p);
      if(ret) return ret;
   }

out:
   if (!saved_p) free(p);// for the last one, free the p unused
  // FIXME: maybe set tid to -1 here, for "-" in display?
#ifdef LOG2STDOUT
   fprintf(LogPtr, "out due to nothing to find\n");
   fflush(LogPtr);
#endif
   return NULL;
}

//////////////////////////////////////////////////////////////////////////////////
// readtask: return a pointer to a proc_t filled with requested info about the
// next task available.  If no more such tasks are available, return a null
// pointer (boolean false).  Use the passed buffer instead of allocating
// space if it is non-NULL.
proc_t* readtask(PROCTAB *restrict const PT, const proc_t *restrict const p, proc_t *restrict t) {
  char path[PROCPATHLEN];    // must hold /proc/2000222000/task/2000222000/cmdline
  proc_t *ret;
  proc_t *saved_t;

  saved_t = t;
  if(!t) t = xcalloc(sizeof *t);
  else free_acquired(t, 1);

  // 1. got to fake a thread for old kernels
#ifdef QUICK_THREADS
  // 2. for single-threaded processes, this is faster (but must patch up stuff that differs!)
  if(task_dir_missing || p->nlwp < 2){
#else
  if(task_dir_missing){
#endif
    if(PT->did_fake) goto out;
    PT->did_fake=1;
    memcpy(t,p,sizeof(proc_t));
    // use the per-task pending, not per-tgid pending
#ifdef SIGNAL_STRING
    memcpy(&t->signal, &t->_sigpnd, sizeof t->signal);
#else
    t->signal = t->_sigpnd;
#endif
#ifdef QUICK_THREADS
    MK_THREAD(t);
#else
    t->environ = NULL;
    t->cmdline = vectorize_this_str("n/a");
    t->cgroup  = NULL;
    t->supgid  = NULL;
    t->supgrp  = NULL;
#endif
    return t;
  }

  for(;;){
    // fills in the path, plus t->tid and t->tgid
    if (unlikely(!PT->taskfinder(PT,p,t,path))) goto out;  // simple_nexttid

    // go read the task data
    ret = PT->taskreader(PT,p,t,path);          // simple_readtask
    if(ret) return ret;
  }

out:
  if(!saved_t) free(t);
  return NULL;
}

//////////////////////////////////////////////////////////////////////////////////
// readeither: return a pointer to a proc_t filled with requested info about
// the next unique process or task available.  If no more are available,
// return a null pointer (boolean false).  Use the passed buffer instead
// of allocating space if it is non-NULL.
proc_t* readeither (PROCTAB *restrict const PT, proc_t *restrict x) {
    static proc_t skel_p;    // skeleton proc_t, only uses tid + tgid
    static proc_t *new_p;    // for process/task transitions
    char path[PROCPATHLEN];
    proc_t *saved_x, *ret;

    saved_x = x;
    if (!x) x = xcalloc(sizeof(*x));
    else free_acquired(x,1);
    if (new_p) goto next_task;

next_proc:
    new_p = NULL;
    for (;;) {
        // fills in the PT->path, plus skel_p.tid and skel_p.tgid
        if (!PT->finder(PT,&skel_p)) goto end_procs;       // simple_nextpid
        if (!task_dir_missing) break;
        if ((ret = PT->reader(PT,x))) return ret;          // simple_readproc
    }

next_task:
    // fills in our path, plus x->tid and x->tgid
    if ((!(PT->taskfinder(PT,&skel_p,x,path)))             // simple_nexttid
    || (!(ret = PT->taskreader(PT,new_p,x,path)))) {       // simple_readtask
        goto next_proc;
    }
    if (!new_p) new_p = ret;
    return ret;

end_procs:
    if (!saved_x) free(x);
    return NULL;
}


//////////////////////////////////////////////////////////////////////////////////

// initiate a process table scan
PROCTAB* openproc(int flags, ...) 
{
   va_list ap;
   struct stat sbuf;
   static int did_stat;
   PROCTAB* PT = xmalloc(sizeof(PROCTAB));

   if (!did_stat)
   {
      task_dir_missing = stat("/proc/self/task", &sbuf);
      did_stat = 1;
   }
   PT->taskdir = NULL;
   PT->taskdir_user = -1;
   PT->taskfinder = simple_nexttid;
   PT->taskreader = simple_readtask;

#ifdef BUILD_4_STOCK
   PT->reader = read_stock_data;
#else
   PT->reader = simple_readproc;
#endif
   if (flags & PROC_PID)
   {
      PT->data_root_fs = NULL;
#ifdef BUILD_4_STOCK
      PT->finder = listed_next_stock_id;
#else
      PT->finder = listed_nextpid;
#endif
   }
   else
   {
#ifdef BUILD_4_STOCK
      PT->data_root_fs = opendir(DATADIR);
#else
      PT->data_root_fs = opendir("/proc");
#endif
      if (!PT->data_root_fs) { free(PT); return NULL; }
#ifdef BUILD_4_STOCK
      PT->finder = simple_next_stock_id;
#else
      PT->finder = simple_nextpid;
#endif
   }
   PT->flags = flags;

   va_start(ap, flags);
   if (flags & PROC_PID)
   {
      PT->pids = va_arg(ap, pid_t*);
   }
   else if (flags & PROC_UID)
   {
      PT->uids = va_arg(ap, uid_t*);
      PT->nuid = va_arg(ap, int);
   }
   va_end(ap);

   if (!src_buffer)
   {
      src_buffer = xmalloc(MAX_BUFSZ);
      dst_buffer = xmalloc(MAX_BUFSZ);
   }
   return PT;
}

// terminate a process table scan
void closeproc(PROCTAB* PT) 
{
   if (PT)
   {
      if (PT->data_root_fs) closedir(PT->data_root_fs);
      if (PT->taskdir) closedir(PT->taskdir);
      memset(PT,'#',sizeof(PROCTAB));
      free(PT);
   }
}

// deallocate space allocated by readproc
void freeproc(proc_t* p) 
{
   if (p) 
   {
      free_acquired(p, 0);
      free(p);
   }
}


//////////////////////////////////////////////////////////////////////////////////
void look_up_our_self(proc_t *p) 
{
   struct utlbuf_s ub = { NULL, 0 };

   if(file2str("/proc/self", "stat", &ub) == -1)
   {
      fprintf(stderr, "Error, do this: mount -t proc proc /proc\n");
      _exit(47);
   }
   stat2proc(ub.buf, p);  // parse /proc/self/stat
   free(ub.buf);
}

HIDDEN_ALIAS(readproc);
HIDDEN_ALIAS(readtask);
HIDDEN_ALIAS(readeither);

/* Convenient wrapper around openproc and readproc to slurp in the whole process
 * table subset satisfying the constraints of flags and the optional PID list.
 * Free allocated memory with exit().  Access via tab[N]->member.  The pointer
 * list is NULL terminated.
 */
proc_t** readproctab(int flags, ...) {
    PROCTAB* PT = NULL;
    proc_t** tab = NULL;
    int n = 0;
    va_list ap;

    va_start(ap, flags);		/* pass through args to openproc */
    if (flags & PROC_UID) {
	/* temporary variables to ensure that va_arg() instances
	 * are called in the right order
	 */
	uid_t* u;
	int i;

	u = va_arg(ap, uid_t*);
	i = va_arg(ap, int);
	PT = openproc(flags, u, i);
    }
    else if (flags & PROC_PID)
	PT = openproc(flags, va_arg(ap, void*)); /* assume ptr sizes same */
    else
	PT = openproc(flags);
    va_end(ap);
    if (!PT)
      return 0;
    do {					/* read table: */
	tab = xrealloc(tab, (n+1)*sizeof(proc_t*));/* realloc as we go, using */
	tab[n] = readproc_direct(PT, NULL);     /* final null to terminate */
    } while (tab[n++]);				  /* stop when NULL reached */
    closeproc(PT);
    return tab;
}

// Try again, this time with threads and selection.
proc_data_t *readproctab2(int(*want_proc)(proc_t *buf), int(*want_task)(proc_t *buf), PROCTAB *restrict const PT) {
    static proc_data_t pd;
    proc_t** ptab = NULL;
    unsigned n_proc_alloc = 0;
    unsigned n_proc = 0;

    proc_t** ttab = NULL;
    unsigned n_task_alloc = 0;
    unsigned n_task = 0;

    proc_t*  data = NULL;
    unsigned num_alloc = 0;
    unsigned long num_used = 0;

    for(;;){
        proc_t *tmp;
        if(num_alloc == num_used){
          //proc_t *old = data;
          num_alloc = num_alloc*5/4+30;  // grow by over 25%
          data = xrealloc(data,sizeof(proc_t)*num_alloc);
          memset(data+num_used, 0, sizeof(proc_t)*(num_alloc-num_used));
        }
        if(n_proc_alloc == n_proc){
          //proc_t **old = ptab;
          n_proc_alloc = n_proc_alloc*5/4+30;  // grow by over 25%
          ptab = xrealloc(ptab,sizeof(proc_t*)*n_proc_alloc);
        }
        tmp = readproc_direct(PT, data+num_used);
        if(!tmp) break;
        if(!want_proc(tmp)) continue;
        ptab[n_proc++] = (proc_t*)(num_used++);
        if(!(  PT->flags & PROC_LOOSE_TASKS  )) continue;
        for(;;){
          proc_t *t;
          if(num_alloc == num_used){
            proc_t *old = data;
            num_alloc = num_alloc*5/4+30;  // grow by over 25%
            data = xrealloc(data,sizeof(proc_t)*num_alloc);
            // have to move tmp too
            tmp = data+(tmp-old);
            memset(data+num_used+1, 0, sizeof(proc_t)*(num_alloc-(num_used+1)));
          }
          if(n_task_alloc == n_task){
            //proc_t **old = ttab;
            n_task_alloc = n_task_alloc*5/4+1;  // grow by over 25%
            ttab = xrealloc(ttab,sizeof(proc_t*)*n_task_alloc);
          }
          t = readtask_direct(PT, tmp, data+num_used);
          if(!t) break;
          if(!want_task(t)) continue;
          ttab[n_task++] = (proc_t*)(num_used++);
        }
    }

    pd.proc  = ptab;
    pd.task  = ttab;
    pd.nproc = n_proc;
    pd.ntask = n_task;
    if(PT->flags & PROC_LOOSE_TASKS){
      pd.tab = ttab;
      pd.n   = n_task;
    }else{
      pd.tab = ptab;
      pd.n   = n_proc;
    }
    // change array indexes to pointers
    while(n_proc--) ptab[n_proc] = data+(long)(ptab[n_proc]);
    while(n_task--) ttab[n_task] = data+(long)(ttab[n_task]);

    return &pd;
}

// Try try yet again, this time treating processes and threads the same...
proc_data_t *readproctab3 (int(*want_task)(proc_t *buf), PROCTAB *restrict const PT) {
    static proc_data_t pd;
    proc_t **tab = NULL;
    unsigned num_alloc = 0;
    unsigned num_used = 0;
    proc_t *p = NULL;

    for (;;) {
        if (num_alloc == num_used) {
            num_alloc = num_alloc*5/4+30;  // grow by over 25%
            tab = xrealloc(tab,sizeof(proc_t*)*num_alloc);
        }
        // let this next guy allocate the necessary proc_t storage
        // (or recycle it) since he can't tolerate realloc relocations
        if (!(p = readeither_direct(PT,p))) break;
        if (want_task(p)) {
            tab[num_used++] = p;
            p = NULL;
        }
    }

    pd.tab = tab;
    pd.n = num_used;
    return &pd;
}

/*
 * get_proc_stats - lookup a single tasks information and fill out a proc_t
 *
 * On failure, returns NULL.  On success, returns 'p' and 'p' is a valid
 * and filled out proc_t structure.
 */
proc_t * get_proc_stats(pid_t pid, proc_t *p) {
    struct utlbuf_s ub = { NULL, 0 };
    static char path[32];
    struct stat statbuf;

    sprintf(path, "/proc/%d", pid);
    if (stat(path, &statbuf)) {
        perror("stat");
        return NULL;
    }

    if (file2str(path, "stat", &ub) >= 0)
        stat2proc(ub.buf, p);
    if (file2str(path, "statm", &ub) >= 0)
        statm2proc(ub.buf, p);
    if (file2str(path, "status", &ub) >= 0)
        status2proc(ub.buf, p, 0);

    free(ub.buf);
    return p;
}

#undef MK_THREAD
#undef IS_THREAD
#undef MAX_BUFSZ
