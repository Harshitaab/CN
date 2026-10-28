
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 8081
#define MAX_FRAME 261
#define GENERATOR "10011"

int receive_all(int sockfd, void *buffer, size_t length)
{
    size_t total = 0;
    char *p = buffer;

    while (total < length) {
        ssize_t n = recv(sockfd, p + total, length - total, 0);

        if (n <= 0)
            return 0;

        total += (size_t)n;
    }

    return 1;
}

int valid_binary(const char *s)
{
    if (*s == '\0')
        return 0;

    for (int i = 0; s[i]; i++) {
        if (s[i] != '0' && s[i] != '1')
            return 0;
    }

    return 1;
}

void divide_crc(char bits[])
{
    int len = strlen(bits);
    int gen_len = strlen(GENERATOR);

    for (int i = 0; i <= len - gen_len; i++) {
        if (bits[i] == '1') {
            for (int j = 0; j < gen_len; j++) {
                bits[i + j] =
                    (bits[i + j] == GENERATOR[j]) ? '0' : '1';
            }
        }
    }
}

int main(void)
{
    int server_fd, client_fd, opt = 1;
    struct sockaddr_in server;

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

    printf("=== CRC RECEIVER ===\n");
    printf("Generator: %s\n", GENERATOR);
    printf("Listening on TCP port %d...\n", PORT);

    client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
        perror("accept");
        close(server_fd);
        return 1;
    }

    uint32_t net_len;

    if (!receive_all(client_fd, &net_len, sizeof(net_len))) {
        printf("Failed to receive frame length.\n");
        close(client_fd);
        close(server_fd);
        return 1;
    }

    uint32_t len = ntohl(net_len);

    if (len < 5 || len >= MAX_FRAME) {
        printf("Invalid frame length.\n");
        close(client_fd);
        close(server_fd);
        return 1;
    }

    char frame[MAX_FRAME];

    if (!receive_all(client_fd, frame, len)) {
        printf("Failed to receive frame.\n");
        close(client_fd);
        close(server_fd);
        return 1;
    }

    frame[len] = '\0';

    printf("\nReceived frame: %s\n", frame);

    char remainder[MAX_FRAME];
    strcpy(remainder, frame);

    if (!valid_binary(frame)) {
        printf("Invalid frame: non-binary characters received.\n");
        send(client_fd, "INVALID FRAME", 13, 0);
    } else {
        divide_crc(remainder);

        int error = 0;
        int gen_len = strlen(GENERATOR);

        printf("CRC remainder at receiver: ");

        for (int i = (int)strlen(frame) - (gen_len - 1);
             i < (int)strlen(frame); i++) {
            printf("%c", remainder[i]);

            if (remainder[i] != '0')
                error = 1;
        }

        printf("\n");

        if (error) {
            printf("Result: ERROR DETECTED\n");
            send(client_fd, "ERROR DETECTED", 14, 0);
        } else {
            printf("Result: NO ERROR DETECTED\n");
            send(client_fd, "NO ERROR DETECTED", 17, 0);
        }
    }

    close(client_fd);
    close(server_fd);
    return 0;
}
