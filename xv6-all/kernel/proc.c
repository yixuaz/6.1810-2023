#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "defs.h"
#include "signal.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;
struct proc *shproc;

uint64 fgproc_mask = 0;
int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(int isthread)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  } else {
    memset(p->trapframe, 0, PGSIZE);
    p->sa_trapframe = (p->trapframe + TRAPFRAMESIZE);
  }

  if (!isthread) {
    // Allocate a usyscall page.
    if((p->usyscall = (struct usyscall *)kalloc()) == 0){
      freeproc(p);
      release(&p->lock);
      return 0;
    }
  

    // An empty user page table.
    p->pagetable = proc_pagetable(p);
    if(p->pagetable == 0){
      freeproc(p);
      release(&p->lock);
      return 0;
    }

    p->usyscall->pid = p->pid;
    initlock(&p->trapframe->tshared.tlock, "tlock");
    initsleeplock(&p->trapframe->tshared.slock, "slock");
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;
  p->tickspassed = 0;
  p->alarminterval = 0;
  p->tmp_sa_mask = 0;
  // setup thread related variable
  
  p->tshared = &p->trapframe->tshared;
  p->trap_va = TRAPFRAME;
  p->isthread = isthread;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  // p->tshared->sz in p->trapframe, so move proc_freepagetable before
  if(p->pagetable && !p->isthread) {
    proc_freepagetable(p->pagetable, p->tshared->sz);
  }
  p->pagetable = 0;
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->usyscall && !p->isthread)
    kfree((void*)p->usyscall);
  p->usyscall = 0;
  p->sa_trapframe = 0;
  p->tshared = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->tstack = 0;
  p->isthread = 0;
  p->trap_va = 0;
  p->state = UNUSED;
  p->tickspassed = 0;
  p->alarminterval = 0;
  p->pending = 0;
  p->tmp_sa_mask = 0;
  p->sa_mask = 0;
  for (int i = 0; i < NUMSIG; i++) p->sa_handler[i] = 0;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  if(mappages(pagetable, USYSCALL, PGSIZE,
              (uint64)(p->usyscall), PTE_R | PTE_U) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }  

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmunmap(pagetable, USYSCALL, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, USYSCALL, 1, 0);
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  
  uvmunmap(pagetable, USYSCALL, 1, 0);
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmunmap(pagetable, PGSIZE, (PGROUNDUP(sz) / PGSIZE) - 1, 1);
  // only freewalk
  uvmfree(pagetable, 0);
}

void
proc_freepagetable_from_zero(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, USYSCALL, 1, 0);
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc(0);
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->tshared->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer
  p->usyscall->pid = p->pid;

  p->pending = 0;
  p->sa_mask = 0;
  for (int i = 0; i < NUMSIG; i++) p->sa_handler[i] = SIG_DFL;

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();
  acquiresleep(&p->tshared->slock);
  acquire(&p->tshared->tlock);
  sz = p->tshared->sz;
  release(&p->tshared->tlock);
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      releasesleep(&p->tshared->slock);
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  acquire(&p->tshared->tlock);
  p->tshared->sz = sz;
  release(&p->tshared->tlock);
  releasesleep(&p->tshared->slock);
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc(0)) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->tshared->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->tshared->sz = p->tshared->sz;

  // Copy signal status from parent to child (exclude pending status)
  np->sa_mask = p->sa_mask;
  np->alarminterval = p->alarminterval;
  *(np->sa_handler) = *(p->sa_handler);

  vma_fork(p, np);
  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  np->trace_arg = p->trace_arg;

  pid = np->pid;
  np->usyscall->pid = pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  if (p == initproc) shproc = np;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

