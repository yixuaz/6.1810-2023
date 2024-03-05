typedef unsigned long size_t;
typedef long int off_t;
enum vmatype;
struct buf;
struct context;
struct file;
struct inode;
struct pipe;
struct proc;
struct spinlock;
struct sleeplock;
struct stat;
struct superblock;
struct mbuf;
struct sock;
struct tcp_cb;
struct ip;

// bio.c
void            binit(void);
struct buf*     bread(uint, uint);
void            brelse(struct buf*);
void            bwrite(struct buf*);
void            bpin(struct buf*);
void            bunpin(struct buf*);
void            bunpin2(uint64);

// console.c
void            consoleinit(void);
void            consoleintr(int (*getc)(void));
void            consputc(int);

// exec.c
int             exec(char*, char**);

// file.c
struct file*    filealloc(void);
void            fileclose(struct file*);
struct file*    filedup(struct file*);
void            fileinit(void);
int             fileread(struct file*, uint64, int n);
int             filestat(struct file*, uint64 addr);
int             filewrite(struct file*, uint64, int n);

// fs.c
void            fsinit(int);
int             dirlink(struct inode*, char*, uint);
struct inode*   dirlookup(struct inode*, char*, uint*);
struct inode*   ialloc(uint, short);
struct inode*   idup(struct inode*);
void            iinit();
void            ilock(struct inode*);
void            iput(struct inode*);
void            iunlock(struct inode*);
void            iunlockput(struct inode*);
void            iupdate(struct inode*);
int             namecmp(const char*, const char*);
struct inode*   namei(char*);
struct inode*   nameiparent(char*, char*);
int             readi(struct inode*, int, uint64, uint, uint);
void            stati(struct inode*, struct stat*);
int             writei(struct inode*, int, uint64, uint, uint);
void            itrunc(struct inode*);
uint64          readblock(struct inode *ip, uint off);

// ramdisk.c
void            ramdiskinit(void);
void            ramdiskintr(void);
void            ramdiskrw(struct buf*);

// kalloc.c
void*           kalloc(void);
void*           kalloc_cow(pte_t *);
void            kfree(void *);
void*           kalloc_suppage(void);
void            kfree_suppage(void *);
void            kinit(void);
int             kincget(void *);
uint            saved_page(int);
uint64          saved_byte(int);
int             kcollect(void);

// log.c
void            initlog(int, struct superblock*);
void            log_write(struct buf*);
void            begin_op(void);
void            end_op(void);

// pipe.c
int             pipealloc(struct file**, struct file**);
void            pipeclose(struct pipe*, int);
int             piperead(struct pipe*, uint64, int);
int             pipewrite(struct pipe*, uint64, int);

// printf.c
void            printf(char*, ...);
void            panic(char*) __attribute__((noreturn));
void            printfinit(void);

// proc.c
int             cpuid(void);
void            exit(int);
int             fork(void);
int             clone(void(*)(void*, void*), void *, void *, void *);
int             join(void **);
int             growproc(int);
void            proc_mapstacks(pagetable_t);
pagetable_t     proc_pagetable(struct proc *);
void            proc_freepagetable(pagetable_t, uint64);
void            proc_freepagetable_from_zero(pagetable_t, uint64);
int             kill(int);
int             killed(struct proc*);
void            setkilled(struct proc*);
struct cpu*     mycpu(void);
struct cpu*     getmycpu(void);
struct proc*    myproc();
void            procinit(void);
void            scheduler(void) __attribute__((noreturn));
void            sched(void);
void            sleep(void*, struct spinlock*);
void            userinit(void);
int             sendsignal(int signal, int pid);
int             wait(uint64);
void            wakeup(void*);
void            yield(void);
int             either_copyout(int user_dst, uint64 dst, void *src, uint64 len);
int             either_copyin(void *dst, int user_src, uint64 src, uint64 len);
void            procdump(void);
uint64          find_swapping_page(int *, struct spinlock *);
void            update_page_age(void);
int             proc_number(void);
void            calculate_load_1m(void);
float           get_avgload_1m(void);
struct proc*    get_proc_by_idx(int idx);
struct proc*    get_proc_by_pid(int pid);

// procfs.c
void            procfsinit(void);

// signal.c
void            signal_handle(struct proc *p);
void            signal_handler_clear(struct proc *p);

// mmap.c
uint64          vma_create(uint64 addr, size_t len, int prot, int flags, struct file *f, struct inode *ip, off_t offset, size_t filesz, enum vmatype type);
uint64          munmap(uint64 addr, size_t len);
void            mmap_clean(struct proc *p);
int             vma_handle(struct proc *p, uint64 va);
uint64          vma_sbrk(struct proc *p, int n);
void            vma_exec_clear(struct proc *p);
void            vma_fork(struct proc *p, struct proc *np);

// swap.c
uint64 pageout(pte_t *pte);
int pagein(pte_t *pte, struct sleeplock *slock);
uint8 pageage(pte_t *pte);
void initswap(int dev, struct superblock *sb);
int swappageclone(pte_t *pte, pagetable_t new, int va);
int swapdecget(int idx);
int swapcollect(void);

