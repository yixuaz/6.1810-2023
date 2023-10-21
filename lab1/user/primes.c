#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include <stdbool.h>

void
printPrimes(int *pleft)
{
  int base;
  close(pleft[1]);
  read(pleft[0], &base, sizeof(int));
  printf("prime %d \n", base);

  int rightPid = -1;
  int pright[2];
  
  int n, candidate;

  while ((n = read(pleft[0], &candidate, sizeof(int))) != 0) {
    if (candidate % base == 0) continue;
    if (rightPid == -1) {
      pipe(pright);
      rightPid = fork();
      if (rightPid > 0) close(pright[0]);
    }
    if (rightPid == 0) {
      printPrimes(pright);
      exit(0);
    } else {
      write(pright[1], &candidate, sizeof(int));
    }
  }
  close(pleft[0]);
  close(pright[1]);
  wait(0);
  exit(0);
}

int
main(int argc, char *argv[])
{
  int p[2];
  pipe(p);

  int pid = fork();
  if (pid == 0) {
    printPrimes(p);
  } else {
    close(p[0]);
    for (int i = 2; i <= 35; i++) {
      write(p[1], &i, sizeof(int));
    }
    close(p[1]);
    wait(0);
  }
  exit(0);
}