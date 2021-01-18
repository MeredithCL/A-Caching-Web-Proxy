/* A Proxy Server
 *
 * A Proxy Server-a simple HTTP proxy that acts as an 
 * intermediary between clients and servers and caches
 * web objects.
 * 
 * This program is capable of accepting incoming 
 * connections, reading and parsing requests. forwarding
 * requests to web servers, reading the servers' responses,
 * forwarding responses to the corresponding clients, 
 * dealing with multiple concurrent connections and dealing
 * with web contents.
*/

#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* define a cache block */
struct xxx
{
    int validity;              /* whether the block is used */
    int lru;                   /* check the cache block that is recently used */
    char requestline[MAXLINE]; /* check the request line */
    char buf[MAX_OBJECT_SIZE]; /* the wanted index that is cached */
};

/* the cache block for this use */
struct xxx cachex[10];
struct xxx temp;

/* semaphores used in cocurrency tasks */
int readcnt = 0;
int writecnt = 0;
sem_t waiting;
sem_t lock;
sem_t mutex1;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void doit(int fd);
void *thread(void *vargp);
void parse_uri(char *hostname, char *port, char *path, char *uri);
void head_result(char *headresult, char *hostname, rio_t *rio);
void sigpipehandler();

/* sigpipehandler-
 * handle the sigpipe signal by ignoring it 
*/
void sigpipehandler()
{
    return;
}

/* head_result-
 * get the proper header
*/
void head_result(char *headresult, char *hostname, rio_t *rio)
{
    char buf[MAXLINE];
    char left[MAXLINE];
    while (Rio_readlineb(rio, buf, MAXLINE) > 0)
    {
        /* if it is the end of the buffer */
        if (strcmp(buf, "\r\n") == 0)
        {
            break;
        }

        /* if it is the example headers, ignore them and add them later on
         * else, add them directly*/
        if (strncasecmp(buf, "User-Agent:", 11) &&
            strncasecmp(buf, "Connection:", 11) && strncasecmp(buf, "Proxy-Connection:", 17))
        {
            strcat(left, buf);
        }
    }

    /* add the example headers */
    strcat(headresult, left);
    strcat(headresult, user_agent_hdr);
    strcat(headresult, "Connection: close\r\n");
    strcat(headresult, "Proxy-Connection: close\r\n");
    strcat(headresult, "\r\n");
    return;
}

/* parse_uri-
 *parsing requests by analyzing the uri characters one by one
*/
void parse_uri(char *hostname, char *port, char *path, char *uri)
{
    /* set up limits */
    int length = strlen(uri);
    int mark1 = 0;

    /* find the format of http:// */
    /* mark1 resembles the start of the index that is the next of the signal // */
    for (mark1 = 0; mark1 < length - 1; mark1++)
    {
        if (uri[mark1] == '/' && uri[mark1 + 1] == '/')
        {
            break;
        }
    }
    if (mark1 == length)
    {
        mark1 = 0;
    }
    else
    {
        mark1 += 2;
    }
    
    int mark2 = mark1;
    /* mark2 resembles the start of the index that is the next of the signal / */
    for (mark2 = mark1; mark2 < length; mark2++)
    {
        if (uri[mark2] == '/')
        {
            break;
        }
    }
    if (mark2 == length)
    {
        path[0] = '/';
    }
    else
    {
        for (int i = mark2; i < length; i++)
        {
            path[i - mark2] = uri[i];
        }
    }
    int mark3 = mark1;

    /* find the port numbers */
    for (mark3 = mark1; mark3 < mark2; mark3++)
    {
        if (uri[mark3] == ':')
        {
            break;
        }
    }

    /* default case: 80 */
    if (mark3 == mark2)
    {
        port[0] = '8';
        port[1] = '0';
    }
    else
    {
        for (int i = mark3 + 1; i < mark2; i++)
        {
            port[i - mark3 - 1] = uri[i];
        }
    }

    /* copy the hostname */
    for (int i = mark1; i < mark3; i++)
    {
        hostname[i - mark1] = uri[i];
    }
    return;
}


/* doit-
 * accept and send information between the server and the client 
 * by checking whether the information is stored in the cache
 * if so, extract the data from the cache and refresh the state
 * if not, decide adding the information to the cache depending
 * on its size
 */
