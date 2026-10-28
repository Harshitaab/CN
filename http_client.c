
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 8192

double elapsed_ms(struct timespec start, struct timespec end)
{
    return (end.tv_sec - start.tv_sec) * 1000.0 +
           (end.tv_nsec - start.tv_nsec) / 1000000.0;
}

int main(void)
{
    char url[2048], host[512], path[1536];
    char request[4096], buffer[BUFFER_SIZE];
    char filename[256], *p, *slash;
    const char *port = "80";
    int sockfd = -1, header_done = 0;
    int status = 0, header_bytes = 0;
    size_t received = 0;
    FILE *output = NULL;
    struct addrinfo hints, *result, *rp;
    struct timespec t_start, t_connected, t_first, t_end;
    int first_received = 0;

    printf("Enter URL (http://host:port/path): ");
    if (!fgets(url, sizeof(url), stdin))
        return 1;
    url[strcspn(url, "\r\n")] = '\0';

    /* Parse HTTP URL */
    if (strncmp(url, "http://", 7) != 0) {
        fprintf(stderr, "Error: Only http:// URLs are supported.\n");
        return 1;
    }

    p = url + 7;
    slash = strpbrk(p, "/?#");
    if (slash) {
        size_t hostlen = slash - p;
        if (hostlen == 0 || hostlen >= sizeof(host)) {
            fprintf(stderr, "Invalid host.\n");
            return 1;
        }
        memcpy(host, p, hostlen);
        host[hostlen] = '\0';

        if (*slash == '/') {
            snprintf(path, sizeof(path), "%s", slash);
        } else {
            snprintf(path, sizeof(path), "/%s", slash);
        }
    } else {
        snprintf(host, sizeof(host), "%s", p);
        strcpy(path, "/");
    }

    /* Extract optional port */
    if (strchr(host, ':')) {
        char *colon = strrchr(host, ':');
        *colon = '\0';
        if (colon[1] == '\0') {
            fprintf(stderr, "Invalid port.\n");
            return 1;
        }
        port = colon + 1;
    }

    if (host[0] == '\0') {
        fprintf(stderr, "Invalid host.\n");
        return 1;
    }

    /* Derive local filename from URL path */
    const char *end = strpbrk(path, "?#");
    size_t pathlen = end ? (size_t)(end - path) : strlen(path);
    char cleanpath[1536];
    if (pathlen >= sizeof(cleanpath))
        pathlen = sizeof(cleanpath) - 1;
    memcpy(cleanpath, path, pathlen);
    cleanpath[pathlen] = '\0';

    slash = strrchr(cleanpath, '/');
    if (slash && slash[1] != '\0') {
        snprintf(filename, sizeof(filename), "downloaded_%s", slash + 1);
    } else {
        strcpy(filename, "downloaded_index.html");
    }

    /* DNS resolution */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int gai = getaddrinfo(host, port, &hints, &result);
    if (gai != 0) {
        fprintf(stderr, "DNS error: %s\n", gai_strerror(gai));
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &t_start);

    /* Create TCP connection */
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype,
                        rp->ai_protocol);
        if (sockfd < 0)
            continue;

        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;

        close(sockfd);
        sockfd = -1;
    }
    freeaddrinfo(result);

    if (sockfd < 0) {
        perror("connect");
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &t_connected);

    /* Send HTTP GET request */
    int req_len = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "User-Agent: CN-Lab-HTTP-Client/1.0\r\n"
        "\r\n", path, host);

    if (req_len < 0 || req_len >= (int)sizeof(request)) {
        fprintf(stderr, "Request too large.\n");
        close(sockfd);
        return 1;
    }

    size_t sent = 0;
    while (sent < (size_t)req_len) {
        ssize_t n = send(sockfd, request + sent,
                         (size_t)req_len - sent, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            perror("send");
            close(sockfd);
            return 1;
        }
        sent += (size_t)n;
    }

    /* Open output file */
    output = fopen(filename, "wb");
    if (!output) {
        perror("fopen");
        close(sockfd);
        return 1;
    }

    /*
     * Read response. HTTP headers end at CRLF CRLF.
     * Write only the body to disk, preserving binary bytes.
     */
    char header[32768];
    size_t hlen = 0;
    int have_status = 0;

    while (1) {
        ssize_t n = recv(sockfd, buffer, sizeof(buffer), 0);

        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) {
            perror("recv");
            fclose(output);
            close(sockfd);
            return 1;
        }
        if (n == 0)
            break;

        if (!first_received) {
            clock_gettime(CLOCK_MONOTONIC, &t_first);
            first_received = 1;
        }

        size_t offset = 0;

        if (!header_done) {
            while (offset < (size_t)n && !header_done) {
                if (hlen + 1 >= sizeof(header)) {
                    fprintf(stderr, "HTTP headers too large.\n");
                    fclose(output);
                    close(sockfd);
                    return 1;
                }

                header[hlen++] = buffer[offset++];
                header[hlen] = '\0';

                if (!have_status && strstr(header, "\r\n")) {
                    if (sscanf(header, "HTTP/%*s %d", &status) == 1)
                        have_status = 1;
                }

                if (hlen >= 4 &&
                    memcmp(header + hlen - 4, "\r\n\r\n", 4) == 0) {
                    header_done = 1;
                    header_bytes = (int)hlen;
                }
            }
        }

        if (header_done && offset < (size_t)n) {
            size_t body_len = (size_t)n - offset;
            if (fwrite(buffer + offset, 1, body_len, output) != body_len) {
                perror("fwrite");
                fclose(output);
                close(sockfd);
                return 1;
            }
            received += body_len;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t_end);

    fclose(output);
    close(sockfd);

    if (!header_done) {
        fprintf(stderr, "Invalid or incomplete HTTP response.\n");
        return 1;
    }

    double response_ms = first_received ?
        elapsed_ms(t_start, t_first) : elapsed_ms(t_start, t_end);
    double download_ms = elapsed_ms(t_first, t_end);
    double connect_ms = elapsed_ms(t_start, t_connected);
    double seconds = download_ms / 1000.0;
    double throughput = seconds > 0 ?
        (received / 1024.0) / seconds : 0;

    printf("\n--- HTTP CLIENT RESULTS ---\n");
    printf("HTTP Status       : %d\n", status);
    printf("Host              : %s\n", host);
    printf("Port              : %s\n", port);
    printf("Path              : %s\n", path);
    printf("Header size       : %d bytes\n", header_bytes);
    printf("Downloaded size   : %zu bytes\n", received);
    printf("TCP connect time  : %.3f ms\n", connect_ms);
    printf("Time to first byte: %.3f ms\n", response_ms);
    printf("Download time     : %.3f ms\n", download_ms);
    printf("Throughput        : %.2f KiB/s\n", throughput);
    printf("Saved file        : %s\n", filename);

    if (status < 200 || status >= 300)
        printf("Note: Server returned a non-success HTTP status.\n");

    return 0;
}
