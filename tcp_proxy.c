#include <stdio.h>
#include <stdlib.h>
#include <string.h>      /* for fgets */
#include <strings.h>     /* for bzero, bcopy */
#include <unistd.h>      /* for read, write */
#include <sys/socket.h>  /* for socket use */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>
#include <dirent.h>
#include <time.h>
#include <openssl/sha.h>

#define MAXLINE  8192  /* max text line length */
#define MAXBUF   8192  /* max I/O buffer size */
#define LISTENQ  1024  /* second argument to listen() */
#define MAX 80
#define PORT 8080
#define SA struct sockaddr

int open_listenfd(int port);
void echo(int connfd);
void *thread(void *vargp);
int create_server_conn();


struct HttpRequest {
    char request_method[7];
    char http_version[10];
    char resource[100];
    char host[100];
    char connection_state[50];
};
struct HttpResponse {
    char http_version[10];
    char status_code[4];
    char content_type[20];
    char content_length[10];
    char *content;
    long length;
};
struct HostDetails {
    int addressFound;
    int isLocal;
    char ipHostName[1000];
    int port;
};
struct thread_data {
    char host[100];
    int file_index;
};

char cache_list[100][100];
int cache_content_expiry[100];
int curr_cache_len = 0;
int curr_host_names_len = 0;
char host_names_cache_list[100][1000];
char host_addresses_cache_list[100][1000];

int main(int argc, char **argv)
{
    int listenfd, *connfdp, port, clientlen=sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    pthread_t tid;
    
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    port = atoi(argv[1]);

    listenfd = open_listenfd(port);
    while (1) {
        connfdp = malloc(4 * sizeof(int));
        *connfdp = accept(listenfd, (struct sockaddr*)&clientaddr, &clientlen);
        if (*connfdp < 1) {
            continue;
        }
        printf("----------------connection made---------------- \n");
        pthread_create(&tid, NULL, thread, connfdp);
    }
}

/* thread routine */
void * thread(void * vargp)
{
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self()); 
    free(vargp);
    echo(connfd);
    printf("-----------closing connection----------------- \n");
    close(connfd);
    return NULL;
}

struct HttpRequest getHttpAttributes(char *buf) {
    int i = 0, j = 0;
    char httpMethod[7], httpVersion[10], resource[100], host[100], connection_state[50];
    // memset(connection_state, 0, 50);
    // memset(httpMethod, 0, 7);
    // memset(httpVersion, 0, 10);
    // memset(resource, 0, 100);
    // memset(host, 0, 100);
    char space = ' ';
    struct HttpRequest request;
    while (!(buf[i] == ' ')) {
        httpMethod[j] = buf[i];
        i++;j++;
    }
    httpMethod[j] = '\0';
    j = 0; i += 1;
    while(!(buf[i] == ' ')) {
        resource[j] = buf[i];
        i++;j++;
    }
    resource[j] = '\0';
    j = 0; i += 1;
    while(j < 8) {
        httpVersion[j] = buf[i];
        i++;j++;
    }
    httpVersion[j] = '\0';
    j = 0; i += 2;
    if (buf[i] == 'H') {
        // "Host: "
        i += 6;
        while(!(buf[i] == '\r')) {
            host[j] = buf[i];
            j += 1; i += 1;
        }
        host[j] = '\0';
        strcpy(request.host, host);
    }
    j = 0;
    int len = strlen(buf);
    while (i < len && (buf[i] != 'C' || buf[i+1] != 'o' || buf[i+2] != 'n')) {
        i++;
    }
    if (i < len) {
        // "Connection: "
        i += 12;
        while(!(buf[i] == '\r')) {
            connection_state[j] = buf[i];
            j += 1; i += 1;
        }
        connection_state[j] = '\0';
    }
    strcpy(request.connection_state, connection_state);
    strcpy(request.http_version, httpVersion);
    strcpy(request.request_method, httpMethod);
    strcpy(request.resource, resource);
    return request;
}

