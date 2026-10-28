
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 8080
#define MAX_WORDS 100

int receive_all(int sockfd, void *buffer, size_t length)
{
    size_t total = 0;
    unsigned char *p = buffer;

    while (total < length) {
        ssize_t n = recv(sockfd, p + total, length - total, 0);

        if (n <= 0)
            return 0;

        total += (size_t)n;
    }

    return 1;
}

int main(void)
{
    int server_fd, client_fd;
    struct sockaddr_in server;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
               &opt, sizeof(opt));

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server,
             sizeof(server)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 5) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Checksum receiver listening on port %d...\n", PORT);

    client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
        perror("accept");
        close(server_fd);
        return 1;
    }

    unsigned int count_net;
    if (!receive_all(client_fd, &count_net, sizeof(count_net))) {
        printf("Failed to receive word count.\n");
        close(client_fd);
        close(server_fd);
        return 1;
    }

    unsigned int n = ntohl(count_net);

    if (n < 1 || n > MAX_WORDS) {
        printf("Invalid word count received.\n");
        close(client_fd);
        close(server_fd);
        return 1;
    }

    unsigned char data[MAX_WORDS], checksum;

    if (!receive_all(client_fd, data, n) ||
        !receive_all(client_fd, &checksum, 1)) {
        printf("Failed to receive data.\n");
        close(client_fd);
        close(server_fd);
        return 1;
    }

    printf("\nReceived data words:\n");
    unsigned int sum = 0;

    for (unsigned int i = 0; i < n; i++) {
        printf("%02X ", data[i]);
        sum += data[i];

        printf("\nAdding %02X: sum = %03X", data[i], sum);

        if (sum > 0xFF) {
            sum = (sum & 0xFF) + 1;
            printf(" (end-around carry applied: %03X)", sum);
        }

        printf("\n");
    }

    printf("Received checksum = %02X\n", checksum);

    sum += checksum;
    if (sum > 0xFF)
        sum = (sum & 0xFF) + 1;

    printf("Sum including checksum = %02X\n", sum);

    char result[128];

    if (sum == 0xFF) {
        printf("Verification: NO ERROR DETECTED\n");
        snprintf(result, sizeof(result), "NO ERROR DETECTED");
    } else {
        printf("Verification: ERROR DETECTED\n");
        snprintf(result, sizeof(result), "ERROR DETECTED");
    }

    send(client_fd, result, strlen(result), 0);

    close(client_fd);
    close(server_fd);
    return 0;
}
