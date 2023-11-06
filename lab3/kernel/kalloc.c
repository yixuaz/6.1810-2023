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

struct super_run {
    struct super_run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  struct super_run *super_freelist; 
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p = (char*)PGROUNDUP((uint64)pa_start);
  if ((uint64)pa_start < SUPSTART) {
    for(; p + PGSIZE <= (char*)MIN(SUPSTART, (uint64)pa_end); p += PGSIZE)
      kfree(p);
  }
  
  p = (char*)SUPPGROUNDUP((uint64)p);
  for(; p + SUPPGSIZE <= (char*)pa_end; p += SUPPGSIZE)
    kfree_suppage(p); 
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

void
kfree_suppage(void *pa)
{
  
  struct super_run *r;

  if(((uint64)pa % SUPPGSIZE) != 0 || (uint64)pa < SUPSTART || (uint64)pa >= PHYSTOP)
    panic("kfree_suppage");
  
  // Fill with junk to catch dangling refs.
  memset(pa, 1, SUPPGSIZE);
  r = (struct super_run*)pa;
  
  acquire(&kmem.lock);
  r->next = kmem.super_freelist;
  kmem.super_freelist = r;
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

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

void * 
kalloc_suppage() 
{
  struct super_run *r;

  acquire(&kmem.lock);
  r = kmem.super_freelist;
  if (r) {
    kmem.super_freelist = r->next;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, SUPPGSIZE); // fill with junk
  return (char*) r;
}
