/*
 * proxy.c - A cacheing web proxy
 * Andrew ID1: zeyuanl
 * Andrew ID2: yiwench
 */

#include <stdio.h>
#include "csapp.h"
#include <string.h>
#include <semaphore.h>
#include <pthread.h>

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

static const char *user_agent = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept1 = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding = "Accept-Encoding: gzip, deflate\r\n";

int doproxy(int fd);
int read_request(rio_t *rp, char *bufrequest, char *hostname, int *port);
int parse_uri(char *uri, char *hostname, int *port);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);
int main(int argc, char **argv)
{
    int listenfd, connfd, port;
    unsigned clientlen;
    struct sockaddr_in clientaddr;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    port = atoi(argv[1]);

    // ignore sigpipe
    signal(SIGPIPE, SIG_IGN);
    
    listenfd = Open_listenfd(port);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
        doproxy(connfd);
        close(connfd);
    }
}
/* $end tinymain */

/*
 * doproxy - serve as a proxy to handle one HTTP request/response transaction
 */
/* $begin doit */
int doproxy(int connfd)
{
    int port, serverfd, len;
    char bufrequest[MAXLINE], bufresponse[MAXLINE], hostname[MAXLINE];
    rio_t rio_client, rio_server;

    // reset buffer
    memset(bufrequest, 0, sizeof(bufrequest));
    memset(bufresponse, 0, sizeof(bufresponse));
    memset(hostname, 0, sizeof(hostname));

    /* Read request line and headers */
    rio_readinitb(&rio_client, connfd);
    // bufrequest is the buffer for outgoing request to real server
    read_request(&rio_client, bufrequest, hostname, &port);

    printf("hostname:%s\tport:%d\n", hostname, port);
    printf("bufrequest: %s\n", bufrequest);
    
    // send request to real server
    if((serverfd = open_clientfd(hostname, port)) < 0)
    {
        fprintf(stderr, "open server fd error\n");
        return -1;
    }
    rio_readinitb(&rio_server, serverfd);

    if(rio_writen(serverfd, bufrequest, sizeof(bufrequest)) < 0)
    {
        fprintf(stderr, "rio_writen send request error\n");
        return -1;
    }

    // get response from server and send back to the client
    while((len = rio_readnb(&rio_server, bufresponse, sizeof(bufresponse))) > 0)
    {
        printf("hostname:%s\tport:%d\nbufresponse:%s\n", hostname, port, bufresponse);

        if(rio_writen(connfd, bufresponse, sizeof(bufresponse)) < 0)
        {
            fprintf(stderr, "rio_writen send response error\n");
            return -1;
        }
        // reset buffer
        memset(bufresponse, 0, sizeof(bufresponse));
    }

    // close fd
    close(serverfd);
    return 0;
}
/* $end doit */

/*
 * read_request - read and parse HTTP request headers
 * return 0 if success, -1 otherwise.
 */
/* $begin read_request */
int read_request(rio_t *rp, char *bufrequest, char *hostname, int *port)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE];

    // request line
    if(rio_readlineb(rp, buf, MAXLINE) < 0)
        return -1;
    sscanf(buf, "%s %s", method, uri);
    // extract hostname and port info
    parse_uri(uri, hostname, port);
    // fill in request for real server
    sprintf(bufrequest, "%s %s HTTP/1.0\r\n", method, uri);

    // request hdr
    if(rio_readlineb(rp, buf, MAXLINE) < 0)
        return -1;
    while(strcmp(buf, "\r\n")) {
        if(strstr(buf, "Host"))
        {
            strcat(bufrequest, "Host: ");
            strcat(bufrequest, hostname);
            strcat(bufrequest, "\r\n");
        }
        else if(strstr(buf, "Accept:"))
            strcat(bufrequest, accept1);
        else if(strstr(buf, "Accept-Encoding:"))
            strcat(bufrequest, accept_encoding);
        else if(strstr(buf, "User-Agent:"))
            strcat(bufrequest, user_agent);
        else if(strstr(buf, "Proxy-Connection:"))
            strcat(bufrequest, "Proxy-Connection: close\r\n");
        else if(strstr(buf, "Connection:"))
            strcat(bufrequest, "Connection: close\r\n");
        else
            // append addtional header except above listed headers
            strcat(bufrequest, buf);

        if(rio_readlineb(rp, buf, MAXLINE) < 0)
        {
            fprintf(stderr, "rio_readlineb read request error\n");
            return -1;
        }
    }
    
    //append header separater
    strcat(bufrequest, "\r\n");

    // append header specified in writeup
    /*strcat(bufrequest, "Host: ");
    strcat(bufrequest, hostname);
    strcat(bufrequest, "\r\n");

    strcat(bufrequest, user_agent);
    strcat(bufrequest, accept1);
    strcat(bufrequest, accept_encoding);
    strcat(bufrequest, "Connection: close\r\n");
    strcat(bufrequest, "Proxy-Connection: close\r\n");*/

    return 0;
}
/* $end read_requesthdrs */

/*
 * parse_uri - parse URI into hostname and port args
 *             return 0 if success, -1 if fail
 */
/* $begin parse_uri */
int parse_uri(char *uri, char *hostname, int *port)
{
    char *ps, *pe, *phost;
    char *uricp = malloc(strlen(uri) + 1);
    strncpy(uricp, uri, strlen(uri));

    // start pointer
    if((ps = strstr(uricp, "http://")) == NULL)
        return -1;
    ps += strlen("http://");

    // end pointer
    if((pe = strstr(ps, "/")) == NULL)
        return -1;
    *pe = '\0';
    
    //strncpy(hostname, ps, sizeof(ps));

    // if hostname contains port
    phost = strsep(&ps, ":");
    if(ps == NULL)
        *port = 80;
    else
        *port = atoi(ps);
    
    strncpy(hostname, phost, strlen(phost));
    
    return 0;
}
/* $end parse_uri */


/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
/* $end clienterror */

