#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>     // socket
#include <arpa/inet.h>      // inet_addr, htons
#include <unistd.h>
#include <poll.h>

#define MSG_LEN 1024

static ssize_t send_all(int fd, const void *buf, size_t len)
{
    size_t sent = 0;
    const char *p = buf;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n < 0) return -1;
        sent += (size_t)n;
    }
    return (ssize_t)sent;
}

int main(int argc , char *argv[])
{
    int sock;
    ssize_t n;
    struct sockaddr_in adresse;
    char buff[MSG_LEN];
    char line[MSG_LEN];

    if (argc != 5) {
        fprintf(stderr, "Usage: %s <server_ip> <server_port> <ROLE> <pseudo>\n", argv[0]);
        fprintf(stderr, "ROLE = OWNER | TENANT\n");
        return 1;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    const char *role = argv[3];
    const char *pseudo = argv[4];
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Port invalide: %s\n", argv[2]);
        return 1;
    }

    sock = socket(AF_INET , SOCK_STREAM , 0);
    if (sock == -1)
    {
        perror("Peut pas créer de socket");
        return 1;
    }
    puts("Socket créée");

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
        perror("Échec de la connexion");
        close(sock);
        return 1;
    }
    puts("Connecté\n");

    if (strcmp(role, "OWNER") != 0 && strcmp(role, "TENANT") != 0) {
        fprintf(stderr, "ROLE must be OWNER or TENANT\n");
        close(sock);
        return 1;
    }

    // Send initial identification
    char hello[MSG_LEN];
    snprintf(hello, sizeof(hello), "%s %s", role, pseudo);
    if (send_all(sock, hello, strlen(hello)) < 0) {
        perror("send hello failed");
        close(sock);
        return 1;
    }

    printf("=== Menu %s ===\n", role);
    if (strcmp(role, "OWNER") == 0) {
        printf("1 <code> : SET CODE <code> (6 chiffres)\n");
        printf("2 <sec>  : SET VALIDITY <sec>\n");
        printf("3        : SHOW (code + durée restante)\n");
        printf("4        : QUIT\n");
    } else {
        printf("1 <code> : tenter un code (6 chiffres)\n");
        printf("2        : QUIT\n");
    }
    printf("-------------------------------\n");

    struct pollfd pfds[2];
    pfds[0].fd = STDIN_FILENO;
    pfds[0].events = POLLIN;
    pfds[1].fd = sock;
    pfds[1].events = POLLIN;

    while (1) {
        int ret = poll(pfds, 2, -1);
        if (ret < 0) {
            perror("poll");
            break;
        }

        // stdin ready
        if (pfds[0].revents & POLLIN) {
            if (!fgets(line, sizeof(line), stdin)) {
                fprintf(stderr, "stdin closed\n");
                break;
            }
            size_t len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
                line[len-1] = '\0';
                len--;
            }
            if (len == 0) continue;

            if (strcmp(role, "OWNER") == 0) {
                if (strncmp(line, "1 ", 2) == 0) {
                    snprintf(buff, sizeof(buff), "SET CODE %s", line + 2);
                } else if (strncmp(line, "2 ", 2) == 0) {
                    snprintf(buff, sizeof(buff), "SET VALIDITY %s", line + 2);
                } else if (strcmp(line, "3") == 0) {
                    snprintf(buff, sizeof(buff), "SHOW");
                } else if (strcmp(line, "4") == 0) {
                    snprintf(buff, sizeof(buff), "QUIT");
                } else {
                    printf("Commande inconnue. Utiliser 1/2/3/4.\n");
                    continue;
                }
            } else { // TENANT
                if (strncmp(line, "1 ", 2) == 0) {
                    snprintf(buff, sizeof(buff), "%s", line + 2);
                } else if (strcmp(line, "2") == 0) {
                    snprintf(buff, sizeof(buff), "QUIT");
                } else {
                    printf("Commande inconnue. Utiliser 1/2.\n");
                    continue;
                }
            }

            if (send_all(sock, buff, strlen(buff)) < 0) {
                perror("send failed");
                break;
            }

            if (strcmp(buff, "QUIT") == 0) {
                printf("Fermeture de la connexion.\n");
                break;
            }
        }

        // socket ready
        if (pfds[1].revents & POLLIN) {
            n = recv(sock, buff, MSG_LEN - 1, 0);
            if (n < 0) {
                perror("recv failed");
                break;
            } else if (n == 0) {
                printf("Serveur fermé la connexion.\n");
                break;
            }
            buff[n] = '\0';
            printf("Réponse du serveur : \"%s\"\n", buff);
        }
    }

    close(sock);
    return 0;
}