struct HttpResponse getResponseContents(struct HttpRequest request) {
    char *base_path = "./";
    char *path;
    path = malloc(1000 * sizeof(char));
    FILE *fp;
    int file_not_found = 0;
    struct HttpResponse response;
    bzero(response.http_version, 8);
    strcpy(response.http_version, request.http_version);
    if (strcmp(request.resource, "/") == 0 || strcmp(request.resource, "/index.html") == 0 ) {
        memset(path, 0, 1000);
        strcat(path, base_path);
        strcat(path, "/index.html");
        fp = fopen(path, "r");
        if (fp == NULL) {
            file_not_found = 1;
        }
    } else {
        memset(path, 0, 1000);
        strcat(path, base_path);
        strcat(path, request.resource);
        printf("requested file %s \n", path);
        fp = fopen(path, "rb");
        if (fp == NULL) {
            file_not_found = 1;
        }
    }
    if (file_not_found) {
        printf("%s, file not found \n", request.resource);
        strcpy(response.status_code, "404");
        response.status_code[4] = '\0';
        char *contents = "<html><head><title>404 File Not Found</title></head><body><h2>404 File Not Found</h2></body></html>";
        response.content = malloc(strlen(contents) * sizeof(char));
        strcpy(response.content, contents);
        strcpy(response.content_type, "text/html");
        sprintf(response.content_length, "%ld", strlen(contents));
        response.length = strlen(contents);
    } else {
        char *file_type = malloc(10 * sizeof(char));
        char *resource = malloc(1000);
        memset(resource, 0, 1000);
        strcpy(resource, request.resource);
        int i = strlen(resource) - 1;
        if (strlen(resource) == 1 && resource[0] == '/') {
            file_type = "html";
        } else {
            while(resource[i] != '.') {
                i--;
            }
            if (i > 0) {
                i += 1; int j = 0;
                while(resource[i] != '\0') {
                    file_type[j] = resource[i];
                    i++;j++;
                }
                file_type[j] = '\0';
            }
        }
        printf("requested file type %s \n", file_type);
        memset(response.content_type, 0, 20);
        if (strcmp(file_type, "html") == 0) {
            char *content_type = "text/html";
            strcpy(response.content_type, content_type);
        } else if (strcmp(file_type, "pdf") == 0) {
            char *content_type = "text/pdf";
            strcpy(response.content_type, content_type);
        } else if (strcmp(file_type, "txt") == 0) {
            char *content_type = "text/plain";
            strcpy(response.content_type, content_type);
        } else if (strcmp(file_type, "gif") == 0) {
            char *content_type = "image/gif";
            strcpy(response.content_type, content_type);
        } else if (strcmp(file_type, "jpg") == 0) {
            char *content_type = "image/jpg";
            strcpy(response.content_type, content_type);
        } else if (strcmp(file_type, "png") == 0) {
            char *content_type = "image/png";
            strcpy(response.content_type, content_type);
        } else if (strcmp(file_type, "css") == 0) {
            char *content_type = "text/css";
            strcpy(response.content_type, content_type);
        } else if (strcmp(file_type, "js") == 0) {
            char *content_type = "text/javascript";
            strcpy(response.content_type, content_type);
        } else if (strcmp(file_type, "ico") == 0) {
            strcpy(response.content_type, "image/x-icon");
        } 
        else {
            strcpy(response.content_type, "");
        }
        fseek(fp, 0, SEEK_END);
        long fsize = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        char *file = malloc(fsize + 1);
        if (file != NULL) {
            fread(file, fsize, 1, fp);
            fclose(fp);
            file[fsize] = 0;
            response.length = fsize;
            response.content = malloc(fsize + 1);
            char *status = "200";
            // memset(response.status_code, 0, 4);
            strcpy(response.status_code, status);
            memcpy(response.content, file, fsize);
            // memset(response.content_length, 0, 10);
            sprintf(response.content_length, "%ld", fsize);
        } else {
            strcpy(response.status_code, "404");
            char *contents = "<html><head><title>404 File Not Found</title></head><body><h2>404 File Not Found</h2></body></html>";
            response.content = malloc(strlen(contents) * sizeof(char));
            strcpy(response.content, contents);
            strcpy(response.content_type, "text/html");
            sprintf(response.content_length, "%ld", strlen(contents));
            response.length = strlen(contents);
        }
    }
    return response;
}

