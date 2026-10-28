
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 5353
#define BUF_SIZE 256

struct DNSRecord {
    const char *hostname;
    const char *ip;
};

/* Simulated DNS database */
struct DNSRecord dns_table[] = {
    {"www.example.com", "192.0.2.10"},
    {"www.google.com",  "192.0.2.20"},
    {"www.ssn.edu.in",  "192.0.2.30"},
    {"localhost",       "127.0.0.1"}
};

#define TABLE_SIZE (sizeof(dns_table) / sizeof(dns_table[0]))

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

int main(void)
{
    int sockfd;
    char hostname[BUF_SIZE], response[BUF_SIZE];
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    printf("UDP DNS Server running on port %d...\n", PORT);
    printf("Press Ctrl+C to stop.\n");

    while (1) {
        ssize_t n = recvfrom(sockfd, hostname,
                             sizeof(hostname) - 1, 0,
                             (struct sockaddr *)&client_addr,
                             &client_len);

        if (n < 0) {
            perror("recvfrom");
            continue;
        }

        hostname[n] = '\0';

        /* Remove trailing newline or carriage return */
        hostname[strcspn(hostname, "\r\n")] = '\0';

        printf("\nQuery received: %s\n", hostname);

        /*
         * Special test case: deliberately drop the request
         * so the client can demonstrate timeout handling.
         */
        if (strcmp(hostname, "timeout.test") == 0) {
            printf("Simulating timeout: no reply sent.\n");
            continue;
        }

        if (!valid_hostname(hostname)) {
            snprintf(response, sizeof(response),
                     "INVALID Invalid hostname format");
        } else {
            int found = 0;

            for (size_t i = 0; i < TABLE_SIZE; i++) {
                if (strcasecmp(hostname,
                               dns_table[i].hostname) == 0) {
                    snprintf(response, sizeof(response),
                             "OK %s", dns_table[i].ip);
                    found = 1;
                    break;
                }
            }

            if (!found) {
                snprintf(response, sizeof(response),
                         "NXDOMAIN Domain not found");
            }
        }

        printf("Response: %s\n", response);

        if (sendto(sockfd, response, strlen(response), 0,
                   (struct sockaddr *)&client_addr,
                   client_len) < 0) {
            perror("sendto");
        }
    }

    close(sockfd);
    return 0;
}
