#include <stdio.h>
#include "csapp.h"

/* 원 서버에 보낼 고정 User-Agent 헤더 */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int fd);
void parse_uri(char *uri, char *hostname, char *port, char *path);
void parse_host_header(char *host_hdr, char *hostname, char *port);
void build_http_header(char *http_header, char *hostname, char *port, char *path, rio_t *client_rio);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* 실행 인자는 프록시가 listen할 포트 하나만 받는다. */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  /* tiny 서버와 비슷하게 listen 소켓을 만들고 클라이언트 연결을 기다린다. */
  listenfd = Open_listenfd(argv[1]);

  /* 클라이언트가 먼저 연결을 끊어도 write 중 SIGPIPE로 죽지 않게 한다. */
  Signal(SIGPIPE, SIG_IGN);

  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}

void doit(int fd)
{
  int serverfd;
  ssize_t n;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], port[MAXLINE], path[MAXLINE], http_header[MAXLINE];
  rio_t client_rio, server_rio;

  /* 클라이언트 소켓에서 요청 라인을 읽는다. */
  Rio_readinitb(&client_rio, fd);
  if (!Rio_readlineb(&client_rio, buf, MAXLINE))
    return;

  printf("Request line: %s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  /* 이 프록시는 GET 요청만 처리한다. */
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not implemented",
                "Proxy does not implement this method");
    return;
  }

  /* URI와 헤더를 분석해서 원 서버에 보낼 HTTP/1.0 요청을 만든다. */
  parse_uri(uri, hostname, port, path);
  build_http_header(http_header, hostname, port, path, &client_rio);

  /* absolute URI가 아니면 Host 헤더에서 hostname을 얻어야 한다. */
  if (hostname[0] == '\0') {
    clienterror(fd, uri, "400", "Bad Request",
                "Proxy could not determine the target host");
    return;
  }

  /* 프록시가 원 서버 입장에서는 클라이언트가 된다. */
  if ((serverfd = open_clientfd(hostname, port)) < 0) {
    clienterror(fd, hostname, "502", "Bad Gateway",
                "Proxy could not connect to the target host");
    return;
  }

  /* 원 서버에는 프록시가 새로 만든 요청 헤더를 보낸다. */
  Rio_writen(serverfd, http_header, strlen(http_header));

  /* 원 서버 응답을 읽는 즉시 클라이언트에게 전달한다. */
  Rio_readinitb(&server_rio, serverfd);
  while ((n = Rio_readnb(&server_rio, buf, MAXLINE)) > 0)
    Rio_writen(fd, buf, n);

  Close(serverfd);
}

void parse_uri(char *uri, char *hostname, char *port, char *path)
{
  char *host_begin, *host_end, *port_begin, *path_begin;
  int host_len, port_len;

  strcpy(port, "80");   /* 포트가 생략되면 HTTP 기본 포트 80 사용 */
  strcpy(path, "/");    /* path가 생략되면 루트 경로 사용 */
  hostname[0] = '\0';

  /* 프록시 요청은 보통 http://host:port/path 형태의 absolute URI다. */
  host_begin = strstr(uri, "://");
  if (!host_begin) {
    /* 브라우저/테스트가 GET /path 형태로 보내면 Host 헤더에서 host를 얻는다. */
    strcpy(path, uri);
    return;
  }
  host_begin += 3;

  /* host 부분과 path 부분을 나눈다. */
  path_begin = strchr(host_begin, '/');
  if (path_begin) {
    strcpy(path, path_begin);
    host_end = path_begin;
  }
  else {
    host_end = host_begin + strlen(host_begin);
  }

  /* host:port 형태이면 host와 port를 분리한다. */
  port_begin = strchr(host_begin, ':');
  if (port_begin && port_begin < host_end) {
    host_len = port_begin - host_begin;
    port_len = host_end - port_begin - 1;
    strncpy(hostname, host_begin, host_len);
    hostname[host_len] = '\0';
    strncpy(port, port_begin + 1, port_len);
    port[port_len] = '\0';
  }
  else {
    host_len = host_end - host_begin;
    strncpy(hostname, host_begin, host_len);
    hostname[host_len] = '\0';
  }
}

void parse_host_header(char *host_hdr, char *hostname, char *port)
{
  char *host_begin, *host_end, *port_begin;
  int host_len, port_len;

  /* Host: localhost:8000 에서 localhost와 8000을 뽑아낸다. */
  host_begin = host_hdr + strlen("Host:");
  while (*host_begin == ' ' || *host_begin == '\t')
    host_begin++;

  host_end = strpbrk(host_begin, "\r\n");
  if (!host_end)
    host_end = host_begin + strlen(host_begin);

  port_begin = strchr(host_begin, ':');
  if (port_begin && port_begin < host_end) {
    host_len = port_begin - host_begin;
    port_len = host_end - port_begin - 1;
    strncpy(hostname, host_begin, host_len);
    hostname[host_len] = '\0';
    strncpy(port, port_begin + 1, port_len);
    port[port_len] = '\0';
  }
  else {
    host_len = host_end - host_begin;
    strncpy(hostname, host_begin, host_len);
    hostname[host_len] = '\0';
  }
}

void build_http_header(char *http_header, char *hostname, char *port, char *path, rio_t *client_rio)
{
  char buf[MAXLINE], request_hdr[MAXLINE], other_hdrs[MAXLINE], host_hdr[MAXLINE];

  /* 원 서버에는 absolute URI가 아니라 path만 담은 HTTP/1.0 요청을 보낸다. */
  sprintf(request_hdr, "GET %s HTTP/1.0\r\n", path);

  /* Host 헤더는 기존 요청에 있으면 나중에 덮어쓴다. */
  if (!strcmp(port, "80"))
    sprintf(host_hdr, "Host: %s\r\n", hostname);
  else
    sprintf(host_hdr, "Host: %s:%s\r\n", hostname, port);
  other_hdrs[0] = '\0';

  /* 클라이언트가 보낸 나머지 요청 헤더를 읽어서 필요한 것만 전달한다. */
  while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
    if (!strcmp(buf, "\r\n"))
      break;

    /* Host 헤더는 보존하되, relative URI 요청이면 여기서 목적지를 얻는다. */
    if (!strncasecmp(buf, "Host:", 5)) {
      strcpy(host_hdr, buf);
      if (hostname[0] == '\0')
        parse_host_header(buf, hostname, port);
      continue;
    }

    /* 과제 스펙에 맞게 이 세 헤더는 프록시가 고정값으로 다시 만든다. */
    if (!strncasecmp(buf, "User-Agent:", 11) ||
        !strncasecmp(buf, "Connection:", 11) ||
        !strncasecmp(buf, "Proxy-Connection:", 17))
      continue;

    strcat(other_hdrs, buf);
  }

  /* 원 서버에 최종적으로 보낼 요청 헤더 조립 */
  sprintf(http_header,
          "%s"
          "%s"
          "%s"
          "Connection: close\r\n"
          "Proxy-Connection: close\r\n"
          "%s"
          "\r\n",
          request_hdr, host_hdr, user_agent_hdr, other_hdrs);
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* 클라이언트에게 보낼 간단한 HTML 에러 응답 생성 */
  snprintf(body, MAXBUF,
           "<html><title>Proxy Error</title>"
           "<body bgcolor=\"ffffff\">\r\n"
           "%s: %s\r\n"
           "<p>%s: %s\r\n"
           "<hr><em>The Tiny Proxy server</em>\r\n",
           errnum, shortmsg, longmsg, cause);

  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

