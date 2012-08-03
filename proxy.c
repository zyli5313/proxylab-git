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
#include <stdlib.h>

#ifdef BIT32
typedef unsigned long long aint;
#else
typedef unsigned long aint;
#endif

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

static const char *user_agent = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept1 = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding = "Accept-Encoding: gzip, deflate\r\n";

int doproxy(int fd);
int read_request(rio_t *rp, char *bufrequest, char *hostname, int *port, char *uri);
int parse_uri(char *uri, char *hostname, int *port);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

void *thread(void *vargp);
int open_clientfd_safe(char *hostname, int port);
void initialization();
static void init();
static void pop_usage(int *array, int key, int len);
static int data_load(char *tag, char *response);
static void data_store(char *tag, char *response);

struct cache_line
{
   	int valid;
    	char *tag;
    	char *block;
};

struct cache_set
{
    	struct cache_line *line;
    	int *usage;
};

struct cache
{
    	struct cache_set *set;
};

static struct cache cache;

/* Global variables */
sem_t mutex;
static int set_num, line_num;

int main(int argc, char **argv)
{
    	int listenfd, *connfd, port;
    	unsigned clientlen;
    	struct sockaddr_in clientaddr;
    	pthread_t tid;

    	/* Check command line args */
    	if (argc != 2) {
        	fprintf(stderr, "usage: %s <port>\n", argv[0]);
        	exit(1);
    	}
   	port = atoi(argv[1]);

    	//ignore sigpipe
    	signal(SIGPIPE, SIG_IGN);
	//initialization 
    	initialization();

    	listenfd = Open_listenfd(port);
    	while(1){
		connfd = malloc(sizeof(int));
    		clientlen = sizeof(clientaddr);
    		*connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
		printf("New connection! connfd:%d\n", *connfd);
    		pthread_create(&tid, NULL, thread, connfd);
//		doproxy(connfd);
//    		close(connfd);
   	}
}

/* Initialization routine */
void initialization()
{
	sem_init(&mutex, 0, 1);
	set_num = 1;
	line_num = 10;
        init();	
}

static void init()
{
    	int i, j;
    	cache.set = malloc(sizeof (struct cache_set) * set_num);
    	for (i = 0; i < set_num; i++)
    	{
        	cache.set[i].line = malloc(sizeof (struct cache_line) * line_num);
       	 	cache.set[i].usage = malloc(sizeof (int) * line_num);
        	for (j = 0; j < line_num; j++)
        	{
            		cache.set[i].usage[j] = j;
            		cache.set[i].line[j].valid = 0;
			cache.set[i].line[j].tag = malloc(MAXLINE);
			cache.set[i].line[j].block = malloc(MAX_OBJECT_SIZE);
        	}
    	}
}

static void pop_usage(int *array, int key, int len)
{
    	int i, j;
    	for(i = 0; i < len; i++)
    	{
        	if(array[i] == key)break;
    	}
    	for(j = i; j > 0; j--)
    	{
        	array[j] = array[j - 1];
    	}
    	array[0] = key;
}

static int data_load(char *tag, char *response)
{
    	aint set_index = 0;
    	int i;
    	for(i = 0; i < line_num; i++)
    	{
        	if(cache.set[set_index].line[i].valid == 1 && (strcmp(cache.set[set_index].line[i].tag, tag) == 0))
        	{
                	pop_usage(cache.set[set_index].usage, i, line_num);
               	 	strcpy(response, cache.set[set_index].line[i].block);
//			printf("lru: %d\n", cache.set[set_index].usage[line_num - 1]);
//			printf("mru: %d\n", cache.set[set_index].usage[0]);
                	break;
        	}
    	}
   	if(i == line_num)
    	{
        	return 0;
    	}
    	else
    	{
        	return 1;
    	}
}

static void data_store(char *tag, char *response)
{
    	aint set_index = 0;
    	int lru;
    	lru = cache.set[set_index].usage[line_num - 1];
//	printf("lru: %d\n", lru);
    	strcpy(cache.set[set_index].line[lru].tag, tag);
    	strcpy(cache.set[set_index].line[lru].block, response);
    	if(cache.set[set_index].line[lru].valid == 0)
    	{
        	cache.set[set_index].line[lru].valid = 1;
    	}
    	pop_usage(cache.set[set_index].usage, lru, line_num);
//	printf("mru: %d\n", cache.set[set_index].usage[0]);
}

/* Thread routine */
void *thread(void *vargp)
{
	int connfd = *((int *)vargp);
	pthread_detach(pthread_self());
	free(vargp);
	doproxy(connfd);
	close(connfd);
	return NULL;
}

/* Another version of open_clientfd function which supports concurrent program */
int open_clientfd_safe(char *hostname, int port)
{
	int clientfd;
	struct hostent *hp;
	struct sockaddr_in serveraddr;

	if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return -1; /* Check errno for cause of error */

	/* Fill in the server’s IP address and port */
	P(&mutex);	
	if ((hp = gethostbyname(hostname)) == NULL)
		return -2; /* Check h_errno for cause of error */
	bzero((char *) &serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	bcopy((char *)hp->h_addr_list[0], (char *)&serveraddr.sin_addr.s_addr, hp->h_length);
	serveraddr.sin_port = htons(port);
	V(&mutex);
	/* Establish a connection with the server */
	if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0)
	return -1;
	return clientfd;
}

/* $end tinymain */

/*
 * doproxy - serve as a proxy to handle one HTTP request/response transaction
 */
