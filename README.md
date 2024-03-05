# MIT 6.1810 2023
Schedule site
https://pdos.csail.mit.edu/6.828/2023/schedule.html

Study blog for mit 6.1810
https://www.jianshu.com/nb/54437584

`folder lab1-10` is the solution file for each lab

`xv6-all` is a runnable xv6 os that has completed all the optional challenges in the course.

## Target
### 1. Base Correctness 
I combined all the existing tests into one file `./grade-lab-all` to ensure the os can pass all existing tests
<img width="539" alt="1709645345395" src="https://github.com/yixuaz/6.1810-2023/assets/19387492/97e9025c-eb06-4049-8eaf-8497aad3fc0f">

### 2. Optional Challenge Correctness 
I wrote a lot of test cases for each optional challenge task `./grade-lab-oc` to ensure all oc can be passed
<img width="539" alt="1709645345396" src="https://github.com/yixuaz/6.1810-2023/assets/19387492/648f8962-0f41-4c6d-ad2f-36e24da6f637">

## List
### Lab 1 (util)
- [x] Write an uptime program that prints the uptime in terms of ticks using the uptime system call. (easy)
- [x] Support regular expressions in name matching for find. grep.c has some primitive support for regular expressions. (easy)
- [x] The xv6 shell (user/sh.c) is just another user program and you can improve it. It is a minimal shell and lacks many features found in real shell. For example, modify the shell to not print a $ when processing shell commands from a file (moderate), modify the shell to support wait (easy), modify the shell to support lists of commands, separated by ";" (moderate), modify the shell to support sub-shells by implementing "(" and ")" (moderate), modify the shell to support tab completion (easy), modify the shell to keep a history of passed shell commands (moderate), or anything else you would like your shell to do. (If you are very ambitious, you may have to modify the kernel to support the kernel features you need; xv6 doesn't support much.)
![lab1oc](https://github.com/yixuaz/6.1810-2023/assets/19387492/457cbf74-c4f0-44f3-89fa-2eb46aa31636)

### Lab 2 (syscall)
- [x] Print the system call arguments for traced system calls (easy).
- [x] Compute the load average and export it through sysinfo(moderate).
<img width="304" alt="1709649011150" src="https://github.com/yixuaz/6.1810-2023/assets/19387492/795a08b2-61aa-4568-a4f2-30126fcd714f">
      
### Lab 3 (pgtbl)
- [x] Use super-pages to reduce the number of PTEs in page tables. (to avoid break other tests, only `mmap` is allowed to use super page)
- [x] Unmap the first page of a user process so that dereferencing a null pointer will result in a fault. You will have to change user.ld to start the user text segment at, for example, 4096, instead of 0.
Add a system call that reports dirty pages (modified pages) using PTE_D.
<img width="377" alt="1709649070351" src="https://github.com/yixuaz/6.1810-2023/assets/19387492/c31a23c7-957f-42ed-af36-f55fcfe507f4">

### Lab 4 (traps)
- [x] Print the names of the functions and line numbers in backtrace() instead of numerical addresses (hard).
<img width="568" alt="1709649131950" src="https://github.com/yixuaz/6.1810-2023/assets/19387492/2af53a28-1ade-4a1d-b02d-f485d4917669">

### Lab 5 (cow)
- [x] Modify xv6 to support both lazy page allocation and COW.
- [x] Measure how much your COW implementation reduces the number of bytes xv6 copies and the number of physical pages it allocates. Find and exploit opportunities to further reduce those numbers.
<img width="277" alt="1709649170965" src="https://github.com/yixuaz/6.1810-2023/assets/19387492/86067578-9a47-4974-81b1-593ddf345b44">

### Lab 6 (thread)
- [x] The user-level thread package interacts badly with the operating system in several ways. For example, if one user-level thread blocks in a system call, another user-level thread won't run, because the user-level threads scheduler doesn't know that one of its threads has been descheduled by the xv6 scheduler. As another example, two user-level threads will not run concurrently on different cores, because the xv6 scheduler isn't aware that there are multiple threads that could run in parallel. Note that if two user-level threads were to run truly in parallel, this implementation won't work because of several races (e.g., two threads on different processors could call thread_schedule concurrently, select the same runnable thread, and both run it on different processors.)
There are several ways of addressing these problems. One is using scheduler activations and another is to use one kernel thread per user-level thread (as Linux kernels do). Implement one of these ways in xv6. This is not easy to get right; for example, you will need to implement TLB shootdown when updating a page table for a multithreaded user process.
Add locks, condition variables, barriers, etc. to your thread package.
![lab6oc](https://github.com/yixuaz/6.1810-2023/assets/19387492/e7714439-2c4e-4708-9c75-49c2a6c7aa91)


### Lab 7 (network)
- [x] In this lab, the networking stack uses interrupts to handle ingress packet processing, but not egress packet processing. A more sophisticated strategy would be to queue egress packets in software and only provide a limited number to the NIC at any one time. You can then rely on TX interrupts to refill the transmit ring. Using this technique, it becomes possible to prioritize different types of egress traffic. (easy)
- [x] The provided networking code only partially supports ARP. Implement a full ARP cache and wire it in to net_tx_eth(). (moderate)
- [ ] The E1000 supports multiple RX and TX rings. Configure the E1000 to provide a ring pair for each core and modify your networking stack to support multiple rings. Doing so has the potential to increase the throughput that your networking stack can support as well as reduce lock contention. (moderate), but difficult to test/measure. (**I don't find clues in e1000 doc to support this; Let me know if you have**)
- [x] sockrecvudp() uses a singly-linked list to find the destination socket, which is inefficient. Try using a hash table and RCU instead to increase performance. (easy), but a serious implementation would difficult to test/measure
- [x] ICMP can provide notifications of failed networking flows. Detect these notifications and propagate them as errors through the socket system call interface. (**I support ICMP ping**)
- [x] The E1000 supports several stateless hardware offloads, including checksum calculation, RSC, and GRO. Use one or more of these offloads to increase the throughput of your networking stack. (moderate), but hard to test/measure
- [x] The networking stack in this lab is susceptible to receive livelock. Using the material in lecture and the reading assignment, devise and implement a solution to fix it. (moderate), but hard to test.
- [x] Implement a UDP server for xv6. (moderate)
- [x] Implement a minimal TCP stack and download a web page. (hard)
![lab7oc](https://github.com/yixuaz/6.1810-2023/assets/19387492/e490162d-144c-48c0-a9d0-48dbb7cc5058)

### Lab 8 (lock)
- [x] maintain the LRU list so that you evict the least-recently used buffer instead of any buffer that is not in use.
- [x] make lookup in the buffer cache lock-free. Hint: use gcc's __sync_* functions. How do you convince yourself that your implementation is correct? (**I use a lock-free queue and a lock-free array-based map and `CAS` to achieve it**)

### Lab 9 (fs)
- [x] Support triple-indirect blocks. (**only need change some config to support**)

### Lab 10 (mmap)
- [x] If two processes have the same file mmap-ed (as in fork_test), share their physical pages. You will need reference counts on physical pages.
- [x] Your solution probably allocates a new physical page for each page read from the mmap-ed file, even though the data is also in kernel memory in the buffer cache. Modify your implementation to use that physical memory, instead of allocating a new page. This requires that file blocks be the same size as pages (set BSIZE to 4096). You will need to pin mmap-ed blocks into the buffer cache. You will need worry about reference counts.
- [x] Remove redundancy between your implementation for lazy allocation and your implementation of mmap-ed files. (Hint: create a VMA for the lazy allocation area.)
- [x] Modify exec to use a VMA for different sections of the binary so that you get on-demand-paged executables. This will make starting programs faster, because exec will not have to read any data from the file system.
- [x] Implement page-out and page-in: have the kernel move some parts of processes to disk when physical memory is low. Then, page in the paged-out memory when the process references it. (**Allocate specific region in fs for paging; The memory page eviction is the aging algorithm**）
<img width="317" alt="1709652551965" src="https://github.com/yixuaz/6.1810-2023/assets/19387492/4941c268-b131-4d04-afb9-dd6963efa279">

### Signal Framework
- [x] support `signal` to register custom signal handler and IGN handler
- [x] support `SIGINT` to interrupt fg process
- [x] support `sigprocmask` to block signal

### Proc Filesystem
- [x] support read process status in `/proc/{pid}/status`
- [x] support read uptime in `/proc/uptime`
![procfs](https://github.com/yixuaz/6.1810-2023/assets/19387492/acbb2971-af41-4939-a7a2-601251481547)

## For self-study, take care using it if u are a student
