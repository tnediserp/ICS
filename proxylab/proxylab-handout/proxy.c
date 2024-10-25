#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void doit(int fd);
int pack_requesthdrs(rio_t *rp, char *hostname, char *port, char *new_header_buf);
void parse_url(char *url, char *hostname, char *uri, char *port);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *headers);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);
void *thread(void *vargp);
void *s2c_thread(void *vargp);
void *c2s_thread(void *vargp);
void handler(int sig) {
    return;
}
void sigchld_handler(int sig) { // reap all children
    int bkp_errno = errno;
    while(waitpid(-1, NULL, WNOHANG)>0);
    errno=bkp_errno;
}
extern int cache_find(int fd, char *dstkey);
extern void cache_insert(char *key, char *value, int datasize);
extern void cache_init();

typedef struct
{
    int fd;
    int clientfd;
    rio_t rio;
    rio_t clientrio;
}trans_arg;


/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";


int main(int argc, char **argv) 
{
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, sigchld_handler);

    int listenfd, *connfdp;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    cache_init();

    /* Check command line args */
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }

    listenfd = open_listenfd(argv[1]);
    
    while (1) {
	    clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
	    *connfdp = accept(listenfd, (SA *)&clientaddr, &clientlen);
        getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        //printf("Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid, NULL, thread, (void *)connfdp);
    }
}

void *thread(void *vargp)
{
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    //Close(connfd);
    return NULL;
}

/*
 * s2c_thread
 * 进行从服务器到客户端的数据传送
 * failed!
 */
void *s2c_thread(void *vargp)
{
    int fd, clientfd;
    int num;
    rio_t clientrio;
    char buf[MAXLINE];

    trans_arg *p = (trans_arg *)vargp;
    fd = p->fd;
    clientfd = p->clientfd;
    clientrio = p->clientrio;
    Pthread_detach(pthread_self());
    Free(vargp);
    printf("from server to client\nfd= %d, clientfd= %d\n", fd, clientfd);

    while((num = Rio_readnb(&clientrio, buf, MAXLINE)) > 0)
    {
        Rio_writen(fd, buf, num);
    }

    return NULL;
}

/*
 * c2s_thread
 * 进行从客户端到服务器的数据传送
 * failed!
 */
