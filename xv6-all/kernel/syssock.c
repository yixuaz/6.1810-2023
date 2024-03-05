#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"


int
sys_connect(void)
{
  struct file *f;
  int fd;
  uint32 raddr;
  uint32 rport;
  uint32 lport;
  int sock_type;
  int is_client;

  argint(0, (int*)&raddr);
  argint(1, (int*)&lport);
  argint(2, (int*)&rport);
  argint(3, (int*)&sock_type);
  argint(4, (int*)&is_client);

  if(sockalloc(&f, raddr, lport, rport, sock_type, is_client) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0){
    fileclose(f);
    return -1;
  }

  return fd;
}

int
sys_accept(void)
{
  int fd;
  struct file *f, *nf;
  if(argfd(0, 0, &f) < 0)
    return -1;
  if(sockaccept(f->sock, &nf) < 0)
    return -1;

  if((fd=fdalloc(nf)) < 0){
    fileclose(nf);
    return -1;
  }

  return fd;  
}

int
sys_recvfrom(void)
{
  struct file *f;
  int buflen;
  uint64 buf, ip, port;

  argaddr(1, &buf);
  argint(2, &buflen);
  if(argfd(0, 0, &f) < 0)
    return -1;
  argaddr(3, &ip);
  argaddr(4, &port);

  if(f->readable == 0 || f->type != FD_SOCK)
    return -1; 
  return sockread1(f->sock, buf, buflen, (uint32 *) ip, (uint16 *) port);
}

int
sys_sendto(void)
{
  struct file *f;
  int buflen, ip, port;
  uint64 buf; 
  
  argaddr(1, &buf);
  argint(2, &buflen);
  if(argfd(0, 0, &f) < 0)
    return -1;
  argint(3, &ip);
  argint(4, &port);  
  if(f->writable == 0 || f->type != FD_SOCK)
    return -1;
  return sockwrite1(f->sock, buf, buflen, ip, port);
}
