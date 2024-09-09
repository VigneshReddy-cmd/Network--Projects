#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#define MAX_CLIENT 10
#define MAX_BYTES 4096
#define MAX_ELEMENT_SIZE 10 * (1 << 10)
#define MAX_SIZE 200 * (1 << 20)

typedef struct cache_element cache_element;
struct cache_element
{
    char *data;
    int len;
    char *url;
    time_t lru_time_track;
    cache_element *next;
};

cache_element *find(char *url);
int add_cache_element(char *data, int size, char *url);
void remove_cache_elment();

int portnumber = 8080;
int proxy_socketId;
int client_socketId;
pthread_t tid[MAX_CLIENT];
sem_t semaphore;
pthread_mutex_t lock;

cache_element *head;
int cache_size;
int server_port;

int checkHTTPversion(char *msg)
{
    int version = -1;
    if (strncmp(msg, "HTTP/1.1", 8) == 0)
    {
        version = 1;
    }
    else if (strncmp(msg, "HTTP/1.0", 8) == 0)
    {
        version = 1; // Handling this similar to version 1.1
    }
    else
    {
        version = -1;
    }
    return version;
}
int sendERRORMESSAGE(int socket, int status_code)
{
    char str[1024];
    char currentTime[50];
    time_t now = time(0);

    struct tm data = *gmtime(&now);
    strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %Z", &data);

    switch (status_code)
    {
    case 400:
        snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
        printf("400 Bad Request\n");
        send(socket, str, strlen(str), 0);
        break;

    case 403:
        snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
        printf("403 Forbidden\n");
        send(socket, str, strlen(str), 0);
        break;

    case 404:
        snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
        printf("404 Not Found\n");
        send(socket, str, strlen(str), 0);
        break;

    case 500:
        snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
        // printf("500 Internal Server Error\n");
        send(socket, str, strlen(str), 0);
        break;

    case 501:
        snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
        printf("501 Not Implemented\n");
        send(socket, str, strlen(str), 0);
        break;

    case 505:
        snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
        printf("505 HTTP Version Not Supported\n");
        send(socket, str, strlen(str), 0);
        break;

    default:
        return -1;
    }
    return 1;
}
int connectToRemoteSocket(char *host_addr, int port_number)
{
    // create new socket between the proxy and the server
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (remoteSocket < 0)
    {
        printf("Server connection error\n");
        return -1;
    }

    // Get host by the name or IP address provided
    struct hostent *host = gethostbyname(host_addr);  // Correct struct type

    // Check if the host was successfully resolved
    if (host == NULL)
    {
        fprintf(stderr, "No such host exists\n");
        return -1;
    }

    struct sockaddr_in server_addr;
    
    // Set family, port, and address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);

    // Copy the server's address to the server_addr structure
    bcopy((char *)host->h_addr, (char *)&server_addr.sin_addr.s_addr, host->h_length);

    // Connect to the remote server
    if (connect(remoteSocket, (struct sockaddr *)&server_addr, (socklen_t)sizeof(server_addr)) < 0)
    {
        fprintf(stderr, "Error in connecting\n");
        return -1;
    }

    return remoteSocket;
}

