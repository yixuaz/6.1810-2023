#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/proc.h"
#include "kernel/signal.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"

#define fail(msg) do {printf("FAILURE: " msg "\n"); failed = 1; goto done;} while (0);
static int failed = 0;

void hello();
void testsignalprocmask();
void testsignalignore();
void testsignalfork();

int global = 0;

int
main(int argc, char *argv[])
{
  testsignalignore();
  testsignalprocmask(); 
  testsignalfork(); 
  exit(failed);
}

void hello()
{
  global = ugetpid();
  printf("control-c trigger hello with pid: %d\n", global);
  sigreturn();
}
void testsignalignore()
{
  sigprocmask(0);
  signal(SIGINT, SIG_IGN);
  sigsend(ugetpid(), SIGINT);
  sleep(1);
  signal(SIGINT, SIG_DFL);
  printf("test signal ignore: ok\n");
}
void testsignalprocmask()
{
  signal(SIGINT, (uint64)hello);
  int pid = ugetpid();
  sigsend(pid, SIGINT);
  if (global != pid) fail("signal handler not work");
  global = 0;
  int old = sigprocmask(1 << SIGINT);
  sigsend(pid, SIGINT);
  sleep(1);
  if (global != 0) fail("sigprocmask block not work");
  if (old != 0) fail("sigprocmask old value wrong");
  sigprocmask(0);
  if (global != pid) fail("sigprocmask unblock not work");
  printf("test signal proc mask: ok\n");
done:   
}

void testsignalfork()
{
  global = 0;
  signal(SIGINT, (uint64)hello);
  int pid = fork();
  if (pid == 0) {
    while(!global) sleep(0);
    if (global != ugetpid()) fail("fork signal not inherit");
    signal(SIGINT, 0);
    sigsend(global - 1, SIGINT);
    sleep(10000);
    exit(1);
  }
  if (sigsend(pid, SIGINT) != 0) fail("fork sig send err1");
  while(!global) sleep(0);
  if (sigsend(pid, SIGINT) != 0) fail("fork sig send err2");
  int xstatus;
  wait(&xstatus);
  if (xstatus != -1) fail("fork signal interrupt not clean");
  printf("test signal fork: ok\n");
done:  
}