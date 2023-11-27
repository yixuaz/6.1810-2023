#include "kernel/types.h"
#include "user.h"

#undef NULL
#define NULL ((void*)0)

#define PGSIZE (4096)

int ppid;
int global = 0;
int res1 = 0;
int res2 = 0;

#define assert(x) if (x) {} else { \
printf("%s: %d ", __FILE__, __LINE__); \
printf("assert failed (%s)\n", # x); \
printf("TEST FAILED\n"); \
kill(ppid); \
exit(1); \
}


void
exittest(void *arg1, void * arg2){
    int int1 = *(int*)arg1;
    int int2 = *(int*)arg2;
    res1 = int1;
    res2 = int2;
    // while(1){;}
    exit(0);
}

void
emptytest(void *arg1, void* arg2) {
    // int i;
    int int1 = *(int*)arg1;
    int int2 = *(int*)arg2;
    int1 = int2 + int1;
    // assert(getpid() == ppid);
    exit(0);
}


void sbrktest(void* arg1, void* arg2) {
    char* b = sbrk(65536);
    // printf("sbrk end\n");
    for (int i = 0; i < 4096000; i++) {
        b[i % 65536] = 0;
    }
    exit(0);
} 

void threadinthread(void* arg1, void* arg2) {
    int int1 = *(int*) arg1;
    if (int1 == 1234) {
        // create a new thread
        int a1 = 0, a2 = 0;
        int threadid = thread_create(threadinthread, &a1, &a2);
        assert(threadid > ppid);
    }
    for (int i = 0; i < 4096000; i++) {
        int1++;
    }
    while(1);
    exit(0);
}

void
stacktest(void *arg1, void* arg2) {
    int int1 = *(int*)arg1;
    int int2 = *(int*)arg2;
    assert(int1 == 1);
    assert(int2 == 2);
    int1 = int2 + int1;
    assert(int1 == 3);
    exit(0);
}

void
heaptest(void *arg1, void* arg2) {
    int int1 = *(int*)arg1;
    int int2 = *(int*)arg2;
    assert(int1 == 1);
    assert(int2 == 2);
    assert(global == 0);
    global++;
    assert(global == 1);
    exit(0);
}

//test1: thread create function
int test1(){
    uint64 arg1 = 1;
    uint64 arg2 = 2;

    int thread_pid1 = thread_create(emptytest, &arg1, &arg2);

    int thread_pid2 = thread_create(emptytest, &arg1, &arg2);

    assert(thread_pid1 > ppid);
    assert(thread_pid2 > ppid);

    printf("TEST1 PASSED\n");
    return 0;
}

//test2: thread join function
int test2(){
    int join_pid = thread_join();
    assert(join_pid > 0);
    join_pid = thread_join();
    assert(join_pid > 0);
    printf("TEST2 PASSED\n");
    return 0;
}

//test3: shared address space
int test3(){

    uint64 arg1 = 1;
    uint64 arg2 = 2;

    int thread_pid1 = thread_create(stacktest, &arg1, &arg2);

    int thread_pid2 = thread_create(heaptest, &arg1, &arg2);

    assert(thread_pid1 > 0);
    assert(thread_pid2 > 0);

    int join_pid = thread_join();
    assert(join_pid > 0);
    join_pid = thread_join();
    assert(join_pid > 0);


    assert(arg1 == 1);
    assert(arg2 == 2);
    assert(global == 1);
    printf("TEST3 PASSED\n");
    return 0;
}

//test4: wait/exit 
int test4(){
    

    int pid = fork();
    if(pid == 0){
        ppid = getpid();
        uint64 arg1 = 1;
        uint64 arg2 = 2;

        int thread_pid1 = thread_create(exittest, &arg1, &arg2);

        int thread_pid2 = thread_create(exittest, &arg1, &arg2);

        assert(thread_pid1 > 0);
        assert(thread_pid2 > 0);
        int join_pid = thread_join();
        assert(join_pid > 0);
        join_pid = thread_join();
        assert(join_pid > 0);


        assert(res1 == 1);
        assert(res2 == 2);
        assert(global == 1);
        exit(0);
    }
    else{
        int status;
        wait(&status);
        assert(status == 0);
        assert(res1 == 0);
        assert(res2 == 0);
        printf("TEST4 PASSED\n");
    return 0;
    }
    
}

//test5: shared size
int test5() {
    int thread_pid1 = thread_create(sbrktest, 0, 0);
    int thread_pid2 = thread_create(sbrktest, 0, 0);
    assert(thread_pid1 > 0);
    assert(thread_pid2 > 0);
    thread_join();
    thread_join();
    printf("TEST5 PASSED\n");
    return 0;
}

//test6: thread in thread
int test6() {
    int pid = fork();
    if (pid == 0) {
        int arg1 = 1234;
        int thread_pid1 = thread_create(threadinthread, &arg1, 0);
        sleep(20);
        assert(thread_pid1 > ppid);
        exit(0);
    } else {
        wait(0);
        printf("TEST6 PASSED\n");
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    ppid = getpid();
    test1();
    test2();
    test3();
    test4();
    test5();
    test6();
    exit(0);
}