int 
clone(void(*fcn)(void*, void*), void *arg1, void *arg2, void *stack)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Ensure stack is page align, which help setup guard page.
  if(((uint64)stack % PGSIZE) != 0)
    return -1;
  // Allocate process.
  if((np = allocproc(1)) == 0){
    return -1;
  }
  
  // use same page table as parent, to keep same memory space
  np->pagetable = p->pagetable;
  // use same usyscall page
  np->usyscall = p->usyscall;
  // share some variable between threads
  np->tshared = p->tshared;
  np->parent = p;

  release(&np->lock);
  acquire(&p->tshared->tlock);
  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);
  release(&p->tshared->tlock);
  
  // setup thread's function address 
  np->trapframe->epc = (uint64)fcn;
  // setup thread's function args 
  // refer to riscv calling covention: https://pdos.csail.mit.edu/6.828/2023/readings/riscv-calling.pdf
  np->trapframe->a0 = (uint64)arg1;
  np->trapframe->a1 = (uint64)arg2;
  // ensure thread without exit return to a invalid address to trigger trap
  np->trapframe->ra = 0xffffffffffffffff;

  // Use the second page as the user stack.
  np->trapframe->sp = (uint64)(stack + 2 * PGSIZE);
  // Keep stack address for "join" to return
  np->tstack = (uint64)stack;
  // setup first stack page as guard page, remove PTE_U
  // uvmclear(np->pagetable, np->tstack);
  if (setguardpage(np) < 0) goto err;

  // find a address to remap TRAPFRAME page
  // it is important since TRAPFRAME page should not be shared across threads
  uint64 trap_va = PHYSTOP_INCLUDESUPPG;
  for(; trap_va < USYSCALL ; trap_va += PGSIZE) {
    if (kwalkaddr(np->pagetable, trap_va) == 0) {
      np->trap_va = trap_va;
      mappages(np->pagetable, np->trap_va, PGSIZE,
           (uint64)(np->trapframe), PTE_R | PTE_W); 
      break;     
    }
  }
  // failed to find a space
  if (trap_va >= USYSCALL) goto err;


  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));
  pid = np->pid;
  // ban instruction re-order
  __sync_synchronize();
  np->state = RUNNABLE;
  return pid;
err:
  freeproc(np);
  return -1;

}

// when a process (not a thread) calls exit, all threads of this process should be exit
void 
tpkill(struct proc *curproc) 
{
  struct proc *p;
  int havethreads;
  acquire(&wait_lock);
  // make all the threads in group to die (all process with same pid will be killed)
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->parent == curproc && p->isthread){
      acquire(&p->lock);
      p->killed = 1;
      if(p->state == SLEEPING) p->state = RUNNABLE;
      release(&p->lock);
    }
  }
  // now let all the threads finish and wait for them become zombie
  for(;;){
    havethreads = 0;
    for(p = proc; p < &proc[NPROC]; p++){
      if(p->parent != curproc || !p->isthread) continue;
      // thread in group is not died yet so suspend untill it dies.
      if(p->state != ZOMBIE){
        havethreads = 1; 
        break;
      } else {
        acquire(&p->lock);
        freeproc(p);
        release(&p->lock);
      }
    }
    // group leader doesn't have any threads 
    if(!havethreads){
        break;
    } 
    // sleep for an exisiting thread in group to be killed
    sleep(curproc, &wait_lock);
  }
  release(&wait_lock);
}


// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p && !pp->isthread){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }
  
  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  tpkill(p);
  if (!p->isthread) mmap_clean(p);
  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);
  fgproc_mask &= ~(1 << (p - proc));
  p->xstate = status;
  p->state = ZOMBIE;

  // unmap since we map trap_va in join
  if (p->isthread) { 
    uvmunmap(p->pagetable, p->trap_va, 1, 0);
  }

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// set frontground process
void setfg(struct proc *sh_proc)
{
  struct proc *pp;
  int i = 0;
  for(pp = proc; pp < &proc[NPROC]; pp++, i++){
    if(pp->parent == sh_proc && !pp->isthread){
      acquire(&pp->lock);
      if (pp->state == RUNNING || pp->state == RUNNABLE || pp->state == SLEEPING) {
        fgproc_mask |= (1 << i);
      }
      release(&pp->lock);
    }
  }
}

