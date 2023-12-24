#include "kernel/types.h"
#include "kernel/net.h"
#include "kernel/stat.h"
#include "user/user.h"

// Encode a DNS name
static void
encode_qname(char *qn, char *host)
{
  char *l = host;
  int hostlen = strlen(host);
  int adddot = (*(host + hostlen - 1) != '.' && *(host + hostlen) == '\0'); 

  for(char *c = host; c < host+hostlen+1; c++) {
    if(*c == '.' || (c == host+hostlen && adddot)) {
      *qn++ = (char) (c-l);
      for(char *d = l; d < c; d++) {
        *qn++ = *d;
      }
      l = c+1; // skip .
    }
  }
  
  *qn = '\0';
}

// Decode a DNS name
static void
decode_qname(char *qn, int max)
{
  char *qnMax = qn + max;
  while(1){
    if(qn >= qnMax){
      printf("invalid DNS reply\n");
      exit(1);
    }
    int l = *qn;
    if(l == 0)
      break;
    for(int i = 0; i < l; i++) {
      *qn = *(qn+1);
      qn++;
    }
    *qn++ = '.';
  }
}

// Make a DNS request
static int
dns_req(uint8 *obuf, char *s)
{
  int len = 0;
  
  struct dns *hdr = (struct dns *) obuf;
  hdr->id = htons(6828);
  hdr->rd = 1;
  hdr->qdcount = htons(1);
  
  len += sizeof(struct dns);
  
  // qname part of question
  char *qname = (char *) (obuf + sizeof(struct dns));

  encode_qname(qname, s);
  len += strlen(qname) + 1;

  // constants part of question
  struct dns_question *h = (struct dns_question *) (qname+strlen(qname)+1);
  h->qtype = htons(0x1);
  h->qclass = htons(0x1);

  len += sizeof(struct dns_question);
  return len;
}

// Process DNS response
static uint8 *
dns_rep(uint8 *ibuf, int cc)
{
  struct dns *hdr = (struct dns *) ibuf;
  int len;
  char *qname = 0;
  int record = 0;

  if(cc < sizeof(struct dns)){
    printf("DNS reply too short\n");
    exit(1);
  }

  if(!hdr->qr) {
    printf("Not a DNS reply for %d\n", ntohs(hdr->id));
    exit(1);
  }

  if(hdr->id != htons(6828)){
    printf("DNS wrong id: %d\n", ntohs(hdr->id));
    exit(1);
  }
  
  if(hdr->rcode != 0) {
    printf("DNS rcode error: %x\n", hdr->rcode);
    exit(1);
  }
  
  //printf("qdcount: %x\n", ntohs(hdr->qdcount));
  //printf("ancount: %x\n", ntohs(hdr->ancount));
  //printf("nscount: %x\n", ntohs(hdr->nscount));
  //printf("arcount: %x\n", ntohs(hdr->arcount));
  
  len = sizeof(struct dns);

  for(int i =0; i < ntohs(hdr->qdcount); i++) {
    char *qn = (char *) (ibuf+len);
    qname = qn;
    decode_qname(qn, cc - len);
    len += strlen(qn)+1;
    len += sizeof(struct dns_question);
  }
  uint8 *res = 0;
  for(int i = 0; i < ntohs(hdr->ancount); i++) {
    if(len >= cc){
      printf("invalid DNS reply\n");
      exit(1);
    }
    
    char *qn = (char *) (ibuf+len);

    if((int) qn[0] > 63) {  // compression?
      qn = (char *)(ibuf+qn[1]);
      len += 2;
    } else {
      decode_qname(qn, cc - len);
      len += strlen(qn)+1;
    }
    
    struct dns_data *d = (struct dns_data *) (ibuf+len);
    len += sizeof(struct dns_data);
    //printf("type %d ttl %d len %d\n", ntohs(d->type), ntohl(d->ttl), ntohs(d->len));
    if(ntohs(d->type) == ARECORD && ntohs(d->len) == 4) {
      record = 1;
      printf("DNS arecord for %s is ", qname ? qname : "" );
      uint8 *ip = (ibuf+len);
      printf("%d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
      res = ip;
      len += 4;
    }
  }

  // needed for DNS servers with EDNS support
  for(int i = 0; i < ntohs(hdr->arcount); i++) {
    char *qn = (char *) (ibuf+len);
    if(*qn != 0) {
      printf("invalid name for EDNS\n");
      exit(1);
    }
    len += 1;

    struct dns_data *d = (struct dns_data *) (ibuf+len);
    len += sizeof(struct dns_data);
    if(ntohs(d->type) != 41) {
      printf("invalid type for EDNS\n");
      exit(1);
    }
    len += ntohs(d->len);
  }

  if(len != cc) {
    printf("Processed %d data bytes but received %d\n", len, cc);
    exit(1);
  }
  if(!record) {
    printf("Didn't receive an arecord\n");
    exit(1);
  }
  return res;
}

uint32 gethostbyname(char *url)
{
  #define N 500
  uint8 obuf[N];
  uint8 ibuf[N];
  uint32 dst;
  int fd;
  int len;

  memset(obuf, 0, N);
  memset(ibuf, 0, N);
  
  // 8.8.8.8: google's name server
  dst = (8 << 24) | (8 << 16) | (8 << 8) | (8 << 0);

  if((fd = connect(dst, 10000, 53, SOCK_DGRAM, SOCK_CLIENT)) < 0){
    fprintf(2, "ping: connect() failed\n");
    exit(1);
  }

  len = dns_req(obuf, url);
  
  if(write(fd, obuf, len) < 0){
    fprintf(2, "dns: send() failed\n");
    exit(1);
  }
  int cc = read(fd, ibuf, sizeof(ibuf));
  if(cc < 0){
    fprintf(2, "dns: recv() failed\n");
    exit(1);
  }
  uint8 *ip = dns_rep(ibuf, cc);
  
  printf("%d %d %d %d\n",ip[0],ip[1],ip[2],ip[3]);
  close(fd);
  return MAKE_IP_ADDR(ip[0],ip[1],ip[2],ip[3]);
}
