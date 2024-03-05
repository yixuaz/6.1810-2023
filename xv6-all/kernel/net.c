//
// networking protocol support (IP, UDP, ARP, etc.).
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "net.h"
#include "defs.h"

uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15); // qemu's idea of the guest IP
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint8 broadcast_mac[ETHADDR_LEN] = { 0xFF, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF };

// arp ip 2 mac mapping
struct arp_entry arp_cache[ARP_MAX_ENTRIES];
int free_arp_idx = 0;

// save request in queue which has no mapping in arp_cache
static struct arpq *arpqs;

// protect arp_cache and free_arp_idx
struct spinlock arplock;
// protect arpq
struct spinlock arpqlock;

static void net_tx_eth(struct mbuf *, uint16 , uint32 , uint16);

void
netinit(void)
{
  initlock(&arplock, "arp_cache");
  initlock(&arpqlock, "arpq");
}

void add_arpq(struct mbuf *m, uint32 dip)
{
  struct arpq *pos, *newarpq;
  acquire(&arpqlock);
  pos = arpqs;
  while (pos) {
    if (pos->ip == dip) {
      mbufq_pushtail(&pos->txq, m);
      release(&arpqlock);
      return;
    }
    pos = pos->next;
  }
  release(&arpqlock);
  if ((newarpq = (struct arpq*)kalloc()) == 0)
    return;
  newarpq->ip = dip;
  mbufq_init(&newarpq->txq);  
  newarpq->next = arpqs;
  mbufq_pushtail(&newarpq->txq, m);
  acquire(&arpqlock);
  arpqs = newarpq;
  release(&arpqlock);
}

void send_remove_arpq(uint32 dip)
{
  struct arpq *pos, *pre = 0;
  acquire(&arpqlock);
  pos = arpqs;
  while (pos) {
    if (pos->ip == dip) {
      while(!mbufq_empty(&pos->txq))
      {
        struct mbuf *cur = mbufq_pophead(&pos->txq);
        net_tx_eth(cur, ETHTYPE_IP, dip, 0);
      }
      if (!pre) {
        arpqs = arpqs->next;
      } else {
        pre->next = pos->next;
      }
      kfree((void *)pos);
      break;
    }
    pre = pos;
    pos = pos->next;
  }
  release(&arpqlock);
}

uint8* arp_lookup(uint32 ip) {
  uint8 *res = 0;
  acquire(&arplock);
  int k = min(free_arp_idx, ARP_MAX_ENTRIES);
  for (int i = 0; i < k; i++) {
    if (arp_cache[i].ip == ip) {
      res = (uint8*)arp_cache[i].mac;
      break;
    }
  }
  release(&arplock);
  return res;
}

int update_arp(uint32 ip, uint8 *mac) {
  acquire(&arplock);
  int res = 0, k = min(free_arp_idx, ARP_MAX_ENTRIES);
  for (int i = 0; i < k; i++) {
    if (arp_cache[i].ip == ip) {
      memmove(arp_cache[i].mac, mac, sizeof(arp_cache[i].mac));
      res = 1;
      break;
    }
  }
  release(&arplock);
  return res;
}

void add_arp(uint32 ip, uint8* mac) {
  acquire(&arplock);
  free_arp_idx++;
  int k = (free_arp_idx-1) % ARP_MAX_ENTRIES;
  arp_cache[k].ip = ip;
  memmove(arp_cache[k].mac, mac, sizeof(arp_cache[k].mac));
  
  release(&arplock);
}


static int net_tx_arp(uint16 op, uint8 dmac[ETHADDR_LEN], uint32 dip);
// Strips data from the start of the buffer and returns a pointer to it.
// Returns 0 if less than the full requested length is available.
char *
mbufpull(struct mbuf *m, unsigned int len)
{
  char *tmp = m->head;
  if (m->len < len)
    return 0;
  m->len -= len;
  m->head += len;
  return tmp;
}

// Prepends data to the beginning of the buffer and returns a pointer to it.
char *
mbufpush(struct mbuf *m, unsigned int len)
{
  m->head -= len;
  if (m->head < m->buf)
    panic("mbufpush");
  m->len += len;
  return m->head;
}

// Appends data to the end of the buffer and returns a pointer to it.
char *
mbufput(struct mbuf *m, unsigned int len)
{
  char *tmp = m->head + m->len;
  m->len += len;
  if (m->len > MBUF_SIZE)
    panic("mbufput");
  return tmp;
}

// Strips data from the end of the buffer and returns a pointer to it.
// Returns 0 if less than the full requested length is available.
char *
mbuftrim(struct mbuf *m, unsigned int len)
{
  if (len > m->len)
    return 0;
  m->len -= len;
  return m->head + m->len;
}

