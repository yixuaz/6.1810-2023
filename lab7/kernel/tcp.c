//
// networking protocol support (TCP, etc.).
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "net.h"
#include "tcp.h"
#include "defs.h"

struct spinlock tcplock;

void tcpinit()
{
  initlock(&tcplock, "tcp_lock");
  srand(ticks);
}

extern uint32 local_ip;
// uint16 cksum16(uint8 *data, uint16 size, uint32 init) {
// 	uint32 sum;
// 	sum = init;
// 	while(size > 1) {
// 		sum += (*(data++)) << 8;
// 		sum += (*(data++));
// 		size -= 2;
// 	}
// 	if (size) {
// 		sum += (*data) << 8;
// 	}
// 	sum = (sum & 0xffff) + (sum >> 16);
// 	sum = (sum & 0xffff) + (sum >> 16);
// 	return ~sum;
// }

// static uint16 
// tcp_checksum(uint32 ip_src, uint32 ip_dst, uint8 ip_p, 
// struct tcp *tcphdr, uint16 len) {

//   uint32 pseudo = 0;

//   pseudo += ntohs((ip_src >> 16) & 0xffff);
//   pseudo += ntohs(ip_src & 0xffff);
//   pseudo += ntohs((ip_dst >> 16) & 0xffff);
//   pseudo += ntohs(ip_dst & 0xffff);
//   pseudo += (uint16)ip_p;
//   pseudo += len;
//   uint32 res = cksum16((uint8 *)tcphdr, len, pseudo);
//   return res;
// }

static uint16 
tcp_partial_checksum(uint32 ip_src, uint32 ip_dst, uint8 ip_p) {

  uint32 pseudo = 0;

  pseudo += ntohs((ip_src >> 16) & 0xffff);
  pseudo += ntohs(ip_src & 0xffff);
  pseudo += ntohs((ip_dst >> 16) & 0xffff);
  pseudo += ntohs(ip_dst & 0xffff);
  pseudo += (uint16)ip_p;

  pseudo = (pseudo >> 16) + (pseudo & 0xffff);
  
  return htons(pseudo);
}

int
net_tx_tcp_content(struct mbuf *m, struct sock *si, 
           uint32 seq, uint32 ack, uint8 flags, int content_len)
{
  if (!m)
    return -1;

  uint32 dip;
  uint16 sport, dport;

  dip = si->raddr;
  sport = si->lport;
  dport = si->rport;

  struct tcp *tcphdr;

  // Put the TCP header
  tcphdr = mbufpushhdr(m, *tcphdr);
  tcphdr->sport = htons(sport);
  tcphdr->dport = htons(dport);
  tcphdr->seq = htonl(seq);
  tcphdr->ack = htonl(ack);
  tcphdr->off = (sizeof(struct tcp) / 4) << 4; // TCP header length in 32-bit words
  tcphdr->flags = flags;
  tcphdr->win = si->tcpcb.rcv.wnd;
  tcphdr->sum = tcp_partial_checksum(htonl(local_ip), htonl(dip), IPPROTO_TCP);
  tcphdr->urp = 0; // Urgent pointer, not used in this minimal implementation
  // uint16 sum = tcp_checksum(htonl(local_ip), htonl(dip), IPPROTO_TCP, tcphdr, content_len + sizeof(struct tcp)); 
  // tcphdr->sum = htons(sum);

  // Now on to the IP layer
  m->checksum_offload |= MBUF_CSUM_OFLD_TCP;
  net_tx_ip(m, IPPROTO_TCP, dip);

  return 0;
}           

int
net_tx_tcp_signal(struct sock *si, 
           uint32 seq, uint32 ack, uint8 flags)
{
  struct mbuf *m = mbufalloc(MBUF_DEFAULT_HEADROOM);
  return net_tx_tcp_content(m, si, seq, ack, flags, 0);  
}

void
sockrecvtcp(struct mbuf *m, uint32 raddr, uint16 lport, uint16 rport, int ispush)
{
  struct sock *si = 0;
  if (sock_hashtable_get(lport, raddr, rport, &si) == 0 && si->raddr != 0 && si->rport != 0)
    goto found;
  mbuffree(m);
  return;
found:
  m->sip = raddr;
  m->sport = rport;
  mbufq_pushtail(&si->rxq, m);
  if (ispush) wakeup(&si->rxq);
}