int handle_request(int socket, ParsedRequest *request, char *tempRequest)
{
    // handle the request is done here
    // get the http url
    char *buf = (char *)malloc(sizeof(char) * MAX_BYTES);
    strcpy(buf, "GET ");
    strcat(buf, request->path);
    strcat(buf, " ");
    strcat(buf, request->version);
    strcat(buf, "\r\n");
    size_t len = strlen(buf);

    // check if the host id connected
   	if (ParsedHeader_set(request, "Connection", "close") < 0){
		printf("set header key not work\n");
	}

    // parsedrequest of host  is null
    if (ParsedHeader_get(request, "HOST") == NULL)
    {
        if (ParsedHeader_set(request, "HOST", request->host) < 0)
        {
            printf("Error is set of the header");
        }
    }

    // unparsing the url
    if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES) < 0)
    {
        printf("UNPARSE FAILED\n");
    }

    // if all are satisfed the connect to the postm 80
    if (request->port != NULL)
    {
        server_port = atoi(request->port);
    }
    // remotesocket is the socket between the proxy and the http server
    int remoteSocketId = connectToRemoteSocket(request->host, server_port);
    if (remoteSocketId < 0)
    {
        return -1;
    }

    // sending the request to the serever
    // acceptig the response from the server
    // sending the response from server to the client
    // storing the data after completion in cache
    // initial request=buf

    // request sent to server
    int bytes_send_to_remoteSocket = send(remoteSocketId, buf, sizeof(buf), 0);
    bzero(buf, MAX_BYTES); // free buf

    int bytes_recive_from_remotesocket = recv(remoteSocketId, buf, MAX_BYTES - 1, 0); // MAX_BYTES_! for the delimeter after every response

    // use temp_buffer to stote the data of one session in cache while replying
    char *temp_buffer = (char *)malloc(sizeof(char) * MAX_BYTES);
    bzero(temp_buffer, MAX_BYTES);
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;

    while (bytes_recive_from_remotesocket > 0)
    {
        int bytes_send_to_client = send(client_socketId, buf, bytes_recive_from_remotesocket, 0);

        // for every exchange copy data to temp_buffer
        for (int i = 0; i < MAX_BYTES; i++)
        {
            temp_buffer[temp_buffer_index] = buf[i];
            temp_buffer_index++;
        }
        if (bytes_send_to_client < 0)
        {
            perror("Error in sending data");
            break;
        }
        bzero(buf, MAX_BYTES);
        bytes_recive_from_remotesocket = recv(remoteSocketId, buf, MAX_BYTES, 0);
        if (bytes_recive_from_remotesocket > 0)
        {
            temp_buffer_size += MAX_BYTES;
            // update the size of the temp_buffer_size by double i order to stotre the next request
            temp_buffer = (char *)realloc(temp_buffer, temp_buffer_size);
        }
    }
    temp_buffer[temp_buffer_index] = '\0';
    free(buf);
    add_cache_element(temp_buffer, temp_buffer_size, tempRequest);
    free(temp_buffer);
    close(remoteSocketId);
    return 0;
}

