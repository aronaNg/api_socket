#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>     // socket
#include <arpa/inet.h>      // inet_addr, htons
#include <unistd.h>
#include <poll.h>

#define MSG_LEN 1024

typedef struct {
    const char *server_ip;
    int port;
    const char *role;
    const char *pseudo;
    const char *password;
} client_cfg_t;

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

static int parse_args(int argc, char **argv, client_cfg_t *out)
{
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <server_ip> <server_port> <ROLE> <pseudo> <password>\n", argv[0]);
        fprintf(stderr, "ROLE = OWNER | TENANT\n");
        return -1;
    }

    int port = atoi(argv[2]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Port invalide: %s\n", argv[2]);
        return -1;
    }

    if (strcmp(argv[3], "OWNER") != 0 && strcmp(argv[3], "TENANT") != 0) {
        fprintf(stderr, "ROLE must be OWNER or TENANT\n");
        return -1;
    }

    out->server_ip = argv[1];
    out->port = port;
    out->role = argv[3];
    out->pseudo = argv[4];
    out->password = argv[5];
    return 0;
}

static int connect_server(const client_cfg_t *cfg)
{
    int sock = socket(AF_INET , SOCK_STREAM , 0);
    if (sock == -1) {
        perror("Peut pas créer de socket");
        return -1;
    }

    struct sockaddr_in adresse;
    memset(&adresse, 0, sizeof(adresse));
    adresse.sin_family = AF_INET;
    adresse.sin_port = htons((uint16_t)cfg->port);
    adresse.sin_addr.s_addr = inet_addr(cfg->server_ip);
    if (adresse.sin_addr.s_addr == INADDR_NONE) {
        fprintf(stderr, "Adresse IP invalide: %s\n", cfg->server_ip);
        close(sock);
        return -1;
    }

    if (connect(sock , (struct sockaddr *)&adresse , sizeof(adresse)) < 0) {
        perror("Échec de la connexion");
        close(sock);
        return -1;
    }
    return sock;
}

static int send_hello(int sock, const client_cfg_t *cfg)
{
    char hello[MSG_LEN];
    snprintf(hello, sizeof(hello), "AUTH %s %s %s", cfg->role, cfg->pseudo, cfg->password);
    if (send_all(sock, hello, strlen(hello)) < 0) {
        perror("send hello failed");
        return -1;
    }
    return 0;
}

static void print_menu(const char *role)
{
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
}

static int build_command(const char *role, const char *line, char buff[MSG_LEN])
{
    if (strcmp(role, "OWNER") == 0) {
        if (strncmp(line, "1 ", 2) == 0) {
            snprintf(buff, MSG_LEN, "SET CODE %s", line + 2);
        } else if (strncmp(line, "2 ", 2) == 0) {
            snprintf(buff, MSG_LEN, "SET VALIDITY %s", line + 2);
        } else if (strcmp(line, "3") == 0) {
            snprintf(buff, MSG_LEN, "SHOW");
        } else if (strcmp(line, "4") == 0) {
            snprintf(buff, MSG_LEN, "QUIT");
        } else {
            printf("Commande inconnue. Utiliser 1/2/3/4.\n");
            return -1;
        }
    } else { // TENANT
        if (strncmp(line, "1 ", 2) == 0) {
            snprintf(buff, MSG_LEN, "%s", line + 2);
        } else if (strcmp(line, "2") == 0) {
            snprintf(buff, MSG_LEN, "QUIT");
        } else {
            printf("Commande inconnue. Utiliser 1/2.\n");
            return -1;
        }
    }
    return 0;
}

static int handle_stdin(const client_cfg_t *cfg, int sock)
{
    char line[MSG_LEN];
    char buff[MSG_LEN];

    if (!fgets(line, sizeof(line), stdin)) {
        fprintf(stderr, "stdin closed\n");
        return -1;
    }
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
        line[len-1] = '\0';
        len--;
    }
    if (len == 0) return 0;

    if (build_command(cfg->role, line, buff) < 0) return 0;

    if (send_all(sock, buff, strlen(buff)) < 0) {
        perror("send failed");
        return -1;
    }

    if (strcmp(buff, "QUIT") == 0) {
        printf("Fermeture de la connexion.\n");
        return 1; // signal to stop loop
    }
    return 0;
}

static int handle_socket(int sock, char *last_msg, size_t last_msg_sz)
{
    char buff[MSG_LEN];
    ssize_t n = recv(sock, buff, MSG_LEN - 1, 0);
    if (n < 0) {
        perror("recv failed");
        return -1;
    } else if (n == 0) {
        printf("Serveur fermé la connexion.\n");
        return 1; // stop
    }
    buff[n] = '\0';
    if (last_msg && last_msg_sz > 0) {
        strncpy(last_msg, buff, last_msg_sz - 1);
        last_msg[last_msg_sz - 1] = '\0';
    }
    printf("Réponse du serveur : \"%s\"\n", buff);
    return 0;
}

static int run_client(const client_cfg_t *cfg, int sock)
{
    struct pollfd pfds[2];
    pfds[0].fd = STDIN_FILENO;
    pfds[0].events = POLLIN;
    pfds[1].fd = sock;
    pfds[1].events = POLLIN;

    while (1) {
        int ret = poll(pfds, 2, -1);
        if (ret < 0) {
            perror("poll");
            return -1;
        }

        if (pfds[0].revents & POLLIN) {
            int res = handle_stdin(cfg, sock);
            if (res != 0) return res;
        }

        if (pfds[1].revents & POLLIN) {
            char last[MSG_LEN] = {0};
            int res = handle_socket(sock, last, sizeof(last));
            if (res != 0) return res;
        }
    }
}

int main(int argc , char *argv[])
{
    client_cfg_t cfg;
    if (parse_args(argc, argv, &cfg) < 0) {
        return 1;
    }

    int sock = connect_server(&cfg);
    if (sock < 0) return 1;

    if (send_hello(sock, &cfg) < 0) {
        close(sock);
        return 1;
    }

    // Attente d'une réponse d'authentification avant d'afficher le menu
    // On boucle tant qu'on ne reçoit pas un succès ou un échec explicite.
    while (1) {
        char msg[MSG_LEN] = {0};
        int rc = handle_socket(sock, msg, sizeof(msg));
        if (rc != 0) { // erreur ou fermeture
            close(sock);
            return 1;
        }

        if (strncmp(msg, "ERR", 3) == 0) {
            printf("Identifiants incorrects, arrêt.\n");
            close(sock);
            return 1;
        }

        if (strncmp(msg, "WELCOME", 7) == 0 || strncmp(msg, "CURRENT CODE", 12) == 0) {
            break; // authentification acceptée
        }

        // Si on reçoit une invite "LOGIN ..." on continue la boucle pour attendre l'issue.
    }

    puts("Connecté (auth OK)\n");
    print_menu(cfg.role);

    int rc = run_client(&cfg, sock);

    close(sock);
    return (rc < 0) ? 1 : 0;
}
