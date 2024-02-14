#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "memlayout.h"

int swappg_refcnt[SWAP_SPACE_BLOCKS];
uint8 pg_age[PG_REFIDX(PHYSTOP)];
struct spinlock swaplock;
uint32 swapstart;

void initswap(int dev, struct superblock *sb) {
  for (int i = 0; i < SWAP_SPACE_BLOCKS; i++) {
    swappg_refcnt[i] = 0;
  }
  initlock(&swaplock, "swap");
  for (int i = 0; i < PG_REFIDX(PHYSTOP); i++) pg_age[i] = 0;
  swapstart = sb->swapstart;
}

uint8 pageage(pte_t *pte) {
  uint64 pa = PTE2PA(*pte);
  acquire(&swaplock);
  
  uint8 age = pg_age[PG_REFIDX(pa)];
  age >>= 1;
  if (*pte & PTE_A) {
    age |= (1 << 7);
    *pte &= (~PTE_A);
  }
  pg_age[PG_REFIDX(pa)] = age;
  release(&swaplock);
  return age;
}

int swappageclone(pte_t *pte, pagetable_t new, int va) {
  int idx = (int)PTE2IDX(*pte);
  acquire(&swaplock);
  ++swappg_refcnt[idx];
  release(&swaplock);
  if(mappages(new, va, PGSIZE, PTE2PA(*pte), PTE_FLAGS(*pte)) != 0)
    return -1;
  pte = walk(new, va, 0);
  *pte &= (~PTE_V);
  return 0;
}

uint64 pageout(pte_t *pte) {
  uint64 pa = 0;
  acquire(&swaplock);
  for (int i = 0; i < SWAP_SPACE_BLOCKS; i++) {
    if (swappg_refcnt[i] == 0) {
      pa = PTE2PA(*pte);
      swappg_refcnt[i] = 1;
      *pte = PTE_PGOUT(i, PTE_FLAGS(*pte));
      release(&swaplock);

      struct buf *buf = bread(ROOTDEV, swapstart+i);
      memmove(buf->data, (void *)pa, PGSIZE);
      bwrite(buf);
      brelse(buf);
      acquire(&swaplock);
      break;
    }
  }
  release(&swaplock);
  return pa;
}

int pagein(pte_t *pte) {
  int idx = (int)PTE2IDX(*pte);
  uint64 mem = (uint64)kalloc();
  if (mem == 0) return -1;

  acquire(&swaplock);
  if(swappg_refcnt[idx] <= 0) panic("pagein");
  swappg_refcnt[idx]--;
  // printf("pagein %d\n", idx);
  release(&swaplock);

  struct buf *buf = bread(ROOTDEV, swapstart + idx);
  memmove((char *)mem, buf->data, PGSIZE);
  *pte = PTE_PGIN(mem, PTE_FLAGS(*pte));
  brelse(buf);
  return 0;
}

int
swapdecget(int idx)
{
  acquire(&swaplock);
  int res = --swappg_refcnt[idx];
  release(&swaplock);
  return res;
}