// Allocates a packet buffer.
struct mbuf *
mbufalloc(unsigned int headroom)
{
  struct mbuf *m;
 
  if (headroom > MBUF_SIZE)
    return 0;
  m = kalloc();
  if (m == 0)
    return 0;
  m->next = 0;
  m->head = (char *)m->buf + headroom;
  m->len = 0;
  memset(m->buf, 0, sizeof(m->buf));
  m->sip = 0;
  m->sport = 0;
  m->checksum_offload = 0;
  return m;
}

// Frees a packet buffer.
void
mbuffree(struct mbuf *m)
{
  kfree(m);
}

// Pushes an mbuf to the end of the queue.
void
mbufq_pushtail(struct mbufq *q, struct mbuf *m)
{
  m->next = 0;
  if (!q->head){
    q->head = q->tail = m;
    return;
  }
  q->tail->next = m;
  q->tail = m;
}

// Pops an mbuf from the start of the queue.
struct mbuf *
mbufq_pophead(struct mbufq *q)
{
  struct mbuf *head = q->head;
  if (!head)
    return 0;
  q->head = head->next;
  return head;
}

// Returns one (nonzero) if the queue is empty.
int
mbufq_empty(struct mbufq *q)
{
  return q->head == 0;
}

// Intializes a queue of mbufs.
void
mbufq_init(struct mbufq *q)
{
  q->head = 0;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

// sends an ethernet packet
static void
net_tx_eth(struct mbuf *m, uint16 ethtype, uint32 dip, uint16 op)
{
  struct eth *ethhdr;
  // must call with acquire arp_lock
  uint8 *dest_mac = op == ARP_OP_REQUEST ? broadcast_mac : arp_lookup(dip);
  if (dest_mac) {
    ethhdr = mbufpushhdr(m, *ethhdr);
    memmove(ethhdr->shost, local_mac, ETHADDR_LEN);
    memmove(ethhdr->dhost, dest_mac, ETHADDR_LEN);
    ethhdr->type = htons(ethtype);
    e1000_cache(m);
  } else {
    // informs the caller that it is throwing the
    // packet away (on the assumption the packet will be retransmitted
    // by a higher network layer), and generates an Ethernet packet with
    // a type field of ether_type$ADDRESS_RESOLUTION
    add_arpq(m, dip);
    net_tx_arp(ARP_OP_REQUEST, broadcast_mac, dip);
  }
}

static uint32
net_ip_route(uint32 dip)
{
  // all route to qemu user mode gateway
  // see https://wiki.qemu.org/Documentation/Networking#User_Networking_.28SLIRP.29
  return MAKE_IP_ADDR(10,0,2,2);
}

// sends an IP packet
void
net_tx_ip(struct mbuf *m, uint8 proto, uint32 dip)
{
  struct ip *iphdr;

  // push the IP header
  iphdr = mbufpushhdr(m, *iphdr);
  memset(iphdr, 0, sizeof(*iphdr));
  iphdr->ip_vhl = (4 << 4) | (20 >> 2);
  iphdr->ip_p = proto;
  iphdr->ip_src = htonl(local_ip);
  iphdr->ip_dst = htonl(dip);
  iphdr->ip_len = htons(m->len);
  iphdr->ip_ttl = 100;
  iphdr->ip_sum = 0; 
  // iphdr->ip_sum = in_cksum((unsigned char *)iphdr, sizeof(*iphdr));

  // now on to the ethernet layer, need to check ip routing table here
  m->checksum_offload |= MBUF_CSUM_OFLD_ENABLE;
  net_tx_eth(m, ETHTYPE_IP, net_ip_route(dip), 0);
}

// sends a UDP packet
void
net_tx_udp(struct mbuf *m, uint32 dip,
           uint16 sport, uint16 dport)
{
  struct udp *udphdr;

  // put the UDP header
  udphdr = mbufpushhdr(m, *udphdr);
  udphdr->sport = htons(sport);
  udphdr->dport = htons(dport);
  udphdr->ulen = htons(m->len);
  udphdr->sum = 0; // zero means no checksum is provided

  // now on to the IP layer
  net_tx_ip(m, IPPROTO_UDP, dip);
}

// sends an ARP packet
static int
net_tx_arp(uint16 op, uint8 dmac[ETHADDR_LEN], uint32 dip)
{
  struct mbuf *m;
  struct arp *arphdr;

  m = mbufalloc(MBUF_DEFAULT_HEADROOM);
  if (!m)
    return -1;

  // generic part of ARP header
  arphdr = mbufputhdr(m, *arphdr);
  arphdr->hrd = htons(ARP_HRD_ETHER);
  arphdr->pro = htons(ETHTYPE_IP);
  arphdr->hln = ETHADDR_LEN;
  arphdr->pln = sizeof(uint32);
  arphdr->op = htons(op);

  // ethernet + IP part of ARP header
  memmove(arphdr->sha, local_mac, ETHADDR_LEN);
  arphdr->sip = htonl(local_ip);
  memmove(arphdr->tha, dmac, ETHADDR_LEN);
  arphdr->tip = htonl(dip);

  // header is ready, send the packet
  net_tx_eth(m, ETHTYPE_ARP, dip, op);
  return 0;
}

// receives an ARP packet
static void
net_rx_arp(struct mbuf *m)
{
  struct arp *arphdr;
  uint8 smac[ETHADDR_LEN];
  uint32 sip, tip;

  arphdr = mbufpullhdr(m, *arphdr);
  if (!arphdr)
    goto done;

  // validate the ARP header
  if (ntohs(arphdr->hrd) != ARP_HRD_ETHER ||
      ntohs(arphdr->pro) != ETHTYPE_IP ||
      arphdr->hln != ETHADDR_LEN ||
      arphdr->pln != sizeof(uint32)) {
    goto done;
  }
  int merge_flag = 0;
  memmove(smac, arphdr->sha, ETHADDR_LEN); // sender's ethernet address
  sip = ntohl(arphdr->sip); // sender's IP address (qemu's slirp)
  merge_flag = update_arp(sip, smac);
  tip = ntohl(arphdr->tip); // target IP address
  if (tip != local_ip) goto done;
  if (!merge_flag) add_arp(sip, smac);
  if (ntohs(arphdr->op) == ARP_OP_REQUEST) {
    net_tx_arp(ARP_OP_REPLY, smac, sip);
  } else {
    send_remove_arpq(sip);
  }

done:
  mbuffree(m);
}

// receives a UDP packet
static void
net_rx_udp(struct mbuf *m, uint16 len, struct ip *iphdr)
{
  struct udp *udphdr;
  uint32 sip;
  uint16 sport, dport;


  udphdr = mbufpullhdr(m, *udphdr);
  if (!udphdr)
    goto fail;

  // TODO: validate UDP checksum

  // validate lengths reported in headers
  if (ntohs(udphdr->ulen) != len)
    goto fail;
  len -= sizeof(*udphdr);
  if (len > m->len)
    goto fail;
  // minimum packet size could be larger than the payload
  mbuftrim(m, m->len - len);

  // parse the necessary fields
  sip = ntohl(iphdr->ip_src);
  sport = ntohs(udphdr->sport);
  dport = ntohs(udphdr->dport);
  sockrecvudp(m, sip, dport, sport);
  return;

fail:
  mbuffree(m);
}

// receives an IP packet
static void
net_rx_ip(struct mbuf *m)
{
  struct ip *iphdr;
  uint16 len;

  iphdr = mbufpullhdr(m, *iphdr);
  if (!iphdr)
	  goto fail;

  // check IP version and header len
  if (iphdr->ip_vhl != ((4 << 4) | (20 >> 2)))
    goto fail;
  // validate IP checksum
  if (in_cksum((unsigned char *)iphdr, sizeof(*iphdr)))
    goto fail;
  // can't support fragmented IP packets
  if (htons(iphdr->ip_off) != 0)
    goto fail;
  // is the packet addressed to us?
  if (htonl(iphdr->ip_dst) != local_ip)
    goto fail;

  len = ntohs(iphdr->ip_len) - sizeof(*iphdr);
  if (iphdr->ip_p == IPPROTO_UDP)
    net_rx_udp(m, len, iphdr);
  else if (iphdr->ip_p == IPPROTO_TCP)
    net_rx_tcp(m, len, iphdr);
  else if (iphdr->ip_p == IPPROTO_ICMP)
    net_rx_icmp(m, len, iphdr);
  else
    goto fail;  
  return;

fail:
  mbuffree(m);
}

// called by e1000 driver's interrupt handler to deliver a packet to the
// networking stack
void net_rx(struct mbuf *m)
{
  struct eth *ethhdr;
  uint16 type;

  ethhdr = mbufpullhdr(m, *ethhdr);
  if (!ethhdr) {
    mbuffree(m);
    return;
  }

  type = ntohs(ethhdr->type);
  if (type == ETHTYPE_IP)
    net_rx_ip(m);
  else if (type == ETHTYPE_ARP)
    net_rx_arp(m);
  else
    mbuffree(m);
}
