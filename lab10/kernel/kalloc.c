// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

int ref_cnt[COW_REFIDX(PHYSTOP)];
void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(uint64 i = COW_REFIDX((uint64)p); p + PGSIZE <= (char*)pa_end; p += PGSIZE, i++) {
    ref_cnt[i] = 1;
    kfree(p); 
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&kmem.lock);
  int idx = COW_REFIDX((uint64) pa);
  if (ref_cnt[idx] < 1) panic("kfree ref_cnt");
  int res = --ref_cnt[idx];
  release(&kmem.lock);

  if(res > 0) return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  struct run *r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);
  if(!r)
    r = (struct run*)find_swapping_page(ref_cnt, &kmem.lock);
  if(r){
    if (kincget(r) != 1) panic("kalloc_ref_cnt");
    memset((char*)r, 5, PGSIZE); // fill with junk
  }
    
  return (void*)r;
}

void *
kalloc_cow(pte_t *pte)
{
  uint64 pa = PTE2PA(*pte);
  int idx = COW_REFIDX(pa);
  acquire(&kmem.lock);
  
  if (ref_cnt[idx] < 1) panic("kalloc_cow ref_cnt");
  if (ref_cnt[idx] == 1){
    *pte = PTE_DECOW(*pte);
    release(&kmem.lock);
    return (void*)pa;
  }
  ref_cnt[idx]--;
  if (ref_cnt[idx] < 1) panic("kfree_cow ref_cnt");
  release(&kmem.lock);
  void * res;
  if((res = kalloc()) == 0)
    kincget((void *)pa);
  return res;  
}

int
kincget(void *pa)
{
  acquire(&kmem.lock);
  int res = ++ref_cnt[COW_REFIDX((uint64)pa)];
  release(&kmem.lock);
  return res;
}

uint savedpg = 0;
uint64 savedbyte = 0;

uint 
saved_page(int unit){
  acquire(&kmem.lock);
  savedpg += unit;
  uint tmp = savedpg;
  release(&kmem.lock);
  return tmp;
}

uint64 
saved_byte(int unit){
  acquire(&kmem.lock);
  savedbyte += unit;
  uint64 tmp = savedbyte;
  release(&kmem.lock);
  return tmp;
}
