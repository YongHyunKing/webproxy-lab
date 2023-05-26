/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method); // @과제11번 수정
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method); // @과제11번 수정
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv) {//argc: 인자 개수, argv: 인자 배열
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) { // 입력 인자 2개가 아니면
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    printf("connfd:%d\n",connfd); //connfd 확인용
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd); 
    Close(connfd);
  }
}
// 클라이언트의 요청 라인을 확인해 정적, 동적 콘텐츠를 확인하고 돌려줌
void doit(int fd) // fd는 connfd라는게 중요함!
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    Rio_readinitb(&rio, fd); 

    Rio_readlineb(&rio, buf, MAXLINE);
    printf("Request headers:\n");
    printf("%s",buf);
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")&&strcasecmp(method,"HEAD")){
        clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
        return;
    }

    read_requesthdrs(&rio);

    is_static = parse_uri(uri, filename, cgiargs);
    if (stat(filename, &sbuf) < 0){
        clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
        return;
    }

    if (is_static){
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){
            clienterror(fd, filename, "403", "Forbidden",
                        "Tiny couldn't read the file");
            return;
        }
        serve_static(fd, filename, sbuf.st_size, method);
    }
    else{ // Serve dynamic content
        // 파일이 실행 가능한지 검증
        if(!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){
            clienterror(fd, filename, "403", "Forbidden",
                        "Tiny couldn't run the CGI program");
            return;
        }
        serve_dynamic(fd, filename, cgiargs, method); // 실행가능하다면 동적(dynamic) 컨텐츠를 클라이언트에게 제공
    }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    // Build the HTTP response body
    // sprintf는 출력하는 결과 값을 변수에 저장하게 해주는 기능있음
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n",body);

    // Print the HTTP response
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

void read_requesthdrs(rio_t *rp)
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);

    while(strcmp(buf, "\r\n")){
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}

int parse_uri(char *uri, char *filename, char *cgiargs)
{
    char *ptr;

    if(!strstr(uri, "cgi-bin")){ 
        strcpy(cgiargs, ""); 
        strcpy(filename, "."); 
        strcat(filename, uri);
        if (uri[strlen(uri)-1] == '/')
            strcat(filename, "home.html");
        return 1;
    }
    else{
        ptr = index(uri, '?');
        if(ptr){
            strcpy(cgiargs, ptr+1);
            *ptr = '\0';
        }
        else strcpy(cgiargs, "");
        strcpy(filename, ".");
        strcat(filename, uri);
        return 0;
    }
}
// static content를 요청하면 서버가 disk에서 파일을 찾아서 메모리 영역으로 복사하고, 복사한 것을 client fd로 복사
void serve_static(int fd, char *filename, int filesize, char *method) // fd는 connfd
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

    // Send response headers to client
    get_filetype(filename, filetype); 
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web Server\r\n",buf);
    sprintf(buf, "%sConnection: close\r\n",buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype); 
    Rio_writen(fd, buf, strlen(buf));
    printf("Response headers: \n");
    printf("%s", buf);

    if(strcasecmp(method,"HEAD") == 0) return;

    srcfd = Open(filename, O_RDONLY, 0);
    srcp = Malloc(filesize);
    Rio_readn(srcfd,srcp,filesize);
    Close(srcfd);
    Rio_writen(fd, srcp, filesize);
    Free(srcp);
}

// get_filetype - Derive file type from filename, Tiny는 5개의 파일형식만 지원한다.
void get_filetype(char *filename, char *filetype)
{
    if(strstr(filename, ".html")) 
        strcpy(filetype, "text/html");
    else if(strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
    else if(strstr(filename, ".png"))
        strcpy(filetype, "image/png");
    else if(strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpeg");
    else if(strstr(filename, ".mp4"))
        strcpy(filetype, "video/mp4");
    else    
        strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method)
{
    char buf[MAXLINE], *emptylist[] = { NULL };

    // Return first part of HTTP response
    // 클라이언트에 성공을 알려주는 응답 라인을 보내는 것으로 시작
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));

    if(Fork() == 0){
        setenv("QUERY_STRING", cgiargs, 1);
        setenv("REQUEST_METHOD", method, 1);

        Dup2(fd, STDOUT_FILENO); // Redirect stdout to client
        Execve(filename, emptylist, environ); // Run CGI program 실행(adder를 실행)
    } 

    Wait(NULL); // Parent waits for and reaps child
}