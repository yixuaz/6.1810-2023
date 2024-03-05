#include "kernel/types.h"
#include "user.h"
#include "kernel/fcntl.h"
#include "kernel/riscv.h"

#undef NULL
#define NULL ((void*)0)

int ppid;
int global = 1;
uint64 size = 0;
lock_t lock, lock2;
int num_threads = 30;
int loops = 10;
int* global_arr;

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
void worker7(void *arg1, void *arg2);
void merge_sort(void *array, void *size);
void worker9(void *array, void *size);
void worker10(void *array, void *size);
void worker11(void *array, void *size);
void worker12(void *array, void *size);
void worker13(void *array, void *size);
void worker14(void *array, void *size);
/* thread user library functions */
void test1()
{
  int arg1 = 35;
  int arg2 = 42;
  int thread_pid = thread_create(worker, &arg1, &arg2);
  assert(thread_pid > 0);

  int join_pid = thread_join();
  assert(join_pid == thread_pid);
  assert(global == 2);

  printf("TEST1 PASSED\n");
}

/* memory leaks from thread library? */
void test2()
{
  int i, thread_pid, join_pid;
  for(i = 0; i < 2000; i++) {
    global = 1;
    thread_pid = thread_create(worker2, 0, 0);
    assert(thread_pid > 0);
    join_pid = thread_join();
    assert(join_pid == thread_pid);
    assert(global == 5);
    assert((uint64)sbrk(0) < (150 * 4096) && "shouldn't even come close");
  }

  printf("TEST2 PASSED\n");
}

/* check that address space size is updated in threads */
void test3()
{
  global = 0;
  int arg1 = 11, arg2 = 22;

  lock_init(&lock);
  lock_init(&lock2);
  lock_acquire(&lock);
  lock_acquire(&lock2);

  for (int i = 0; i < num_threads; i++) {
    int thread_pid = thread_create(worker3, &arg1, &arg2);
    assert(thread_pid > 0);
  }

  size = (uint64)sbrk(0);

  while (global < num_threads) {
    lock_release(&lock);
    sleep(2);
    lock_acquire(&lock);
  }
  global = 0;
  sbrk(10000);
  size = (uint64)sbrk(0);
  lock_release(&lock);

  while (global < num_threads) {
    lock_release(&lock2);
    sleep(2);
    lock_acquire(&lock2);
  }
  lock_release(&lock2);

  for (int i = 0; i < num_threads; i++) {
    int join_pid = thread_join();
    assert(join_pid > 0);
  }

  printf("TEST3 PASSED\n");
}

/* multiple threads with some depth of function calls */
uint fib(uint n) {
   if (n == 0) {
      return 0;
   } else if (n == 1) {
      return 1;
   } else {
      return fib(n - 1) + fib(n - 2);
   }
}

void test4()
{
  assert(fib(28) == 317811);

   int arg1 = 11, arg2 = 22;

   for (int i = 0; i < num_threads; i++) {
      int thread_pid = thread_create(worker4, &arg1, &arg2);
      assert(thread_pid > 0);
   }

   for (int i = 0; i < num_threads; i++) {
      int join_pid = thread_join();
      assert(join_pid > 0);
   }

   printf("TEST4 PASSED\n");
}

/* no exit call in thread, should trap at bogus address */
void test5()
{
  int arg1 = 42, arg2 = 24;
  int thread_pid = thread_create(worker5, &arg1, &arg2);
  assert(thread_pid > 0);

  int join_pid = thread_join();
  assert(join_pid == thread_pid);
  assert(global == 2);

  printf("TEST5 PASSED\n");
}

/* test lock correctness */
void test6()
{
  global = 0;
  lock_init(&lock);

  int i;
  for (i = 0; i < num_threads; i++) {
    int thread_pid = thread_create(worker6, 0, 0);
    assert(thread_pid > 0);
  }

  for (i = 0; i < num_threads; i++) {
    int join_pid = thread_join();
    assert(join_pid > 0);
  }

  assert(global == num_threads * loops);

  printf("TEST6 PASSED\n");
}