static void tcp_segments_arrives(
  struct sock *si, struct tcp *hdr, struct mbuf *m, 
  uint32 raddr, uint16 lport, uint16 rport, uint32 len)
{
  uint32 seq, ack;
  uint16 win;
  

  struct tcp_cb *cb = &(si->tcpcb);
  
  win = ntohs(hdr->win);
  cb->rcv.wnd = win;
  ack = ntohl(hdr->ack);
	seq = ntohl(hdr->seq);
  switch (cb->state) {
    case TCP_CB_STATE_CLOSED:
      /*
       all data in the incoming segment is discarded.  An incoming
        segment containing a RST is discarded.  An incoming segment not
        containing a RST causes a RST to be sent in response.
      */
      if (!TCP_FLG_ISSET(hdr->flags, TCP_FLG_RST)) {
        /*If the ACK bit is off, sequence number zero is used,
          <SEQ=0><ACK=SEG.SEQ+SEG.LEN><CTL=RST,ACK>
        If the ACK bit is on,
          <SEQ=SEG.ACK><CTL=RST>
        Return.
        */
        if (!TCP_FLG_ISSET(hdr->flags, TCP_FLG_ACK))
          net_tx_tcp_signal(si, 0, seq+len, TCP_FLG_RST|TCP_FLG_ACK);
        else
          net_tx_tcp_signal(si, ack, 0, TCP_FLG_RST);
      }
    case TCP_CB_STATE_LISTEN:
      if (TCP_FLG_ISSET(hdr->flags, TCP_FLG_RST)) return; // An incoming RST should be ignored.  Return.
      if (TCP_FLG_ISSET(hdr->flags, TCP_FLG_ACK)) {
        /*
        Any acknowledgment is bad if it arrives on a connection still in
        the LISTEN state.  An acceptable reset segment should be formed
        for any arriving ACK-bearing segment.  The RST should be
        formatted as follows:
          <SEQ=SEG.ACK><CTL=RST>
        Return.
        */
        net_tx_tcp_signal(si, ack, 0, TCP_FLG_RST);
        return;
      }
      if (TCP_FLG_ISSET(hdr->flags, TCP_FLG_SYN)) {
        // Set RCV.NXT to SEG.SEQ+1, IRS is set to SEG.SEQ and any other
        // control or text should be queued for processing later.
        cb->rcv.nxt = seq + 1;
        cb->irs = seq;
        // ISS should be selected and a SYN segment sent of the form:
        cb->iss = (uint32)rand();
        // <SEQ=ISS><ACK=RCV.NXT><CTL=SYN,ACK>
        net_tx_tcp_signal(si, cb->iss, cb->rcv.nxt, TCP_FLG_SYN | TCP_FLG_ACK);
        // SND.NXT is set to ISS+1 and SND.UNA to ISS.  
        cb->snd.nxt = cb->iss + 1;
        cb->snd.una = cb->iss;
        // The connection state should be changed to SYN-RECEIVED.
        cb->state = TCP_CB_STATE_SYN_RCVD;
      }
      // unlikely to get here, but if you do, drop the segment, and return.
      return;
    case TCP_CB_STATE_SYN_SENT:
      int ack_acceptable = 0;
      if (TCP_FLG_ISSET(hdr->flags, TCP_FLG_ACK)) {
        // If SEG.ACK =< ISS, or SEG.ACK > SND.NXT
        if (ack <= cb->iss || ack > cb->snd.nxt) {
          // send a reset (unless the RST bit is set, if so drop the segment and return)
          if (!TCP_FLG_ISSET(hdr->flags, TCP_FLG_RST)) {
            // <SEQ=SEG.ACK><CTL=RST>
            net_tx_tcp_signal(si, ack, 0, TCP_FLG_RST);
          }
          return;
        }
        // If SND.UNA =< SEG.ACK =< SND.NXT then the ACK is acceptable.
        ack_acceptable = (ack >= cb->snd.una && ack <= cb->snd.nxt);
      }
      if (TCP_FLG_ISSET(hdr->flags, TCP_FLG_RST)) {
        /* TODO:
        If the ACK was acceptable then signal the user "error:
          connection reset", drop the segment, enter CLOSED state,
          delete TCB, and return.  Otherwise (no ACK) drop the segment
          and return.
        */
        if (ack_acceptable) {
          cb->state = TCP_CB_STATE_CLOSED;
        }
        return;
      }
      // TODO: third check the security and precedence

      // fourth check the SYN bit
      // This step should be reached only if the ACK is ok, or there is
      //  no ACK, and it the segment did not contain a RST.
      if (!ack_acceptable && !(!TCP_FLG_ISSET(hdr->flags, TCP_FLG_ACK) && !TCP_FLG_ISSET(hdr->flags, TCP_FLG_RST)))
        panic("TCP_CB_STATE_SYN_SENT error");
      if (TCP_FLG_ISSET(hdr->flags, TCP_FLG_SYN)) {
        
        //  RCV.NXT is set to SEG.SEQ+1, IRS is set to SEG.SEQ.
        cb->rcv.nxt = seq + 1;
        cb->irs = seq;
        
        // SND.UNA should be advanced to equal SEG.ACK (if there is an ACK)
        if (ack_acceptable) {
          cb->snd.una = ack;
        }
        // If SND.UNA > ISS (our SYN has been ACKed), change the connection
        // state to ESTABLISHED, form an ACK segment <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK>
        if (cb->snd.una > cb->iss) {
          cb->state = TCP_CB_STATE_ESTABLISHED;
          net_tx_tcp_signal(si, cb->snd.nxt, cb->rcv.nxt, TCP_FLG_ACK);
          wakeup(cb);
        } else {
          // Otherwise enter SYN-RECEIVED, form a SYN,
          // ACK segment <SEQ=ISS><ACK=RCV.NXT><CTL=SYN,ACK>
          cb->state = TCP_CB_STATE_SYN_RCVD;
          net_tx_tcp_signal(si, cb->iss, cb->rcv.nxt, TCP_FLG_SYN | TCP_FLG_ACK);
        }
        return;
      }
      // fifth, if neither of the SYN or RST bits is set then drop the segment and return.
      return;
    default:
      break; 
  }
  // Otherwise,
  // first check sequence number
  /*
  Segment Receive  Test
  Length  Window
  ------- -------  -------------------------------------------
      0       0     SEG.SEQ = RCV.NXT
      0      >0     RCV.NXT =< SEG.SEQ < RCV.NXT+RCV.WND
      >0      0     not acceptable
      >0     >0     RCV.NXT =< SEG.SEQ < RCV.NXT+RCV.WND
                    or RCV.NXT =< SEG.SEQ+SEG.LEN-1 < RCV.NXT+RCV.WND
  */
  int acceptable = 0, tmp = seq + len - 1;
  
  if ((len == 0 && cb->rcv.wnd == 0 && seq == cb->rcv.nxt) ||
      (len == 0 && cb->rcv.wnd > 0 && seq >= cb->rcv.nxt && seq < cb->rcv.nxt + cb->rcv.wnd) ||
      (len > 0 && cb->rcv.wnd > 0 
        && ((seq >= cb->rcv.nxt && seq < cb->rcv.nxt + cb->rcv.wnd) 
            || (tmp >= cb->rcv.nxt && tmp < cb->rcv.nxt + cb->rcv.wnd))
      ) 
  ) {
  acceptable = 1;
  }
  /*If an incoming segment is not acceptable, an acknowledgment
      should be sent in reply (unless the RST bit is set, if so drop
      the segment and return):
        <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK>
      After sending the acknowledgment, drop the unacceptable segment
      and return.
  */
  if (!acceptable) {
    net_tx_tcp_signal(si, cb->snd.nxt, cb->rcv.nxt, TCP_FLG_ACK); 
    return;
  }

  // second check the RST bit
  // simplify to enter the CLOSED state, delete the TCB, and return.
  if (TCP_FLG_ISSET(hdr->flags, TCP_FLG_RST)) {
    cb->state = TCP_CB_STATE_CLOSED;
    return;
  }
  // third check security and precedence, todo
  // fourth, check the SYN bit,
  // send a reset, any outstanding RECEIVEs and SEND should receive "reset" responses, 
  // enter the CLOSED state, delete the TCB, and return.
  if (TCP_FLG_ISSET(hdr->flags, TCP_FLG_SYN)) {
    net_tx_tcp_signal(si, ack, 0, TCP_FLG_RST);
    cb->state = TCP_CB_STATE_CLOSED;
    return;
  }
  // fifth check the ACK field,
  // if the ACK bit is off drop the segment and return
  if (!TCP_FLG_ISSET(hdr->flags, TCP_FLG_ACK)) {
    return;
  }
  switch (cb->state) {
    /*
    SYN-RECEIVED STATE
      If SND.UNA =< SEG.ACK =< SND.NXT then enter ESTABLISHED state
      and continue processing.
        If the segment acknowledgment is not acceptable, form a
        reset segment,
          <SEQ=SEG.ACK><CTL=RST>
        and send it.
    */
    case TCP_CB_STATE_SYN_RCVD:
      if (cb->snd.una <= ack && ack <= cb->snd.nxt) {
        cb->state = TCP_CB_STATE_ESTABLISHED;
        wakeup(cb);
      } else {
        net_tx_tcp_signal(si, ack, 0, TCP_FLG_RST);
        break;
      }
    case TCP_CB_STATE_ESTABLISHED:
    case TCP_CB_STATE_FIN_WAIT1:
    case TCP_CB_STATE_FIN_WAIT2:
    case TCP_CB_STATE_CLOSE_WAIT:
    case TCP_CB_STATE_CLOSING:
      // If SND.UNA < SEG.ACK =< SND.NXT then, set SND.UNA <- SEG.ACK.
      if (cb->snd.una < ack && ack <= cb->snd.nxt) {
        cb->snd.una = ack;
      } else if (ack > cb->snd.nxt) {
        // If the ACK acks something not yet sent (SEG.ACK > SND.NXT) then send an ACK,
        //  drop the segment, and return.
        net_tx_tcp_signal(si, cb->snd.nxt, cb->rcv.nxt, TCP_FLG_ACK);
        return;
      }
      /*
      In addition to the processing for the ESTABLISHED state, if
      our FIN is now acknowledged then enter FIN-WAIT-2 and continue
      processing in that state.
      */
      if (cb->state == TCP_CB_STATE_FIN_WAIT1) {
        if (ack == cb->snd.nxt) {
          cb->state = TCP_CB_STATE_FIN_WAIT2;
        }
      } else if (cb->state == TCP_CB_STATE_FIN_WAIT2) {
        // the user's CLOSE can be acknowledged ("ok") but do not delete the TCB
      } else if (cb->state == TCP_CB_STATE_CLOSING) {
        // if the ACK acknowledges our FIN then enter the TIME-WAIT state, otherwise ignore the segment.  
        if (ack == cb->snd.nxt) {
          cb->state = TCP_CB_STATE_TIME_WAIT;
          wakeup(cb);
        }
        return;
      }
      break;
    case TCP_CB_STATE_LAST_ACK:
      // If our FIN is now acknowledged, 
      // delete the TCB, enter the CLOSED state, and return.
      wakeup(cb);
      if (ack == cb->snd.nxt) {
        cb->state = TCP_CB_STATE_CLOSED;
      }
      return;
    case TCP_CB_STATE_TIME_WAIT:
      // TODO: Acknowledge it, and restart the 2 MSL timeout.
  }
  // TODO: sixth, check the URG bit
  // seventh, process the segment text,
  if (len)
  {
    switch (cb->state) {
      case TCP_CB_STATE_ESTABLISHED:
      case TCP_CB_STATE_FIN_WAIT1:
      case TCP_CB_STATE_FIN_WAIT2:
        sockrecvtcp(m, raddr, lport, rport, TCP_FLG_ISSET(hdr->flags, TCP_FLG_PSH));
        /*
        Once the TCP takes responsibility for the data it advances
        RCV.NXT over the data accepted, and adjusts RCV.WND as
        apporopriate to the current buffer availability.  The total of
        RCV.NXT and RCV.WND should not be reduced.
        */ 
        cb->rcv.nxt = seq + len;
        cb->rcv.wnd -= len;
        net_tx_tcp_signal(si, cb->snd.nxt, cb->rcv.nxt, TCP_FLG_ACK);
        // wakeup(cb);
        break;
      default:
        break;
    }
  }

  //  eighth, check the FIN bit,
  if (TCP_FLG_ISSET(hdr->flags, TCP_FLG_FIN)) {
    // Do not process the FIN if the state is CLOSED, LISTEN or SYN-SENT
    // since the SEG.SEQ cannot be validated; drop the segment and return.
    switch (cb->state) {
      case TCP_CB_STATE_CLOSED:
      case TCP_CB_STATE_LISTEN:
      case TCP_CB_STATE_SYN_SENT:
        return;
    }
    // advance RCV.NXT over the FIN, and send an acknowledgment for the FIN
    cb->rcv.nxt++;
    net_tx_tcp_signal(si, cb->snd.nxt, cb->rcv.nxt, TCP_FLG_ACK);
    switch (cb->state) {
      case TCP_CB_STATE_SYN_RCVD:
      case TCP_CB_STATE_ESTABLISHED:
        acquire(&si->lock);
        cb->state = TCP_CB_STATE_CLOSE_WAIT;
        // wakeup receive tcp
        wakeup(&si->rxq);
        release(&si->lock);
        break;
      case TCP_CB_STATE_FIN_WAIT1:
        cb->state = TCP_CB_STATE_CLOSING;
        break;
      case TCP_CB_STATE_FIN_WAIT2:
        cb->state = TCP_CB_STATE_TIME_WAIT;
        // wakeup close tcp
        wakeup(cb);
        break;
      default:
        break;
    }
    return;
  }
}

