#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16

static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
// 还有一个传输环，驱动程序将需要E1000发送的数据包放入其中
static struct mbuf *tx_mbufs[TX_RING_SIZE];
// queue egress packets in software and only provide a limited number to the NIC at any one time
struct mbufq txq;

#define RX_RING_SIZE 16
// rx_desc描述描述符格式。描述符数组称为接收环或接收队列
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
// 数据包缓冲区
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// 全局变量regs包含指向E1000第一个控制寄存器的指针；
// 您的驱动程序可以通过将regs索引为数组来获取其他寄存器。
// 您需要特别使用索引 E1000_RDT 和 E1000_TDT 
// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");
  mbufq_init(&txq);

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  // Allocate a region of memory for the transmit descriptor list.
  memset(tx_ring, 0, sizeof(tx_ring));

  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  // Program the Transmit Descriptor Base Address(TDBAL) register(s) with the address of the region
  regs[E1000_TDBAL] = (uint64) tx_ring;
  // This register must be 128-byte aligned.
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  // Set the Transmit Descriptor Length (TDLEN) register to the size (in bytes) of the descriptor ring.  
  regs[E1000_TDLEN] = sizeof(tx_ring);
  // The Transmit Descriptor Head and Tail (TDH/TDT) registers are initialized (by hardware) to 0b 
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    // e1000_init()使用mbufalloc()为要进行DMA的E1000分配mbuf数据包缓冲区
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }

  // Program the Receive Descriptor Base Address 
  // (RDBAL/RDBAH) register(s) with the address of the region
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");

  // Head should point to the first valid receive descriptor in the 
  // descriptor ring and tail should point to one descriptor beyond the last valid descriptor in the 
  // descriptor ring.  
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  // Set the Receive Descriptor Length (RDLEN) register to the size (in bytes) of the descriptor ring
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive and transmit interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  // rely on TX interrupts to refill the transmit ring
  regs[E1000_TADV] = 0x800; // Counts in units of 1.024 us
  regs[E1000_IMS] = E1000_RXDW | E1000_TXDW; 

}

int
e1000_context_desc_transmit(struct mbuf *m)
{
  int tdt = regs[E1000_TDT], tdh = regs[E1000_TDH];
  if ( (tdt + 1) % TX_RING_SIZE == tdh ||
    (tx_ring[tdt].status & E1000_TXD_STAT_DD) == 0) {
    release(&e1000_lock);
    return -1;
  }
  uint16 ipcse = 33; 
  uint32 ipcso = 24, ipcss = 14;
  tx_ring[tdt].addr = (ipcse << 16) | (ipcso << 8) | ipcss; //TUCSE, TUCSO, TUCSS, IPCSE, IPCSO, IPCSS

  int enableIp = 2;
  tx_ring[tdt].cmd = E1000_TXD_CMD_DEXT | E1000_TXD_CMD_RS | E1000_TXD_CMD_IDE | enableIp; //TUCMD

  if (m->checksum_offload & MBUF_CSUM_OFLD_TCP) {
    // Setting TUCSE field to 0b indicates that the checksum covers from TUCCS to the end of the packet.
    uint64 tucse = 0;
    uint64 tucso = 50, tucss = 34;
    tx_ring[tdt].addr |= ((tucse << 48) | (tucso << 40) | (tucss << 32));

    tx_ring[tdt].length = m->len; 
    int enabletcp = 1;
    tx_ring[tdt].cmd |= (enabletcp | E1000_TXD_CMD_TSE);
    tx_ring[tdt].special = TCP_CB_WND_SIZE; // MSS
    tx_ring[tdt].css = tucso + 4; // HDRLEN
  }
  
  regs[E1000_TDT] = (tdt + 1) % TX_RING_SIZE;
  return 0;
}

// 在RAM中读取和写入描述符环外，您的驱动程序还需要通过其内存映射控制寄存器与E1000交互，
// 以检测接收到数据包何时可用，并通知E1000驱动程序已经用要发送的数据包填充了一些TX描述符.

// 完成实验后，E1000驱动程序将发送数据包，qemu将其发送到主机，
// make server将看到它并发送响应数据包，然后E1000驱动程序和nettests将看到响应数据包

