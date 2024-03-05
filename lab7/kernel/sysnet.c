//
// network system calls.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"
#include "tcp.h"

static struct spinlock locks[SOCK_HASHTABLE_SIZE];

static struct sock *sockets[SOCK_HASHTABLE_SIZE];

void
sockinit(void)
{
  for (int i = 0; i < SOCK_HASHTABLE_SIZE; i++) {
    initlock(&locks[i], "socktbl");
    sockets[i] = 0;
  }
}

inline int hash(uint16 i) {
  return (i % SOCK_HASHTABLE_SIZE);
}

int sock_hashtable_add(uint16 lport, uint32 raddr, uint16 rport, struct sock *si)
{
  int key = hash(si->lport);
  struct spinlock lock = locks[key];
  acquire(&lock);
  struct sock *pos = sockets[key];
  while (pos) {
    if (pos->lport == lport && si->raddr == raddr && si->rport == rport) {
      int is_tcp_accept_sock = (!raddr && !rport && si->tcpcb.parent); 
      if (!is_tcp_accept_sock) {
        release(&lock);
        return -1;
      }
    }
    pos = pos->next;
  }
  si->next = sockets[key];
  sockets[key] = si;
  release(&lock);
  return 0;
}

int sock_hashtable_remove(struct sock *si)
{
  int key = hash(si->lport);
  struct spinlock lock = locks[key];
  acquire(&lock);
  struct sock **pos = &sockets[key];
  int res = -1;
  while (*pos) {
    if (*pos == si){
      *pos = si->next;
      res = 0;
      break;
    }
    pos = &(*pos)->next;
  }
  release(&lock);
  return res;
}

int sock_hashtable_update(struct sock *si, uint32 raddr, uint16 rport)
{
  int key = hash(si->lport);
  struct spinlock lock = locks[key];
  acquire(&lock);
  struct sock *pos = sockets[key];
  int res = -1;
  while (pos) {
    if (pos == si){
      si->raddr = raddr;
      si->rport = rport;
      res = 0;
      break;
    }
    pos = pos->next;
  }
  release(&lock);
  return res;
}

int sock_hashtable_get(uint16 lport, uint32 raddr, uint16 rport, struct sock **ssi)
{
  int key = hash(lport);
  
  struct spinlock lock = locks[key];
  acquire(&lock);
  struct sock *si = sockets[key];
  int backup = 0;
  while (si) {
    if (si->lport == lport) {
      if (si->raddr == raddr && si->rport == rport) {
        *ssi = si;
        release(&lock);
        return 0;
      } else if (!backup && si->raddr == 0 && si->rport == 0) {
        // in tcp, we assume accept sock should insert before its parent
        backup = 1;
        *ssi = si;
      }
    }
    si = si->next;
  }
  release(&lock);
  return backup ? 0 : -1;
}

int
sockalloc(struct file **f, uint32 raddr, uint16 lport, uint16 rport, int sock_type, int is_client)
{
  struct sock *si;

  si = 0;
  *f = 0;
  if ((*f = filealloc()) == 0)
    goto bad;
  if ((si = (struct sock*)kalloc()) == 0)
    goto bad;

  // initialize objects
  si->raddr = raddr;
  si->lport = lport;
  si->rport = rport;
  si->type = sock_type;
  si->tcpcb.parent = 0;
  initlock(&si->lock, "sock");
  mbufq_init(&si->rxq);
  (*f)->type = FD_SOCK;
  (*f)->readable = 1;
  (*f)->writable = 1;
  (*f)->sock = si;
  // add to list of sockets
  if (sock_hashtable_add(lport,  raddr, rport, si) < 0) goto bad;

  if (sock_type == SOCK_STREAM) {
    if (is_client) tcp_init_client(si);
    else tcp_init_server(si);
  }
  
  return 0;

bad:
  if (si)
    kfree((char*)si);
  if (*f)
    fileclose(*f);
  return -1;
}

