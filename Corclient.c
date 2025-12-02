#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>     // socket
#include <arpa/inet.h>      // inet_addr, htons
#include <unistd.h>
#include <poll.h>
#include <errno.h>

#define MSG_LEN 1024

int main(int argc , char *argv[])
{
    int sock;
    int n;
    struct sockaddr_in adresse;
    char send_buff[MSG_LEN];
    char recv_buff[MSG_LEN];

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip_address> <server_port>\n", argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Port invalide: %s\n", argv[2]);
        return 1;
    }

    sock = socket(AF_INET , SOCK_STREAM , 0);
    if (sock == -1)
    {
        perror("Could not create socket");
        return 1;
    }
    puts("Socket created");

    memset(&adresse, 0, sizeof(adresse));

    adresse.sin_family = AF_INET;
    adresse.sin_port = htons(port);

    // conversion de l'adresse IP fournie en argument
    adresse.sin_addr.s_addr = inet_addr(server_ip);
    if (adresse.sin_addr.s_addr == INADDR_NONE) {
        fprintf(stderr, "Adresse IP invalide: %s\n", server_ip);
        close(sock);
        return 1;
    }

    if (connect(sock , (struct sockaddr *)&adresse , sizeof(adresse)) < 0)
    {
        perror("connect failed. Error");
        close(sock);
        return 1;
    }
    puts("Connected\n");

    struct pollfd pfds[2];
    pfds[0].fd = STDIN_FILENO;
    pfds[0].events = POLLIN;
    pfds[1].fd = sock;
    pfds[1].events = POLLIN;

    int running = 1;
    while (running) {
        int ready = poll(pfds, 2, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll failed");
            break;
        }

        if (pfds[0].revents & POLLIN) {
            printf("Entrez un message ('/quit' pour quitter) : ");
            fflush(stdout);

            if (fgets(send_buff, MSG_LEN, stdin) == NULL) {
                printf("\nFin de l'entrée standard.\n");
                running = 0;
            } else {
                size_t len = strlen(send_buff);
                if (len > 0 && send_buff[len - 1] == '\n') {
                    send_buff[len - 1] = '\0';
                    len--;
                }

                if (strcmp(send_buff, "/quit") == 0) {
                    printf("Fermeture de la connexion.\n");
                    running = 0;
                } else if (len > 0) {
                    if (send(sock, send_buff, len, 0) < 0) {
                        perror("send failed");
                        running = 0;
                    }
                }
            }
        }

        if (pfds[1].revents & POLLIN) {
            n = recv(sock, recv_buff, MSG_LEN - 1, 0);
            if (n < 0) {
                perror("recv failed");
                break;
            } else if (n == 0) {
                printf("Serveur a fermé la connexion.\n");
                break;
            }

            recv_buff[n] = '\0';
            printf("\nRéponse du serveur : \"%s\"\n", recv_buff);
        }

        if (pfds[1].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            printf("Connexion serveur interrompue.\n");
            break;
        }
    }

    close(sock);

    return 0;
}
