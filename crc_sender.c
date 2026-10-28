
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 8081
#define MAX_BITS 256
#define GENERATOR "10011"

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

int main(void)
{
    char data[MAX_BITS], work[MAX_BITS + 5];
    char frame[MAX_BITS + 5];
    int sockfd, inject, bitpos;
    struct sockaddr_in server;

    printf("=== CRC SENDER ===\n");
    printf("Generator polynomial: %s (x^4 + x + 1)\n", GENERATOR);
    printf("Enter binary data (maximum %d bits): ", MAX_BITS - 5);

    if (scanf("%255s", data) != 1 || !valid_binary(data) ||
        strlen(data) > MAX_BITS - 5) {
        printf("Invalid binary data.\n");
        return 1;
    }

    int data_len = strlen(data);
    int gen_len = strlen(GENERATOR);
    int crc_len = gen_len - 1;

    strcpy(work, data);
    for (int i = 0; i < crc_len; i++)
        work[data_len + i] = '0';
    work[data_len + crc_len] = '\0';

    printf("\nData with appended zeros: %s\n", work);

    divide_crc(work);

    printf("CRC remainder: ");
    for (int i = data_len; i < data_len + crc_len; i++)
        printf("%c", work[i]);
    printf("\n");

    strcpy(frame, data);
    strncat(frame, work + data_len, crc_len);

    printf("Transmitted frame: %s\n", frame);

    printf("\nIntroduce a bit error? (1=Yes, 0=No): ");
    if (scanf("%d", &inject) != 1 || (inject != 0 && inject != 1)) {
        printf("Invalid choice.\n");
        return 1;
    }

    if (inject) {
        printf("Enter bit position to flip (1-%zu, leftmost bit = 1): ",
               strlen(frame));

        if (scanf("%d", &bitpos) != 1 ||
            bitpos < 1 || bitpos > (int)strlen(frame)) {
            printf("Invalid bit position.\n");
            return 1;
        }

        int index = bitpos - 1;
        frame[index] = (frame[index] == '0') ? '1' : '0';

        printf("Corrupted frame: %s\n", frame);
        printf("Flipped bit position %d.\n", bitpos);
    } else {
        printf("No error introduced.\n");
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

    if (connect(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }

    uint32_t len = htonl((uint32_t)strlen(frame));

    if (send(sockfd, &len, sizeof(len), 0) != sizeof(len) ||
        send(sockfd, frame, strlen(frame), 0) != (ssize_t)strlen(frame)) {
        perror("send");
        close(sockfd);
        return 1;
    }

    char result[128] = {0};
    ssize_t n = recv(sockfd, result, sizeof(result) - 1, 0);

    if (n > 0) {
        result[n] = '\0';
        printf("\nReceiver result: %s\n", result);
    } else {
        printf("No receiver response.\n");
    }

    close(sockfd);
    return 0;
}
  
