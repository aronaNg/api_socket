
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<errno.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<poll.h>

#define MSG_LEN 1024
#define BACKLOG 16

typedef struct client_node {
	int fd;
	struct sockaddr_in addr;
	struct client_node *next;
} client_node_t;

static void add_client(client_node_t **head, int fd, const struct sockaddr_in *addr)
{
	client_node_t *node = malloc(sizeof(client_node_t));
	if (!node)
	{
		perror("malloc");
		close(fd);
		return;
	}
	node->fd = fd;
	node->addr = *addr;
	node->next = *head;
	*head = node;
}

static void remove_client(client_node_t **head, client_node_t *target)
{
	if (!target)
	{
		return;
	}

	client_node_t **cursor = head;
	while (*cursor)
	{
		if (*cursor == target)
		{
			client_node_t *tmp = *cursor;
			*cursor = tmp->next;
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
	while (head)
	{
		++count;
		head = head->next;
	}
	return count;
}

static void log_client_endpoint(const client_node_t *node, const char *prefix)
{
	char ip[INET_ADDRSTRLEN] = {0};
	inet_ntop(AF_INET, &(node->addr.sin_addr), ip, sizeof(ip));
	printf("%s %s:%u\n", prefix, ip, ntohs(node->addr.sin_port));
	fflush(stdout);
}

int main(int argc , char *argv[])
{

	int socket_desc;
	struct sockaddr_in server;
	char *endptr = NULL;
	long port;
	client_node_t *clients = NULL;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s <server_port>\n", argv[0]);
		return 1;
	}

	errno = 0;
	port = strtol(argv[1], &endptr, 10);
	if (errno != 0 || endptr == argv[1] || port <= 0 || port > 65535)
	{
		fprintf(stderr, "Invalid port: %s\n", argv[1]);
		return 1;
	}

	//Create socket
	socket_desc = socket(AF_INET , SOCK_STREAM , 0);
	if (socket_desc == -1)
	{
		perror("Could not create socket");
		return 1;
	}
	puts("Socket created");

	//Allow quick reuse of the address if the server restarts quickly
	int optval = 1;
	if (setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
	{
		perror("setsockopt SO_REUSEADDR failed");
		close(socket_desc);
		return 1;
	}
	
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	server.sin_port = htons((uint16_t)port);
	
	if( bind(socket_desc,(struct sockaddr *)&server , sizeof(server)) < 0)
	{
		perror("bind failed. Error");
		close(socket_desc);
		return 1;
	}
	puts("bind done");
	
	if (listen(socket_desc , BACKLOG) < 0)
	{
		perror("listen failed");
		close(socket_desc);
		return 1;
	}
	
	puts("Waiting for incoming connections...");

	while (1) 
	{
		size_t count = client_count(clients) + 1; // +1 for listening socket
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
		for (client_node_t *node = clients; node != NULL; node = node->next)
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

		//New connection
		if (pfds[0].revents & POLLIN)
		{
			struct sockaddr_in client_addr;
			socklen_t addrlen = sizeof(client_addr);
			int client_sock = accept(socket_desc, (struct sockaddr *)&client_addr, &addrlen);
			if (client_sock < 0)
			{
				perror("accept failed");
			}
			else
			{
				add_client(&clients, client_sock, &client_addr);
				client_node_t temp = {.fd = client_sock, .addr = client_addr, .next = NULL};
				log_client_endpoint(&temp, "New client");
			}
		}

		//Handle client data
		char client_message[MSG_LEN];
		for (size_t i = 1; i < count; ++i)
		{
			client_node_t *node = nodes[i];
			if (!node)
			{
				continue;
			}

			short revents = pfds[i].revents;
			if (revents & POLLIN)
			{
				ssize_t bytes = recv(node->fd, client_message, MSG_LEN - 1, 0);
				if (bytes > 0)
				{
					client_message[bytes] = '\0';
					printf("Received from client [%s:%u]: %s\n",
					       inet_ntoa(node->addr.sin_addr),
					       ntohs(node->addr.sin_port),
					       client_message);
					fflush(stdout);

					ssize_t sent = send(node->fd, client_message, (size_t)bytes, 0);
					if (sent < 0)
					{
						perror("send failed");
						remove_client(&clients, node);
					}
				}
				else if (bytes == 0)
				{
					log_client_endpoint(node, "Client disconnected");
					remove_client(&clients, node);
				}
				else
				{
					perror("recv failed");
					remove_client(&clients, node);
				}
			}
			else if (revents & (POLLHUP | POLLERR | POLLNVAL))
			{
				log_client_endpoint(node, "Client disconnected (poll event)");
				remove_client(&clients, node);
			}
		}

		free(pfds);
		free(nodes);
	}	
	
	while (clients)
	{
		client_node_t *tmp = clients;
		clients = clients->next;
		close(tmp->fd);
		free(tmp);
	}

	close(socket_desc);
	
	return 0;
}