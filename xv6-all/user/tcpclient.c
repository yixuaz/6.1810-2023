
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
  int sock;
  uint32 dst = (10 << 24) | (0 << 16) | (2 << 8) | (2 << 0);
  //172.17.209.209
  // uint32 dst = (172 << 24) | (17 << 16) | (209 << 8) | (208 << 0);
  // Connect to the server
  if((sock = connect(dst, port, 26099, SOCK_STREAM, SOCK_CLIENT)) < 0){
    fprintf(2, "connect() failed\n");
    exit(1);
  }
  fprintf(2, "connect() succeed\n");

  char *obuf = "a message from xv6!";
  if(write(sock, obuf, strlen(obuf)) < 0){
    fprintf(2, "tcp ping: send() failed\n");
    exit(1);
  }

  char ibuf[128];
  int cc = read(sock, ibuf, sizeof(ibuf)-1);
  if(cc < 0){
    fprintf(2, "tcp ping: recv() failed\n");
    exit(1);
  }
  ibuf[cc] = '\0';
  printf("receive:%s\n", ibuf);
  close(sock);
  return 0;
}