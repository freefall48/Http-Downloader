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
// The maximum chunk size is 40MB.
#define CHUNKING_MAX_BYTES 41943040
// (dividend + (divisor - 1)) / divisor is a method to perform
// round up integer divison.
#define INT_DIVISION_RUP(n, d) ((n + (d - 1)) / d)

int resolve_hostname(struct sockaddr_in *out, const char *host)
{
    struct addrinfo hints, *addr;

    // Zero out then populate the hints for getaddrinfo().
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    // Attempt to resolve the hostname to an IPv4 address.
    if (getaddrinfo(host, NULL, &hints, &addr) != 0)
    {
        perror("getaddrinfo");
        return -1;
    }

    // Copy the first result returned
    memcpy(out, addr->ai_addr, addr->ai_addrlen);
    freeaddrinfo(addr);
    return 0;
}

int read_response(Buffer **dst,  int *sockfd)
{
    size_t bytes_read, length = BUF_SIZE;
    char received[BUF_SIZE], *tmp;

    (*dst) = malloc(sizeof(Buffer));

    (*dst)->data = calloc(BUF_SIZE, 1);

    (*dst)->length = 0;

    while (bytes_read = read(*sockfd, received, BUF_SIZE), bytes_read > 0)
    {   
        if ((*dst)->length + bytes_read > length) {
            length *= 2;
            tmp = realloc((*dst)->data, length);

            if (tmp) {
                (*dst)->data = tmp;
            } else {
                fprintf(stderr, "realloc() did not return a pointer! Likely out of memory.\n");
                return -1;
            }
        }
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
    Buffer *data;
    char request[BUF_SIZE];
    int sockfd;

    struct sockaddr_in addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    // Zero out the array then create the 
    // required HTTP/1.0 GET Request packet.
    memset(request, '\0', BUF_SIZE);
    snprintf(request, BUF_SIZE,
             "GET /%s HTTP/1.0\r\n"
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

    // Attempt to connect to the server.
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        perror("connect");
        return NULL;
    }

    write(sockfd, request, sizeof(request));

    // Read the response from the server into a Buffer.
    if (read_response(&data, &sockfd) != 0)
    {
        perror("read_response");
        return NULL;
    }

    return data;
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
    *host = calloc(BUF_SIZE, 1);
    strncpy(*host, url, BUF_SIZE);

    // Find the '/' as this splits the host and page.
    *page = strchr(*host, '/');

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

int server_accepts_ranges(const char *response)
{
    char *start, *end, range[10];

    // Determine if the server response contains the 
    // required field.
    start = strstr(response, "Accept-Ranges:");
    if (start)
    {
        // The server mentions Accept-Ranges. However,
        // may still explicitly disallow them.
        // Find the start and end of the server response
        // reguarding ranges.
        start = strchr(start, ' ');
        end = strchr(start, '\n');

        strncpy(range, start, end - start);

        if (strncmp("bytes", range, 10))
        {
            // The server respects byte ranges
            return 1;
        }
    }
    // The server either implicitly or explicitly
    // does not allow ranges.
    return 0;
}

int remote_content_length(Buffer *response)
{
    char *prefix;
    int content_length;

    // Extract the content length from the server response.
    prefix = strstr(response->data, "Content-Length:");
    sscanf(prefix, "Content-Length: %d\r\n", &content_length);

    return content_length;
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
    // TODO: This method is to long
    Buffer *response;
    char *host, *page, request[BUF_SIZE] = {0};
    struct sockaddr_in addr;
    int sockfd, total_bytes, downloads;

    // Try to split the url into 2 parts. Host and page.
    if (split_url(url, &host, &page) < 0) {
        // The url did not conform to host/page, so could
        // not be split.
        free(host);
        return -1;
    }

    // Create the HTTP HEAD message to send to the server.
    snprintf(request, BUF_SIZE,
             "HEAD /%s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "User-Agent: getter\r\n\r\n",
             page, host);

    // // Resolve the hostname to an IPv4 address
    if (resolve_hostname(&addr, host) != 0)
    {
        // The hostname could not be resolved.
        perror("resolve_hostname");
        return -1;
    }

    free(host);
    addr.sin_port = htons(80);

    // // Attempt to connect to the server.
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        perror("connect");
        return -1;
    }

    write(sockfd, request, sizeof(request));

    // Read the server response into a Buffer.
    if (read_response(&response, &sockfd) != 0)
    {
        // Server response couldn't be parsed into
        // a Buffer.
        perror("read_response");
        return -1;
    }

    total_bytes = remote_content_length(response);
    if (server_accepts_ranges(response->data) && threads > 1)
    {
        int chunk_size, additional_downloads = 0;
        // The server indicated it respects ranges so partial downloads
        // can occur.
        // Make sure the chunk size does not exceed the defined max.
        do {
            chunk_size = INT_DIVISION_RUP(total_bytes, threads + additional_downloads);
        } while (++additional_downloads, chunk_size > CHUNKING_MAX_BYTES);

        downloads = threads + additional_downloads;
        max_chunk_size = chunk_size;
    }
    else
    {
        // The server does not accept byte ranges. Therefore
        // only a single download may occur.
        max_chunk_size = total_bytes;
        downloads = 1;
    }

    free(response->data);
    free(response);

    return downloads;
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
    char *host, *page;

    if (split_url(url, &host, &page) < 0) {
        free(host);
        return NULL;
    }

    Buffer *data = http_query(host, page, range, 80);

    free(host);
    return data;
}

int get_max_chunk_size()
{
    return max_chunk_size;
}