int
sockaccept(struct sock *osi, struct file **f)
{
  // only for tcp
  if (osi->type != SOCK_STREAM) {
    return -1;
  }

  // only for listen sock
  if (osi->rport != 0 || osi->raddr != 0 || osi->tcpcb.parent) {
    return -1;
  }

  struct sock *si = 0;
  *f = 0;
  if ((*f = filealloc()) == 0)
    goto bad;
  if ((si = (struct sock*)kalloc()) == 0)
    goto bad;
 
  si->raddr = 0;
  si->lport = osi->lport;
  si->rport = 0;
  si->type = osi->type;
  si->tcpcb = osi->tcpcb;
  si->tcpcb.parent = &osi->tcpcb;
  initlock(&si->lock, "sock");
  mbufq_init(&si->rxq);
  (*f)->type = FD_SOCK;
  (*f)->readable = 1;
  (*f)->writable = 1;
  (*f)->sock = si;

  if (sock_hashtable_add(si->lport, 0, 0, si) < 0) goto bad;

  return tcp_api_accept(si);
bad:
  if (si)
    kfree((char*)si);
  if (*f)
    fileclose(*f);
  return -1;    
}

void
sockclose(struct sock *si)
{
  struct mbuf *m;

  if (si->type == SOCK_STREAM) {
    tcp_api_close(si);
  }

  // remove from list of sockets
  sock_hashtable_remove(si);

  // free any pending mbufs
  while (!mbufq_empty(&si->rxq)) {
    printf("close mbufq_pophead\n");
    m = mbufq_pophead(&si->rxq);
    mbuffree(m);
  }

  kfree((char*)si);
}

int
sockread(struct sock *si, uint64 addr, int n)
{
  return sockread1(si, addr, n, 0, 0);
}

int
sockread1(struct sock *si, uint64 addr, int n, uint32 *sip, uint16 *sport)
{
  struct proc *pr = myproc();
  struct mbuf *m;
  int len;
  if (si->type == SOCK_STREAM) {
    return tcp_api_receive(si, addr, n, pr);
  }
  acquire(&si->lock);
  while (mbufq_empty(&si->rxq) && !pr->killed) {
    sleep(&si->rxq, &si->lock);
  }
  if (pr->killed) {
    release(&si->lock);
    return -1;
  }
  m = mbufq_pophead(&si->rxq);
  if (sip) {
    if (copyout(pr->pagetable, (uint64)sip, (char *) &m->sip, sizeof(m->sip))) goto bad;
  }
  if (sport) {
    if (copyout(pr->pagetable, (uint64)sport, (char *) &m->sport, sizeof(m->sport))) goto bad;
  }
  
  release(&si->lock);

  len = m->len;
  if (len > n)
    len = n;
  if (copyout(pr->pagetable, addr, m->head, len)) goto bad;
  
  mbuffree(m);
  
  return len;
bad:
  mbuffree(m);
  return -1;  
}

int
sockwrite(struct sock *si, uint64 addr, int n)
{
  return sockwrite1(si, addr, n, si->raddr, si->rport);
}

int
sockwrite1(struct sock *si, uint64 addr, int n, int rip, int rport)
{
  struct proc *pr = myproc();
  struct mbuf *m;

  m = mbufalloc(MBUF_DEFAULT_HEADROOM);
  if (!m)
    return -1;

  if (copyin(pr->pagetable, mbufput(m, n), addr, n) == -1) {
    mbuffree(m);
    return -1;
  }
  if (si->type == SOCK_STREAM) {
    // don't consider n > bufsize
    return tcp_api_send(m, si, TCP_FLG_PSH | TCP_FLG_ACK, n);
  } else if (si->type == SOCK_DGRAM) {
    net_tx_udp(m, rip, si->lport, rport);
  } else {
    net_tx_icmp(m, rip);
  }
  return n;
}

// called by protocol handler layer to deliver UDP packets
void
sockrecvudp(struct mbuf *m, uint32 raddr, uint16 lport, uint16 rport)
{
  //
  // Find the socket that handles this mbuf and deliver it, waking
  // any sleeping reader. Free the mbuf if there are no sockets
  // registered to handle it.
  //
  struct sock *si = 0;
  if (sock_hashtable_get(lport, raddr, rport, &si) == 0)
    goto found;
  mbuffree(m);
  return;

found:
  acquire(&si->lock);
  m->sip = raddr;
  m->sport = rport;
  mbufq_pushtail(&si->rxq, m);
  wakeup(&si->rxq);
  release(&si->lock);
}