char* getmd5sum(char *file, unsigned char hash[SHA_DIGEST_LENGTH]) {
    // unsigned char c[MD5_DIGEST_LENGTH];
    // char *filename="file.c";
    // int i;
    // FILE *inFile = fopen (filename, "rb");
    // MD5_CTX mdContext;
    // int bytes;
    // unsigned char data[1024];
    // char *filemd5 = (char*) malloc(33 *sizeof(char));

    // if (inFile == NULL) {
    //     printf ("%s can't be opened.\n", filename);
    //     return 0;
    // }

    // MD5_Init (&mdContext);
    // while ((bytes = fread (data, 1, 1024, inFile)) != 0)
    //     MD5_Update (&mdContext, data, bytes);
    // MD5_Final(c,&mdContext);
    // for(i = 0; i < MD5_DIGEST_LENGTH; i++) {
	// 	sprintf(&filemd5[i*2], "%02x", (unsigned int)c[i]);
	// }
    // fclose (inFile);
    size_t length = strlen(file);
    SHA1(file, length, hash);
    printf("%s", hash);
    return hash;
    // return "12345xasd";
}

// checks if the resource is present in cache and also if the cached item has expired
// returns 0 or 1 based on item presence and its expiry.
int check_cache(char* hash) {
    int i = 0;
    for (i = 0; i < 100; i++) {
        if (strcmp(hash, cache_list[i]) == 0) {
            int expiry_time = cache_content_expiry[i];
            int current_time = (int)time(NULL);
            if (current_time > expiry_time) {
                printf("found in cache but content has expired \n");
                return 0;
            }
            printf("resource found in cache. \n");
            return 1;
        }
    }
    printf("resource not found in cache \n");
    return 0;
}

int exists(const char *fname)
{
    FILE *file;
    if ((file = fopen(fname, "r")))
    {
        fclose(file);
        return 1;
    }
    return 0;
}

