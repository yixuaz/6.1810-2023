#include "kernel/param.h"
#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "kernel/riscv.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  char *buf = malloc(32 * PGSIZE);
  // record init dirty page, check delta
  dirtypages();
  // write two page
  buf[PGSIZE] += 1;
  buf[PGSIZE * 3] += 1;
  dirtypages();
  // read only
  char c = buf[PGSIZE * 4];
  c++;
  dirtypages();
  exit(0);
}