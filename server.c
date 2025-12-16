/* server.c - clean implementation with SQLite history
 * Usage: server <port>
 */

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<errno.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<poll.h>
#include<sys/random.h>
#include<time.h>
#include<sqlite3.h>

#define MSG_LEN 1024
#define BACKLOG 16

typedef enum {
    ROLE_UNKNOWN = 0,
    ROLE_OWNER,
    ROLE_TENANT
} client_role_t;

typedef struct client_node {
    int fd;
    struct sockaddr_in addr;
    client_role_t role;
    char pseudo[64];
    int attempts;
    struct client_node *next;
} client_node_t;

typedef struct {
    char code[7]; // 6 digits + '\0'
    int validity_secs;
    time_t expires_at;
    int owner_fd; // -1 if none
    char owner_pseudo[64];
    int has_code;
} lock_state_t;

static lock_state_t g_lock = {
    .code = "000000",
    .validity_secs = 3600,
    .expires_at = 0,
    .owner_fd = -1,
    .owner_pseudo = {0},
    .has_code = 0
};

static const char *DB_PATH = "history.db";
static sqlite3 *g_db = NULL;

static const char *ALLOWED_OWNERS[] = {"owner", "Arona", NULL};
static const char *ALLOWED_TENANTS[] = {"tenant", "Corentin", NULL};

/* ------------------------- utilitaires sockets ------------------------- */
static int parse_port_arg(int argc, char **argv, uint16_t *out_port)
{
	long port;
	char *endptr = NULL;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s <server_port>\n", argv[0]);
		return -1;
	}

	errno = 0;
	port = strtol(argv[1], &endptr, 10);
	if (errno != 0 || endptr == argv[1] || port <= 0 || port > 65535)
	{
		fprintf(stderr, "Invalid port: %s\n", argv[1]);
		return -1;
	}

	*out_port = (uint16_t)port;
	return 0;
}

static int create_listen_socket(uint16_t port)
{
	int socket_desc = socket(AF_INET , SOCK_STREAM , 0);
	if (socket_desc == -1)
	{
		perror("Could not create socket");
		return -1;
	}
	puts("Socket created");

	int optval = 1;
	if (setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
	{
		perror("setsockopt SO_REUSEADDR failed");
		close(socket_desc);
		return -1;
	}

	struct sockaddr_in server;
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	server.sin_port = htons(port);

	if( bind(socket_desc,(struct sockaddr *)&server , sizeof(server)) < 0)
	{
		perror("bind failed. Error");
		close(socket_desc);
		return -1;
	}
	puts("bind done");

	if (listen(socket_desc , BACKLOG) < 0)
	{
		perror("listen failed");
		close(socket_desc);
		return -1;
	}

	puts("Waiting for incoming connections...");
	return socket_desc;
}

static int secure_random_digit(void)
{
    unsigned int val = 0;
    if (getrandom(&val, sizeof(val), 0) != sizeof(val)) {
        val = (unsigned int)rand();
    }
    return (int)(val % 10);
}

static int db_init(void)
{
	if (sqlite3_open(DB_PATH, &g_db) != SQLITE_OK)
	{
		fprintf(stderr, "sqlite3_open failed: %s\n", sqlite3_errmsg(g_db));
		return -1;
	}

	const char *sql =
		"CREATE TABLE IF NOT EXISTS history ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"ts INTEGER NOT NULL,"
		"pseudo TEXT NOT NULL,"
		"result TEXT NOT NULL"
		");";

	char *errmsg = NULL;
	int rc = sqlite3_exec(g_db, sql, NULL, NULL, &errmsg);
	if (rc != SQLITE_OK)
	{
		fprintf(stderr, "sqlite3_exec(create table) failed: %s\n", errmsg ? errmsg : "unknown");
		sqlite3_free(errmsg);
		return -1;
	}
	return 0;
}

static void db_close(void)
{
	if (g_db)
	{
		sqlite3_close(g_db);
		g_db = NULL;
	}
}

static void generate_code(char out[7])
{
	for (int i = 0; i < 6; ++i) {
		out[i] = (char)('0' + secure_random_digit());
	}
	out[6] = '\0';
}

static void log_history(const char *pseudo, const char *result)
{
	time_t now = time(NULL);
	if (!g_db)
	{
		// base non initialisée : on ne bloque pas l'exécution, on log juste sur stderr
		fprintf(stderr, "log_history: database not initialized\n");
		return;
	}

	const char *sql = "INSERT INTO history(ts, pseudo, result) VALUES(?, ?, ?);";
	sqlite3_stmt *stmt = NULL;

	if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
	{
		fprintf(stderr, "sqlite3_prepare_v2 failed: %s\n", sqlite3_errmsg(g_db));
		return;
	}

	const char *p = pseudo ? pseudo : "unknown";
	const char *r = result ? result : "";

	sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now);
	sqlite3_bind_text(stmt, 2, p, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, r, -1, SQLITE_TRANSIENT);

	int rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE)
	{
		fprintf(stderr, "sqlite3_step(insert) failed: %s\n", sqlite3_errmsg(g_db));
	}

	sqlite3_finalize(stmt);
}

