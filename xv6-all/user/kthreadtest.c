#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/riscv.h"


#undef NULL
#define NULL ((void*)0)

int ppid;
volatile int arg1 = 11;
volatile int arg2 = 22;

volatile int global = 1;
volatile uint64 newfd = 0;


#define assert(x) if (x) {} else { \
   printf("%s: %d ", __FILE__, __LINE__); \
   printf("assert failed (%s)\n", # x); \
   printf("TEST FAILED\n"); \
   kill(ppid); \
   exit(0); \
}



void worker(void *arg1, void *arg2);
void worker2(void *arg1, void *arg2);
void worker3(void *arg1, void *arg2);
void worker4(void *arg1, void *arg2);
void worker5(void *arg1, void *arg2);
void worker6(void *arg1, void *arg2);

/* clone and verify that address space is shared */
void test1(void *stack)
{
  int clone_pid = clone(worker, 0, 0, stack);
  assert(clone_pid > 0);
  while(global != 5);
  printf("TEST1 PASSED\n");
  
  void *join_stack;
  int join_pid = join(&join_stack);
  assert(join_pid == clone_pid);
  global = 1;
}

/* clone and play with the argument */
void test2(void *stack)
{
  int clone_pid = clone(worker2, (void*)&arg1, (void*)&arg2, stack);
  assert(clone_pid > 0);
  while(global != 33);
  assert(arg1 == 44);
  assert(arg2 == 55);
  printf("TEST2 PASSED\n");
  
  void *join_stack;
  int join_pid = join(&join_stack);
  assert(join_pid == clone_pid);
  
}

/* clone copies file descriptors, but doesn't share */
void test3(void *stack)
{
  int fd = open("tmp", O_WRONLY|O_CREATE);
  assert(fd == 3);
  int clone_pid = clone(worker3, 0, 0, stack);
  assert(clone_pid > 0);
  while(!newfd);
  assert(write(newfd, "goodbye\n", 8) == -1);
  printf("TEST3 PASSED\n");

  void *join_stack;
  int join_pid = join(&join_stack);
  assert(join_pid == clone_pid);

}

/* clone with bad stack argument */
void test4(void *stack)
{
  assert(clone(worker4, 0, 0, stack+4) == -1);
  printf("TEST4 PASSED\n");
}

/* clone and join syscalls */
void test5(void *stack)
{
  global = 1;
  int arg1 = 42, arg2 = 24;
  int clone_pid = clone(worker5, &arg1, &arg2, stack);
  assert(clone_pid > 0);

  void *join_stack;
  int join_pid = join(&join_stack);
  assert(join_pid == clone_pid);
  assert(stack == join_stack);
  assert(global == 2);
  
  printf("TEST5 PASSED\n");
}

/* join argument checking */
void test6(void *stack)
{
  global = 1;
  int arg1 = 42, arg2 = 24;
  int clone_pid = clone(worker5, &arg1, &arg2, stack);
  assert(clone_pid > 0);

  sbrk(PGSIZE);
  void **join_stack = (void**) ((uint64)sbrk(0) - 8);
  assert(join((void**)((uint64)join_stack + 4)) == -1);
  assert(join(join_stack) == clone_pid);
  assert(stack == *join_stack);
  assert(global == 2);
  
  printf("TEST6 PASSED\n");
}

/* join should not handle child processes (forked) */
void test7(void *stack)
{
  global = 1;
  int fork_pid = fork();
   if(fork_pid == 0) {
     exit(0);
   }
   assert(fork_pid > 0);

   void *join_stack;
   int join_pid = join(&join_stack);
   assert(join_pid == -1);

   assert(wait(0) > 0);
   printf("TEST7 PASSED\n");
}

/* join, not wait, should handle threads */
void test8(void *stack)
{
  global = 1;
  int arg1 = 42, arg2 = 24;
  int clone_pid = clone(worker5, &arg1, &arg2, stack);
  assert(clone_pid > 0);

  sleep(10);
  assert(wait(0) == -1);

  void *join_stack;
  int join_pid = join(&join_stack);
  assert(join_pid == clone_pid);
  assert(stack == join_stack);
  assert(global == 2);

  printf("TEST8 PASSED\n");
}

/* set up stack correctly (and without extra items) */
void test9(void *stack)
{
  global = 1;
  int clone_pid = clone(worker6, stack, 0, stack);
  assert(clone_pid > 0);
  while(global != 5);
  printf("TEST9 PASSED\n");

  void *join_stack;
  int join_pid = join(&join_stack);
  assert(join_pid == clone_pid);
}

void (*functions[])() = {test1, test2, test3, test4, test5, test6, test7, test8, test9};

int
main(int argc, char *argv[])
{
  int len = sizeof(functions) / sizeof(functions[0]);
  for(int i = 0; i < len; i++) {
    ppid = getpid();
    void *stack, *p = malloc(PGSIZE * 2);
    assert(p != NULL);
    stack = ((uint64)p % PGSIZE) ? (p + (PGSIZE - (uint64)p % PGSIZE)) : p;
    
    (*functions[i])(stack); 
    free(p);
  }

  exit(0);
}

void
worker(void *arg1, void *arg2) {
  assert(global == 1);
  global = 5;
  exit(0);
}

void
worker2(void *arg1, void *arg2) {
  int tmp1 = *(int*)arg1;
  int tmp2 = *(int*)arg2;
  *(int*)arg1 = 44;
  *(int*)arg2 = 55;
  assert(global == 1);
  global = tmp1 + tmp2;
  exit(0);
}

void
worker3(void *arg1, void *arg2) {
  assert(write(3, "hello\n", 6) == 6);
  xchg(&newfd, open("tmp2", O_WRONLY|O_CREATE));
  exit(0);
}

void
worker4(void *arg1, void *arg2) {
  exit(0);
}

void
worker5(void *arg1, void *arg2) {
   int tmp1 = *(int*)arg1;
   int tmp2 = *(int*)arg2;
   assert(tmp1 == 42);
   assert(tmp2 == 24);
   assert(global == 1);
   global++;
   exit(0);
}

void
worker6(void *arg1, void *arg2) {
  // arg1 -> top stack
  // arg1 -8 -> ra
  // arg1 -16 -> fp
  // arg1 - 24 -> a0
  // arg1 - 32 -> a1
  assert(*((uint64*) (arg1 + 2 * PGSIZE - 8)) == 0xffffffffffffffff);
  assert((uint64)&arg2 == ((uint64)arg1 + 2 * PGSIZE - 32));
  assert((uint64)&arg1 == ((uint64)arg1 + 2 * PGSIZE - 24));
  global = 5;
  exit(0);
}

void
worker7(void *arg1, void *arg2) {
  int arg1_int = *(int*)arg1;
  int arg2_int = *(int*)arg2;
  assert(arg1_int == 35);
  assert(arg2_int == 42);
  assert(global == 1);
  global++;
  exit(0);
}