/* nested thread user library functions */
void test7()
{
  int arg1 = 35;
  int arg2 = 42;
  int thread_pid = thread_create(worker7, &arg1, &arg2);
  assert(thread_pid > 0);

  int join_pid = thread_join();
  assert(join_pid == thread_pid);
  assert(global == 3);

  printf("TEST7 PASSED\n");
}

/* merge sort using nested threads  */
void test8()
{
  /*
   1. Create global array and populate it
   2. invoke merge sort (array ptr, size)

   Merge sort:
   0. base case - size = 1 --> return 
   1. thread create with merge sort (array left, size/2)
   2. thread create with merge sort (array + size/2, size - size/2)
   3. join both threads
   4. Merge function
   */

    int size = 11;
    global_arr = (int*)malloc(size * sizeof(int));
    for(int i = 0; i < size; i++){
        global_arr[i] = size - i - 1;
    }

   int thread_pid = thread_create(merge_sort, global_arr, &size);
   assert(thread_pid > 0);

   int join_pid = thread_join();
   assert(join_pid == thread_pid);
   assert(global_arr[0] == 0);
   assert(global_arr[5] == 5); 
   assert(global_arr[10] == 10); 

   printf("TEST8 PASSED\n");
}

/* test lock correctness using nested threads */
void test9()
{
  global = 0;
  lock_init(&lock);

  int i;
  for (i = 0; i < num_threads; i++) {
    int thread_pid = thread_create(worker9, 0, 0);
    assert(thread_pid > 0);
  }

  for (i = 0; i < num_threads; i++) {
    int join_pid = thread_join();
    assert(join_pid > 0);
  }

  assert(global == num_threads * 2);

  printf("TEST9 PASSED\n");
}

/* no exit call in nested thread, should trap at bogus address */
void test10()
{
  int arg1 = 42, arg2 = 24;
  int thread_pid = thread_create(worker10, &arg1, &arg2);
  assert(thread_pid > 0);

  int join_pid = thread_join();
  assert(join_pid == thread_pid);
  assert(global == 3);

  printf("TEST10 PASSED\n");
}

/* check that address space size is updated in threads */
void test11()
{
  int arg1 = 11, arg2 = 22;

  size = (uint64)sbrk(0);
  int thread_pid = thread_create(worker11, &arg1, &arg2);
  assert(thread_pid > 0);
  
  int join_pid = thread_join();
  assert(join_pid > 0);
  printf("TEST11 PASSED\n");
}

/* check that thread stack overflow, should trap */
void test12()
{
  int arg1 = 11, arg2 = 22;

  size = (uint64)sbrk(0);
  int thread_pid = thread_create(worker12, &arg1, &arg2);
  assert(thread_pid > 0);
  
  int join_pid = thread_join();
  assert(join_pid > 0);
  assert(global == 3);
  printf("TEST12 PASSED\n");
}

/* check no malloc stack race condition */
void test13()
{
  num_threads = 30;
  int i;
  int arg1 = 35;
  int arg2 = 42;
  uint64 origin = (uint64)sbrk(0);
  for (i = 0; i < num_threads; i++) {
    int thread_pid = thread_create(worker13, &arg1, &arg2);
    assert(thread_pid > 0);
  }

  for (i = 0; i < num_threads; i++) {
    int join_pid = thread_join();
    assert(join_pid > 0);
  }
  assert((uint64)sbrk(0) < (origin + (16 + num_threads * 2 * 3) * 4096) && "shouldn't even come close");
  printf("TEST13 PASSED\n");
}

/* check no mmap race condition */
void test14()
{
  num_threads = 5;
  int i;
  int a[num_threads];
  char *sz = sbrk(0); 
  global = ((uint64)sz + 4095) / 4096 + 4 * num_threads; // ensure mmap to a valid vm, 4 becasue (malloc_align(8192))
  for (i = 0; i < num_threads; i++) a[i] = i;
  
  for (i = 0; i < num_threads; i++) {
    int thread_pid = thread_create(worker14, &a[i], &num_threads);
    assert(thread_pid > 0);
  }

  for (i = 0; i < num_threads; i++) {
    int join_pid = thread_join();
    assert(join_pid > 0);
  }

  uint64 baseaddr = (4096 * global), st = baseaddr;
  char *c = (char *)baseaddr;
  uint64 end = 4096 * (global + num_threads);
  for (; baseaddr < end; c++, baseaddr++) {
    char d = *c;
    int diff = (baseaddr - st) / 4096;
    assert(d == 'a' + diff);
  } 
  printf("TEST14 PASSED\n");
}

