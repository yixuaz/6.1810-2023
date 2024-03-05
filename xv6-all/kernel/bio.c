// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct {
  
  struct buf buf[NBUF + 1];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  uint32 hashtable[FSSIZE];

  uint64 head;
  uint64 tail;
} bcache;

inline int CAS64(uint64* ptr, uint64* expected, uint64 desired) {
  return __atomic_compare_exchange_n(ptr, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

inline int CAS32(uint32* ptr, uint32* expected, uint32 desired) {
  return __atomic_compare_exchange_n(ptr, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

void enqueue(struct buf *b) {
  uint32 expect = 0;
  if (!CAS32(&b->in_q, &expect, 1)) return;

  int idx = b - bcache.buf;
  b->qnext = CNTPTR(0LL, PTRNULL);
  uint64 tail, next;
  while (1) {
    tail = bcache.tail;
    next = bcache.buf[PTR(tail)].qnext;
    if (tail == bcache.tail) {
      if (PTR(next) == PTRNULL) {
        if (CAS64(&bcache.buf[PTR(tail)].qnext, &next, CNTPTR(CNT(next) + 1, idx)))
          break;
      } else {
        CAS64(&bcache.tail, &tail, CNTPTR(CNT(tail) + 1, PTR(next)));
      }
    }
  }
  CAS64(&bcache.tail, &tail, CNTPTR(CNT(tail) + 1, idx));
}

struct buf *dequeue() {
  uint64 head, tail, next;
  struct buf *ret;
  while(1) {
    head = bcache.head;
    tail = bcache.tail;
    next = bcache.buf[PTR(head)].qnext;
    if (head == bcache.head)
    {
      if (PTR(head) == PTR(tail)) {
        if (PTR(next) == PTRNULL) return 0;
        CAS64(&bcache.tail, &tail, CNTPTR(CNT(tail) + 1, PTR(next)));
      } else {
        ret = &bcache.buf[PTR(head)];
        if(CAS64(&bcache.head, &head, CNTPTR(CNT(head) + 1, PTR(next)))) {
          __atomic_store_n(&ret->in_q, 0, __ATOMIC_RELEASE);
          break;
        }
      }
    }
  }
  return ret;
}

void
binit(void)
{
  struct buf *b = bcache.buf+NBUF;
  // init hashmap
  for (int i = 0; i < FSSIZE; i++) 
    bcache.hashtable[i] = REFCNT_IDX(0, NONE);
  // init dummy node for queue
  bcache.head = bcache.tail = CNTPTR(0LL, NBUF);
  initsleeplock(&b->lock, "buffer");
  b->data = kalloc();
  b->qnext = CNTPTR(0LL, PTRNULL);
  b->in_q = 0;
  // init data node in queue
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->data = kalloc();
    b->in_q = 0;
    enqueue(b);
    initsleeplock(&b->lock, "buffer");
  }

}


int bhash(uint dev, uint blockno) {
  int hash = dev ^ blockno;
  hash = hash * 31 + blockno;
  if (hash < 0) hash = -hash;
  return hash % NBUFBUC;
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  if (dev != 1) panic("bget");
  struct buf *b;
  b = 0;
  while(1) {
    uint32 refcnt_idx = bcache.hashtable[blockno];
    // Is the block already cached?
    if (IDX(refcnt_idx) != NONE) {
      if (CAS32(&bcache.hashtable[blockno], &refcnt_idx, INC_REFCNT(refcnt_idx))){
        if(b) enqueue(b);
        acquiresleep(&bcache.buf[IDX(refcnt_idx)].lock);
        return &bcache.buf[IDX(refcnt_idx)];
      }
    } else {
      // Recycle the least recently used (LRU) unused buffer.
      if (!b) {
        while (1) {
          b = dequeue();
          if (!b) panic("bget: no buffers");
          // b->blockno is threadsafe since until this func return, 
          // other thread cannot deque same buf, and reset its blockno
          uint32 blockno = b->blockno, expect = REFCNT_IDX(0, IDX(bcache.hashtable[blockno]));
          if (CAS32(&bcache.hashtable[blockno], &expect, REFCNT_IDX(0, NONE)))
            break;
        }
      }
      // assert(refcnt_idx = REFCNT_IDX(0, NONE));
      int idx = b - bcache.buf;
      if (CAS32(&bcache.hashtable[blockno], &refcnt_idx, REFCNT_IDX(1, idx))){
        acquiresleep(&b->lock);
        b->valid = 0;
        b->dev = dev;
        b->blockno = blockno;
        return b;
      }
    }
  }
  
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if (b->dev != 1) panic("brelse");
  
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  int blockno = b->blockno;
  while (1) {
    uint32 refcnt_idx = bcache.hashtable[blockno];
    // assert(blockno == b->blockno && IDX(refcnt_idx) != NONE && REFCNT(refcnt_idx) > 0);
    int new_refcnt = REFCNT(refcnt_idx) - 1, idx = IDX(refcnt_idx);
    if (CAS32(&bcache.hashtable[blockno], &refcnt_idx, REFCNT_IDX(new_refcnt, idx))) {
      if (new_refcnt == 0) enqueue(b);
      return;
    }
  }
}

void
bpin(struct buf *b) {
  int blockno = b->blockno;
  uint32 refcnt_idx;
  do {
    refcnt_idx = bcache.hashtable[blockno];
    // assert(blockno == b->blockno && IDX(refcnt_idx) != NONE && REFCNT(refcnt_idx) > 0);
  } while(!CAS32(&bcache.hashtable[blockno], &refcnt_idx, INC_REFCNT(refcnt_idx)));
}

void
bunpin(struct buf *b) {
  int blockno = b->blockno;
  uint32 refcnt_idx;
  do {
    refcnt_idx = bcache.hashtable[blockno];
    // assert(blockno == b->blockno && IDX(refcnt_idx) != NONE && REFCNT(refcnt_idx) > 1);
  } while(!CAS32(&bcache.hashtable[blockno], &refcnt_idx, DEC_REFCNT(refcnt_idx)));
}

void
bunpin2(uint64 addr)
{
  
  struct buf *b;
  for(b = bcache.buf; b <= bcache.buf+NBUF; b++){
    if ((uint64)b->data == addr) {
      bunpin(b);
      return;
    }
  }
  panic("bunpin2");
  
}

