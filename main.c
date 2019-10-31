#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else /* unix */
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32 /* unix */
#define SOCKET int
#define INVALID_SOCKET	-1
#define closesocket(sockfd) close(sockfd)
#endif


#define PORT_DEFAULT_STR "27007"

size_t getLine(char *buf, int buf_sz);
void socket_disconnected(int signal);
void close_program(void);
void exit_program(int signal);
void print_help(void);
void print_version(void);
int open_socket(const char *address, const char *port);
void set_signals(void);
static pthread_t start_thread(void *(* func)(void *), void *data);
static void console(void);
static void *thread_listen(void *data);

static SOCKET sockfd = INVALID_SOCKET;
static pthread_t thread_id = 0;


int main(int argc, char *argv[])
{
	char port[10] = PORT_DEFAULT_STR;


	if(argc < 2) {
		printf("Usage: %s [host] [port]\n", argv[0]);
		exit(1);
	}
	if(argc > 2) {
		strncpy(port, argv[2], sizeof port - 1);
	}

	print_version();
	printf("Attempting to connect to %s:%s\n", argv[1], port);

	if(open_socket(argv[1], port) != 0) {
		fprintf(stderr, "Closing client.\n");
		exit(1);
	}

	set_signals();

	puts("Type \"/quit\" or ^C to close this program.");

	thread_id = start_thread(thread_listen, &sockfd);

	console();

	return 0;
}

void set_signals(void)
{
#ifdef _WIN32
	signal(SIGTERM, exit_program);
	signal(SIGINT, exit_program);
#else
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
#endif
	atexit(close_program);
}

int open_socket(const char *address, const char *port)
{
	struct addrinfo hints, *result, *rp;
	int errcode = 0;
	SOCKET fd = INVALID_SOCKET;

#ifdef _WIN32
	WSADATA wsaData = {0};

	// Initialize Winsock
	errcode = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (errcode != 0) {
		wprintf(L"WSAStartup failed: %d\n", errcode);
		return errcode;
	}
#endif

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	errcode = getaddrinfo(address, port, &hints, &result);
	if(errcode != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerrorA(errcode));
		return errcode;
	}

	for(rp = result; rp != NULL; rp = rp->ai_next)
	{
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if(fd == INVALID_SOCKET)
			continue;
		
		if((errcode = connect(fd, rp->ai_addr, (int) rp->ai_addrlen)) != SOCKET_ERROR)
			break;  /* success */

		closesocket(fd);
	}

	if(rp == NULL) {  // couldn't find address
		fprintf(stderr, "Could not connect.\n");
		return 1;
	}

	sockfd = fd; // set file descriptor as global

	printf("Connected to %s (%s)\n", address, inet_ntoa(((struct sockaddr_in*)rp->ai_addr)->sin_addr));

	freeaddrinfo(result);

	return errcode;
}

static pthread_t start_thread(void *(* func)(void *), void *data)
{
	pthread_t id;
//	pthread_attr_t thread_attr;

//	pthread_attr_init(&thread_attr);
//	pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);

//	pthread_create(&id, &thread_attr, thread_listen, data);
	pthread_create(&id, NULL, func, data);
//  pthread_detach(id);

//	pthread_attr_destroy(&thread_attr);

	return id;
}

static void console(void)
{
	char buf[128];
	ssize_t nbytes;
	size_t len;

	fputs("-> ", stdout);
	while(strncmp(buf, "/quit", 6) != 0)
	{
		len = getLine(buf, sizeof buf);
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
			nbytes = send(sockfd, buf, len+1, 0);
			if(nbytes == -1) {
				perror("write");
				exit(1);
			}
		}
		fputs("-> ", stdout);
	}
}

static void *thread_listen(void *data)
{
//	SOCKET sockfd = *(SOCKET*)data;
	(void)data;
	char buf[128];
	const size_t buffsize = sizeof(buf);
	ssize_t nbytes;

	while(1)
	{
		nbytes = recv(sockfd, buf, buffsize, 0);
		if(nbytes == -1) {
#ifdef _WIN32
			int error = WSAGetLastError();
		//	if(!(error == WSAESHUTDOWN || error == WSAEINTR))
				fprintf(stderr, "recv: %d\n", error);
#else
			perror("read");
#endif
			pthread_exit(NULL);
		}
		else if(nbytes == 0) {
			fputs("\rConnection lost.\n", stderr);
			exit(1); // see what to do with this
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

__attribute__((__noreturn__))
void socket_disconnected(int signal)
{
	(void)signal; // SIGPIPE

	fputs("\nSocket disconnected\n", stderr);
	exit(1);
}

void close_program(void)
{
	putchar('\n');

#ifdef _WIN32
	shutdown(sockfd, SD_BOTH);
#endif
	closesocket(sockfd);
	sockfd = INVALID_SOCKET;
#ifdef _WIN32
	WSACleanup();
#endif
	pthread_join(thread_id, NULL);
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
