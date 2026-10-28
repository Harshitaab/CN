
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 8080
#define MAX_WORDS 100

unsigned char calculate_checksum(unsigned char data[], int n)
{
    unsigned int sum = 0;

    printf("\n--- Sender: Checksum Calculation ---\n");

    for (int i = 0; i < n; i++) {
        sum += data[i];

        printf("Adding %02X: sum = %03X", data[i], sum);

        if (sum > 0xFF) {
            sum = (sum & 0xFF) + 1;
            printf(" (end-around carry applied: %03X)", sum);
        }

        printf("\n");
    }

    unsigned char checksum = (unsigned char)(~sum & 0xFF);

    printf("Final sum = %02X\n", sum);
    printf("Generated checksum = %02X\n", checksum);

    return checksum;
}

int main(void)
{
    int sockfd;
    struct sockaddr_in server;
    unsigned char data[MAX_WORDS];
    unsigned char checksum;
    int n;

    printf("=== CHECKSUM SENDER ===\n");

    printf("Enter number of 8-bit words (1-%d): ", MAX_WORDS);

    if (scanf("%d", &n) != 1 || n < 1 || n > MAX_WORDS) {
        printf("Invalid number of words.\n");
        return 1;
    }

    printf("Enter each word as a decimal value (0-255):\n");

    for (int i = 0; i < n; i++) {
        int value;

        printf("Word %d: ", i + 1);

        if (scanf("%d", &value) != 1 || value < 0 || value > 255) {
            printf("Invalid 8-bit value.\n");
            return 1;
        }

        data[i] = (unsigned char)value;
    }

    /* Calculate checksum before modifying data */
    checksum = calculate_checksum(data, n);

    int inject_error;

    printf("\nIntroduce a bit error? (1 = Yes, 0 = No): ");

    if (scanf("%d", &inject_error) != 1 ||
        (inject_error != 0 && inject_error != 1)) {
        printf("Invalid choice.\n");
        return 1;
    }

    if (inject_error == 1) {
        int word_no, bit_no;

        printf("Enter word number to modify (1-%d): ", n);

        if (scanf("%d", &word_no) != 1 ||
            word_no < 1 || word_no > n) {
            printf("Invalid word number.\n");
            return 1;
        }

        printf("Enter bit position (0-7, 0 = rightmost bit): ");

        if (scanf("%d", &bit_no) != 1 ||
            bit_no < 0 || bit_no > 7) {
            printf("Invalid bit position.\n");
            return 1;
        }

        printf("\nOriginal word = %u (0x%02X)\n",
               data[word_no - 1], data[word_no - 1]);

        /* Flip the selected bit */
        data[word_no - 1] ^= (unsigned char)(1u << bit_no);

        printf("Modified word = %u (0x%02X)\n",
               data[word_no - 1], data[word_no - 1]);

        printf("Bit error introduced successfully.\n");
    } else {
        printf("No error introduced.\n");
    }

    /* Create TCP socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

    if (connect(sockfd, (struct sockaddr *)&server,
                sizeof(server)) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }

    /* Send word count, data and original checksum */
    unsigned int count = htonl((unsigned int)n);

    if (send(sockfd, &count, sizeof(count), 0) != sizeof(count) ||
        send(sockfd, data, n, 0) != n ||
        send(sockfd, &checksum, 1, 0) != 1) {
        perror("send");
        close(sockfd);
        return 1;
    }

    printf("\nData and checksum sent to receiver.\n");
    printf("Data bytes = %d, checksum bytes = 1\n", n);

    char result[128] = {0};
    ssize_t received = recv(sockfd, result, sizeof(result) - 1, 0);

    if (received > 0) {
        result[received] = '\0';
        printf("Receiver verification: %s\n", result);
    } else {
        printf("No verification response received.\n");
    }

    close(sockfd);
    return 0;
}
