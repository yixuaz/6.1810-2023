#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

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
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
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


#ifdef LAB_PGTBL
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
    if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0) return -1;
    if (PTE2PA(*pte) >= SUPSTART) pgsize = SUPPGSIZE;
    if((*pte & PTE_A) == 0) continue;
    abits |= (1 << i);
    *pte &= ~PTE_A;
  }
  if(copyout(p->pagetable, bit, (char *)&abits, sizeof(abits)) < 0)
      return -1;
  return 0;
}
#endif

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
  printf("proc memory sz:%d\n", p->sz);
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
