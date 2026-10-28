
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 5353
#define BUF_SIZE 256
#define CACHE_SIZE 32

struct CacheEntry {
    char hostname[BUF_SIZE];
    char ip[64];
    time_t saved_at;
};

struct CacheEntry cache[CACHE_SIZE];
int cache_count = 0;

double elapsed_ms(struct timespec a, struct timespec b)
{
    return (b.tv_sec - a.tv_sec) * 1000.0 +
           (b.tv_nsec - a.tv_nsec) / 1000000.0;
}

int valid_hostname(const char *name)
{
    size_t len = strlen(name);

    if (len == 0 || len > 253)
        return 0;

    if (name[0] == '.' || name[len - 1] == '.')
        return 0;

    int label_len = 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];

        if (c == '.') {
            if (label_len == 0 || label_len > 63 ||
                name[i - 1] == '-')
                return 0;
            label_len = 0;
        } else if (isalnum(c) || c == '-') {
            if (label_len == 0 && c == '-')
                return 0;
            label_len++;
        } else {
            return 0;
        }
    }

    return label_len > 0 && label_len <= 63 &&
           name[len - 1] != '-';
}

int find_cache(const char *hostname, char *ip)
{
    time_t now = time(NULL);

    for (int i = 0; i < cache_count; i++) {
        if (difftime(now, cache[i].saved_at) <= 300 &&
            strcasecmp(hostname, cache[i].hostname) == 0) {
            strcpy(ip, cache[i].ip);
            return 1;
        }
    }

    return 0;
}

void add_cache(const char *hostname, const char *ip)
{
    int index = cache_count;

    if (cache_count < CACHE_SIZE) {
        cache_count++;
    } else {
        index = 0; /* Replace the oldest entry */
        for (int i = 1; i < CACHE_SIZE; i++) {
            if (cache[i].saved_at < cache[index].saved_at)
                index = i;
        }
    }

    snprintf(cache[index].hostname,
             sizeof(cache[index].hostname), "%s", hostname);
    snprintf(cache[index].ip, sizeof(cache[index].ip), "%s", ip);
    cache[index].saved_at = time(NULL);
}

int main(void)
{
    int sockfd;
    char hostname[BUF_SIZE], response[BUF_SIZE], ip[64];
    struct sockaddr_in server_addr;
    struct timeval timeout = {2, 0};

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    /* Wait at most two seconds for a server response */
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt");
        close(sockfd);
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1",
              &server_addr.sin_addr);

    printf("UDP DNS Client started.\n");
    printf("Type a hostname, or 'exit' to quit.\n");
    printf("Try www.example.com, unknown.test, timeout.test\n");

    while (1) {
        printf("\nEnter domain name: ");
        if (!fgets(hostname, sizeof(hostname), stdin))
            break;

        hostname[strcspn(hostname, "\r\n")] = '\0';

        if (strcmp(hostname, "exit") == 0)
            break;

        if (!valid_hostname(hostname)) {
            printf("Error: INVALID hostname format.\n");
            continue;
        }

        /* Check the client-side cache first */
        if (find_cache(hostname, ip)) {
            printf("Cache HIT: %s -> %s\n", hostname, ip);
            printf("Network query time: 0 ms (cache lookup)\n");
            continue;
        }

        printf("Cache MISS: contacting DNS server...\n");

        struct timespec start, finish;
        clock_gettime(CLOCK_MONOTONIC, &start);

        ssize_t sent = sendto(sockfd, hostname,
                              strlen(hostname), 0,
                              (struct sockaddr *)&server_addr,
                              sizeof(server_addr));

        if (sent < 0) {
            perror("sendto");
            continue;
        }

        socklen_t addr_len = sizeof(server_addr);

        ssize_t n = recvfrom(sockfd, response,
                             sizeof(response) - 1, 0,
                             (struct sockaddr *)&server_addr,
                             &addr_len);

        clock_gettime(CLOCK_MONOTONIC, &finish);

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("TIMEOUT: No response within 2 seconds.\n");
            } else {
                perror("recvfrom");
            }
            continue;
        }

        response[n] = '\0';

        double rtt = elapsed_ms(start, finish);

        printf("Server response: %s\n", response);
        printf("Round-trip time: %.3f ms\n", rtt);
        printf("Request size: %zd bytes\n", sent);
        printf("Response size: %zd bytes\n", n);
        printf("Total message data: %zd bytes\n", sent + n);

        if (strncmp(response, "OK ", 3) == 0) {
            snprintf(ip, sizeof(ip), "%s", response + 3);
            add_cache(hostname, ip);
            printf("Resolved IP: %s\n", ip);
            printf("Result stored in client cache for 300 seconds.\n");
        } else if (strncmp(response, "NXDOMAIN", 8) == 0) {
            printf("NXDOMAIN: Domain does not exist in DNS database.\n");
        } else if (strncmp(response, "INVALID", 7) == 0) {
            printf("Error: Server rejected invalid hostname format.\n");
        }
    }

    close(sockfd);
    printf("DNS client terminated.\n");
    return 0;
}