void *prefetching_parser(void * vargp) {
    char file_links[100][200];
    char buf[2000];
    int link_index = 0;
    struct HttpRequest request;
    struct thread_data *args = (struct thread_data *)vargp;
    int file_index = args->file_index;
    strcpy(request.host, args->host);

    pthread_detach(pthread_self());
    free(vargp);
    char *filename = cache_list[file_index];
    printf("started - parsing the filename: %s for link content \n", filename);
    FILE *fp;
    fp = fopen(filename, "rb");
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *file = malloc(fsize + 1);
    if (file != NULL) {
        fread(file, fsize, 1, fp);
        fclose(fp);
        file[fsize] = 0;
    }
    int i = 0;
    while(i < fsize) {
        if (file[i] == 'h' && 
            file[i + 1] == 'r' && 
            file[i + 2] == 'e' && 
            file[i + 3] == 'f' &&
            file[i + 4] == '=' &&
            i + 6 < fsize
        ) {
            i = i + 6;
            char link[200];
            int j = 0;
            while (file[i] != '"') {
                link[j] = file[i];
                i++;j++;
            }
            link[j] = '\0';
            strcpy(file_links[link_index], link);
            bzero(link, 200);
            link_index++;
        }
        i++;
    }
    i = 0;
    int server_conn = create_server_conn(request, 0);
    char *request_str = malloc(20000);
    char *response_str = malloc(200000);
    int n = 1;
    FILE *new_file;
    char *resource_hash = malloc(1000);
    unsigned char filehash[SHA_DIGEST_LENGTH];
    while (i <= link_index) {
        bzero(filehash, SHA_DIGEST_LENGTH);
        bzero(resource_hash, 1000);
        n = 1;
        printf("Found a link: %s \n", file_links[i]);
        strcpy(request_str, "GET ");
        strcat(request_str, file_links[i]);
        // strcat(request_str, "/services/");
        strcat(request_str, " ");
        strcat(request_str, "HTTP/1.1\r\n");
        strcat(request_str, "Host: ");
        strcat(request_str, request.host);
        strcat(request_str, "\r\n");
        if (i == link_index) {
            strcat(request_str, "Connection: close\r\n\r\n");
        } else {
            strcat(request_str, "Connection: Keep-alive\r\n\r\n");
        }
        printf("%s \n", request_str);
        write(server_conn, request_str, strlen(request_str));
        size_t length = strlen(file_links[i]);
        SHA1(file_links[i], length, filehash);
        for(i = 0; i < SHA_DIGEST_LENGTH; i++) {
            sprintf(&resource_hash[i*2], "%02x", (unsigned int)filehash[i]);
        }
        if (exists(resource_hash) == 0) {
            new_file = fopen(resource_hash, "wb+");
            while(n > 0) {
                n = read(server_conn, buf, 2000);
                if (new_file > 0) {
                    if (n < 0) {
                        printf("server finished writing on connection \n");
                        break;
                    } else {
                        fwrite(buf, 1, 2000, new_file);
                    }
                } else {
                    printf("failed to create a file \n");
                }
                bzero(buf, sizeof(buf));
            }
            fclose(new_file);
        } else {
            printf("link content already exists in cache \n");
        }
        bzero(request_str, sizeof(request_str));
        bzero(new_filename, sizeof(new_filename));
        i++;
    }
    close(server_conn);
    printf("finished parsing \n");
}

void echo(int connfd) {
    int n;
    char buf[MAXLINE];
    int keepAlive = 0, is_first = 1;
    struct HttpRequest request;
    struct HttpResponse response;
    pthread_mutex_t lock_m;
    pthread_t parser_thread;
    char *resource_hash = malloc(1000);
    unsigned char filehash[SHA_DIGEST_LENGTH];
    char *response_str;
    response_str = malloc(200000 * sizeof(char));
    n = read(connfd, buf, MAXLINE);
    if (n > 0 && strlen(buf) > 1) {
        printf("%s \n", buf);
        while((keepAlive == 1 || is_first == 1) && strlen(buf) > 1) {
            printf("Request made on socket: %d \n", connfd);
            request = getHttpAttributes(buf);
            if (strcmp(request.connection_state, "Keep-alive") == 0 || strcmp(request.connection_state, "keep-alive") == 0) {
                keepAlive = 1;
            } else {
                keepAlive = 0;
            }
            if (strcmp(request.request_method, "GET") == 0 || strcmp(request.request_method, "Get") == 0) {
                // thread locked to send req to server and read response
                pthread_mutex_lock(&lock_m);
                // check if the resource exists in cache.
                // getmd5sum(request.resource, hash);
                size_t length = strlen(request.resource);
                SHA1(request.resource, length, filehash);
                int i = 0;
                for(i = 0; i < SHA_DIGEST_LENGTH; i++) {
                	sprintf(&resource_hash[i*2], "%02x", (unsigned int)filehash[i]);
                }
                int in_cache = check_cache(resource_hash);
                if (in_cache) {
                    strcpy(request.resource, resource_hash);
                    response = getResponseContents(request);
                    write(connfd, response.content, response.length);
                } else {
                    // if not then make a request to the server.
                    printf("did not find resource in cache, fetching from server \n");
                    int server_conn = create_server_conn(request, connfd);
                    if (server_conn > 0) {
                        write(server_conn, buf, sizeof(buf));
                        bzero(response_str, sizeof(response_str));
                        n = 1;
                        FILE *new_file;
                        new_file = fopen(resource_hash, "wb+");
                        while (n > 0) {
                            n = read(server_conn, response_str, sizeof(response_str));
                            if (new_file != NULL) {
                                if (n < 0) {
                                    printf("server finished writing on connection \n");
                                    close(server_conn);
                                    break;
                                } else {
                                    fwrite(response_str, 1, sizeof(response_str), new_file);
                                    write(connfd, response_str, sizeof(response_str));
                                }
                            }
                            bzero(response_str, sizeof(response_str));
                        }
                        strcpy(cache_list[curr_cache_len], resource_hash);
                        cache_content_expiry[curr_cache_len] = (int)time(NULL) + 60;
                        struct thread_data *data= malloc (sizeof (struct thread_data));
                        data->file_index = curr_cache_len;
                        strcpy(data->host, request.host);
                        pthread_create(&parser_thread, NULL, prefetching_parser, data);
                        curr_cache_len += 1;
                        fclose(new_file);
                    }
                    close(server_conn);
                }
                bzero(resource_hash, SHA_DIGEST_LENGTH);
                pthread_mutex_unlock(&lock_m);
            } else {
                char *contents = "<html><head><title>404 File Not Found</title></head><body><h2>404 File Not Found</h2></body></html>";
                char content_length[10];
                sprintf(content_length, "%ld", strlen(contents));
                strcat(response_str, request.http_version);
                strcat(response_str, " ");
                strcat(response_str, "400");
                strcat(response_str, " ");
                strcat(response_str, "Document Follows");
                strcat(response_str, "\r\n");
                strcat(response_str, "Content-Type:");
                strcat(response_str, "text/html");
                strcat(response_str, "\r\n");
                strcat(response_str, "Content-Length:");
                strcat(response_str, content_length);
                strcat(response_str, "\r\n\r\n");
                strcat(response_str, contents);
                write(connfd, response_str, strlen(response_str));
            }
            // memset(response_str, 0, strlen(response_str));
            memset(buf, 0, MAXLINE);
            is_first = 0;
            if (keepAlive == 1) {
                printf("waiting to read \n");
                // bzero(buf, strlen(buf));
                n = read(connfd, buf, MAXLINE);
                printf("reading request \n");
                // printf("%s \n", buf);
                if (n < 0) {
                    printf("empty message \n");
                }
            }
        }
    }
}


