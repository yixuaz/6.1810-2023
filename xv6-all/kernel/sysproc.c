#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "sysinfo.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int n;
  argint(0, &n);
  return vma_sbrk(myproc(), n);
}

uint64
sys_sbrknaive(void)
{
  uint64 addr;
  int n;

  argint(0, &n);

  addr = myproc()->tshared->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


int
sys_pgaccess(void)
{
  int page_nums;
  uint64 buf, bit;
  argaddr(0, &buf);
  argint(1, &page_nums);
  if (page_nums > 32)
    return -1;
  argaddr(2, &bit);
  unsigned int abits = 0;
  struct proc *p = myproc();
  pagetable_t pagetable = p->pagetable;
  pte_t *pte;
  int pgsize = PGSIZE;
  for (int i = 0; i < page_nums; i++, buf += pgsize)
  {
    pte = walk(pagetable, buf, 0);
    if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0) continue;
    if (PTE2PA(*pte) >= PHYSTOP) pgsize = SUPPGSIZE;
    if((*pte & PTE_A) == 0) continue;
    abits |= (1 << i);
    *pte &= ~PTE_A;
  }
  if(copyout(p->pagetable, bit, (char *)&abits, sizeof(abits)) < 0)
      return -1;
  return 0;
}


void dirty_page_print_internal(pagetable_t pagetable, int depth)
{
  for(int i = 0; i < 512; i++){
    pte_t *pte = &pagetable[i];
    if((*pte & PTE_V) && (*pte & (PTE_R|PTE_W|PTE_X)) == 0){
      uint64 child = PTE2PA(*pte);
      dirty_page_print_internal((pagetable_t)child, depth + 1);
    } else if((*pte & PTE_V) && (*pte & PTE_D)){
      printf("%d: pte %p pa %p\n", i, *pte, PTE2PA(*pte));
    }
  }
}

void
dirty_page_print(pagetable_t pagetable)
{
  printf("dirty pages :\n", pagetable);
  dirty_page_print_internal(pagetable, 1);
}

int
sys_dirtypages(void)
{
  struct proc *p = myproc();
  dirty_page_print(p->pagetable);
  return 0;
}

int
sys_vmprint(void)
{
  int abbr;
  argint(0, &abbr);
  struct proc *p = myproc();
  acquire(&p->tshared->tlock);
  printf("proc memory sz:%d\n", p->tshared->sz);
  release(&p->tshared->tlock);
  if (abbr)
    vmprint_abbr(p->pagetable);
  else
    vmprint(p->pagetable);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_svprint(void)
{
  printf("saved bytes: %d, saved pages: %d\n", saved_byte(0), saved_page(0));
  return 0;
}

uint64
sys_trace(void)
{
  int n;
  // fetch the argument
  argint(0, &n);
  myproc()->trace_arg = n;  // Remember the argument in the proc structure
  return 0;  // Success
}

uint64
sys_sysinfo(void)
{
  struct proc *p = myproc();
	struct sysinfo info;
  uint64 addr; // user pointer to struct sysinfo

  argaddr(0, &addr);
  info.freemem = kcollect() + swapcollect();
	info.nproc = proc_number();
  info.loadavg1m = get_avgload_1m();
  if(copyout(p->pagetable, addr, (char *)&info, sizeof(info)) < 0)
    return -1;
  return 0;
}  

uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  p->sa_mask = p->tmp_sa_mask;
  p->tmp_sa_mask = 0;
  *p->trapframe = *p->sa_trapframe;
  return p->trapframe->a0;
}

uint64
sys_sigalarm(void)
{
  int n;
  uint64 func;
  argint(0, &n);
  argaddr(1, &func);
  struct proc *p = myproc();
  p->alarminterval = n;
  p->sa_handler[SIGALARM] = func;
  return 0;
}

uint64
sys_testbacktrace(void)
{
  backtrace();
  return 0;
}

uint64
sys_clone(void)
{
  uint64 fcn, arg1, arg2, stack;
  argaddr(0, &fcn);
  argaddr(1, &arg1);
  argaddr(2, &arg2);
  argaddr(3, &stack);
  return clone((void *)fcn, (void *)arg1, (void *)arg2, (void *)stack);
}

uint64
sys_join(void)
{
  uint64 stack;
  argaddr(0, &stack);
  return join((void **)stack);
}

int
sys_sigsend(void)
{
  int pid;
  int signum;
  argint(0, &pid);
  argint(1, &signum);
  if (pid < 0 || signum < 0 || signum >= NUMSIG)
    return -1;
  return sendsignal(signum, pid);
}

uint64
sys_signal(void)
{
  int signum;
  uint64 func;
  argint(0, &signum);
  argaddr(1, &func);
  if (signum < 0 || signum >= NUMSIG || signum == SIGKILL) return -1;
  struct proc *p = myproc();
  p->sa_handler[signum] = func;
  return 0;
}

int
sys_sigprocmask(void)
{
  int mask;
  argint(0, &mask);
  struct proc *p = myproc();
  int old = p->sa_mask;
  p->sa_mask = mask;
  return old;
}

