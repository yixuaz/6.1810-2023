#include "kernel/types.h"
#include "kernel/sysinfo.h"
#include "user/user.h"


void
sinfo(struct sysinfo *info) {
  if (sysinfo(info) < 0) {
    printf("FAIL: sysinfo failed");
    exit(1);
  } else {
    printf("%d %d %d\n", info->freemem, info->nproc, (int) info->loadavg1m);
  }
}

int
main(int argc, char *argv[])
{
	struct sysinfo s;
  sinfo(&s);
  exit(0);
}
