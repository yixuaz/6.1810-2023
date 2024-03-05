#include "kernel/types.h"
#include "user/user.h"


int main(int argc, char *argv[]) {
    vmprint(1);
    char *long_content = malloc(2076656);
    long_content[0] = 'c';
    printf("add suppg 0, content:%c\n", long_content[0]);
    vmprint(1);
    char *long_content2 = malloc(2097152-16);
    long_content2[0] = 'b';
    printf("add suppg 1, content:%c\n", long_content2[0]);
    vmprint(1);
    char *long_content3 = malloc(2097152);
    long_content3[0] = 'a';
    printf("add suppg 2, content:%c\n", long_content3[0]);
    vmprint(1);
    char *long_content4 = malloc(4096 - 16);
    long_content4[0] = 'd';
    printf("add suppg 0, content:%c\n", long_content4[0]);
    vmprint(1);
    char *long_content5 = malloc(2097152 - 32 - 4096);
    long_content5[0] = 'e';
    printf("add suppg 0, content:%c\n", long_content5[0]);
    vmprint(1);

    char *addr = long_content2;

    int fd = open("README", 0);
    if(fd < 0){
      printf("open(README) failed\n");
      exit(1);
    }
    int n = read(fd, (void*)addr, 8192);
    if(n <= 0 || addr[0] != 'x'){
      printf("read error1 %d, %c\n", n, addr[0]);
      exit(1);
    }
    close(fd);

    int fds[2];
    if(pipe(fds) < 0){
      printf("pipe() failed\n");
      exit(1);
    }
    n = write(fds[1], "k", 1);
    if(n != 1){
      printf("pipe write failed\n");
      exit(1);
    }
    n = read(fds[0], (void*)addr, 8192);
    if(n != 1 || addr[0] != 'k'){
      printf("read error2\n");
      exit(1);
    }
    close(fds[0]);
    close(fds[1]);

    free(long_content5);
    free(long_content4);
    free(long_content3);
    free(long_content2);
    free(long_content);
    
    exit(0);
}