// swtch.S
void            swtch(struct context*, struct context*);

// spinlock.c
void            acquire(struct spinlock*);
int             holding(struct spinlock*);
void            initlock(struct spinlock*, char*);
void            release(struct spinlock*);
void            push_off(void);
void            pop_off(void);
int             atomic_read4(int *addr);
void            freelock(struct spinlock*);

// sleeplock.c
void            acquiresleep(struct sleeplock*);
void            releasesleep(struct sleeplock*);
int             holdingsleep(struct sleeplock*);
void            initsleeplock(struct sleeplock*, char*);

// string.c
int             memcmp(const void*, const void*, uint);
void*           memmove(void*, const void*, uint);
void*           memset(void*, int, uint);
char*           safestrcpy(char*, const char*, int);
int             strlen(const char*);
int             strncmp(const char*, const char*, uint);
char*           strncpy(char*, const char*, int);

// syscall.c
void            argint(int, int*);
int             argstr(int, char*, int);
void            argaddr(int, uint64 *);
int             fetchstr(uint64, char*, int);
int             fetchaddr(uint64, uint64*);
void            syscall();

// trap.c
extern uint     ticks;
void            trapinit(void);
void            trapinithart(void);
extern struct spinlock tickslock;
void            usertrapret(void);

// uart.c
void            uartinit(void);
void            uartintr(void);
void            uartputc(int);
void            uartputc_sync(int);
int             uartgetc(void);

// vm.c
void            kvminit(void);
void            kvminithart(void);
void            kvmmap(pagetable_t, uint64, uint64, uint64, int);
uint64          kwalkaddr(pagetable_t, uint64);
int             mappages(pagetable_t, uint64, uint64, uint64, int);
pagetable_t     uvmcreate(void);
void            uvmfirst(pagetable_t, uchar *, uint);
uint64          uvmalloc(pagetable_t, uint64, uint64, int);
uint64          uvmdealloc(pagetable_t, uint64, uint64);
int             uvmcopy(pagetable_t, pagetable_t, uint64);
void            uvmfree(pagetable_t, uint64);
void            uvmunmap(pagetable_t, uint64, uint64, int);
void            uvmclear(pagetable_t, uint64);
int             setguardpage(struct proc *);
void            rmguardpage(struct proc *);
pte_t *         walk(pagetable_t, uint64, int);
uint64          walkaddr(pagetable_t, uint64);
int             copyonwrite(pte_t *);
int             copyout(pagetable_t, uint64, char *, uint64);
int             copyin(pagetable_t, char *, uint64, uint64);
int             copyinstr(pagetable_t, char *, uint64, uint64);
void            vmprint(pagetable_t);
void            vmprint_abbr(pagetable_t);

// plic.c
void            plicinit(void);
void            plicinithart(void);
int             plic_claim(void);
void            plic_complete(int);

// virtio_disk.c
void            virtio_disk_init(void);
void            virtio_disk_rw(struct buf *, int);
void            virtio_disk_intr(void);

// debugtbl.c
int            initdebugtbl(void);
void           backtrace(void);

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))



// stats.c
void            statsinit(void);
void            statsinc(void);

// sprintf.c
int             snprintf(char*, int, char*, ...);



#ifdef KCSAN
void            kcsaninit();
#endif

// pci.c
void            pci_init();

// e1000.c
void            e1000_init(uint32 *);
void            e1000_intr(void);
void            e1000_cache(struct mbuf*);

// net.c
void            netinit();
void            net_rx(struct mbuf*);
void            net_tx_udp(struct mbuf*, uint32, uint16, uint16);

// sysnet.c
void            sockinit(void);
int             sockalloc(struct file **, uint32, uint16, uint16, int, int);
void            sockclose(struct sock *);
int             sockaccept(struct sock *, struct file **);
int             sockread(struct sock *, uint64, int);
int             sockread1(struct sock *si, uint64, int, uint32 *, uint16 *);
int             sockwrite(struct sock *, uint64, int);
int             sockwrite1(struct sock *, uint64, int, int, int);
void            sockrecvudp(struct mbuf*, uint32, uint16, uint16);
int             sock_hashtable_get(uint16, uint32, uint16,struct sock **);
int             sock_hashtable_update(struct sock *, uint32, uint16);

// rand.c
void            srand(int seed);
int             rand();

// tcp.c
void            tcpinit();
int             tcp_init_client(struct sock *);
int             tcp_init_server(struct sock *);
int             tcp_api_accept(struct sock *);
int             tcp_api_send(struct mbuf *, struct sock *, uint8, int);
int             tcp_api_close(struct sock *);
int             tcp_api_receive(struct sock *, uint64, int, struct proc *);
void            net_rx_tcp(struct mbuf *, uint16, struct ip *);

// icmp.c
void            net_tx_icmp(struct mbuf *m, int rip);
void            net_rx_icmp(struct mbuf *m, uint16 len, struct ip *iphdr);
// sysfile.c
int      argfd(int, int *, struct file **);
int      fdalloc(struct file *);

// net.c
void            net_tx_ip(struct mbuf *, uint8, uint32);
