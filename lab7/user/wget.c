
#include "kernel/types.h"
#include "kernel/net.h"
#include "kernel/stat.h"
#include "user/user.h"

#define SERVER_PORT 80
#define BUFFER_SIZE 512
#define URI_MAX_LEN 128
#define PATH_MAX_LEN 64

void static buildHTTPGet(char *request, char *uri, char *path)
{
  #define GET_PART "GET "
  #define HOST_PART " HTTP/1.1\r\nHost: "
  #define END_PART "\r\nConnection: close\r\n\r\n"
  strcpy(request, GET_PART);
  request += strlen(GET_PART);
  strcpy(request, path);
  request += strlen(path);
  strcpy(request, HOST_PART);
  request += strlen(HOST_PART);
  strcpy(request, uri);
  request += strlen(uri);
  strcpy(request, END_PART);
  request += strlen(END_PART);
}

int
main(int argc, char *argv[])
{
  
  if (argc != 2) {
      printf("usage: %s <URL>\n", argv[0]);
      exit(1);
  }

  char uri[URI_MAX_LEN], path[PATH_MAX_LEN];
  parseURL(argv[1], uri, path, URI_MAX_LEN, PATH_MAX_LEN);
  printf("path %s, uri: %s\n", path, uri);
  uint32 server_ip = gethostbyname(uri); 

  int sock;
  // Connect to the server
  if((sock = connect(server_ip, 46666, SERVER_PORT, SOCK_STREAM, SOCK_CLIENT)) < 0){
    printf("connect() failed\n");
    exit(1);
  }
  printf("connect success\n");
  // Send HTTP GET request
  char request[52 + URI_MAX_LEN + PATH_MAX_LEN];
  buildHTTPGet(request, uri, path);
  printf("%s\n", request);
  if(write(sock, request, strlen(request)) < 0){
    printf("http: send() failed\n");
    close(sock);
    exit(1);
  }

  // Receive response and print it to stdout
  char buffer[BUFFER_SIZE];
  while (1) {
    int cc = read(sock, buffer, BUFFER_SIZE - 1);
    if (cc < 0) {
        printf("Receive failed\n");
        break;
    }
    if (cc == 0) {
        break; // Connection closed
    }
    buffer[cc] = '\0';
    printf("%s", buffer);
  }

  // Close the socket
  close(sock);

  return 0;
}