/* 
 * open_listenfd - open and return a listening socket on port
 * Returns -1 in case of failure 
 */
int open_listenfd(int port)
{
    int listenfd, optval=1;
    struct sockaddr_in serveraddr;
    struct timeval tv;
    tv.tv_sec = 10;
	tv.tv_usec = 0;
  
    /* Create a socket descriptor */
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;
    /* Eliminates "Address already in use" error from bind. */
    if (setsockopt(listenfd, SOL_SOCKET, SO_RCVTIMEO,
            (const char*)&tv, sizeof tv) < 0)
        return -1;

    /* listenfd will be an endpoint for all requests to port
       on any IP address for this host */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET; 
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    serveraddr.sin_port = htons((unsigned short)port); 
    if (bind(listenfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) < 0)
        return -1;

    /* Make it a listening socket ready to accept connection requests */
    if (listen(listenfd, LISTENQ) < 0)
        return -1;
    return listenfd;
} /* end open_listenfd */

struct HostDetails fetch_host_address(char *request_address) {
    int i = 0, j = 0, k = 0;
    char hostname[1000];
    char port[10];
    struct HostDetails details;

    while (request_address[i] != ':' && i < strlen(request_address)) {
        hostname[i] = request_address[i];
        i++;
    }
    if (i < strlen(request_address)) {
        strcpy(details.ipHostName, hostname);
        details.addressFound = 1;
        details.isLocal = 1;
        while(request_address[i] != '\0') {
            port[j] = request_address[i];
            i++;j++;
        }
        details.port= atoi(port);
    } else {
        details.isLocal = 0;
        for (i = 0; i < 100; i++) {
            if (strcmp(host_names_cache_list[i], request_address) == 0) {
                break;
            }
        }
        if (i >= 100) {
            details.addressFound = 0;
        } else {
            details.addressFound = 1;
            j = 0;k=0;
            while(host_addresses_cache_list[i][j] != ':') {
                hostname[k] = host_addresses_cache_list[i][j];
                j++;k++;
            }
            hostname[k] = '\0';
            j++;
            k = 0;
            while(host_addresses_cache_list[i][j] != '\0') {
                port[k] = host_addresses_cache_list[i][j];
                j++;k++;
            }
            port[k] = '\0';
            strcpy(details.ipHostName, hostname);
            details.port = atoi(port);
        }
    }
    return details;
}