// receives a TCP packet
void
net_rx_tcp(struct mbuf *m, uint16 len, struct ip *iphdr)
{
  struct tcp *tcphdr;
  uint32 sip;
  uint16 sport, dport;

  tcphdr = mbufpullhdr(m, *tcphdr);
  if (!tcphdr)
    goto fail; 
  // TODO: validate TCP checksum

  len -= sizeof(*tcphdr);
  if (len > m->len)
    goto fail;
  // minimum packet size could be larger than the payload
  mbuftrim(m, m->len - len);

  // parse the necessary fields
  sip = ntohl(iphdr->ip_src);
  dport = ntohs(tcphdr->dport);
  sport = ntohs(tcphdr->sport);

  struct sock *si = 0;
  if(sock_hashtable_get(dport, sip, sport, &si))
    goto fail;
  // accept syscall should setup si->tcpcb.parent
  
  if (si->raddr == 0 || si->rport == 0) {
    if (si->tcpcb.parent == 0) goto fail;
    if (si->tcpcb.state != TCP_CB_STATE_LISTEN) panic("tcp accept1");
    if (sock_hashtable_update(si, sip, sport) < 0) panic("tcp accept2");
  }
  acquire(&tcplock); 
  tcp_segments_arrives(si, tcphdr, m, sip, dport, sport, len);
  release(&tcplock); 
  return;
fail:
  mbuffree(m);
}

