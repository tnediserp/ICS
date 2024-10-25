#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void doit(int fd);
void read_requesthdrs(rio_t *rp, char *req_header_buf);
int pack_requesthdrs(rio_t *rp, char *hostname, char *port, char *new_header_buf);
int parse_uri(char *uri, char *filename, char *cgiargs);
void parse_url(char *url, char *hostname, char *uri, char *port);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *headers);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);
void *thread(void *vargp);


/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

int main(int argc, char **argv) 
{
    signal(SIGPIPE, SIG_IGN);

    int listenfd, *connfdp;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }

    listenfd = open_listenfd(argv[1]);
    
    while (1) {
	    clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
	    *connfdp = accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        //printf("Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid, NULL, thread, (void *)connfdp);
    }
}
/* $end tinymain */

void *thread(void *vargp)
{
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void doit(int fd) 
{
    int clientfd;
    char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], uri[MAXLINE], port[MAXLINE];
    char new_header_buf[MAXLINE];
    rio_t rio, clientrio;

    /* Read request line and headers */
    rio_readinitb(&rio, fd);
    if (!rio_readlineb(&rio, buf, MAXLINE))  //line:netp:doit:readrequest
    {
        printf("Fail to read request line\n");
        return;
    }

    if(strlen(buf) >= MAXLINE - 1)
    {
        printf("Request line is too long\r\n");
        return;
    }
        
    //printf("%s", buf);
    sscanf(buf, "%s %s %s", method, url, version);       //line:netp:doit:parserequest
    if (strcasecmp(method, "GET")) 
    {                     //line:netp:doit:beginrequesterr
        clienterror(fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
        printf("%s: Not Implemented\n", method);
        return;
    }                                                    //line:netp:doit:endrequesterr
    //read_requesthdrs(&rio, req_header_buf);              //line:netp:doit:readrequesthdrs

    /* Parse URL from GET request */
    strcpy(buf, url);
    parse_url(buf, hostname, uri, port);       //line:netp:doit:staticcheck

    /* 创建将要向服务器发送的请求行和请求报头 */
    //sprintf(new_header_buf, "GET %s HTTP/1.0\r\n", uri);
    new_header_buf[0] = '\0';
    strcat(new_header_buf, method);
    strcat(new_header_buf, " ");
    strcat(new_header_buf, uri);
    strcat(new_header_buf, " HTTP/1.0\r\n");
    if(pack_requesthdrs(&rio, hostname, port, new_header_buf) < 0)
    {
        printf("fail to pack request headers\r\n");
        return;
    }

    printf("host: %s\n", hostname);

    /* proxy向服务器传输请求 */
    clientfd = Open_clientfd(hostname, port);
    if(clientfd < 0)
    {
        printf("connection failed.\n");
        return;
    }
    Rio_readinitb(&clientrio, clientfd);
    Rio_writen(clientfd, new_header_buf, MAXLINE);

    /* proxy接收服务器传回的数据，传给客户端 */
    int num;
    while((num = Rio_readlineb(&clientrio, buf, MAXLINE)))
    {
        Rio_writen(fd, buf, num);
    }
    Close(clientfd);
}
/* $end doit */

/*
 * read_requesthdrs - read HTTP request headers
 */
/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp, char *req_header_buf) 
{
    char buf[MAXLINE];
    req_header_buf[0]='\0';

    rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    strcat(req_header_buf, buf);
    while(strcmp(buf, "\r\n")) {          //line:netp:readhdrs:checkterm
        rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
        strcat(req_header_buf, buf);
    }
    return;
}
/* $end read_requesthdrs */

/*
 * pack_requesthdrs
 * 创建将要向服务器发送的请求
 */
