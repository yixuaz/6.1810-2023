#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/param.h"
#include <stddef.h>

int
main(int argc, char *argv[])
{
  int n;
  char c;
  char *nargv[MAXARG];
  char buf[1024];
  char *p = buf, *prep = buf;
  int j = argc - 1;
  for (int i = 1; i < argc; i++)
    nargv[i - 1] = argv[i];
  while((n = read(0, &c, sizeof(char)))) {
    if(c == ' ' || c == '\n') {
      *p++ = 0;
      if (strlen(prep) != 0) {
        if (j == MAXARG) exit(1);
        nargv[j++] = prep;
        prep = p;
      } else {
        p--;
      }
      if(c == '\n') {
        if(fork() == 0) {
          // for (int i = 0; i < j; i++) printf("~%s \n", nargv[i]);
          exec(nargv[0], nargv);
        } else {
          wait(0);
          while (j > argc - 1) {
            j--;
            nargv[j] = NULL;
          }
          p = buf;
          prep = buf;
        }
      } 
    } else {
      *p++ = c;
    }
    // printf("!%d %d\n", n, c);
  }
  exit(0);
}