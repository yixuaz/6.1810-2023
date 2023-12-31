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
  char name[20];
} kmems[NCPU];

void
kinit()
{
  for (int i = 0; i < NCPU; i++) {
    snprintf(kmems[i].name, 20, "kmem-%d", i);
    initlock(&kmems[i].lock, kmems[i].name);
  }
  freerange(end, (void *)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
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

  push_off();
  int i = cpuid();
  
  acquire(&kmems[i].lock);
  r->next = kmems[i].freelist;
  kmems[i].freelist = r;
  release(&kmems[i].lock);
  pop_off();
}

struct run *
ksteal(struct run *head)
{
  struct run *slow = head, *fast = head->next;
  while (fast && fast->next) {
    slow = slow->next;
    fast = fast->next->next;
  }
  fast = slow->next;
  slow->next = 0;
  return fast;
}


// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int i = cpuid();
  
  acquire(&kmems[i].lock);
  r = kmems[i].freelist;
  if(r)
    kmems[i].freelist = r->next;
  release(&kmems[i].lock);

  if (!r) {
    for (int j = 0; j < NCPU && !r; j++) {
      if (j == i) continue;
      acquire(&kmems[j].lock);
      if (kmems[j].freelist) {
        r = kmems[j].freelist;
        kmems[j].freelist = ksteal(kmems[j].freelist);
      }
      release(&kmems[j].lock);
    }
    if (r) {
      acquire(&kmems[i].lock);
      kmems[i].freelist = r->next;
      release(&kmems[i].lock);
    }
  }  
  pop_off();
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