void doit(int fd)
{
    char buf[MAX_CACHE_SIZE], method[MAX_CACHE_SIZE], uri[MAX_CACHE_SIZE], version[MAX_CACHE_SIZE];
    char hostname[MAXLINE], port[MAXLINE];
    char path[MAXLINE];
    char headx[MAXLINE];

    /* initialize */
    memset(hostname, 0, MAXLINE);
    memset(port, 0, MAXLINE);
    memset(headx, 0, MAXLINE);
    memset(uri, 0, MAX_CACHE_SIZE);
    memset(version, 0, MAX_CACHE_SIZE);
    memset(buf, 0, MAX_CACHE_SIZE);
    memset(method, 0, MAX_CACHE_SIZE);
    memset(path, 0, MAXLINE);
    rio_t rio;
    
    /* read the information */
    Rio_readinitb(&rio, fd);
    int tempt = Rio_readlineb(&rio, buf, MAX_CACHE_SIZE);

    /* check whether the information is appropriate */
    if (tempt < 0)
    {
        return;
    }
    sscanf(buf, "%s %s %s", method, uri, version);
    
    /* check whether the uri is appropriate */
    if (strlen(uri) > MAXLINE)
    {
        return;
    }

    /* check whether the method is appropriate */
    if (strcmp(method, "GET"))
    {
        return;
    }

    /* check whether the data is stored in cache blocks previously */
    int flag = 0;
    for (int i = 0; i < 10; i++)
    {
        if (cachex[i].validity == 1)
        {
            if (strcmp(cachex[i].requestline, uri) == 0) /* find the cached result */
            {
                flag = 1;

                /* semaphores */
                P(&waiting);
                P(&mutex1);
                if (readcnt == 0)
                {
                    P(&lock);
                }
                readcnt ++;
                V(&mutex1);
                V(&waiting);

                /* write the data to destination */
                Rio_writen(fd, cachex[i].buf, sizeof(cachex[i].buf));

                P(&mutex1);
                readcnt --;
                if (readcnt == 0)
                {
                    V(&lock);
                }
                V(&mutex1);
                
                /* change the state of lru in cache blocks */
                P(&waiting);
                P(&lock);
                cachex[i].lru = 0;
                for (int j = 0; j < 10; j ++)
                {
                    if (i != j)
                    {
                        cachex[j].lru ++; 
                        /* increasing lru of other blocks */
                    }
                }
                
                V(&lock);
                V(&waiting);
                break;
            }
        }
    }
    
    /* not previously cached */
    if (flag == 0)
    {
        int flag1 = 0;
        int cacheblock = 0;
        
        /* find whether there is an eviction */
        for (cacheblock = 0; cacheblock < 10; cacheblock ++)
        {
            if (cachex[cacheblock].validity == 0)
            {
                flag1 = 1;
                break;
            }
        }

        /* cacheblock: the place where we shall write the new-cached */
        if (flag1 == 1)
        {

            /* semaphores */
            P(&waiting);
            P(&lock);
   
            /* change the state of the lru */
            for (int j = 0; j < 10; j ++)
            {
                if (j != cacheblock)
                {
                    cachex[j].lru ++;
                }
            }

            /* store it in temp in case the size of the data exceeds */
            temp.validity = cachex[cacheblock].validity;
            temp.lru = cachex[cacheblock].lru;
            strcpy(temp.requestline, cachex[cacheblock].requestline);
            for (int i = 0; i < MAX_OBJECT_SIZE; i ++)
            {
                temp.buf[i] = cachex[cacheblock].buf[i];
            }

            /* change the state of the target cache block */
            cachex[cacheblock].validity = 1;
            cachex[cacheblock].lru = 0; /* the most recently used */
            strcpy(cachex[cacheblock].requestline, uri);
            for (int i = 0; i < MAX_OBJECT_SIZE; i ++)
            {
                cachex[cacheblock].buf[i] = '\0';
            }

            V(&lock);
            V(&waiting);
        }
        else
        {
            /* semaphore */
            P(&waiting);
            P(&lock);

            int maxi = 0;
            int markk = 0;
            for (int i = 0; i < 10; i ++)
            {
                if (cachex[i].lru > maxi)
                {
                    maxi = cachex[i].lru;
                    markk = i;
                }
            } /* find the least recently used */
            
            for (int i = 0; i < 10; i ++)
            {
                if (i != markk)
                {
                    cachex[i].lru ++;
                }
            }
            
            /* store it in temp */
            temp.validity = cachex[markk].validity;
            temp.lru = cachex[markk].lru;
            strcpy(temp.requestline, cachex[markk].requestline);
            for (int i = 0; i < MAX_OBJECT_SIZE; i ++)
            {
                temp.buf[i] = cachex[markk].buf[i];
            }

            cachex[markk].validity = 1;
            cachex[markk].lru = 0;
            strcpy(cachex[markk].requestline, uri);
            cacheblock = markk;
            
            V(&lock);
            V(&waiting);
        }

        /* deal with the requests */
        parse_uri(hostname, port, path, uri);

        /* generate header */
        head_result(headx, hostname, &rio);
        int clientfd = Open_clientfd(hostname, port);

        /* generate header */
        rio_t riox;
        int cnt;
        char httpstring[MAXLINE] = {'\0'};
        strcat(httpstring, method);
        strcat(httpstring, " ");
        strcat(httpstring, path);
        strcat(httpstring, " ");
        strcat(httpstring, "HTTP/1.0\r\n");

        Rio_readinitb(&riox, clientfd);
        
        Rio_writen(clientfd, httpstring, strlen(httpstring));
        Rio_writen(clientfd, headx, strlen(headx));

        long count = 0;
        long markx = 0;
        int flagk = 0;

        while ((cnt = Rio_readlineb(&riox, buf, MAXLINE)) > 0)
        {

            Rio_writen(fd, buf, cnt);

            /* storing information in cache block that is found previously */
            if (!flagk)
            {

                /* semaphore */
                P(&waiting);
                P(&lock);

                /* refresh the information */
                for (int i = 0; i < cnt; i ++)
                {
                    cachex[cacheblock].buf[markx] = buf[i];
                    markx ++;
                }

                V(&lock);
                V(&waiting);
                
                /* check whether the size of data exceeds */
                count += cnt;
                if (count >= MAX_OBJECT_SIZE)
                {
                    flagk = 1;
                }
                if (count >= MAX_CACHE_SIZE)
                {
                    return;
                }
            }
        }

        /* if the data's size exceedes, return the cache to the original state */
        if (flagk == 1) 
        {
            /* semaphores */
            P(&waiting);
            P(&lock);

            cachex[cacheblock].validity = temp.validity;
            cachex[cacheblock].lru = temp.lru;
            strcpy(cachex[cacheblock].requestline, temp.requestline);
            for (int i = 0; i < MAX_OBJECT_SIZE; i ++)
            {
                cachex[cacheblock].buf[i] = temp.buf[i];
            }
            for (int i = 0; i < 10; i ++)
            {
                if (i != cacheblock)
                {
                    cachex[i].lru --;
                }
            }

            V(&lock);
            V(&waiting);
        }
        Close(clientfd);
    }
    return;
}