void *thread_fn(void *socketNew)
{
    // sem wait for the criircal sev=ction problem
    sem_wait(&semaphore);
    int p;
    sem_getvalue(&semaphore,&p);
    // getting current value of the semaphore
    printf(" BEFORE Semaphore value is:%d", p);
    int *t = (int *)socketNew; // assigning socket int reference to t
    int socket = *t;           // dereferencing the t from the socket
    int bytes_send_client, len;
    char *buffer = (char *)calloc(MAX_BYTES, sizeof(char)); // placing a new buffer to avoid over flow of MAX_BYTES size
    bzero(buffer, MAX_BYTES);                               // removing the garbage values from the buffer
    bytes_send_client = recv(socket, buffer, MAX_BYTES, 0); // reciving the new socket of buffer ,MAX_BYTES
                                                            // and default protocol "0" and fillingin buffer

    // loop runs till the sucessful request and the recived url if recives url then break
    while (bytes_send_client > 0)
    {
        // if the recivded message if positive ie request from client to http
        len = strlen(buffer);

        // parsing for the url
        if (strstr(buffer, "\r\n\r\n") == NULL)
        {
            // again recive the request for the url
            bytes_send_client = recv(socket, buffer, MAX_BYTES, 0);
        }
        else
        {
            break;
        }
    }

    // storing the request in the temporary form of url
    char *temprequest = (char *)malloc(strlen(buffer) * sizeof(char) + 1);
    for (int i = 0; i < strlen(buffer); i++)
    {
        temprequest[i] = buffer[i];
    }

    cache_element *temp = find(temprequest);
    if (temp != NULL)
    {
        // we found the element in the cache
        // hit case
        int size = temp->len / sizeof(char);
        int pos = 0;
        // loading the response
        char response[MAX_BYTES];
        while (pos < 0)
        {
            // remove garbage from response
            bzero(response, MAX_BYTES);
            // storing the retribved data from cache in to response
            for (int i = 0; i < MAX_BYTES; i++)
            {
                response[i] = temp->data[i];
                pos++;
            }
            // sending the ewsponse for every buffer full size
            send(socket, response, MAX_BYTES, 0);
        }
        printf("Data retrived from Cache\n");
        printf("Response:%s\n\n", response);
    }
    else if (bytes_send_client > 0)
    {
        // if not in cache then miss
        len = strlen(buffer);
        // retriving the parsedRequest from the headerfile custom
        ParsedRequest *request = ParsedRequest_create();

        if (ParsedRequest_parse(request, buffer, len) < 0)
        {
            printf("Parsing Failed\n");
        }
        else
        {
            // on sucessful parsing
            bzero(buffer, MAX_BYTES);
            if (!strcmp(request->method, "GET"))
            {
                // for GET method triggred
                if (request->host && request->path && checkHTTPversion(request->version) == 1)
                {
                    // custom method handle request
                    bytes_send_client = handle_request(socket, request, temprequest);
                    if (bytes_send_client == -1)
                    {
                        // if bad request error 500
                        sendERRORMESSAGE(socket, 500);
                    }
                }
                else
                {
                    // if the error in paylload
                    sendERRORMESSAGE(socket, 500);
                }
            }
            else
            {
                printf("THIS PROXY DOESN'T SUPPORT OTHER THAN GET\n");
            }
        }
        // destroy the request
        ParsedRequest_destroy(request);
    }
    else if (bytes_send_client == 0)
    {
        // if the request is null
        printf("CLient is Disconnected\n");
    }
    // complete destroy the docet after processing
    shutdown(socket, ESHUTDOWN);
    close(socket);
    free(buffer);
    sem_post(&semaphore); // changing the semaphore value
    sem_getvalue(&semaphore,&p);
    // updated semaphore value
    printf("Updated Semaphore VAlue is:%d", p);
    free(temprequest);
    return NULL;
}

int main(int argc, char *argv[])
{
    int socked_id, client_len;
    server_port = 80;
    struct sockaddr_in server_addr, client_addr;
    sem_init(&semaphore, 0, MAX_CLIENT);
    pthread_mutex_init(&lock, NULL);
    if (argc == 2)
    {
        // ./ proxy 9990
        portnumber = atoi(argv[1]);
    }
    else
    {
        printf("Too few Arguments");
        exit(1);
    }

    printf("starting  proxy server @port %d\n", portnumber);

    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    /*
         int sockfd = socket(domain, type, protocol)
         domain=AF_INET and AF_I NET 6
        type= SOCK_STREAM: TCP(reliable,onnection-oriented)
              SOCK_DGRAM: UDP(unreliable, connectionless)
        protocol: Protocol value for Internet Protocol(IP), which is 0. This is the same number that appears on the protocol field in the IP header of a packet.(man protocols for more details)
    */
    if (proxy_socketId < 0)
    {
        perror("No socket is free");
        exit(1);
    }
    int reuse = 1;
    if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)))
    {
        perror("setSocketOpt Failed");
    }

    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(portnumber);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(proxy_socketId, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("port is not avaliable");
    }
    printf("Binding Port no:%d\n", portnumber);

    // listen to request
    int listen_status = listen(proxy_socketId, MAX_CLIENT);

    if (listen_status < 0)
    {
        perror("Error in listen");
        exit(1);
    }

    // iterator for thr request
    int i = 0;
    int connect_socketId[MAX_CLIENT];
    while (1)
    {
        bzero((char *)&client_addr, sizeof(client_addr));
        client_len = sizeof(client_addr);
        client_socketId = accept(proxy_socketId, (struct sockaddr *)&client_addr, (socklen_t *)&client_len);
        if (client_socketId < 0)
        {
            printf("Not able to connect\n");
            exit(1);
        }
        else
        {
            connect_socketId[i] = client_socketId;
        }
        // reference pointer
        struct sockaddr_in *client_ptr = (struct sockaddr_in *)&client_addr;
        struct in_addr ip_addr = client_ptr->sin_addr;
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);
        printf("Clien t is connected%d with ipaddress:%s\n", ntohs(client_addr.sin_port), str);

        // for new connection start createion of new thread till max _CLients
        pthread_create(&tid[i], NULL, thread_fn, (void *)&connect_socketId[i]);
        i++;
    }
    close(proxy_socketId);
    return 0;
}