static void notify_owner(const char *msg)
{
    if (g_lock.owner_fd >= 0) {
        ssize_t sent = send(g_lock.owner_fd, msg, strlen(msg), 0);
        if (sent < 0) perror("notify_owner send");
    }
}

static void rotate_code_and_notify(const char *reason)
{
    generate_code(g_lock.code);
    g_lock.expires_at = time(NULL) + g_lock.validity_secs;
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "ALERT %s NEWCODE %s VALIDITY %d\n",
             reason ? reason : "update", g_lock.code, g_lock.validity_secs);
    notify_owner(buffer);
}

static void add_client(client_node_t **head, int fd, const struct sockaddr_in *addr)
{
    client_node_t *node = malloc(sizeof(client_node_t));
    if (!node) { perror("malloc"); close(fd); return; }
    node->fd = fd;
    node->addr = *addr;
    node->role = ROLE_UNKNOWN;
    node->pseudo[0] = '\0';
    node->attempts = 0;
    node->next = *head;
    *head = node;
}

static void remove_client(client_node_t **head, client_node_t *target)
{
    if (!target) return;
    client_node_t **cursor = head;
    while (*cursor) {
        if (*cursor == target) {
            client_node_t *tmp = *cursor;
            *cursor = tmp->next;
            if (tmp->fd == g_lock.owner_fd) g_lock.owner_fd = -1;
            close(tmp->fd);
            free(tmp);
            return;
        }
        cursor = &(*cursor)->next;
    }
}

static size_t client_count(const client_node_t *head)
{
    size_t count = 0;
    while (head) { ++count; head = head->next; }
    return count;
}

static void log_client_endpoint(const client_node_t *node, const char *prefix)
{
    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &(node->addr.sin_addr), ip, sizeof(ip));
    printf("%s %s:%u\n", prefix, ip, ntohs(node->addr.sin_port));
    fflush(stdout);
}

static void trim_newline(char *s)
{
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r')) { s[len-1] = '\0'; --len; }
}

static int is_six_digits(const char *s)
{
    if (!s) return 0;
    if (strlen(s) != 6) return 0;
    for (size_t i = 0; i < 6; ++i) if (s[i] < '0' || s[i] > '9') return 0;
    return 1;
}

static int remaining_validity_seconds(void)
{
    time_t now = time(NULL);
    if (g_lock.expires_at <= now) return 0;
    return (int)(g_lock.expires_at - now);
}

static int pseudo_allowed(client_role_t role, const char *pseudo)
{
    const char *const *list = (role == ROLE_OWNER) ? ALLOWED_OWNERS : ALLOWED_TENANTS;
    if (!pseudo || !list) return 0;
    for (size_t i = 0; list[i]; ++i) if (strcmp(list[i], pseudo) == 0) return 1;
    return 0;
}

static void ensure_code_fresh(void)
{
	if (!g_lock.has_code)
	{
		generate_code(g_lock.code);
		g_lock.expires_at = time(NULL) + g_lock.validity_secs;
		g_lock.has_code = 1;
		return;
	}

	if (g_lock.expires_at > 0 && time(NULL) >= g_lock.expires_at)
	{
		rotate_code_and_notify("code expired");
	}
}

/* ------------------------- gestion des clients ------------------------- */
static void accept_new_client(int listen_fd, client_node_t **clients)
{
	struct sockaddr_in client_addr;
	socklen_t addrlen = sizeof(client_addr);
	int client_sock = accept(listen_fd, (struct sockaddr *)&client_addr, &addrlen);
	if (client_sock < 0)
	{
		perror("accept failed");
		return;
	}

	add_client(clients, client_sock, &client_addr);
	client_node_t temp = {.fd = client_sock, .addr = client_addr, .next = NULL};
	log_client_endpoint(&temp, "New client");
}

static void send_owner_welcome(client_node_t *node)
{
	g_lock.owner_fd = node->fd;
	strncpy(g_lock.owner_pseudo, node->pseudo, sizeof(g_lock.owner_pseudo) - 1);
	if (!g_lock.has_code)
	{
		generate_code(g_lock.code);
		g_lock.expires_at = time(NULL) + g_lock.validity_secs;
		g_lock.has_code = 1;
	}
	char welcome[128];
	snprintf(welcome, sizeof(welcome), "WELCOME %s CODE %s VALIDITY %d\n",
	         node->pseudo, g_lock.code, remaining_validity_seconds());
	send(node->fd, welcome, strlen(welcome), 0);
}

