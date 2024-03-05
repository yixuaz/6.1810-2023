#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// Fetch the uint64 at addr from the current process.
int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  if(addr >= p->tshared->sz || addr+sizeof(uint64) > p->tshared->sz) // both tests needed, in case of overflow
    return -1;
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
int
fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  if(copyinstr(p->pagetable, buf, addr, max) < 0)
    return -1;
  return strlen(buf);
}

static uint64
argraw(int n)
{
  struct proc *p = myproc();
  switch (n) {
  case 0:
    return p->trapframe->a0;
  case 1:
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  panic("argraw");
  return -1;
}

// Fetch the nth 32-bit system call argument.
void
argint(int n, int *ip)
{
  *ip = argraw(n);
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
void
argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  argaddr(n, &addr);
  return fetchstr(addr, buf, max);
}

// Prototypes for the functions that handle system calls.
extern uint64 sys_fork(void);
extern uint64 sys_exit(void);
extern uint64 sys_wait(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_kill(void);
extern uint64 sys_exec(void);
extern uint64 sys_fstat(void);
extern uint64 sys_chdir(void);
extern uint64 sys_dup(void);
extern uint64 sys_getpid(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_uptime(void);
extern uint64 sys_open(void);
extern uint64 sys_write(void);
extern uint64 sys_mknod(void);
extern uint64 sys_unlink(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_close(void);
extern uint64 sys_mmap(void);
extern uint64 sys_munmap(void);
extern uint64 sys_svprint(void);
extern uint64 sys_trace(void);
extern uint64 sys_sysinfo(void);
extern uint64 sys_sbrknaive(void);
extern uint64 sys_dirtypages(void);
extern uint64 sys_vmprint(void);
extern uint64 sys_pgaccess(void);
extern uint64 sys_sigalarm(void);
extern uint64 sys_sigreturn(void);
extern uint64 sys_testbacktrace(void);
extern uint64 sys_clone(void);
extern uint64 sys_join(void);
extern uint64 sys_connect(void);
extern uint64 sys_recvfrom(void);
extern uint64 sys_sendto(void);
extern uint64 sys_accept(void);
extern uint64 sys_symlink(void);
extern uint64 sys_sigsend(void);
extern uint64 sys_signal(void);
extern uint64 sys_sigprocmask(void);

// An array mapping syscall numbers from syscall.h
// to the function that handles the system call.
static uint64 (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
[SYS_mmap]    sys_mmap,
[SYS_munmap]  sys_munmap,
[SYS_svprint] sys_svprint,
[SYS_trace]   sys_trace,
[SYS_sysinfo] sys_sysinfo,
[SYS_sbrknaive]    sys_sbrknaive,
[SYS_dirtypages] sys_dirtypages,
[SYS_vmprint] sys_vmprint,
[SYS_pgaccess] sys_pgaccess,
[SYS_sigalarm]   sys_sigalarm,
[SYS_sigreturn]   sys_sigreturn,
[SYS_testbacktrace]   sys_testbacktrace,
[SYS_clone]   sys_clone,
[SYS_join]   sys_join,
[SYS_connect] sys_connect,
[SYS_recvfrom] sys_recvfrom,
[SYS_sendto] sys_sendto,
[SYS_accept] sys_accept,
[SYS_symlink] sys_symlink,
[SYS_sigsend] sys_sigsend,
[SYS_signal] sys_signal,
[SYS_sigprocmask] sys_sigprocmask,
};

char *syscall_names[] = {
  "",
  "fork",
  "exit",
  "wait",
  "pipe",
  "read",
  "kill",
  "exec",
  "fstat",
  "chdir",
  "dup",
  "getpid",
  "sbrk",
  "sleep",
  "uptime",
  "open",
  "write",
  "mknod",
  "unlink",
  "link",
  "mkdir",
  "close",
  "mmap",
  "munmap",
  "svprint",
  "trace",
  "sysinfo",
  "sbrknaive",
  "dirtypages",
  "vmprint",
  "pgaccess",
  "sigalarm",
  "sigreturn",
  "testbacktrace",
  "clone",
  "join",
  "connect",
  "recvfrom",
  "sendto",
  "accept",
  "symlink",
  "sigsend",
  "signal",
  "sigprocmask",
};

int syscall_arg_counts[] = {
  0,  // placeholder for syscall 0
  0,  // fork
  1,  // exit
  1,  // wait
  1,  // pipe
  3,  // read
  1,  // kill
  2,  // exec
  2,  // fstat
  1,  // chdir
  1,  // dup
  0,  // getpid
  1,  // sbrk
  1,  // sleep
  0,  // uptime
  2,  // open
  3,  // write
  3,  // mknod
  1,  // unlink
  2,  // link
  1,  // mkdir
  1,  // close
  6,  // mmap
  2,  // munmap
  0,  // svprint
  1,  // trace
  0,   // sysinfo
  1,  // sbrknaive
  0,  // dirtypages
  0,  // vmprint
  3,   // pgaccess
  2,   // sigalarm
  0,   // sigreturn
  0,   // testbacktrace
  0,   // clone
  0,   // join
  5,   // connect
  3,   // recvfrom
  5,   // sendto
  1,   // accept
  2,   // symlink
  2,   // sigsend
  2,   // signal
  1,   // sigprocmask
};

void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    uint64 tmp = p->trapframe->a0;
    // Use num to lookup the system call function for num, call it,
    // and store its return value in p->trapframe->a0
    p->trapframe->a0 = syscalls[num]();
    if (p->trace_arg & (1 << num)) {
      printf("%d: syscall %s -> %d ", p->pid, syscall_names[num], p->trapframe->a0);
      if (syscall_arg_counts[num] > 0) printf(",params: %d ", tmp);
      if (syscall_arg_counts[num] > 1) printf("%d ", p->trapframe->a1);
      if (syscall_arg_counts[num] > 2) printf("%d ", p->trapframe->a2);
      printf("\n");
    }
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
