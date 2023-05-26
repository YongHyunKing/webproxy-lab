#include <stdio.h>
#include <signal.h>
#include <malloc.h>
#include "csapp.h"
#include "cache.h"

void *thread(void *vargp);
void doit(int clientfd);
void read_requesthdrs(rio_t *rp, void *buf, int serverfd, char *hostname, char *port);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void parse_uri(char *uri, char *hostname, char *port, char *path);

static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";



int main(int argc, char **argv)
{
  int listenfd, *clientfd;
  char client_hostname[MAXLINE], client_port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;
  signal(SIGPIPE, SIG_IGN);

  head = NULL;
  tail = NULL;
  total_cache_size = 0;

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); // 전달받은 포트 번호를 사용해 수신 소켓 생성
  while (1)
  {
    clientlen = sizeof(clientaddr);
    clientfd = Malloc(sizeof(int));
    *clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // 클라이언트 연결 요청 수신
    Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", client_hostname, client_port);
    Pthread_create(&tid, NULL, thread, clientfd); // Concurrent 프록시
  }
}

void *thread(void *vargp)
{
  int clientfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  doit(clientfd);
  Close(clientfd);
  return NULL;
}

void doit(int clientfd)
{
  int serverfd, size;
  // char request_buf[MAXLINE], response_buf[MAXLINE];
  char buf[MAXLINE];
  char method[MAXLINE], uri[MAXLINE], path[MAXLINE], hostname[MAXLINE], port[MAXLINE];
  char *data, filename[MAXLINE], cgiargs[MAXLINE];
  ssize_t n;
  rio_t rio;

  Rio_readinitb(&rio, clientfd);

  // request_rio의 값을 request_buf로 복사
  Rio_readlineb(&rio, buf, MAXLINE);

  printf("Request headers:\n %s\n", buf);

  sscanf(buf, "%s %s", method, uri);
  parse_uri(uri, hostname, port, path);

  sprintf(buf, "%s %s %s\r\n", method, path, "HTTP/1.0");

  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD"))
  {
    clienterror(clientfd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  //여기까지 client가 원하는 요청을 request_buf에 담아온다.

  // web_object_t *cached_object = find_cache1(path);
  web_object_t *cached_object = deleteNode(path);
  if (cached_object)
  {

    sprintf(buf, "HTTP/1.0 200 OK\r\n");                                           // 상태 코드
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);                            // 서버 이름
    sprintf(buf, "%sConnection: close\r\n", buf);                                  // 연결 방식
    sprintf(buf, "%sContent-length: %d\r\n\r\n", buf, head->size); // 컨텐츠 길이
    Rio_writen(clientfd, buf, strlen(buf));
    Rio_writen(clientfd, cached_object->data, cached_object->size);
    insertNode(cached_object);
    return;
  }

  // client가 원하는 서버에 접근
  serverfd = Open_clientfd(hostname, port);
  if (serverfd < 0)
  {
    clienterror(serverfd, method, "502", "Bad Gateway", "Failed to establish connection with the end server");
    return;
  }
  

  //서버에 요청을 보낸다.
  printf("Request headers to server: \n");
  printf("GET %s %s\n", path, "HTTP/1.0");
  sprintf(buf, "GET %s %s\r\n", path, "HTTP/1.0");
  sprintf(buf, "%sHost: %s\r\n", buf, hostname);
  sprintf(buf, "%s%s", buf, user_agent_hdr);
  sprintf(buf, "%sConnections: close\r\n", buf);
  sprintf(buf, "%sProxy-Connection: close\r\n\r\n", buf);

  //buf에 담긴 서버에 응답을 serverfd에 저장한다.
  Rio_writen(serverfd, buf, (size_t)strlen(buf));


  //응답의 헤더를 읽는다.
  Rio_readinitb(&rio, serverfd);
  while (strcmp(buf, "\r\n"))
  {
    Rio_readlineb(&rio, buf, MAXLINE);
    Rio_writen(clientfd, buf, strlen(buf));
  }


  // 응답의 data를 읽는다.
  n = Rio_readnb(&rio, buf, MAX_OBJECT_SIZE+1);
  

  if (n<= MAX_OBJECT_SIZE) 
  {
    Rio_writen(clientfd, buf, n);
    web_object_t *web_object = makeObject(path,buf,n);
    insertNode(web_object);
  }
  
  Close(serverfd);
}


void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor="
                "ffffff"
                ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);


  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

void parse_uri(char *uri, char *hostname, char *port, char *path)
{
  char *uri_hostname = strstr(uri, "//") + 2;
  char *uri_port = strchr(uri_hostname  , ':');
  char *uri_path = strchr(uri_hostname  , '/');
  strcpy(path, uri_path);
  
  *uri_path='\0';
  if (uri_port) 
  {
    *uri_port='\0';
    uri_port+=1;
    strcpy(port, uri_port); 
  }
  else strcpy(port, "80");

  strcpy(hostname, uri_hostname);
}