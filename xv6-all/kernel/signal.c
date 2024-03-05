#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "defs.h"
#include "signal.h"

void
default_handler(int signum, struct proc *p)
{
  if (signum == SIGINT) {
    setkilled(p);
    printf("Control-C\n");
  } else if (signum == SIGALARM) {
    // ignore
  } else {
    setkilled(p);
  }
}

void
signal_handle(struct proc *p)
{
  if (p->isthread) return;
  for (int sig = 0; sig < NUMSIG; sig++) {
    if (!(p->pending & (1 << sig)) || ((1 << sig) & p->sa_mask)) continue;
    p->pending &= ~(1 << sig);
    if (p->sa_handler[sig] == SIG_DFL) default_handler(sig, p);
    else if (p->sa_handler[sig] == SIG_IGN) continue;
    else if (p->sa_handler[sig] == SIG_KILL) setkilled(p);
    else {
      p->tmp_sa_mask = p->sa_mask;
      p->sa_mask |= (1 << sig);
      *p->sa_trapframe = *p->trapframe;
      p->trapframe->epc = (uint64)p->sa_handler[sig];
    }
    return;
  }
}

void 
signal_handler_clear(struct proc *p)
{
  for (int i = 0; i < NUMSIG; i++) {
    if (p->sa_handler[i] != SIG_DFL || p->sa_handler[i] != SIG_IGN)
      p->sa_handler[i] = SIG_DFL;
  }
}
  