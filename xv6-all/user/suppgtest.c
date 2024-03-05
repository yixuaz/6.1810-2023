#include "kernel/types.h"
#include "kernel/riscv.h"
#include "kernel/fcntl.h"
#include "user/user.h"

void err(char *msg) {
  printf("fail %s\n", msg);
  exit(1);
}

int main(int argc, char *argv[]) {
  vmprint(1);
  char *addr = mmap(0, SUPPGSIZE*2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_SUPPG, -1, 0);
  addr[0] = 'a';
  vmprint(1);
  addr[SUPPGSIZE + 1] = 'b';
  vmprint(1);
  if (addr[0] != 'a' || addr[SUPPGSIZE + 1] != 'b') {
    err("read mismatch");
  }
  if(munmap(addr, SUPPGSIZE*2) < 0){
    err("munmap < 0");
  }

  addr = mmap(0, SUPPGSIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_SUPPG, -1, 0);
  int pid = fork();
  if (pid == 0) {
    sleep(1);
    addr[1] = 'a';
    vmprint(1);
    sleep(5);
    exit(0);
  } 
  addr[1] = 'b';
  sleep(5);
  vmprint(1);
  if (addr[1] != 'b') err("read mismatch2");
  int xstatus;
  wait(&xstatus);
  if (xstatus != 0) err("chd err");
  printf("sup page test pass\n");
  exit(0);
}