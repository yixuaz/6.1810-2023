#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "net.h"
#include "tcp.h"
#include "defs.h"
#include "icmp.h"

void
net_tx_icmp(struct mbuf *m, int rip)
{
	// SOCK_RAW only consider ICMP now
  net_tx_ip(m, IPPROTO_ICMP, rip);
}

void
sockrecvicmp(struct mbuf *m, uint32 raddr)
{
  struct sock *si = 0;
  if (sock_hashtable_get(0, raddr, 0, &si) == 0)
    goto found;
  mbuffree(m);
  return;
found:
  acquire(&si->lock);
  mbufq_pushtail(&si->rxq, m);
  wakeup(&si->rxq);
  release(&si->lock);
}

void
net_rx_icmp(struct mbuf *m, uint16 len, struct ip *iphdr)
{
  struct icmp *icmphdr;
  uint8 type;
	
  icmphdr = mbufpullhdr(m, *icmphdr);
  if (!icmphdr)
    goto fail;
	len -= sizeof(*icmphdr);
	if (len > m->len)
    goto fail;
	uint16 padding = m->len - len;	
	mbuftrim(m, padding);
  // parse the necessary fields
  type = ntohs(icmphdr->type);

  if (type == ICMP_ECHOREPLY) {
		uint16 *recv_len = (uint16 *)mbufput(m, 2);
		*recv_len = (uint16)(padding + sizeof(struct ip) + sizeof(struct eth) + sizeof(struct icmp));
		uint8 *ttl = (uint8 *)mbufput(m, 1);
		*ttl = iphdr->ip_ttl;
		sockrecvicmp(m, ntohl(iphdr->ip_src));
		return;
  } else {
		panic("net_rx_icmp");	 
	}

fail:
  mbuffree(m);
}


