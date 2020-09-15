#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include "http.h"

#define BUF_SIZE 1024

int resolve_hostname(struct sockaddr_in *out, const char *host)
{
    struct addrinfo hints, *resolved = NULL;

    // Zero out then populate the hints for getaddrinfo().
    bzero(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    // Attempt to resolve the hostname to an IPv4 address.
    if (getaddrinfo(host, NULL, &hints, &resolved) != 0)
    {
        perror("getaddrinfo");
        return -1;
    }

    // Copy the first result returned
    memcpy(out, resolved->ai_addr, resolved->ai_addrlen);
    freeaddrinfo(resolved);
    return 0;
}

int read_response(Buffer **dst, int *sockfd)
{
    int bytes_read;
    char received[BUF_SIZE];

    while (1)
    {
        // Read at most one less byte than the maximum buffer size,
        // then check there was not an error.
        // Break the loop if an EOF was read.
        bytes_read = read(*sockfd, received, BUF_SIZE - 1);
        if (bytes_read < 0)
        {
            perror("read");
            return -1;
        }
        else if (bytes_read == 0)
        {
            break;
        }

        received[BUF_SIZE - 1] = '\0';

        // Realloc the Buffers data then append the new data.
        (*dst)->data = realloc((*dst)->data, bytes_read);
        memcpy((*dst)->data + (*dst)->length, received, bytes_read);
        (*dst)->length += bytes_read;
    }

    return 0;
}

/**
 * Perform an HTTP 1.0 query to a given host and page and port number.
 * host is a hostname and page is a path on the remote server. The query
 * will attempt to retrievev content in the given byte range.
 * User is responsible for freeing the memory.
 * 
 * @param host - The host name e.g. www.canterbury.ac.nz
 * @param page - e.g. /index.html
 * @param range - Byte range e.g. 0-500. NOTE: A server may not respect this
 * @param port - e.g. 80
 * @return Buffer - Pointer to a buffer holding response data from query
 *                  NULL is returned on failure.
 */
Buffer *http_query(char *host, char *page, const char *range, int port)
{
    Buffer *out;
    char req[BUF_SIZE];
    int sockfd;

    struct sockaddr_in addr;

    // printf("%d\n", max_chunk_size);

    // Allocate the response buffer and an inital data buffer.
    out = (Buffer *)malloc(sizeof(Buffer));

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    // Create the required HTTP/1.0 GET Request packet.
    snprintf(req, BUF_SIZE,
             "GET %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "Range: bytes=%s\r\n"
             "User-Agent: getter\r\n\r\n",
             page, host, range);

    // Resolve the hostname to an IPv4 address
    if (resolve_hostname(&addr, host) != 0)
    {
        perror("resolve_hostname");
        return NULL;
    }

    addr.sin_port = htons(port);

    // Attempt to connect to the server. If the connection
    // is successful, send the HTTP GET request.
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        perror("connect");
        return NULL;
    }

    write(sockfd, req, sizeof(req));

    if (read_response(&out, &sockfd) != 0)
    {
        perror("read_response");
        return NULL;
    }

    return out;
}

/**
 * Separate the content from the header of an http request.
 * NOTE: returned string is an offset into the response, so
 * should not be freed by the user. Do not copy the data.
 * @param response - Buffer containing the HTTP response to separate 
 *                   content from
 * @return string response or NULL on failure (buffer is not HTTP response)
 */
char *http_get_content(Buffer *response)
{

    char *header_end = strstr(response->data, "\r\n\r\n");

    if (header_end)
    {
        return header_end + 4;
    }
    else
    {
        return response->data;
    }
}

int split_url(const char *url, char **host, char **page)
{
    strncpy(*host, url, BUF_SIZE);

    *page = strstr(*host, "/");

    if (*page)
    {
        *page[0] = '\0';

        ++*page;
        return 0;
    }
    else
    {

        fprintf(stderr, "could not split url into host/page %s\n", url);
        return -1;
    }
}

/**
 * Splits an HTTP url into host, page. On success, calls http_query
 * to execute the query against the url. 
 * @param url - Webpage url e.g. learn.canterbury.ac.nz/profile
 * @param range - The desired byte range of data to retrieve from the page
 * @return Buffer pointer holding raw string data or NULL on failure
 */
Buffer *http_url(const char *url, const char *range)
{
    char host[BUF_SIZE];
    char *page;
    
    if (split_url(url, (char **)&host, &page) != 0) {
        return NULL;
    };

    return http_query(host, page, range, 80);
}

/**
 * Makes a HEAD request to a given URL and gets the content length
 * Then determines max_chunk_size and number of split downloads needed
 * @param url   The URL of the resource to download
 * @param threads   The number of threads to be used for the download
 * @return int  The number of downloads needed satisfying max_chunk_size
 *              to download the resource
 */
int get_num_tasks(char *url, int threads)
{
    // Buffer *res = (Buffer *) malloc(sizeof(Buffer));
    char req[BUF_SIZE];
    // int sockfd;

    // struct sockaddr_in addr;

    // sockfd = socket(AF_INET, SOCK_STREAM, 0);

    char host_raw[BUF_SIZE];
    char *page, *host;
    split_url(url, (char **)&host_raw, &page);
    host = host_raw;
    
    snprintf(req, BUF_SIZE,
             "HEAD %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "User-Agent: getter\r\n\r\n",
             page, host);

    printf("%s\n", req);

    // // Resolve the hostname to an IPv4 address
    // if (resolve_hostname(&addr, host) != 0)
    // {
    //     perror("resolve_hostname");
    //     return NULL;
    // }

    // addr.sin_port = htons(80);

    // // Attempt to connect to the server. If the connection
    // // is successful, send the HTTP GET request.
    // if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    // {
    //     perror("connect");
    //     return NULL;
    // }

    // write(sockfd, req, sizeof(req));

    // if (read_response(&res, &sockfd) != 0)
    // {
    //     perror("read_response");
    //     return NULL;
    // }

    // printf("%s\n", res->data);
    return 4;
}

int get_max_chunk_size()
{
    return max_chunk_size;
}
