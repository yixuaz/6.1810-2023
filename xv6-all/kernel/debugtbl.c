#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

#define MAX_LINE_LENGTH 128
#define MAX_ROW_LENGTH 8192
uint debugtbl_addr[MAX_ROW_LENGTH];
char debugtbl_info[MAX_ROW_LENGTH][MAX_LINE_LENGTH];
int debugtbl_row = 0;
char cache[PGSIZE];
int cache_idx = 0;
int cache_cnt = 0;


int readline(struct inode *ip, uint *off, char *buf) {
  int i = 0, n;
  while (i < MAX_LINE_LENGTH) {
    while (i < MAX_LINE_LENGTH && cache_cnt > cache_idx) {
      buf[i] = cache[cache_idx++];
      if (buf[i] == '\n') {
        buf[i] = 0;
        return i;
      }
      i++;
    }
    if (i == MAX_LINE_LENGTH) {
      panic("line char overflow");
      return -1;
    }
    n = readi(ip, 0, (uint64)cache, *off, PGSIZE);
    if (n <= 0) break; // Error or end of file
    *off += n;
    cache_cnt = n;
    cache_idx = 0;
  }
  
  return i; // Return the length of the line
}

int readfile(struct inode *ip) {
  uint off = 0; // Offset in the file
  int n;
  char buf[MAX_LINE_LENGTH];

  while ((n = readline(ip, &off, buf)) > 0) {
    uint addr = 0;
    if (n < 8) {
      panic("debugtbl format error");
    }
    for(int i = 0; i < 8; i++) {
      char c = buf[i];
      if (c >= '0' && c <= '9') c = c - '0';
      else if (c >= 'a' && c <= 'f') c = c - 'a' + 10;
      else panic("invalid address content");
      addr = (addr << 4) | c;
    }
    debugtbl_addr[debugtbl_row] = addr;
    memmove(debugtbl_info[debugtbl_row++], buf + 9, n); 
  }
  return n < 0 ? -1 : 0;
}

int initdebugtbl(){
	struct inode *ip;
	begin_op();
	if((ip = namei("/debugtbl")) == 0){
		end_op();
		return -1;
	}
	ilock(ip);
	if (readfile(ip) < 0)
    goto bad;
	iunlockput(ip);
  end_op();
  ip = 0;	
  return 0;
bad:
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}

int get_debuginfo(uint64 address) {
  uint left = 0;
  uint right = debugtbl_row - 1;
  while(left <= right){
	  int mid = (left + right) >> 1;
	  if(address < debugtbl_addr[mid]){
      right = mid - 1;
	  }else{
      left = mid + 1;
	  }
  }
  return left - 1;
}

void backtrace()
{
  int idx;
  printf("backtrace:\n");
  uint64 fp = r_fp(); // get the frame pointer value
  for (uint64 i = fp; PGROUNDDOWN(fp) == PGROUNDDOWN(i); i = *(uint64*)(i - 16)) {
    uint64 addr = *(uint64*)(i - 8);
    if ((idx = get_debuginfo(addr)) < 0)
      printf("%p\n", addr);
    else
      printf("%p %s\n", addr, debugtbl_info[idx]);
  }
}