int check_blacklist(int client_conn, char ipaddress[100]) {
    FILE *blacklist_FP;
    blacklist_FP = fopen("./blacklist", "r");
    fseek(blacklist_FP, 0, SEEK_END);
    long fsize = ftell(blacklist_FP);
    fseek(blacklist_FP, 0, SEEK_SET);
    char *file = malloc(fsize + 1);
    if (file != NULL) {
        fread(file, fsize, 1, blacklist_FP);
        fclose(blacklist_FP);
        file[fsize] = 0;
    }
    int i = 0;
    while(i < fsize) {
        int j = 0;
        char line[200];
        while(file[i] != ';') {
            line[j] = file[i];
            i++;
            j++;
        }
        line[j] = '\0';
        if (strcmp(ipaddress, line) == 0) {
            char *response = "HTTP/1.1 403 Document Follows \r\n Content_Type:text/html\r\nContent-Length:9\r\n\r\nForbidden";
            printf("Request has been blocked");
            write(client_conn, response, strlen(response));
            return 1;
        }
        i++;
    }
    return 0;
}

int create_server_conn(struct HttpRequest request, int client_conn) {
    struct timeval tv;
    tv.tv_sec = 10;
	tv.tv_usec = 0;
    int sockfd;
    struct sockaddr_in servaddr, cli;
    struct HostDetails details;
    struct hostent *hostdet;
    char ipaddress[100];
    char full_address[200];

    while (1) {
        bzero(full_address, 200);
        bzero(ipaddress, 100);
        // socket create and verification
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd == -1) {
            printf("socket creation failed...\n");
            exit(0);
        }
        else
            printf("Socket successfully created..\n");
        bzero(&servaddr, sizeof(servaddr));
    
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
            (const char*)&tv, sizeof tv) < 0)
            return -1;
        
        // assign IP, PORT
        servaddr.sin_family = AF_INET;
        details = fetch_host_address(request.host);
        if (details.addressFound) {
            printf("address found in cache %s %d \n", details.ipHostName, details.port);
            servaddr.sin_addr.s_addr = inet_addr(details.ipHostName);
            servaddr.sin_port = htons(details.port);
        } else {
            printf("address not found in cache \n");
            hostdet = gethostbyname(request.host);
            strcpy(ipaddress, inet_ntoa(*(struct in_addr*)hostdet->h_addr));
            servaddr.sin_addr.s_addr = inet_addr(ipaddress);
            servaddr.sin_port = htons(80);
        }
        int is_blacklisted = check_blacklist(client_conn, ipaddress);
        if (is_blacklisted) {
            return -1;
        }
        // connect the client socket to server socket
        if (connect(sockfd, (SA*)&servaddr, sizeof(servaddr)) != 0) {
            printf("connection with the server failed...trying again after 2secs timeout\n");
            sleep(2000);
            // exit(0);
        }
        else {
            if (!details.addressFound) {
                printf("caching the server address \n"); 
                bzero(full_address, 200);
                strcpy(host_names_cache_list[curr_host_names_len], request.host);
                strcat(full_address, ipaddress);
                strcat(full_address, ":");
                strcat(full_address, "80");
                strcpy(host_addresses_cache_list[curr_host_names_len], full_address);
                curr_host_names_len++;
            }
            printf("connected to the server..\n");
            break;
        }
    }
    return sockfd;
}