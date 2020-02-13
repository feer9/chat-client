#include "client.h"

static SOCKET sockfd = INVALID_SOCKET;
static pthread_t threadlisten_id = 0;
static int program_closing = 0;
#ifdef _WIN32
  static HANDLE hStdout = NULL, hStdin = NULL;
#endif

int main(int argc, char* argv[])
{
	char port[10] = PORT_DEFAULT_STR;

#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
//	printf("locale: %s\n", setlocale(LC_ALL, ".utf8"));

	hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	hStdin  = GetStdHandle(STD_INPUT_HANDLE);
#endif

	if (argc < 2) {
		printf("Usage: %s [host] [port]\n", argv[0]);
		exit(1);
	}
	if (argc > 2) {
		strncpy(port, argv[2], sizeof port - 1);
	}

	print_version();
	printf("Attempting to connect to %s:%s\n", argv[1], port);

	if (open_socket(argv[1], port) != 0) {
		fprintf(stderr, "Closing client.\n");
		exit(1);
	}

	set_signals();

	puts("Type \"/quit\" or ^C to close this program.");

	threadlisten_id = start_thread(thread_listen, &sockfd);

	console();

	return 0;
}

#ifdef _WIN32
static BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
{
	switch (fdwCtrlType)
	{
		// Handle the CTRL-C signal.
	case CTRL_C_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		exit(0); // then exit() calls closing_procedure()

		// CTRL-CLOSE: cmd windows closing
	case CTRL_CLOSE_EVENT:
		closing_procedure();
		return TRUE;

		// Pass other signals to the next handler.
	case CTRL_BREAK_EVENT:
		printf("\rCtrl-Break event\n\n");
		sleep(1);
		return FALSE;

	default:
		return FALSE;
	}
}
#endif

void set_signals(void)
{
#ifdef _WIN32
	SetConsoleCtrlHandler(CtrlHandler, TRUE);
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
	atexit(closing_procedure);
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
		printf("WSAStartup failed: %d\n", errcode);
		return errcode;
	}
#endif

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	errcode = getaddrinfo(address, port, &hints, &result);
	if (errcode != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerrorA(errcode));
		return errcode;
	}

	for (rp = result; rp != NULL; rp = rp->ai_next)
	{
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd == INVALID_SOCKET)
			continue;

		if ((errcode = connect(fd, rp->ai_addr, (int)rp->ai_addrlen)) != SOCKET_ERROR)
			break;  /* success */

		closesocket(fd);
	}

	if (rp == NULL) {  // couldn't find address
		fprintf(stderr, "Could not connect.\n");
		return 1;
	}

	sockfd = fd; // set file descriptor as global

	printf("Connected to %s (%s)\n", address, inet_ntoa(((struct sockaddr_in*)rp->ai_addr)->sin_addr));

	freeaddrinfo(result);

	return errcode;
}

static void console(void)
{
	char buf[192] = { 0 };
	ssize_t nbytes;
	TXRX_SZ len;

	fputs("-> ", stdout);
	fflush(stdout);

	while (strncmp(buf, "/quit", 6) != 0)
	{
		len = (TXRX_SZ) getLine(buf, sizeof buf);

		if (buf[0] == '/')
		{
			char* cmd = &buf[1];

			if (strncmp(cmd, "help", 5) == 0)
				print_help();

			else if (strncmp(cmd, "quit", 5) == 0)
				break;

			else
				fputs("Unknown command. Type \"/help\" for more info.\n", stdout);

		}
		else
		{
			nbytes = send(sockfd, buf, len + 1, 0);
			if (nbytes == -1) {
				perror("write");
				exit(1);
			}
		}
		fputs("-> ", stdout);
		fflush(stdout);
	}
}

static pthread_t start_thread(THREAD_RET_T(*func)(void*), void* data)
{
#ifdef _WIN32
//	_configthreadlocale(_ENABLE_PER_THREAD_LOCALE);

	return (HANDLE) _beginthreadex(NULL, 0, func, data, 0, NULL);
#else
	pthread_t id;
	//	pthread_attr_t thread_attr;

	//	pthread_attr_init(&thread_attr);
	//	pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);

	//	pthread_create(&id, &thread_attr, thread_listen, data);
	pthread_create(&id, NULL, func, data);
	//  pthread_detach(id);

	//	pthread_attr_destroy(&thread_attr);

	return id;
#endif
}

static THREAD_RET_T STDCALL thread_listen(void* data)
{
	//	SOCKET sockfd = *(SOCKET*)data;
	(void)data;
	char buf[256];
	const TXRX_SZ buffsize = sizeof(buf);
	ssize_t nbytes;

	while (1)
	{
		nbytes = recv(sockfd, buf, buffsize, 0);
		if (nbytes < 1) {

			if (program_closing) {
				break;
			}
			if (nbytes == -1) {

#ifdef _WIN32
				int error = WSAGetLastError();
				if(!(error == WSAESHUTDOWN || error == WSAEINTR))
					fprintf(stderr, "\rrecv: %d\n", error);
#else
				perror("\rrecv");
#endif
				continue;
			}
			else if (nbytes == 0) {

				fputs("\rConnection lost.\n", stderr);
				sleep(1);
				exit(0);
			}
		}

		//	printf("\033[3D%s\n-> ", buf); // Move left X column;
		printf("\r%s\n-> ", buf);
		fflush(stdout);
	}

#ifdef _WIN32
	_endthreadex(0);
#else
	pthread_exit(NULL);
#endif
	return 0; // remove some warning, get another, meh
}


// returns the number of BYTES read
size_t getLine(char* buf, int buf_sz)
{
	size_t size_rd = 0;
#ifdef _WIN32
	static wchar_t wstr[128] = { 0 };
	unsigned long len;
	int ret;

	ReadConsoleW(hStdin, wstr, 128, &len, NULL);
	ret = WideCharToMultiByte(CP_UTF8, 0, wstr, (int)len, buf, buf_sz, NULL, NULL);
	if(ret > 0)
	{
		size_rd = (size_t) ret;
		if (size_rd > 1) {
			size_rd -= 2;
			buf[size_rd] = '\0';
		}
	}
	else {
		buf[0] = '\0';
	}
#else

	fgets(buf, buf_sz, stdin);
	size_rd = strlen(buf);
	if (size_rd > 0)
		buf[--size_rd] = '\0';

#endif

	return size_rd;
}

NORETURN void socket_disconnected(int signal)
{
	(void)signal; // SIGPIPE

	fputs("\nSocket disconnected\n", stderr);
	sleep(1);
	exit(1);
}

void closing_procedure(void)
{
	putchar('\n');

	program_closing = 1;

#ifdef _WIN32
	shutdown(sockfd, SD_BOTH);
#else
	shutdown(sockfd, SHUT_RDWR);
#endif

	closesocket(sockfd);
	sockfd = INVALID_SOCKET;

#ifdef _WIN32
	WSACleanup();
	WaitForSingleObject( threadlisten_id, 500 );
	CloseHandle( threadlisten_id );
#else
	pthread_join(threadlisten_id, NULL);
#endif
}

NORETURN void exit_program(int signal)
{
	(void)signal;
	exit(0); // calling exit(0) produces further call to closing_procedure()
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

/*
https://stackoverflow.com/questions/421860/capture-characters-from-standard-input-without-waiting-for-enter-to-be-pressed
*/
