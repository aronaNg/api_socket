
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/socket.h>		//socket
#include <arpa/inet.h>		//inet_addr
#include <unistd.h>
#define MSG_LEN 1024


int main(int argc , char *argv[])
{
	int sock;
	int n;
	struct sockaddr_in adresse;
	char buff[MSG_LEN];
	
	if (argc != 3)
	{
		fprintf(stderr, "Usage: %s <server_ip> <server_port>\n", argv[0]);
		return 1;
	}
	
	//Create socket //////////////////////////////////////////////////////
	sock = socket(AF_INET , SOCK_STREAM , 0);
	if (sock == -1)
	{
		printf("Could not create socket");
	}
	puts("Socket created");
	
	
	memset(&adresse, 0, sizeof(adresse));
	
	
	adresse.sin_family = AF_INET;
	if (inet_aton(argv[1], &adresse.sin_addr) == 0)
	{
		fprintf(stderr, "Invalid IP address: %s\n", argv[1]);
		close(sock);
		return 1;
	}
	adresse.sin_port = htons((uint16_t)atoi(argv[2]));



	//Connect to remote server //////////////////////////////////////////////////////
	if (connect(sock , (struct sockaddr *)&adresse , sizeof(adresse)) < 0)
	{
		perror("connect failed. Error");
		return 1;
	}
	puts("Connected");
	
	while (1) {
		printf("Message to send (type 'quit' to exit): ");
		fflush(stdout);
		if (fgets(buff, sizeof(buff), stdin) == NULL)
		{
			break;
		}

		if (strncmp(buff, "quit", 4) == 0)
		{
			break;
		}

		n = send(sock, buff, strlen(buff), 0);
		if (n < 0)
		{
			perror("send failed");
			break;
		}

		n = recv(sock, buff, sizeof(buff) - 1, 0);
		if (n < 0)
		{
			perror("recv failed");
			break;
		}
		else if (n == 0)
		{
			printf("Server closed the connection\n");
			break;
		}

		buff[n] = '\0';
		printf("Server reply: %s", buff);
	}

	close(sock);
	
	
	return 0;
}