int tcp_api_accept(struct sock *si)
{
  acquire(&tcplock);
  if (si->tcpcb.state != TCP_CB_STATE_LISTEN) {
    release(&tcplock);
    return -1;
  }
  while (si->tcpcb.state == TCP_CB_STATE_LISTEN) {
    sleep(&si->tcpcb, &tcplock);
  }
  release(&tcplock);
  return 0;
}

int tcp_init_server(struct sock *si)
{
  acquire(&tcplock);
  si->tcpcb.state = TCP_CB_STATE_LISTEN;
  release(&tcplock);
  return 0;
}

int tcp_init_client(struct sock *si)
{
  // Create a new transmission control block (TCB) to hold connection state information. 
  // issue a SYN segment
  acquire(&tcplock);
  struct tcp_cb *cb = &(si->tcpcb);
  cb->rcv.wnd = sizeof(cb->window);
  // An initial send sequence number(ISS) is selected
  cb->iss = (uint32)rand();
  // A SYN segment of the form <SEQ=ISS><CTL=SYN> is sent
  net_tx_tcp_signal(si, cb->iss, 0, TCP_FLG_SYN);
  // Set SND.UNA to ISS, SND.NXT to ISS+1, enter SYN-SENT
  // state, and return.
  cb->snd.una = cb->iss;
  cb->snd.nxt = cb->iss + 1;
  cb->state = TCP_CB_STATE_SYN_SENT;

  while (cb->state == TCP_CB_STATE_SYN_SENT) {
    sleep(cb, &tcplock);
  }
  release(&tcplock);
  return 0;
}

