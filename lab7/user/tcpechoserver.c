
#include "kernel/types.h"
#include "kernel/net.h"
#include "kernel/stat.h"
#include "user/user.h"

#define BUFFER_SIZE 1024

int
main(int argc, char *argv[])
{
  int port = 2000;
  if (argc >= 2) port = atoi(argv[1]);
  int sock, acc;
  // sock + bind
  if ((sock = connect(0, port, 0, SOCK_STREAM, SOCK_SERVER)) < 0){
    printf("listen() failed\n");
    exit(1);
  }

  if ((acc = accept(sock)) < 0) {
    printf("accept: failure\n");
    close(sock);
    exit(1);
  }

  char ibuf[128];
  printf("accept: success\n");
  while (1) {
    int ret;
    if ((ret = read(acc, ibuf, sizeof(ibuf)-1)) < 0) {
      break;
    }
    printf("recv: %d bytes data received\n", ret);
    if(write(acc, ibuf, ret) < 0){
      close(acc);
      close(sock);
      return 1;
    }
  }
  close(acc);  
  close(sock);  
  return 0;
}