#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  if(argc <= 1){
    fprintf(2, "usage: vmprint 0 or 1, which mean abbr or not\n");
    exit(1);
  }
  vmprint(atoi(argv[1]));
  exit(0);
}