int tcp_api_close(struct sock *si)
{
  int ret = 0;
  acquire(&tcplock);
  struct tcp_cb *cb = &(si->tcpcb);
  switch (cb->state) {
    case TCP_CB_STATE_SYN_RCVD:
    /*
    If no SENDs have been issued and there is no pending data to send,
      then form a FIN segment and send it, and enter FIN-WAIT-1 state;
      otherwise queue for processing after entering ESTABLISHED state.
      (since connect is block, we only consider first case)
    */
    case TCP_CB_STATE_ESTABLISHED:
    /*
    Queue this until all preceding SENDs have been segmentized, then
      form a FIN segment and send it.  In any case, enter FIN-WAIT-1
      state.
    */
      net_tx_tcp_signal(si, cb->snd.nxt, cb->rcv.nxt, TCP_FLG_FIN | TCP_FLG_ACK);
      cb->state = TCP_CB_STATE_FIN_WAIT1;
      cb->snd.nxt++;
      sleep(cb, &tcplock);
      break;
    case TCP_CB_STATE_CLOSE_WAIT:
    /* Queue this request until all preceding SENDs have been
      segmentized; then send a FIN segment, enter LAST-ACK state.*/
      net_tx_tcp_signal(si, cb->snd.nxt, cb->rcv.nxt, TCP_FLG_FIN | TCP_FLG_ACK);
      cb->state = TCP_CB_STATE_LAST_ACK;
      cb->snd.nxt++;
      sleep(cb, &tcplock);
      break;
    default:
    /*
    CLOSED STATE (i.e., TCB does not exist)
      If the user does not have access to such a connection, return
      "error:  connection illegal for this process".
      Otherwise, return "error:  connection does not exist".
    LISTEN STATE
      Any outstanding RECEIVEs are returned with "error:  closing"
      responses.  Delete TCB, enter CLOSED state, and return.
    SYN-SENT STATE
      Delete the TCB and return "error:  closing" responses to any
      queued SENDs, or RECEIVEs.
    FIN-WAIT-1 STATE
    FIN-WAIT-2 STATE
      Strictly speaking, this is an error and should receive a "error:
      connection closing" response.  An "ok" response would be
      acceptable, too, as long as a second FIN is not emitted (the first
      FIN may be retransmitted though).  
    CLOSING STATE
    LAST-ACK STATE
    TIME-WAIT STATE
      Respond with "error:  connection closing".  
    */
      ret = -1;
      break;
  }
  release(&tcplock);
  return ret;
}