int sendsignal(int signal, int pid) 
{
  struct proc *pp;
  int i = 0, ret = -1;
  for(pp = proc; pp < &proc[NPROC]; pp++, i++){
    if (pp->isthread) continue;
    acquire(&pp->lock);
    if (pp->state == RUNNING || pp->state == RUNNABLE || pp->state == SLEEPING) {
      if(pid == pp->pid) {
        pp->pending |= (1 << signal);
        release(&pp->lock);
        return 0;
      } else if (pid == FG && ((1 << i) & fgproc_mask)) {
        ret = 0;
        pp->pending |= (1 << signal);
        if (pp->state == SLEEPING) pp->state = RUNNABLE;
      }
    }
    release(&pp->lock);
  }
  
  if (ret < 0 && pid == FG) {
    acquire(&shproc->lock);
    shproc->pending |= (1 << signal);
    if (shproc->state == SLEEPING) shproc->state = RUNNABLE;
    release(&shproc->lock);
  }
  return ret;
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  if (p == shproc) {
    setfg(p); 
  }

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      // wait only consider process
      if(pp->parent == p && !pp->isthread){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          release(&pp->lock);
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            
            release(&wait_lock);
            return -1;
          }
          acquire(&pp->lock);
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

int
join(void **stack)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p && pp->isthread){
        acquire(&pp->lock);
        havekids = 1;
        if(pp->state == ZOMBIE){

          pid = pp->pid;
          release(&pp->lock);
          if(stack != 0 && copyout(p->pagetable, (uint64)stack, (char *)&pp->tstack,
                                  sizeof(pp->tstack)) < 0) {
            release(&wait_lock);
            return -1;
          }
          acquire(&pp->lock);
          // reset guard page with PTE_U
          rmguardpage(pp);
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }

    sleep(p, &wait_lock);  
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting.
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);

  // check signal
  signal_handle(p);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

uint64
find_swapping_page(int *refcnt, struct spinlock* memlock)
{
begin:  
  struct proc *p;
  pte_t *pte, *ret = 0;
  uint8 minage = 255;
  uint64 res;
  
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if((p->state == RUNNING || p->state == RUNNABLE || p->state == SLEEPING) && (p->pid > 2))
    {
      acquire(&p->tshared->tlock);
      acquire(memlock);
      for(int a = 0; a < p->tshared->sz; a += PGSIZE){
        if((pte = walk(p->pagetable, a, 0)) == 0)
          continue;
        if((*pte & PTE_V) == 0)
          continue;
        if(*pte & PTE_COW)
          continue;
        if(PTE_FLAGS(*pte) == PTE_V)
          panic("find_nfup_proc: not a leaf");
        
        uint8 age = pageage(pte);
        if (age < minage && refcnt[COW_REFIDX(PTE2PA(*pte))] == 1) {
          
          minage = age;
          ret = pte;
        }
        if (minage == 0) break;
      }
      release(memlock);
      release(&p->tshared->tlock);
    }
    release(&p->lock);
    if (minage == 0) break;
  }
  
  if (ret == 0) {
    return 0;
  }
  acquire(memlock);
  int idx = COW_REFIDX(PTE2PA(*ret));
  int old_ref_cnt = refcnt[idx];
  if (old_ref_cnt != 1) {
    panic("find_swapping_page 2");
  }
  release(memlock);
  res = pageout(ret);
  if (res < 0) goto begin;
  if (res > 0) {
    acquire(memlock);
    refcnt[idx] = 0;
    release(memlock);
  }
  return res;
}

void update_page_age()
{
  struct proc *p;
  pte_t *pte;
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if((p->state == RUNNING || p->state == RUNNABLE || p->state == SLEEPING) && (p->pid > 2))
    {
      acquire(&p->tshared->tlock); 
      for(int a = 0; a < p->tshared->sz; a += PGSIZE){
        if((pte = walk(p->pagetable, a, 0)) == 0)
          continue;
        if((*pte & PTE_V) == 0)
          continue;  
        pageage(pte);
      }
      release(&p->tshared->tlock);
    }
    release(&p->lock);
  }
}

int
proc_number(void)
{
  struct proc *p;
  int n = 0;
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    n++;
  }
  return n;
}

struct proc*
get_proc_by_pid(int pid)
{
  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state != UNUSED && p->state != USED && p->pid == pid){
      release(&p->lock); 
      return p;
    }
    release(&p->lock);  
  }
  return NULL;
}

struct proc*
get_proc_by_idx(int idx)
{
  if (idx < 0) return NULL;

  struct proc *p, *i;
  i = myproc();
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state != UNUSED && p->state != USED && (i == shproc || p->pid != i->pid)) {
      if (idx == 0) {
        release(&p->lock);
        return p;
      }
      idx--;
    } 
    release(&p->lock);
  }
  return NULL;
}

const float alpha1m = 2.0/61.0;
float loadavg = 0.0;

void
calculate_load_1m(void)
{
  int count = proc_number();
  loadavg = alpha1m * count + (1 - alpha1m) * loadavg;
}

float
get_avgload_1m(void)
{
  return loadavg;
}