cache_element *find(char *url)
{
    cache_element *site = NULL;
    bool flag = false;
    // aquire lock on the cache
    int temp_val_lock = pthread_mutex_lock(&lock);
    printf("lock Aquired on the find_CacheElement: %d\n", temp_val_lock);

    if (head != NULL)
    {
        site = head;
        // linear search
        while (site != NULL)
        {
            if (!strcmp(url, site->url))
            {
                flag = true;
                printf("URL FOUND OF TIMETRACK: %ld\n", site->lru_time_track);
                site->lru_time_track = time(NULL);
                printf("URL timetrack after changing timetrack: %ld\n", site->lru_time_track);
                break;
            }
            site = site->next;
        }
    }
    else
    {
        printf("URL NOT FOUND");
    }
    temp_val_lock = pthread_mutex_unlock(&lock);
    printf("FIND CACHE LOCK AFTER UNLOCK :%d\n", temp_val_lock);
    if (flag)
    {
        return site;
    }
    else
    {
        return NULL;
    }
}

void remove_cache_element()
{
    // aquire lock
     cache_element *p;
        cache_element *q;
        cache_element *temp;
    int temp_val_lock = pthread_mutex_lock(&lock);
     
    printf("Lock aquired on removal of element from cacle :%d\n", temp_val_lock);
    if (head != NULL)
    {
      
        for (p = head, q = head, temp = head; q->next != NULL; q = q->next)
        {
            // if smallest time stamp found then remove it from the vcache
            // else continue
            if ((q->next)->lru_time_track < (temp->lru_time_track))
            {
                temp = q->next;
                p = q;
            }
        }
        // remove element with the smallest time stamp
        if (temp == head)
        {
            head = head->next;
        }
        else
        {
            p->next = temp->next;
        }
        // remove all the element size which we included in the add method lenrefrs to the size of data
        cache_size=cache_size-(temp->len)-sizeof(cache_element)-sizeof(temp->url)-1;
       
    }
    // remove llock
     free(temp->data);
        free(temp->url);
        free(temp);
    temp_val_lock=pthread_mutex_unlock(&lock);
    printf("Remove element lock removed :%d\n",temp_val_lock);
}

int add_cache_element(char *data, int size, char *url)
{
    // Aquire lock on cache
    int temp_val_lock = pthread_mutex_lock(&lock);
    printf("Lock Aquired on adding element in Cache :%d\n", temp_val_lock);

    // total element size =size of individual components + cache size
    int element_size = size + 1 + strlen(url) + sizeof(cache_element);

    if (element_size > MAX_ELEMENT_SIZE)
    {
        temp_val_lock = pthread_mutex_unlock(&lock);
        printf("Lock removed on adding element to CACHE :%d\n", temp_val_lock);
        return 0;
    }
    else
    {
        // removing the element till reach the MAX_SIZE-1 to add new element
        while(cache_size+element_size>MAX_SIZE)
        {
            remove_cache_element();
        }
        cache_element *element = (cache_element *)malloc(sizeof(cache_element));
        // adding the elements to the data by creating dynamically
        element->data = (char *)malloc(size + 1);
        strcpy(element->data, data);
        element->url = (char *)malloc(1 + (strlen(url) * sizeof(char)));
        strcpy(element->url, url);
        // highest time stamp recent one
        element->lru_time_track = time(NULL);
        element->next = head;
        element->len = size;
        head = element;
        cache_size += element_size;
        temp_val_lock = pthread_mutex_unlock(&lock);
        printf("LOCK removed on the adding to CACHE :%d\n", temp_val_lock);
        return 1;
    }
    printf("ELEMENT SIZE IS greater that prescribed\n");
    return 0;
}

