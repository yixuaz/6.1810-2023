#define CNT(x) ((x) >> 32)
#define PTRNULL 0x00000000ffffffff
#define PTR(x) ((x) & PTRNULL)
#define CNTPTR(x, y) (((x) << 32) | (y))

#define REFCNT(x) ((x) >> 5)
#define NONE 0x1f
#define IDX(x) ((x) & NONE)
#define REFCNT_IDX(x, y) (((x) << 5) | (y))
#define INC_REFCNT(x) REFCNT_IDX((REFCNT(x)+1), IDX(x))
#define DEC_REFCNT(x) REFCNT_IDX((REFCNT(x)-1), IDX(x))

struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uchar *data;
  uint64 qnext; // saved lru
  uint32 in_q;
};

