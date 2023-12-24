#include "kernel/types.h"
#include "kernel/net.h"
#include "kernel/stat.h"
#include "user/user.h"

//
// send a UDP packet to the localhost (outside of qemu),
// and receive a response.
//
static void
ping(uint16 sport, uint16 dport, int attempts)
{
  int fd;
  char *obuf = "a message from xv6!";
  uint32 dst;

  // 10.0.2.2, which qemu remaps to the external host,
  // i.e. the machine you're running qemu on.
  dst = (10 << 24) | (0 << 16) | (2 << 8) | (2 << 0);

  // you can send a UDP packet to any Internet address
  // by using a different dst.
  
  if((fd = connect(dst, sport, dport, SOCK_DGRAM, SOCK_CLIENT)) < 0){
    fprintf(2, "ping: connect() failed\n");
    exit(1);
  }

  for(int i = 0; i < attempts; i++) {
    if(write(fd, obuf, strlen(obuf)) < 0){
      fprintf(2, "ping: send() failed\n");
      exit(1);
    }
  }

  char ibuf[128];
  int cc = read(fd, ibuf, sizeof(ibuf)-1);
  if(cc < 0){
    fprintf(2, "ping: recv() failed\n");
    exit(1);
  }

  close(fd);
  ibuf[cc] = '\0';
  if(strcmp(ibuf, "this is the host!") != 0){
    fprintf(2, "ping didn't receive correct payload\n");
    exit(1);
  }
}

static void
dns()
{
  uint32 ip = gethostbyname("pdos.csail.mit.edu.");
  if(ip != MAKE_IP_ADDR(128,52,129,126)) {
    printf("wrong ip address");
    exit(1);
  }
}  

int
main(int argc, char *argv[])
{
  int i, ret;
  uint16 dport = NET_TESTS_PORT;

  printf("nettests running on port %d\n", dport);
  
  printf("testing ping: ");
  ping(2000, dport, 1);
  printf("OK\n");
  
  printf("testing single-process pings: ");
  for (i = 0; i < 100; i++)
    ping(2000, dport, 1);
  printf("OK\n");
  
  printf("testing multi-process pings: ");
  for (i = 0; i < 10; i++){
    int pid = fork();
    if (pid == 0){
      ping(2000 + i + 1, dport, 1);
      exit(0);
    }
  }
  for (i = 0; i < 10; i++){
    wait(&ret);
    if (ret != 0)
      exit(1);
  }
  printf("OK\n");
  
  printf("testing DNS\n");
  dns();
  printf("DNS OK\n");
  
  printf("all tests passed.\n");
  exit(0);
}
