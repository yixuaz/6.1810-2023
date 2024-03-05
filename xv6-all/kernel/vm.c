#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

void vmprint_internal(pagetable_t pagetable, int depth, int abbr)
{
  int count_dp3 = 0;
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      uint64 child = PTE2PA(pte);
      for(int j = 0; j < depth; j++) printf(".. ");
      printf("%d: pte %p pa %p\n", i, pte, child);
      vmprint_internal((pagetable_t)child, depth + 1, abbr);
    } else if(pte & PTE_V){
      if (abbr && depth == 3) count_dp3++;
      else {
        for(int j = 0; j < depth; j++) printf(".. ");
        printf("%d: pte %p pa %p\n", i, pte, PTE2PA(pte));
      }
    }
  }
  if (abbr && count_dp3 > 0 && depth == 3) {
    for(int j = 0; j < depth; j++) printf(".. ");
    printf("%d\n",count_dp3);
  }
}

void
vmprint(pagetable_t pagetable)
{
  printf("page table %p\n", pagetable);
  vmprint_internal(pagetable, 1, 0);
}

void
vmprint_abbr(pagetable_t pagetable)
{
  printf("page table %p\n", pagetable);
  vmprint_internal(pagetable, 1, 1);
}

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PCI-E ECAM (configuration space), for pci.c
  kvmmap(kpgtbl, 0x30000000L, 0x30000000L, 0x10000000, PTE_R | PTE_W);

  // pci.c maps the e1000's registers here.
  kvmmap(kpgtbl, 0x40000000L, 0x40000000L, 0x20000, PTE_R | PTE_W);
  
  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP_INCLUDESUPPG-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);
  // vmprint_abbr(kpgtbl); // verify PLIC is using huge page
  return kpgtbl;
}