int tcp_api_send(struct mbuf *m, struct sock *si, uint8 flag, int len)
{
	if (m == 0)
		return -1;
  acquire(&tcplock);  
  struct tcp_cb *cb = &(si->tcpcb);
  int ret = 0;  
	switch(cb->state) {
    case TCP_CB_STATE_LISTEN:
    /*select an ISS.  Send a SYN segment, set
      SND.UNA to ISS, SND.NXT to ISS+1.  Enter SYN-SENT state.*/
      if (si->raddr) {
        cb->iss = rand();
        net_tx_tcp_signal(si, cb->iss, 0, TCP_FLG_SYN);
        cb->state = TCP_CB_STATE_SYN_SENT;
        cb->snd.una = cb->iss;
        cb->snd.nxt = cb->iss+1;
      } else {
        // "error: foreign socket unspecified";
        ret = -1;
      }
      break;
    case TCP_CB_STATE_SYN_SENT:
    case TCP_CB_STATE_SYN_RCVD:
      // todo: push_to_cb_txq(cb, m, cb->snd.nxt, flg, m->len);
      ret = -1;
      break;
    case TCP_CB_STATE_ESTABLISHED:
      // Segmentize the buffer and send it with a piggybacked
      // acknowledgment (acknowledgment value = RCV.NXT)
      net_tx_tcp_content(m, si, cb->snd.nxt, cb->rcv.nxt, flag, len);
      cb->snd.nxt += len;
      break;
    default:
      ret = -1;
	}
  release(&tcplock);
	return ret;
}

int tcp_api_receive(struct sock *si, uint64 addr, int n, struct proc *pr)
{
  struct mbuf *m;
  int len = 0;
  acquire(&tcplock);
  if (mbufq_empty(&si->rxq) && si->tcpcb.state == TCP_CB_STATE_CLOSE_WAIT) goto good;
  while (mbufq_empty(&si->rxq) && !pr->killed) {
    sleep(&si->rxq, &tcplock);
    if (si->tcpcb.state == TCP_CB_STATE_CLOSE_WAIT) goto good;
  }
  if (pr->killed) {
    release(&tcplock);
    return -1;  
  }
  
  while (1) {
    m = mbufq_pophead(&si->rxq);
    int recv_sz = n > m->len ? m->len : n;
    if (copyout(pr->pagetable, addr, m->head, recv_sz)) goto bad;
    addr += recv_sz;
    si->tcpcb.rcv.wnd += recv_sz;
    len += recv_sz;
    if (n < m->len) {
      mbufpull(m, n);
      m->next = si->rxq.head;
	    si->rxq.head = m;
    } else {
      mbuffree(m);
    }
    n -= recv_sz;
    if (n == 0 || mbufq_empty(&si->rxq)) break;
  }
good:
  release(&tcplock);
  return len;
bad:
  mbuffree(m);
  release(&tcplock);
  return -1;    
}