static void send_tenant_welcome(client_node_t *node)
{
	node->attempts = 0;
	ensure_code_fresh();
	char msg[160];
	snprintf(msg, sizeof(msg), "CURRENT CODE %s VALIDITY %d\nENTER CODE\n",
	         g_lock.code, remaining_validity_seconds());
	send(node->fd, msg, strlen(msg), 0);
}

static int handle_initial_ident(client_node_t **clients, client_node_t *node, const char *msg)
{
	if (strncmp(msg, "OWNER ", 6) == 0)
	{
		node->role = ROLE_OWNER;
		strncpy(node->pseudo, msg + 6, sizeof(node->pseudo) - 1);
		node->pseudo[sizeof(node->pseudo) - 1] = '\0';
		if (!pseudo_allowed(node->role, node->pseudo))
		{
			const char *err = "ERR unauthorized pseudo\n";
			send(node->fd, err, strlen(err), 0);
			remove_client(clients, node);
			return 1;
		}
		send_owner_welcome(node);
		return 0;
	}

	if (strncmp(msg, "TENANT ", 7) == 0)
	{
		node->role = ROLE_TENANT;
		strncpy(node->pseudo, msg + 7, sizeof(node->pseudo) - 1);
		node->pseudo[sizeof(node->pseudo) - 1] = '\0';
		if (!pseudo_allowed(node->role, node->pseudo))
		{
			const char *err = "ERR unauthorized pseudo\n";
			send(node->fd, err, strlen(err), 0);
			remove_client(clients, node);
			return 1;
		}
		send_tenant_welcome(node);
		return 0;
	}

	const char *err = "ERR specify OWNER <pseudo> or TENANT <pseudo>\n";
	send(node->fd, err, strlen(err), 0);
	return 0;
}

static int handle_owner_command(client_node_t **clients, client_node_t *node, const char *msg)
{
	if (strncmp(msg, "SET CODE ", 9) == 0)
	{
		const char *newcode = msg + 9;
		if (!is_six_digits(newcode))
		{
			const char *err = "ERR code must be 6 digits\n";
			send(node->fd, err, strlen(err), 0);
			return 0;
		}
		strncpy(g_lock.code, newcode, sizeof(g_lock.code));
		g_lock.expires_at = time(NULL) + g_lock.validity_secs;
		g_lock.has_code = 1;
		char resp[128];
		snprintf(resp, sizeof(resp), "OK CODE %s VALIDITY %d\n",
		         g_lock.code, g_lock.validity_secs);
		send(node->fd, resp, strlen(resp), 0);
		return 0;
	}

	if (strncmp(msg, "SET VALIDITY ", 13) == 0)
	{
		const char *val = msg + 13;
		int seconds = atoi(val);
		if (seconds <= 0)
		{
			const char *err = "ERR validity must be > 0\n";
			send(node->fd, err, strlen(err), 0);
			return 0;
		}
		g_lock.validity_secs = seconds;
		g_lock.expires_at = time(NULL) + g_lock.validity_secs;
		char resp[128];
		snprintf(resp, sizeof(resp), "OK CODE %s VALIDITY %d\n",
		         g_lock.code, g_lock.validity_secs);
		send(node->fd, resp, strlen(resp), 0);
		return 0;
	}

	if (strcmp(msg, "SHOW") == 0)
	{
		char resp[128];
		snprintf(resp, sizeof(resp), "OK CODE %s VALIDITY %d\n",
		         g_lock.code, remaining_validity_seconds());
		send(node->fd, resp, strlen(resp), 0);
		return 0;
	}

	if (strcmp(msg, "QUIT") == 0)
	{
		const char *bye = "BYE\n";
		send(node->fd, bye, strlen(bye), 0);
		remove_client(clients, node);
		return 1;
	}

	const char *err = "ERR unknown command\n";
	send(node->fd, err, strlen(err), 0);
	return 0;
}

