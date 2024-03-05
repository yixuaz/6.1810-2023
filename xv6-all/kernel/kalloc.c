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
void freerange_suppage(void *pa_start, void *pa_end);
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
  char name[20];
} kmems[NCPU];


struct super_run *super_freelist;
int ref_cnt[COW_REFIDX(PHYSTOP)];
struct spinlock ref_lock; // protect ref_cnt
struct spinlock sup_lock; // protect super_freelist 

void
kinit()
{
  initlock(&ref_lock, "reflock");
  initlock(&sup_lock, "suplock");
  for (int i = 0; i < NCPU; i++) {
    snprintf(kmems[i].name, 20, "kmem-%d", i);
    initlock(&kmems[i].lock, kmems[i].name);
  }
  freerange(end, (void*)PHYSTOP);
  freerange_suppage((void*)PHYSTOP, (void*)PHYSTOP_INCLUDESUPPG);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p = (char*)PGROUNDUP((uint64)pa_start);
  for(uint64 i = COW_REFIDX((uint64)p); p + PGSIZE <= (char*)pa_end; p += PGSIZE, i++) {
    ref_cnt[i] = 1;
    kfree(p); 
  }
}

void freerange_suppage(void *pa_start, void *pa_end)
{
  char *p = (char *)pa_start;
  for(; p + SUPPGSIZE <= (char*)pa_end; p += SUPPGSIZE) {
    kfree_suppage(p);
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

  acquire(&ref_lock);
  int idx = COW_REFIDX((uint64) pa);
  if (ref_cnt[idx] < 1) panic("kfree ref_cnt");
  int res = --ref_cnt[idx];
  release(&ref_lock);

  if(res > 0) return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  struct run *r = (struct run*)pa;

  push_off();
  int i = cpuid();
  
  acquire(&kmems[i].lock);
  r->next = kmems[i].freelist;
  kmems[i].freelist = r;
  release(&kmems[i].lock);
  pop_off();
}


void
kfree_suppage(void *pa)
{
  
  struct super_run *r;

  if(((uint64)pa % SUPPGSIZE) != 0 || (uint64)pa < PHYSTOP || (uint64)pa >= PHYSTOP_INCLUDESUPPG)
    panic("kfree_suppage");
  
  // Fill with junk to catch dangling refs.
  memset(pa, 1, SUPPGSIZE);
  r = (struct super_run*)pa;
  
  acquire(&sup_lock);
  r->next = super_freelist;
  super_freelist = r;
  release(&sup_lock);
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

  if(!r)
    r = (struct run*)find_swapping_page(ref_cnt, &ref_lock);

  if(r){
    if (kincget(r) != 1) panic("kalloc_ref_cnt");
    memset((char*)r, 5, PGSIZE); // fill with junk
  }
    
  return (void*)r;
}

void * 
kalloc_suppage() 
{
  struct super_run *r;

  acquire(&sup_lock);
  r = super_freelist;
  if (r) {
    super_freelist = r->next;
  }
  release(&sup_lock);

  if(r)
    memset((char*)r, 5, SUPPGSIZE); // fill with junk
  return (char*) r;
}

void *
kalloc_cow(pte_t *pte)
{
  uint64 pa = PTE2PA(*pte);
  int idx = COW_REFIDX(pa);
  acquire(&ref_lock);
  
  if (ref_cnt[idx] < 1) panic("kalloc_cow ref_cnt");
  if (ref_cnt[idx] == 1){
    *pte = PTE_DECOW(*pte);
    release(&ref_lock);
    return (void*)pa;
  }
  ref_cnt[idx]--;
  if (ref_cnt[idx] < 1) panic("kfree_cow ref_cnt");
  release(&ref_lock);
  void * res;
  if((res = kalloc()) == 0)
    kincget((void *)pa);
  return res;  
}

int
kincget(void *pa)
{
  acquire(&ref_lock);
  int res = ++ref_cnt[COW_REFIDX((uint64)pa)];
  release(&ref_lock);
  return res;
}

uint savedpg = 0;
uint64 savedbyte = 0;

uint 
saved_page(int unit){
  acquire(&ref_lock);
  savedpg += unit;
  uint tmp = savedpg;
  release(&ref_lock);
  return tmp;
}

uint64 
saved_byte(int unit){
  acquire(&ref_lock);
  savedbyte += unit;
  uint64 tmp = savedbyte;
  release(&ref_lock);
  return tmp;
}

int 
kcollect(void)
{
  struct run *r;
  int n = 0;
  for (int j = 0; j < NCPU; j++) acquire(&kmems[j].lock);
  for (int j = 0; j < NCPU; j++) {
    r = kmems[j].freelist;
    for (;r; r = r->next) n++;
  }
  for (int j = 0; j < NCPU; j++) release(&kmems[j].lock);
  return n * PGSIZE;
}