// 当net.c中的网络栈需要发送数据包时，
// 它会调用e1000_transmit()，并使用一个保存要发送的数据包的mbuf作为参数
int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //

  // struct tx_desc tx_ring[TX_RING_SIZE]
  // 传输代码必须在TX（传输）环 tx_ring 的描述符中放置指向数据包数据的指针

  // 您需要确保每个mbuf最终被释放，但只能在E1000完成数据包传输之后
  // E1000在描述符中设置E1000_TXD_STAT_DD位以指示此情况
  // 首先，通过读取E1000_TDT控制寄存器，向E1000询问等待下一个数据包的TX环索引
  
  if (m->checksum_offload & MBUF_CSUM_OFLD_ENABLE) {
    if (e1000_context_desc_transmit(m) < 0) return -1;
  }

  int tdt = regs[E1000_TDT], tdh = regs[E1000_TDH];
  // 然后检查环是否溢出。如果E1000_TXD_STAT_DD未在E1000_TDT索引的描述符中设置，
  // 则E1000尚未完成先前相应的传输请求，因此返回错误。
  if ( (tdt + 1) % TX_RING_SIZE == tdh ||
    (tx_ring[tdt].status & E1000_TXD_STAT_DD) == 0) {
    release(&e1000_lock);
    return -1;
  }
  // 使用 mbuffree()释放从该描述符传输的最后一个mbuf（如果有）
  if (tx_mbufs[tdt] != 0)
    mbuffree(tx_mbufs[tdt]);
  // 填写描述符
  tx_ring[tdt].addr = (uint64)m->head;
  tx_ring[tdt].length = m->len;
  tx_ring[tdt].cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP | E1000_TXD_CMD_IDE;
  if (m->checksum_offload & MBUF_CSUM_OFLD_ENABLE) {
    tx_ring[tdt].cmd |= E1000_TXD_CMD_DEXT;
    tx_ring[tdt].cso = E1000_TXD_DTYP_D;
    tx_ring[tdt].css = E1000_TXD_POPTS_IXSM;
    if (m->checksum_offload & MBUF_CSUM_OFLD_TCP) {
      tx_ring[tdt].cmd |= E1000_TXD_CMD_TSE;
      tx_ring[tdt].css |= E1000_TXD_POPTS_TXSM;
    }
  }
  
  tx_mbufs[tdt] = m;
  regs[E1000_TDT] = (tdt + 1) % TX_RING_SIZE;
  return 0;
}

void e1000_send()
{
  int i = 0;
  acquire(&e1000_lock);
  while (!mbufq_empty(&txq)) {
    struct mbuf *cur = mbufq_pophead(&txq);
    if (e1000_transmit(cur))
      mbuffree(cur);
    i++;  
  }
  release(&e1000_lock);
  // if (i > 1) printf("%d\n", i);
}

void
e1000_cache(struct mbuf *m)
{
  acquire(&e1000_lock);
  // queue egress packets in software 
  mbufq_pushtail(&txq, m);
  release(&e1000_lock);
  // tx ring is empty, file an TXDW interupt to call e1000_send
  if (regs[E1000_TDT] == regs[E1000_TDH]) 
    regs[E1000_ICS] = E1000_TXDW;
}

// 当E1000从以太网接收到每个包时，它首先将包DMA到下一个RX(接收)环描述符rx_ring指向的mbuf
static void
e1000_recv(void)
{
  // 必须扫描RX环，并通过调用net_rx()将每个新数据包的mbuf发送到网络栈（在net.c中）

  // 然后，您需要分配一个新的mbuf并将其放入描述符中，以便当E1000再次到达RX环中的该点时，它会找到一个新的缓冲区，以便DMA新数据包

  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //

  // 首先通过提取E1000_RDT控制寄存器并加一对RX_RING_SIZE取模，
  // 向E1000询问下一个等待接收数据包（如果有）所在的环索引。
  int rdt = regs[E1000_RDT];
  while ((rx_ring[(rdt + 1) % RX_RING_SIZE].status & E1000_RXD_STAT_DD)) {
    rdt = (rdt + 1) % RX_RING_SIZE;
    rx_mbufs[rdt]->len = rx_ring[rdt].length;
    net_rx(rx_mbufs[rdt]);
    rx_mbufs[rdt] = mbufalloc(0);
    rx_ring[rdt].addr = (uint64) rx_mbufs[rdt]->head;
    rx_ring[rdt].status = 0;
  }
  regs[E1000_RDT] = rdt;
}

void
e1000_intr(void)
{
  // avoid live lock -> disable recieve interrupt
  regs[E1000_IMC] = E1000_RXDW; 

  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  int reg = regs[E1000_ICR];
  // only reset the cause being triggered
  regs[E1000_ICR] = reg;
  // only provide a limited number to the NIC at any one time
  if (reg & E1000_TXDW)
    e1000_send();
  if (reg & E1000_RXDW)
    e1000_recv();

  // reenable receive and transmit interrupt after finished reading all
  regs[E1000_IMS] = E1000_RXDW | E1000_TXDW;   
}
