#include "types.h"
#include "stat.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "proc.h"

#define INODE_BASE  10000  // mkfs NINODES=200,so any number > 200 is fine
struct procfname_handler {
  int (*writedata)(void *, char*);
  char name[DIRSIZ];
};

int write_uptime(void *, char *);
int write_proc_status(void *, char *);

#define FNUM_IN_MAIN_DIR 1 // the number except proc directory
struct procfname_handler maindir[FNUM_IN_MAIN_DIR] = {
  {write_uptime, "uptime"}
};
#define PROC_BASE  (FNUM_IN_MAIN_DIR + INODE_BASE) // the inode num on first proc directory
#define INUMS_PER_PROC 2  // dir and status
struct procfname_handler procdir[INUMS_PER_PROC - 1] = {
  {write_proc_status, "status"}
};
#define IS_MAIN_DIR(inum, major) ((inum < INODE_BASE) && (major == PROCFS))

static int main_dir_inum = 0;

int 
procfsisdir(struct inode *ip) {
  return IS_MAIN_DIR(ip->inum, ip->major) || 
   // In proc's inums and dividable by 3
   (ip->inum >= PROC_BASE && (ip->inum - PROC_BASE) % INUMS_PER_PROC == 0);
}

void 
procfsiread(struct inode *ip) {
  ip->valid = 1; // avoid ilock read inode from disk
  ip->nlink = 1; // avoid iput write inode to disk
  ip->type = T_DEVICE;
  ip->major = PROCFS;
}

int read_main_dir(struct inode *ip, int user_dst, uint64 dst, int off)
{
  struct dirent de;
  int idx = off / sizeof(struct dirent);
  if (idx == 0) {
    memmove(de.name, ".", 2);
    de.inum = ip->inum;
    goto ret;
  } else if (idx == 1) {
    memmove(de.name, "..", 3);
    de.inum = ROOTINO;
    goto ret;
  } 
  else {
    idx -= 2;
  }
  if (idx < FNUM_IN_MAIN_DIR) {
    memmove(de.name, maindir[idx].name, DIRSIZ);
    de.inum = INODE_BASE + idx;
    goto ret;
  } 
  else {
    idx -= FNUM_IN_MAIN_DIR;
    struct proc *p = get_proc_by_idx(idx);
    if (p != NULL) {
      de.inum = INUMS_PER_PROC * p->pid + PROC_BASE;
      int n = snprintf(de.name, DIRSIZ - 1, "%d", p->pid);
      de.name[n] = '\0';
      goto ret;
    }
  }
  return 0;
ret:
  if (either_copyout(user_dst, dst, &de, sizeof(struct dirent)) < 0)
    return -1;
  return sizeof(struct dirent);
}

int read_proc_dir(struct inode *ip, int user_dst, uint64 dst, int off) 
{
  struct dirent de;
  int idx = off / sizeof(struct dirent);
  if (idx == 0) {
    memmove(de.name, ".", 2);
    de.inum = ip->inum;
    goto ret;
  } else if (idx == 1) {
    memmove(de.name, "..", 3);
    if (!main_dir_inum) 
      panic("read_proc_dir");
    de.inum = main_dir_inum;
    goto ret;
  } else {
    idx -= 2;
  }
  if (idx < INUMS_PER_PROC - 1) {
    memmove(de.name, procdir[idx].name, DIRSIZ);
    de.inum = ip->inum + idx + 1;
    goto ret;
  }
  return 0;
ret:
  if (either_copyout(user_dst, dst, &de, sizeof(struct dirent)) < 0)
    return -1;
  return sizeof(struct dirent);  
}

int write_proc_status(void *pp, char *data)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used  ",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p = (struct proc *)pp;
  int len = snprintf(data, 100, "proc pid: %d\n"
                     "state: %s\n"
                     "heap size: %d\n",
                     p->pid,
                     states[p->state],
                     p->tshared->sz);
  data[len] = '\0';                 
  return len;
}

int write_uptime(void *ignore, char *data)
{
  int len = snprintf(data, 20, "uptime: %d ticks\n", ticks);
  data[len] = '\0';
  return len;
}

int
procfsread(struct inode *ip, int user_dst, uint64 dst, int off, int n) {
  if (IS_MAIN_DIR(ip->inum, ip->major)) {
    if (!main_dir_inum)
      main_dir_inum = ip->inum;
    return read_main_dir(ip, user_dst, dst, off);
  }
  char data[300];
  int len = 0;
  // handle proc dir
  if (ip->inum >= PROC_BASE) {
    int idx = (ip->inum - PROC_BASE) % INUMS_PER_PROC;
    int pid = (ip->inum - PROC_BASE) / INUMS_PER_PROC;
    if (idx == 0) {
      return read_proc_dir(ip, user_dst, dst, off);
    } else {
      struct proc *p = get_proc_by_pid(pid);
      if ((len = procdir[idx - 1].writedata(p, data)) < 0)
        return -1;
    }
  } else { // handle node in main dir
    int idx = ip->inum - INODE_BASE;
    if ((len = maindir[idx].writedata(NULL, data)) < 0)
      return -1;
  }

  n = MIN(n, 1 + len - off);
  if (n <= 0) return 0;
  if (either_copyout(user_dst, dst, data + off, n) < 0)
    return -1; 
  return n;
}

int
procfswrite(int user_src, uint64 src, int n)
{
  return -1;
}

void
procfsinit(void)
{
  devsw[PROCFS].isdir = procfsisdir;
  devsw[PROCFS].inode_read = procfsiread;
  devsw[PROCFS].write = procfswrite;
  devsw[PROCFS].read = procfsread;
}