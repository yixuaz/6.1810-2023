#ifdef LAB_FS
#define NPROC        10  // maximum number of processes
#else
#define NPROC        64  // maximum number of processes (speedsup bigfile)
#endif
#define NCPU          8  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGSIZE      (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache

#define SWAP_SPACE_BLOCKS 1024  // size of swap region in blocks
// (after uprog increase, to pass bigwrite test ,we need more file space)
#define FSSIZE       (40000 + SWAP_SPACE_BLOCKS)  // size of file system in blocks 
#define NBUFBUC      13  // size of block cache hash table bucket
#define MAXPATH      128   // maximum file path name


