#include "kernel/types.h"
#include "kernel/net.h"
#include "kernel/stat.h"
#include "user/user.h"


// UDP server
int
main(int argc, char *argv[])
{
  if(argc <= 1){
    fprintf(2, "usage: udpserver pattern [port]\n");
    exit(1);
  }

  int fd, port;
  uint16 client_port;
  uint32 client_ip;

  port = atoi(argv[1]);

  printf("listening on port %d \n", port);
  if((fd = connect(0, port, 0, SOCK_DGRAM, SOCK_SERVER)) < 0){
    fprintf(2, "bind() failed\n");
    exit(1);
  }
  char ibuf[256];
  while (1) {
    int cc = recvfrom(fd, ibuf, sizeof(ibuf)-1, (uint32 *)&client_ip, (uint16 *)&client_port);
    if(cc < 0){
      fprintf(2, "recv() failed\n");
      exit(1);
    }
    ibuf[cc] = '\0';
    fprintf(1, "%s \n", ibuf);

    char *obuf = "this is the host!";
    if(sendto(fd, obuf, strlen(obuf), client_ip, client_port) < 0){
      fprintf(2, "send() failed\n");
      exit(1);
    }
  }
  close(fd);
  exit(0);
}