int pack_requesthdrs(rio_t *rp, char *hostname, char *port, char *new_header_buf)
{
    char buf[MAXLINE];
    int flag=0;
    while(Rio_readlineb(rp, buf, MAXLINE) > 0)
    {
        if(!strcmp(buf, "\r\n"))
            break;
        /* 如果读到Host，标记 */
        if(strstr(buf, "Host"))
            flag = 1;
        /* 如果读到User-Agent, Connection, Proxy-Connection, 忽略 */
        if(strstr(buf, "User-Agent") ||
           strstr(buf, "Connection") ||
           strstr(buf, "Proxy-Connection"))
            continue;
        /* 否则strcat */
        strcat(new_header_buf, buf);
    }

    /* 如果没有读到Host，就要用hostname来请求 */
    if(!flag)
    {
        sprintf(new_header_buf, "%sHost: %s:%s\r\n", new_header_buf, hostname, port);
    }
    /* 设置指定的请求报头 */
    strcat(new_header_buf, user_agent_hdr);
    strcat(new_header_buf, connection_hdr);
    strcat(new_header_buf, proxy_connection_hdr);

    /* 结尾加上\r\n，（否则会segmentation fault） */
    strcat(new_header_buf, "\r\n");

    if(strlen(new_header_buf) >= MAXLINE)
        return -1;
    return 0;
}

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
/* $begin parse_uri */
int parse_uri(char *uri, char *filename, char *cgiargs) 
{
    char *ptr;

    if (!strstr(uri, "cgi-bin")) {  /* Static content */ //line:netp:parseuri:isstatic
        strcpy(cgiargs, "");                             //line:netp:parseuri:clearcgi
        strcpy(filename, ".");                           //line:netp:parseuri:beginconvert1
        strcat(filename, uri);                           //line:netp:parseuri:endconvert1
        if (uri[strlen(uri)-1] == '/')                   //line:netp:parseuri:slashcheck
            strcat(filename, "home.html");               //line:netp:parseuri:appenddefault
        return 1;
    }
    else {  /* Dynamic content */                        //line:netp:parseuri:isdynamic
        ptr = index(uri, '?');                           //line:netp:parseuri:beginextract
        if (ptr) {
            strcpy(cgiargs, ptr+1);
            *ptr = '\0';
        }
        else 
            strcpy(cgiargs, "");                         //line:netp:parseuri:endextract
        strcpy(filename, ".");                           //line:netp:parseuri:beginconvert2
        strcat(filename, uri);                           //line:netp:parseuri:endconvert2
        return 0;
    }
}
/* $end parse_uri */

/*
 * parse_url
 * 解析URL。获得hostname, uri, port.
 */
void parse_url(char *url, char *hostname, char *uri, char *port)
{
/*    strcpy(port, "80");
    char *p;
    strcpy(hostname, url);*/
    /* 处理 http:// 格式 */
/*    if((p = strstr(hostname, "http://")) != NULL)
    {
        strcpy(hostname, p+7);
    }
    /* 如果规定了端口号，hostname的结尾应该是':' */
/*    if((p = strchr(hostname, ':')) != NULL)
    {
        *p = '\0';
        strcpy(port, p+1);
        p = strchr(port, '/');
    }
    /* 否则是默认端口号，hostname的结尾应该是'/' */
/*    else
    {
        p = strchr(hostname, '/');
    }
    /* 剩余部分是URI */
/*    if(p != NULL)
    {
        strcpy(uri, p);
        *p = '\0';
    }
    else
    {
        strcpy(uri, "");
    }
*/
    char *p, *q;
    /* 处理http */
    if(strstr(url, "http://"))
    {
        url += strlen("http://");
    }
    p = strchr(url, ':');
    q = strchr(url, '/');
    /* 规定端口，且URI不为空 */
    if(p != NULL && q != NULL)
    {
        *p = '\0';
        strcpy(hostname, url);
        *q = '\0';
        strcpy(port, p + 1);
        *q = '/';
        strcpy(uri, q);
    }
    /* 默认端口，且URI不为空 */
    else if(p == NULL && q != NULL)
    {
        *q = '\0';
        strcpy(hostname, url);
        *q = '/';
        strcpy(uri, q);
        strcpy(port, "80");
    }
    /* 规定端口，且URI为空 */
    else if(p != NULL && q == NULL)
    {
        *p = '\0';
        strcpy(hostname, url);
        strcpy(port, p + 1);
        strcpy(uri, "/");
    }
    /* 默认端口，且URI为空 */
    else
    {
        strcpy(hostname, url);
        strcpy(port, "80");
        strcpy(uri, "/");
    }
}

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-overflow"
    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);
#pragma GCC diagnostic pop

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    rio_writen(fd, buf, strlen(buf));
    rio_writen(fd, body, strlen(body));
}
/* $end clienterror */