// Initialize the one kernel_pagetable
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  int level = 2;
  // alloc = 0, only get; alloc != 0, PGSIZE create normal page,  SUPPGSIZE create super page
  int end = alloc == SUPPGSIZE ? 1 : 0;  
  for(; level > end; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
      if ((*pte & PTE_R) || (*pte & PTE_X)) {
        if (level != 1) panic("walk_level");
        return pte;
      }
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  
  if (level > 1 || level < 0) panic("walk_level");
  return &pagetable[PX(level, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

uint64
kwalkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  int suppg = IS_SUPPG(pa);
  int pgsize = suppg ? SUPPGSIZE : PGSIZE;
  if((va % pgsize) != 0)
    panic("mappages: va not aligned");

  if((size % pgsize) != 0)
    panic("mappages: size not aligned");

  if(size == 0)
    panic("mappages: size");
   
  a = va;
  last = va + size - pgsize;
  for(;;){
    if((pte = walk(pagetable, a, pgsize)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += pgsize;
    pa += pgsize;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");
  int step_pgsize = PGSIZE;  
  for(a = va; a < va + npages*PGSIZE; a += step_pgsize){
    
    if((pte = walk(pagetable, a, 0)) == 0)
      continue;
    if(*pte & PTE_PG){
      *pte &= (~PTE_PG);
      swapdecget(PTE2IDX(*pte));
    }
    if((*pte & PTE_V) == 0)
      continue;
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    uint64 pa = PTE2PA(*pte);
    int suppg = IS_SUPPG(pa);
    step_pgsize = suppg ? SUPPGSIZE : PGSIZE;  
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      if (do_free & FREE_BCACHE)
        bunpin2(pa);
      if (do_free & 1)
        suppg ? kfree_suppage((void*)pa) : kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvmfirst(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("uvmfirst: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    if (newsz == 0) {
      newsz = PGSIZE;
      npages--;
    }
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  int ret = 0;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      continue;
    if ((*pte & PTE_PG) && swappageclone(pte, new, i) < 0) goto err;
    if((*pte & PTE_V) == 0) 
      continue;
    pa = PTE2PA(*pte);
    if (IS_SUPPG(pa)){
      char *mem;
      if((mem = kalloc_suppage()) == 0) goto err;
      memmove(mem, (char*)pa, SUPPGSIZE);
      if(mappages(new, i, SUPPGSIZE, (uint64)mem, PTE_FLAGS(*pte)) != 0){
        kfree_suppage(mem);
        goto err;
      }
      i += SUPPGSIZE - PGSIZE;
      continue;
    }
    if (*pte & PTE_W) *pte = PTE_ENCOW(*pte);
    kincget((void *)pa);
    if(mappages(new, i, PGSIZE, pa, PTE_FLAGS(*pte)) != 0) {
    err:  
      uvmunmap(new, 0, i / PGSIZE, 1);
      ret = -1;
      break;
    }
  }
  saved_page(i / PGSIZE);
  saved_byte(i);
  return ret;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

int
copyonwrite(pte_t *pte)
{
  if ((*pte & PTE_COW) == 0) {
    return -1;
  }
  struct proc *p = myproc();
  acquiresleep(&p->tshared->slock);
  acquire(&p->tshared->tlock);
  if (*pte & PTE_W) goto success;
  char *mem;
  uint64 pa = PTE2PA(*pte);
  if (IS_SUPPG(pa)) panic("copyonwrite");
  uint flags = PTE_FLAGS(*pte);
  release(&p->tshared->tlock);
  if ((mem = kalloc_cow(pte)) == 0) {
    releasesleep(&p->tshared->slock);
    return -1;
  }
  acquire(&p->tshared->tlock);
  if ((uint64)mem == pa) goto success;
  *pte = (PA2PTE((uint64)mem) | PTE_DECOW(flags));
  memmove(mem, (char*)pa, PGSIZE);
  saved_page(-1);
  saved_byte(-PGSIZE);
success:
  release(&p->tshared->tlock);
  releasesleep(&p->tshared->slock); 
  return 0;
}

int
setguardpage(struct proc *p)
{
  if (!p->isthread) panic("setguardpage");
  pte_t *pte;
  acquire(&p->tshared->tlock);
  if ((pte = walk(p->pagetable, p->tstack, 0)) == 0 || ((*pte & PTE_V)==0)) {
    release(&p->tshared->tlock);
    if (vma_handle(p, p->tstack) < 0) return -1;
    acquire(&p->tshared->tlock);
    pte = walk(p->pagetable, p->tstack, 0);
  }
  if (!(*pte & PTE_V)) panic("setguardpage2");
  *pte &= ~PTE_U;
  release(&p->tshared->tlock);
  return 0;
}


void
rmguardpage(struct proc *p)
{
  // we need check here in thread since return guard page to user space, since guard page alloc from user space
  if (!p->isthread) panic("rmguardpage");
  acquire(&p->tshared->tlock);
  pte_t *pte = walk(p->pagetable, p->tstack, 0);
  *pte |= PTE_U;
  release(&p->tshared->tlock);
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  int pgsize;
  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if(va0 >= MAXVA)
      return -1;  
    pte = walk(pagetable, va0, 0);
    if(pte == 0 || (*pte & PTE_V) == 0) {
      int suppg = vma_handle(myproc(), va0);
      if(suppg < 0) return -1;
      pte = walk(pagetable, suppg ? SUPPGROUNDDOWN(va0) : va0, 0);
    }
    if((*pte & PTE_U) == 0)
      return -1;
    if((*pte & PTE_W) == 0)
      if(copyonwrite(pte)) 
        return -1;
    pa0 = PTE2PA(*pte);
    pgsize = IS_SUPPG(pa0) ? SUPPGSIZE : PGSIZE;
    n = pgsize - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + pgsize;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;
  

  int pgsize;
  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0) {
      int suppg = vma_handle(myproc(), va0);
      if(suppg < 0) return -1;
      pa0 = walkaddr(pagetable, suppg ? SUPPGROUNDDOWN(va0) : va0);
    }
    pgsize = IS_SUPPG(pa0) ? SUPPGSIZE : PGSIZE;
    n = pgsize - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + pgsize;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;
  int pgsize;
  
  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0) {
      int suppg = vma_handle(myproc(), va0);
      if(suppg < 0) return -1;
      pa0 = walkaddr(pagetable, suppg ? SUPPGROUNDDOWN(va0) : va0);
    }
    pgsize = IS_SUPPG(pa0) ? SUPPGSIZE : PGSIZE;
    n = pgsize - (srcva - va0);

    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + pgsize;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}