void (*functions[])() = {
  test1,
  test2,
  test3,
  test4,
  test5,
  test6,
  test7,
  test8,
  test9,
  test10,
  test11,
  test12,
  test13,
  test14
  };

int
main(int argc, char *argv[])
{
  int len = sizeof(functions) / sizeof(functions[0]);
  for(int i = 0; i < len; i++) {
    global = 1;
    ppid = getpid();
    (*functions[i])(); 
  }

  exit(0);
}


void
worker(void *arg1, void *arg2) {
   int arg1_int = *(int*)arg1;
   int arg2_int = *(int*)arg2;
   assert(arg1_int == 35);
   assert(arg2_int == 42);
   assert(global == 1);
   global++;
   exit(0);
}

void
worker2(void *arg1, void *arg2) {
   assert(global == 1);
   global += 4;
   exit(0);
}

void
worker3(void *arg1, void *arg2) {
   lock_acquire(&lock);
   assert((uint64)sbrk(0) == size);
   global++;
   lock_release(&lock);

   lock_acquire(&lock2);
   assert((uint64)sbrk(0) == size);
   global++;
   lock_release(&lock2);
   exit(0);
}

void
worker4(void *arg1, void *arg2) {
   int tmp1 = *(int*)arg1;
   int tmp2 = *(int*)arg2;
   assert(tmp1 == 11);
   assert(tmp2 == 22);
   assert(global == 1);
   assert(fib(2) == 1);
   assert(fib(3) == 2);
   assert(fib(9) == 34);
   assert(fib(15) == 610);
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
   // no exit() in thread
}

void
worker6(void *arg1, void *arg2) {
   int i, j, tmp;
   for (i = 0; i < loops; i++) {
      lock_acquire(&lock);
      tmp = global;
      for(j = 0; j < 50; j++); // take some time
      global = tmp + 1;
      lock_release(&lock);
   }
   exit(0);
}

void nested_worker(void *arg1, void *arg2){
   int arg1_int = *(int*)arg1;
   int arg2_int = *(int*)arg2;
   assert(arg1_int == 35);
   assert(arg2_int == 42);
   assert(global == 2);
   global++;
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
   int nested_thread_pid = thread_create(nested_worker, &arg1_int, &arg2_int);
   int nested_join_pid = thread_join();
   assert(nested_join_pid == nested_thread_pid);
   exit(0);
}

void merge(int* array, int* array_right,int size_left, int size_right,int*temp_array){
    int i = 0;
    int j = 0;
    int k = 0;
    while(i < size_left && j < size_right){
        if(array[i] < array_right[j]){
            temp_array[k] = array[i];
            i++;
        }
        else{
            temp_array[k] = array_right[j];
            j++;
        }
        k++;
    }
    while(i < size_left){
        temp_array[k] = array[i];
        i++;
        k++;
    }
    while(j < size_right){
        temp_array[k] = array_right[j];
        j++;
        k++;
    }
    for(int i = 0; i < size_left + size_right; i++){
        array[i] = temp_array[i];
    }
   
}

void merge_sort(void *arg1, void *arg2) {
  int *array = (int*)arg1;
  int size = *(int*)arg2;

  if (size==1){
      exit(0);
  }
  
  int size_left = size/2;
  int size_right = size-size/2;

  int* array_right = (int*)(array + size_left);
  
  int nested_thread_pid_l = thread_create(merge_sort, array, &size_left);
  int nested_thread_pid_r = thread_create(merge_sort, array_right, &size_right);

  int nested_join_pid_1 = thread_join();
  int nested_join_pid_2 = thread_join();


  int* temp_array = malloc(size*sizeof(int));

  merge(array,array_right,size_left,size_right,temp_array);

  free(temp_array);

  assert(nested_thread_pid_l == nested_join_pid_1 || nested_thread_pid_l == nested_join_pid_2);
  assert(nested_thread_pid_r == nested_join_pid_1 || nested_thread_pid_r == nested_join_pid_2);
  exit(0);
}

