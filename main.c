#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

size_t getLine(char *buf, int buf_sz);
void *thread_listen(void *data);
void socket_disconnected(int signal);
void close_program(void);
void exit_program(int signal);
void print_help(void);
void print_version(void);


static int sockfd = 0;

void set_signals(void)
{
	struct sigaction sa;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = exit_program;
	if (sigaction(SIGTERM, &sa, NULL) == -1 ||
		sigaction(SIGINT,  &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}
	sa.sa_handler = socket_disconnected;
	if (sigaction(SIGPIPE, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}
	atexit(close_program);
}

int main(int argc, char *argv[])
{
	char port[10] = "27007";
	struct addrinfo hints, *result, *rp;
	int errcode;
	char buf[128];
	const size_t buffsize = sizeof(buf);
	ssize_t nbytes;
	size_t len;

	if(argc < 2) {
		printf("Usage: %s [host] [port]\n", argv[0]);
		exit(1);
	}
	if(argc == 3) {
		strncpy(port, argv[2], sizeof port - 1);
	}
	print_version();
	printf("Attempting to connect to %s:%s\n", argv[1], port);

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	errcode = getaddrinfo(argv[1], port, &hints, &result);
	if(errcode != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror((errcode)));
		exit(1);
	}

	for(rp = result; rp != NULL; rp = rp->ai_next)
	{
		sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if(sockfd == -1)
			continue;

		if(connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1)
			break;  /* success */

		close(sockfd);
	}

	if(rp == NULL) {  // no se encontró dirección
		fprintf(stderr, "Could not connect.\n");
		exit(1);
	}

	set_signals();

	printf("Connected to %s (%s)\n", argv[1], inet_ntoa(((struct sockaddr_in*)rp->ai_addr)->sin_addr));
	puts("Type \"/quit\" or ^C to close this program.");
	freeaddrinfo(result);


	pthread_t thread_id;
	pthread_create(&thread_id, NULL, thread_listen, (void *)&sockfd);
	pthread_detach(thread_id);

	fputs("-> ", stdout);
	while(strncmp(buf, "/quit", 6) != 0)
	{
		len = getLine(buf, buffsize);
		if(buf[0] == '/')
		{
			char *cmd = &buf[1];

			if(strncmp(cmd, "help",  5) == 0)
				print_help();
			
			else if(strncmp(cmd, "quit",  5) == 0)
				break;

			else
				fputs("Unknown command. Type \"/help\" for more info.\n", stdout);

		}
		else
		{
			nbytes = write(sockfd, buf, len+1);
			if(nbytes == -1) {
				perror("write");
				exit(1);
			}
		}
		fputs("-> ", stdout);
	}

	close(sockfd);
	return 0;
}

void *thread_listen(void *data)
{
	int sockfd = *(int*)data;
	char buf[128];
	const size_t buffsize = sizeof(buf);
	ssize_t nbytes;

	while(1)
	{
		nbytes = recv(sockfd, buf, buffsize, 0);
		if(nbytes == -1) {
			perror("read");
			exit(1);
		}
		else if(nbytes == 0) {
			fputs("\rConnection lost.\n", stderr);
			exit(1);
		}
	//	printf("\033[3D%s\n-> ", buf); // Move left X column;
		printf("\r%s\n-> ", buf);
		fflush(stdout);
	}
}

size_t getLine(char *buf, int buf_sz)
{
	fgets(buf, buf_sz, stdin);
	size_t len = strlen(buf);
	if(len>0)
		buf[--len] = '\0';

	return len;
}

void socket_disconnected(int signal)
{
	if(signal == SIGPIPE)
	{
		fputs("\nSocket disconnected\n", stderr);
		exit(1);
	}
}

void close_program(void)
{
	putchar('\n');
	close(sockfd);
}

__attribute__((__noreturn__))
void exit_program(int signal)
{
	(void)signal;
	exit(0); // calling exit(0) produces further call to close_program()
}

void print_help(void)
{
	print_version();
	puts("All commands start with a forward slash \"/\"");
	puts("");
	puts("Known commands:");
	puts("/help\tDisplay this message.");
	puts("/quit\tDisconnect from the server and exit.");
	puts("");
}

void print_version(void)
{
	puts("Chat client v0.1");
}