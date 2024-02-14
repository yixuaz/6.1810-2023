#include "kernel/param.h"
#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/memlayout.h"
#include "kernel/fs.h"
#include "user/user.h"

void swap_page_write_read();
void swap_page_fork_test();

int
main(int argc, char *argv[])
{
  swap_page_write_read();
  swap_page_fork_test();
  printf("swaptest: all tests succeeded\n");
  exit(0);
}

void
swap_page_write_read()
{
  uint64 st = (uint64)sbrk(0);
  uint64 prev_a = st;
  int maxpg = (PHYSTOP - KERNBASE) / PGSIZE;
  int overflowpg = maxpg + 500;
  for (int i = 0; i < overflowpg; i++) {
    uint64 a = (uint64) sbrk(4096);
    if(a == 0xffffffffffffffff){
      printf("swap write failed\n");
      exit(1);
    }

    // modify the memory to make sure it's really allocated.
    *(char *)(a + 4096 - 1) = (i % 255);

    prev_a = a;
  }

  // read all, ensure swapping dont change value in memory 
  int i = 0;
  for (uint64 k = st; k <= prev_a; k += 4096, i++) {
    if ((*(char *)(k + 4096 - 1)) != i % 255) {
      printf("swap read failed\n");
      exit(1);
    }
  }
  sbrk(-overflowpg*4096);
  printf("swap_page_write_read pass\n");
}

void
swap_page_fork_test()
{
  uint64 st = (uint64)sbrk(0);
  int maxpg = (PHYSTOP - KERNBASE) / PGSIZE;
  // ensure still has phy mem to fork new process
  int acquirepg = maxpg - 500;
  for (int i = 0; i < acquirepg; i++) {
    uint64 a = (uint64) sbrk(4096);
    if(a == 0xffffffffffffffff){
      printf("swap write failed\n");
      exit(1);
    }
    *(char *)(a + 4096 - 1) = (i % 255);
  }


  uint64 end = (uint64)sbrk(0);
  int pid = fork();
  if (pid == 0) {
    int i = 0;
    for (uint64 k = st; k < end; k += 4096, i++) {
      if ((*(char *)(k + 4096 - 1)) != i % 255) {
        printf("fork chd swap read failed\n");
        exit(1);
      }
      
      *(char *)(k + 4096 - 1) = 233;
    }
    exit(0);
  }
  // parent leave 1000 pg, chd alloc maxpg - 500; total phy pg size = maxpg + 500
  // if no swap page, it will overflow
  sbrk(-(acquirepg - 1000)*4096);
  end = (uint64)sbrk(0);
  int i = 0;
  for (uint64 k = st; k < end; k += 4096, i++) {
    if ((*(char *)(k + 4096 - 1)) != i % 255) {
      printf("fork parent swap read failed\n");
      exit(1);
    }
  }

  int xstatus;
  wait(&xstatus);
  if (xstatus != 0) {
    printf("fork swap chd failed\n");
    exit(xstatus);
  }
  printf("swap_page_fork_test pass\n");
}