static int handle_tenant_command(client_node_t *node, const char *msg)
{
	if (!g_lock.has_code)
	{
		const char *err = "ERR no code available\n";
		send(node->fd, err, strlen(err), 0);
		return 0;
	}

	time_t now = time(NULL);
	if (g_lock.has_code && g_lock.expires_at > 0 && now >= g_lock.expires_at)
	{
		rotate_code_and_notify("code expired");
		const char *expired = "ERR CODE EXPIRED\n";
		send(node->fd, expired, strlen(expired), 0);
		log_history(node->pseudo, "code expired");
		return 0;
	}

	if (!is_six_digits(msg))
	{
		const char *err = "ERR code must be 6 digits\n";
		send(node->fd, err, strlen(err), 0);
		return 0;
	}

	if (strcmp(msg, g_lock.code) == 0)
	{
		const char *ok = "ACCESS GRANTED\n";
		send(node->fd, ok, strlen(ok), 0);
		log_history(node->pseudo, "success");
		node->attempts = 0;
		return 0;
	}

	node->attempts += 1;
	if (node->attempts >= 3)
	{
		log_history(node->pseudo, "alarm triggered");
		rotate_code_and_notify("alarm");
		const char *alarm = "ALARM TRIGGERED\n";
		send(node->fd, alarm, strlen(alarm), 0);
		node->attempts = 0;
		return 0;
	}

	char err[64];
	snprintf(err, sizeof(err), "INVALID CODE (%d/3)\n", node->attempts);
	send(node->fd, err, strlen(err), 0);
	log_history(node->pseudo, "failed attempt");
	return 0;
}

static int handle_client_message(client_node_t **clients, client_node_t *node, const char *msg)
{
	if (node->role == ROLE_UNKNOWN)
	{
		return handle_initial_ident(clients, node, msg);
	}

	if (node->role == ROLE_OWNER)
	{
		return handle_owner_command(clients, node, msg);
	}

	if (node->role == ROLE_TENANT)
	{
		return handle_tenant_command(node, msg);
	}
	return 0;
}

static void process_client_data(client_node_t **clients, client_node_t *node, const char *msg)
{
	printf("Received from client fd=%d [%s:%u]: %s \n",
	       node->fd,
	       inet_ntoa(node->addr.sin_addr),
	       ntohs(node->addr.sin_port),
	       msg);
	fflush(stdout);

	int should_remove = handle_client_message(clients, node, msg);
	if (should_remove)
	{
		// already removed inside handler
		return;
	}
}

static void handle_client_event(client_node_t **clients, client_node_t *node, short revents)
{
	char client_message[MSG_LEN];

	if (revents & POLLIN)
	{
		ssize_t bytes = recv(node->fd, client_message, MSG_LEN - 1, 0);
		if (bytes > 0)
		{
			client_message[bytes] = '\0';
			trim_newline(client_message);
			process_client_data(clients, node, client_message);
			return;
		}

		if (bytes == 0)
		{
			log_client_endpoint(node, "Client disconnected");
			remove_client(clients, node);
			return;
		}

		perror("recv failed");
		remove_client(clients, node);
		return;
	}

	if (revents & (POLLHUP | POLLERR | POLLNVAL))
	{
		log_client_endpoint(node, "Client disconnected (poll event)");
		remove_client(clients, node);
	}
}

static void poll_loop(int socket_desc, client_node_t **clients)
{
	while (1)
	{
		size_t count = client_count(*clients) + 1; // +1 for listening socket
		struct pollfd *pfds = calloc(count, sizeof(struct pollfd));
		client_node_t **nodes = calloc(count, sizeof(client_node_t *));
		if (!pfds || !nodes)
		{
			perror("calloc");
			free(pfds);
			free(nodes);
			break;
		}

		pfds[0].fd = socket_desc;
		pfds[0].events = POLLIN;

		size_t idx = 1;
		for (client_node_t *node = *clients; node != NULL; node = node->next)
		{
			pfds[idx].fd = node->fd;
			pfds[idx].events = POLLIN;
			nodes[idx] = node;
			++idx;
		}

		int ready = poll(pfds, count, -1);
		if (ready < 0)
		{
			if (errno == EINTR)
			{
				free(pfds);
				free(nodes);
				continue;
			}
			perror("poll failed");
			free(pfds);
			free(nodes);
			break;
		}

		if (g_lock.has_code && g_lock.expires_at > 0 && time(NULL) >= g_lock.expires_at)
		{
			rotate_code_and_notify("code expired");
		}

		if (pfds[0].revents & POLLIN)
		{
			accept_new_client(socket_desc, clients);
		}

		for (size_t i = 1; i < count; ++i)
		{
			client_node_t *node = nodes[i];
			if (!node) continue;
			handle_client_event(clients, node, pfds[i].revents);
		}

		free(pfds);
		free(nodes);
	}
}

int main(int argc , char *argv[])
{

	client_node_t *clients = NULL;
	uint16_t port = 0;

	srand((unsigned int)time(NULL));

	if (parse_port_arg(argc, argv, &port) < 0)
	{
		return 1;
	}

	if (db_init() != 0)
	{
		return 1;
	}

	int socket_desc = create_listen_socket(port);
	if (socket_desc < 0)
	{
		db_close();
		return 1;
	}

	poll_loop(socket_desc, &clients);
	
	while (clients)
	{
		client_node_t *tmp = clients;
		clients = clients->next;
		close(tmp->fd);
		free(tmp);
	}

	close(socket_desc);
	db_close();
	
	return 0;
}