/* $begin doit */
int doproxy(int connfd)
{
//	printf("doproxy in!\n");
    	int port, serverfd, len, response_len;
    	char bufrequest[MAX_OBJECT_SIZE], bufresponse[MAX_OBJECT_SIZE], hostname[MAXLINE], cacheresponse[MAX_OBJECT_SIZE], uri[MAXLINE];
    	rio_t rio_client, rio_server;

    	// reset buffer
    	memset(bufrequest, 0, sizeof(bufrequest));
    	memset(bufresponse, 0, sizeof(bufresponse));
    	memset(hostname, 0, sizeof(hostname));
    	memset(cacheresponse, 0, sizeof(cacheresponse)); 	
	memset(uri, 0, sizeof(uri));

    	/* Read request line and headers */
    	rio_readinitb(&rio_client, connfd);
    	// bufrequest is the buffer for outgoing request to real server
//    	printf("read_request in!\n");
    	
	if(read_request(&rio_client, bufrequest, hostname, &port, uri) < 0)
		return -1;
//	
//	printf("read_request out!\n");
    	printf("uri:%s\thostname:%s\tport:%d\n", uri, hostname, port);
    	printf("bufrequest: %s\n", bufrequest);

	if((data_load(uri, cacheresponse)) == 1)
	{
		printf("Cache hit!\n");
		if(rio_writen(connfd, cacheresponse, sizeof(cacheresponse)) < 0)
        	{
            		fprintf(stderr, "rio_writen send cache response error\n");
            		return -1;
        	}
        	// reset buffer
        	memset(cacheresponse, 0, sizeof(cacheresponse));
	}
	else
	{
		printf("Cache miss!\n");
		// send request to real server
		if((serverfd = open_clientfd_safe(hostname, port)) < 0)
    		{
        		fprintf(stderr, "open server fd error\n");
        		return -1;
    		}
		rio_readinitb(&rio_server, serverfd);

    		if(rio_writen(serverfd, bufrequest, strlen(bufrequest)) < 0)
    		{
        		printf("bufrequest error:\n%s\n", bufrequest);
			fprintf(stderr, "rio_writen send request error\n");
      			close(serverfd);	
			// send back error msg to client
			//clienterror(connfd, "request error", "404", "Not found", "Send request to server error");

			return -1;
    		}
	
		// get response from server and send back to the client
		memset(cacheresponse, 0, sizeof(cacheresponse));
		response_len = 0;
		while((len = rio_readnb(&rio_server, bufresponse, sizeof(bufresponse))) > 0)
    		{
//        		printf("hostname:%s\tport:%d\nbufresponse:%s\n", hostname, port, bufresponse);
			strcat(cacheresponse, bufresponse);
			response_len += len;

			// TODO: n error!
        		if(rio_writen(connfd, bufresponse, len) < 0)
        		{
        			printf("bufresponse error:%s\nlen:%d\n%s\n", strerror(errno), len, bufresponse); 
            			fprintf(stderr, "rio_writen send response error\n");
				//clienterror(connfd, "response error", "404", "Not found", "Send response to client error");
            			close(serverfd);
				return -1;
        		}
			// reset buffer
			memset(bufresponse, 0, sizeof(bufresponse));
    		}
		if(response_len <= MAX_OBJECT_SIZE)
			data_store(uri, cacheresponse);
		// close fd
		close(serverfd);
	}

    	return 0;
}

/* $end doit */

/*
 * read_request - read and parse HTTP request headers
 * return 0 if success, -1 otherwise.
 */
/* $begin read_request */
int read_request(rio_t *rp, char *bufrequest, char *hostname, int *port, char *uri)
{
	char buf[MAXLINE], method[MAXLINE];
	memset(buf, 0, sizeof(buf));
	memset(method, 0, sizeof(method));
	// request line
	if(rio_readlineb(rp, buf, MAXLINE) <= 0)
        	return -1;
	//printf("buf: %s\n", buf);
    	sscanf(buf, "%s %s", method, uri);
	
	// extract hostname and port info
	parse_uri(uri, hostname, port);
    	// fill in request for real server
    	sprintf(bufrequest, "%s %s HTTP/1.0\r\n", method, uri);
	printf("bufrequest: %s\n", bufrequest);
    	
	// request hdr
    	if(rio_readlineb(rp, buf, MAXLINE) < 0)
	{
		printf("rio_readlineb fail!\n");
        	return -1;
    	}
	while(strcmp(buf, "\r\n")) {
		//printf("buf: %s\n", buf);
		//printf("while loop\n");
		if(strcmp(buf, "\n") == 0)
		{
			printf("hahaha!\n");
		}
		if(strcmp(buf, "\r") == 0)
		{
			printf("lalala!\n");
		}
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
       	 	else if(!strstr(buf, "Cookie:"))
            		// append addtional header except above listed headers
        		strcat(bufrequest, buf);
		
		memset(buf, 0, sizeof(buf));
        	if(rio_readlineb(rp, buf, MAXLINE) < 0)
        	{
            		fprintf(stderr, "rio_readlineb read request error\n");
            		return -1;
        	}
    	}
    
    	//append header separater
	strcat(bufrequest, "\r\n");

	return 0;
}
/* $end read_requesthdrs */

/*
 * parse_uri - parse URI into hostname and port args
 * return 0 if success, -1 if fail
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
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
    	char buf[MAXLINE], body[MAXBUF];
	memset(buf, 0, sizeof(buf));
	memset(buf, 0, sizeof(body));
    	/* Build the HTTP response body */
	sprintf(body, "<html><title>Tiny Error</title>");
    	sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    	sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

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

