#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"


int verify_dir(char *path, int fd) {
  char buf[512], *p;
  struct dirent de;
  struct stat st;

  static char *expect_names[] = {
    ".",
    "..",
    "uptime",
    "1",
    "2"
  };

  if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
    printf("procfs verify dir: path too long\n");
    return -1;
  }
  strcpy(buf, path);
  p = buf+strlen(buf);
  *p++ = '/';
  int idx = 0;
  while(read(fd, &de, sizeof(de)) == sizeof(de)){
    if(de.inum == 0)
      continue;
    if (strcmp(de.name, expect_names[idx])) {
      printf("procfs verify dir: name not macth %s %s\n", de.name, expect_names[idx]);
      return -1;
    }
    memmove(p, de.name, DIRSIZ);
    p[DIRSIZ] = 0;
    if(stat(buf, &st) < 0){
      printf("procfs verify dir: cannot stat %s\n", buf);
      return -1;
    }
    idx++;
    if (idx == 5) break;
  }
  return 0;
}

int cat_proc_status(int fd)
{
  int n = 0;
  char buf[512];

  n = read(fd, buf, sizeof(buf));
  if (n <= 10) {
    fprintf(2, "procfs cat proc status failed\n");
    return -1;
  }
  buf[n] = '\0';
  printf("\n%s\n", buf);
  n = read(fd, buf, sizeof(buf));
  if (n > 0) {
    fprintf(2, "procfs cat proc status content too much\n");
    return -1;
  }
  return 0;
}

int
main(int argc, char *argv[])
{
  int fd;
  struct stat st;
  char *path = "/proc";

  if((fd = open(path, O_RDONLY)) < 0){
    fprintf(2, "procfs: cannot open %s\n", path);
    exit(-1);
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "procfs: cannot stat %s\n", path);
    goto bad;
  }

  if(!st.dev_dir) {
    fprintf(2, "procfs: not set dev_dir\n");
    goto bad;
  }

  if(verify_dir(path, fd) < 0) goto bad;
  printf("procfs verify dir done; ok\n"); 
  close(fd);

  if((fd = open("/proc/1/status", O_RDONLY)) < 0){
    fprintf(2, "procfs: cannot open proc 1 status\n");
    exit(-1);
  }

  if(cat_proc_status(fd) < 0) goto bad;
  printf("procfs cat proc 1 status done; ok\n"); 

  printf("procfs test done; ok\n"); 

  exit(0);
bad:
  close(fd);
  exit(-1);
}