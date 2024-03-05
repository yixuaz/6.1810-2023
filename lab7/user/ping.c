#include "kernel/types.h"
#include "kernel/net.h"
#include "kernel/stat.h"
#include "kernel/icmp.h"
#include "user/user.h"

int is_digit(char s) {
  return s >= '0' && s <= '9';
}

int to_int(char *s, int *num) {
  int result = 0;
  while (*s) {
    if (!is_digit(*s)) return -1; 
    result = result * 10 + (*s - '0');
    s++;
  }
  *num = result;
  return 0;
}

uint is_valid_ip(char *ip) {
  int segs = 0; 
  int seg_val = 0;
  char *temp = ip;
  if (!ip || *ip == '\0') {
      return 0; 
  }
  uint res = 0;
  int base = 24;
  while (*temp) {
    char *seg_start = temp;
    while (*temp && *temp != '.') {
        temp++;
    }
    char backup = *temp;
    *temp = '\0'; 
    if (to_int(seg_start, &seg_val) != 0 || seg_val < 0 || seg_val > 255) {
      *temp = backup;
      return 0;
    }
    res |= (seg_val) << base;
    base -= 8;
    *temp = backup; 
    if (*temp) {
        temp++;
    }
    segs++;
    if (segs > 4) break;
  }
  return (segs == 4) ? res : 0; 
}

uint16 checksum(void *b, int len) {    
    uint16 *buf = b;
    uint sum = 0;
    uint16 result;

    for (sum = 0; len > 1; len -= 2)
        sum += *buf++;
    if (len == 1)
        sum += *(unsigned char *)buf;
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    return result;
}

int
main(int argc, char *argv[])
{
  int fd;
  uint32 dst;
  char obuf[8];
  char uri[128], path[60];
  

  if (argc == 1) {
    dst = (10 << 24) | (0 << 16) | (2 << 8) | (2 << 0);
  } else if (argc == 2) {
    if (!(dst = is_valid_ip(argv[1]))) {
      parseURL(argv[1], uri, path, 128, 60);
      dst = gethostbyname(uri);
      if (dst < 0) {
        printf("input addr invalid\n");
        exit(1);
      }
    }
  } else {
    printf("usage: %s <URL>\n", argv[0]);
    exit(1);
  }
  
  // build a raw sock
  if((fd = connect(dst, 0, 0, SOCK_RAW, SOCK_CLIENT)) < 0){
    fprintf(2, "ping: connect() failed\n");
    exit(1);
  }

  // build icmp header
  struct icmp *icmp = (struct icmp *)obuf;
  icmp->type = ICMP_ECHO;
  icmp->code = 0;
  icmp->checksum = 0;
  icmp->ih_id = htons(1);
  icmp->checksum = checksum(icmp, sizeof(struct icmp));

  struct {
    uint16 len;
    uint8 ttl;
  } echo_reply;

  // send icmp echo to dst
  for (int i = 1; i <= 3; i++) {
    icmp->ih_seq = htons(i);
    icmp->checksum = 0;
    icmp->checksum = checksum(icmp, sizeof(struct icmp));
    uint st = uptime();
    if(write(fd, obuf, sizeof(struct icmp)) < 0){
      fprintf(2, "ping: send() failed\n");
      exit(1);
    }
    int cc = read(fd, &echo_reply, 3);
    uint ed = uptime();
    if(cc != 3){
      fprintf(2, "ping: recv() failed\n");
      exit(1);
    }
    printf("%d bytes from %d.%d.%d.%d : icmp_seq=%d ttl=%d time=%d ticks\n", 
    echo_reply.len, 
    dst >> 24, (dst & 0x00ffffff) >> 16, (dst & 0x0000ffff) >> 8, (dst & 0x000000ff),
    ntohs(icmp->ih_seq) , echo_reply.ttl, ed-st);
  }

  close(fd);
  return 0;
}