/* thread function-
 * detach a thread and deal with the request
 */
void *thread(void *vargp)
{
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self()); /* detach the function */
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

/* main function-
 * initialize the handler, semaphores,
 * listenfd and the cache blocks,
 * then create threads to perform 
 * concurrency
 */
int main(int argc, char **argv)
{
    Signal(SIGPIPE, sigpipehandler);
    Sem_init(&waiting, 0, 1);
    Sem_init(&lock, 0, 1);
    Sem_init(&mutex1, 0, 1);
    int listenfd;
    int *connfdp;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* initialize the blocks */
    for (int i = 0; i < 10; i ++)
    {
        cachex[i].validity = 0;
        cachex[i].lru = 0;
        for (int j = 0; j < MAXLINE; j ++)
        {
            cachex[i].requestline[j] = '\0';
        }
        for (int j = 0; j < MAX_OBJECT_SIZE; j ++)
        {
            cachex[i].buf[j] = '\0';
        }
    }

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return 0;
    }

    listenfd = Open_listenfd(argv[1]);

    /* multiple requests */
    while (1)
    {
        memset(hostname, 0, MAXLINE);
        memset(port, 0, MAXLINE);
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE,
                    port, MAXLINE, 0);
        Pthread_create(&tid, NULL, thread, connfdp);
    }
    
    return 0;
}