void *c2s_thread(void *vargp)
{
    int fd, clientfd;
    int num;
    rio_t rio;
    char buf[MAXLINE];

    trans_arg *p = (trans_arg *)vargp;
    fd = p->fd;
    clientfd = p->clientfd;
    rio = p->rio;
    Pthread_detach(pthread_self());
    Free(vargp);
    printf("from client to server\nfd= %d, clientfd= %d\n", fd, clientfd);

    while((num = Rio_readnb(&rio, buf, MAXLINE)) > 0)
    {
        Rio_writen(clientfd, buf, num);
    }
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
    Rio_readinitb(&rio, fd);
    if (!rio_readlineb(&rio, buf, MAXLINE))
    {
        printf("Fail to read request line\n");
        Close(fd);
        return;
    }

    /* 请求行过长 */
    if(strlen(buf) >= MAXLINE - 1)
    {
        printf("Request line is too long\r\n");
        Close(fd);
        return;
    }
        
    //printf("%s", buf);
    sscanf(buf, "%s %s %s", method, url, version);

    /* 处理HTTPS */
    if(!strcasecmp(method, "CONNECT"))
    {
        int num;
        pthread_t tid;
        char c;
        /* 接收请求报头，简单丢弃 */
        while(Rio_readlineb(&rio, buf, MAXLINE) > 0)
        {
            if(!strcmp(buf, "\r\n"))
                break;
        }

        /* 连接服务器，并向客户端发送HTTP/1.1 200 Connection Established */
        strcpy(buf, url);
        parse_url(buf, hostname, uri, port);
        printf("CONNECT: hostname= %s, port= %s\n", hostname, port);
        clientfd = Open_clientfd(hostname, port);
        Rio_readinitb(&clientrio, clientfd);
        
        Rio_writen(fd, "HTTP/1.1 200 Connection Established\r\n\r\n", 40);

        /* 进行服务器和客户端之间的数据传送 
         * failed!
         */
        /* 服务器 -> 客户端 */
        trans_arg *vargp = Malloc(sizeof(trans_arg));
        vargp->fd = fd;
        vargp->clientfd = clientfd;
        vargp->rio = rio;
        vargp->clientrio = clientrio;
        Pthread_create(&tid, NULL, s2c_thread, (void *)vargp);

        /* 客户端 -> 服务器 */
        vargp = Malloc(sizeof(trans_arg));
        vargp->fd = fd;
        vargp->clientfd = clientfd;
        vargp->rio = rio;
        vargp->clientrio = clientrio;
        Pthread_create(&tid, NULL, c2s_thread, (void *)vargp);

        return;
    }

    /* 未实现的其他方法 */
    if (strcasecmp(method, "GET")) 
    {
        clienterror(fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
        printf("%s: Not Implemented\n", method);
        Close(fd);
        return;
    }

    /* cache命中 */
    if(cache_find(fd, url))
    {
        printf("cache hit\n");
        Close(fd);
        return;
    }
       

    /* Parse URL from GET request */
    strcpy(buf, url);
    parse_url(buf, hostname, uri, port);       

    /* 创建将要向服务器发送的请求行和请求报头
     * in new_header_buf
     */
    new_header_buf[0] = '\0';
    strcat(new_header_buf, method);
    strcat(new_header_buf, " ");
    strcat(new_header_buf, uri);
    strcat(new_header_buf, " HTTP/1.0\r\n");
    if(pack_requesthdrs(&rio, hostname, port, new_header_buf) < 0)
    {
        printf("fail to pack request headers\r\n");
        Close(fd);
        return;
    }

    printf("host: %s\n", hostname);

    /* proxy向服务器传输请求 */
    clientfd = Open_clientfd(hostname, port);
    if(clientfd < 0)
    {
        printf("connection failed.\n");
        Close(fd);
        return;
    }
    Rio_writen(clientfd, new_header_buf, MAXLINE);

    /* proxy接收服务器传回的数据，传给客户端 */
    int num, datasize=0;
    char value[MAX_OBJECT_SIZE];
    Rio_readinitb(&clientrio, clientfd);
    while((num = Rio_readnb(&clientrio, buf, MAXLINE)) > 0)
    {
        Rio_writen(fd, buf, num);
        if(datasize + num < MAX_OBJECT_SIZE)
            memcpy(value + datasize, buf, num);
        datasize += num;
    }
    /* 写入cache */
    if(datasize < MAX_OBJECT_SIZE)
    {
        cache_insert(url, value, datasize);
    }

    Close(fd);
    Close(clientfd);
}
/* $end doit */


/*
 * pack_requesthdrs
 * 创建将要向服务器发送的请求
 */
int pack_requesthdrs(rio_t *rp, char *hostname, 
                    char *port, char *new_header_buf)
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
        sprintf(new_header_buf, "%sHost: %s:%s\r\n", 
        new_header_buf, hostname, port);
    }
    /* 设置指定的请求报头 */
    strcat(new_header_buf, user_agent_hdr);
    strcat(new_header_buf, connection_hdr);
    strcat(new_header_buf, proxy_connection_hdr);

    /* 结尾加上\r\n，（否则会segmentation fault） */
    strcat(new_header_buf, "\r\n");

    /* 若长度过长，返回-1 */
    if(strlen(new_header_buf) >= MAXLINE)
        return -1;
    return 0;
}

/*
 * parse_url
 * 解析URL。获得hostname, uri, port.
 */
void parse_url(char *url, char *hostname, char *uri, char *port)
{
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