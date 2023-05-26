#include <stdio.h>
#include <signal.h>

#include "csapp.h"
#include "cache.h"

void *thread(void *vargp);
void doit(int clientfd);
void read_requesthdrs(rio_t *rp, void *buf, int serverfd, char *hostname, char *port);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void parse_uri(char *uri, char *hostname, char *port, char *path);

static const int is_local_test = 1; // 테스트 환경에 따른 도메인&포트 지정을 위한 상수 (0 할당 시 도메인&포트가 고정되어 외부에서 접속 가능)
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
  signal(SIGPIPE, SIG_IGN); // SIGPIPE 예외처리

  rootp = (web_object_t *)calloc(1, sizeof(web_object_t));
  lastp = (web_object_t *)calloc(1, sizeof(web_object_t));

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
  int serverfd, content_length;
  char request_buf[MAXLINE], response_buf[MAXLINE];
  char method[MAXLINE], uri[MAXLINE], path[MAXLINE], hostname[MAXLINE], port[MAXLINE];
  char *response_ptr, filename[MAXLINE], cgiargs[MAXLINE];
  rio_t request_rio, response_rio;

  Rio_readinitb(&request_rio, clientfd);
  Rio_readlineb(&request_rio, request_buf, MAXLINE);
  printf("Request headers:\n %s\n", request_buf);

  sscanf(request_buf, "%s %s", method, uri);
  parse_uri(uri, hostname, port, path);

  sprintf(request_buf, "%s %s %s\r\n", method, path, "HTTP/1.0");

  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD"))
  {
    clienterror(clientfd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  
  web_object_t *cached_object = find_cache(path);
  if (cached_object)
  {
    send_cache(cached_object, clientfd); 
    read_cache(cached_object);          
    return;
  }


  serverfd = is_local_test ? Open_clientfd(hostname, port) : Open_clientfd("52.79.234.188", port);
  if (serverfd < 0)
  {
    clienterror(serverfd, method, "502", "Bad Gateway", "📍 Failed to establish connection with the end server");
    return;
  }
  Rio_writen(serverfd, request_buf, strlen(request_buf));

  
  read_requesthdrs(&request_rio, request_buf, serverfd, hostname, port);

  
  Rio_readinitb(&response_rio, serverfd);
  while (strcmp(response_buf, "\r\n"))
  {
    Rio_readlineb(&response_rio, response_buf, MAXLINE);
    if (strstr(response_buf, "Content-length"))
      content_length = atoi(strchr(response_buf, ':') + 1);
    Rio_writen(clientfd, response_buf, strlen(response_buf));
  }

  
  response_ptr = malloc(content_length);
  Rio_readnb(&response_rio, response_ptr, content_length);
  Rio_writen(clientfd, response_ptr, content_length);

  if (content_length <= MAX_OBJECT_SIZE) 
  {
    // `web_object` 구조체 생성
    web_object_t *web_object = (web_object_t *)calloc(1, sizeof(web_object_t));
    web_object->response_ptr = response_ptr;
    web_object->content_length = content_length;
    strcpy(web_object->path, path);
    write_cache(web_object); // 캐시 연결 리스트에 추가
  }
  else
    free(response_ptr); // 캐싱하지 않은 경우만 메모리 반환

  Close(serverfd);
}


void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  // 에러 Bdoy 생성
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor="
                "ffffff"
                ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  // 에러 Header 생성 & 전송
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));

  // 에러 Body 전송
  Rio_writen(fd, body, strlen(body));
}

void parse_uri(char *uri, char *hostname, char *port, char *path)
{
  // host_name의 시작 위치 포인터: '//'가 있으면 //뒤(ptr+2)부터, 없으면 uri 처음부터
  char *hostname_ptr = strstr(uri, "//") ? strstr(uri, "//") + 2 : uri;
  char *port_ptr = strchr(hostname_ptr, ':');
  char *path_ptr = strchr(hostname_ptr, '/');
  strcpy(path, path_ptr);

  if (port_ptr) // port 있는 경우
  {
    strncpy(port, port_ptr + 1, path_ptr - port_ptr - 1); 
    strncpy(hostname, hostname_ptr, port_ptr - hostname_ptr);
  }
  else // port 없는 경우
  {
    if (is_local_test)
      strcpy(port, "80"); // port의 기본 값인 80으로 설정
    else
      strcpy(port, "8000");
    strncpy(hostname, hostname_ptr, path_ptr - hostname_ptr);
  }
}

// Request Header를 읽고 Server에 전송하는 함수
// 필수 헤더가 없는 경우에는 필수 헤더를 추가로 전송
void read_requesthdrs(rio_t *request_rio, void *request_buf, int serverfd, char *hostname, char *port)
{
  int is_host_exist;
  int is_connection_exist;
  int is_proxy_connection_exist;
  int is_user_agent_exist;

  Rio_readlineb(request_rio, request_buf, MAXLINE); // 첫번째 줄 읽기
  while (strcmp(request_buf, "\r\n"))
  {
    if (strstr(request_buf, "Proxy-Connection") != NULL)
    {
      sprintf(request_buf, "Proxy-Connection: close\r\n");
      is_proxy_connection_exist = 1;
    }
    else if (strstr(request_buf, "Connection") != NULL)
    {
      sprintf(request_buf, "Connection: close\r\n");
      is_connection_exist = 1;
    }
    else if (strstr(request_buf, "User-Agent") != NULL)
    {
      sprintf(request_buf, user_agent_hdr);
      is_user_agent_exist = 1;
    }
    else if (strstr(request_buf, "Host") != NULL)
    {
      is_host_exist = 1;
    }

    Rio_writen(serverfd, request_buf, strlen(request_buf)); // Server에 전송
    Rio_readlineb(request_rio, request_buf, MAXLINE);       // 다음 줄 읽기
  }

  
  if (!is_proxy_connection_exist)
  {
    sprintf(request_buf, "Proxy-Connection: close\r\n");
    Rio_writen(serverfd, request_buf, strlen(request_buf));
  }
  if (!is_connection_exist)
  {
    sprintf(request_buf, "Connection: close\r\n");
    Rio_writen(serverfd, request_buf, strlen(request_buf));
  }
  if (!is_host_exist)
  {
    if (!is_local_test)
      hostname = "52.79.234.188";
    sprintf(request_buf, "Host: %s:%s\r\n", hostname, port);
    Rio_writen(serverfd, request_buf, strlen(request_buf));
  }
  if (!is_user_agent_exist)
  {
    sprintf(request_buf, user_agent_hdr);
    Rio_writen(serverfd, request_buf, strlen(request_buf));
  }

  sprintf(request_buf, "\r\n"); // 종료문
  Rio_writen(serverfd, request_buf, strlen(request_buf));
  return;
}