void nest_worker(void *arg1,void *arg2){
  int j;
  lock_acquire(&lock);
  for(j=0;j<50;j++);
  global++;
  lock_release(&lock);
  exit(0);
}
void
worker9(void *arg1, void *arg2) {
  lock_acquire(&lock);
  int j;
  for(j = 0; j < 50; j++); // take some time
  global++;
  lock_release(&lock);

  int nested_thread_pid = thread_create(nest_worker, 0, 0);
  assert(nested_thread_pid > 0);
  int nested_join_pid = thread_join();
  assert(nested_join_pid > 0);
  assert(nested_thread_pid==nested_join_pid);
  exit(0);
}

void nested_worker2(void *arg1, void *arg2){
   int arg1_int = *(int*)arg1;
   int arg2_int = *(int*)arg2;
   assert(arg1_int == 42);
   assert(arg2_int == 24);
   assert(global == 2);
   global++;
   // no exit() in thread
}

void
worker10(void *arg1, void *arg2) {
   int tmp1 = *(int*)arg1;
   int tmp2 = *(int*)arg2;
   assert(tmp1 == 42);
   assert(tmp2 == 24);
   assert(global == 1);
   global++;

   int nested_thread_pid = thread_create(nested_worker2, &tmp1, &tmp2);
   assert(nested_thread_pid > 0);
   for(int j=0;j<10000;j++);

   int nested_join_pid = thread_join();
   assert(nested_join_pid)
   assert(nested_join_pid == nested_thread_pid);
   exit(0);
}


void nest_worker3(void *arg1, void *arg2)
{
  lock_acquire(&lock);
  assert((uint64)sbrk(0) == size);
  global++;
  lock_release(&lock);

  lock_acquire(&lock2);
  assert((uint64)sbrk(0) == size);
  global++;
  lock_release(&lock2);
  
  exit(0);
}


void worker11(void *arg1, void *arg2) {
  num_threads = 1;
  lock_init(&lock);
  lock_init(&lock2);
  lock_acquire(&lock);
  lock_acquire(&lock2);

  int nested_thread_id = thread_create(nest_worker3, 0, 0);
  assert(nested_thread_id > 0);
  size = (uint64)sbrk(0);

  while (global < num_threads) {
    lock_release(&lock);
    sleep(2);
    lock_acquire(&lock);
  }

  global = 0;
  sbrk(10000);
  size = (uint64)sbrk(0);
  lock_release(&lock);

  while (global < num_threads) {
    lock_release(&lock2);
    sleep(2);
    lock_acquire(&lock2);
  }
  lock_release(&lock2);
  int nested_join_pid = thread_join();
  assert(nested_join_pid > 0);
  exit(0);
}

void call_forever()
{
  int k = 3;
  global = k;
  call_forever();
}

void
worker12(void *arg1, void *arg2) {
  int tmp1 = *(int*)arg1;
  int tmp2 = *(int*)arg2;
  assert(tmp1 == 11);
  assert(tmp2 == 22);
  assert(global == 1);
  call_forever();
  exit(0);
}

void empty(void *arg1, void *arg2)
{
  int arg1_int = *(int*)arg1;
  int arg2_int = *(int*)arg2;
  assert(arg1_int == 35);
  assert(arg2_int == 42);
  exit(0);
}

void
worker13(void *arg1, void *arg2) {
  sleep(3);
  int arg1_int = *(int*)arg1;
  int arg2_int = *(int*)arg2;
  assert(arg1_int == 35);
  assert(arg2_int == 42);
  sleep(3);
  int nested_thread_pid = thread_create(empty, &arg1_int, &arg2_int);
  int nested_join_pid = thread_join();
  assert(nested_join_pid == nested_thread_pid);
  exit(0);
}

void
worker14(void *arg1, void *arg2) {
  int idx = *(int*)arg1;
  int tot = *(int*)arg2;
  assert(tot == 5);
  uint64 baseaddr = 4096 * (global + idx);
  char *p1 = mmap((void *)baseaddr, PGSIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, -1, 0);
  for (int i = 0; i < 4096; i++, p1++) *p1 = 'a' + idx;
  exit(0);
}
