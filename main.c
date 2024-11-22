#include "client.h"

static SOCKET sockfd = INVALID_SOCKET;
static pthread_t threadlisten_tid = 0;
static pthread_t threadmain_tid = 0;
static atomic_int program_closing = 0;
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

	threadmain_tid = pthread_self();
	threadlisten_tid = start_thread(thread_listen, &sockfd);

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

#ifndef _WIN32
static void set_signal_handler(int signum, struct sigaction *sa)
{
	if (sigaction(signum, sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}
}
#endif

void set_signals(void)
{
#ifdef _WIN32
	SetConsoleCtrlHandler(CtrlHandler, TRUE);
#else
	struct sigaction sa = {0};
	sigemptyset(&sa.sa_mask);

	sa.sa_flags = 0;
//	sa.sa_flags |= SA_RESTART;
	/* Este flag hace que cuando se recibe una interrupción     */
	/* la syscall no retorne error, sino q vuelva a ejecutarse. */

	sa.sa_handler = exit_program;
	set_signal_handler(SIGTERM, &sa);
	set_signal_handler(SIGINT,  &sa);

	sa.sa_handler = socket_disconnected;
	set_signal_handler(SIGPIPE,  &sa);

	sa.sa_handler = SIG_IGN;
	set_signal_handler(SIGUSR1,  &sa);
	set_signal_handler(SIGUSR2,  &sa);
#endif
	atexit(closing_procedure);
}

void cosas_ssl(void)
{
	SSL_library_init();
	SSL_load_error_strings();
	const SSL_METHOD *method = SSLv23_client_method();
	SSL_CTX *ctx = SSL_CTX_new(method);
	SSL *ssl = SSL_new(ctx);
	SSL_set_fd(ssl, sockfd);
	if (SSL_connect(ssl) <= 0)
	{
		ERR_print_errors_fp(stderr);
	}
	else
	{
		printf("Connected to server via SSL.\n");
		SSL_write(ssl, "Hello, server!", strlen("Hello, server!"));
		char buffer[256] = {0};
		SSL_read(ssl, buffer, sizeof(buffer));
		printf("Received: %s\n", buffer);
	}
	SSL_shutdown(ssl);
	SSL_free(ssl);
	SSL_CTX_free(ctx);
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

	printf("Connected to %s (%s)\n", address, 
	       inet_ntoa(((struct sockaddr_in*)rp->ai_addr)->sin_addr));

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

			else if (strncmp(cmd, "users", 6) == 0)
				print_help(); // TODO: implement

			else
				fputs("Unknown command. Type \"/help\" for more info.\n", stdout);

		}
		else if (len > 0)
		{
			nbytes = send(sockfd, buf, len + 1, 0);
			if (nbytes == -1) {
				perror("send");
				exit(1);
			}
		}
		else {
			printf("\033[A\033[D"); /* move cursor up & scroll up one line */
		}
		
		fputs("\r-> \033[K", stdout);
		fflush(stdout);
	}
}

// returns the number of BYTES read
size_t getLine(char* buf, int buf_sz)
{
	size_t size_rd = 0;
	buf[0] = '\0';
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
		buf[0] = '\0'; // redundante?
	}

#else
	char *ret;
	do {
		ret = fgets(buf, buf_sz, stdin);
		if(ret == NULL) {
			if(errno == 0 || (errno == EINTR && program_closing)) {
				exit(0);
			}
			if(errno != EINTR) {
				perror("fgets");
				exit(1);
			}
		}
	}
	while(ret == NULL);

	size_rd = strnlen(buf, buf_sz);
	if (size_rd > 0) {
		buf[--size_rd] = '\0'; // clear new line char
	}
#endif

	return size_rd;
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
	(void)data;
	char buf[256];
	const TXRX_SZ buffsize = sizeof(buf);
	ssize_t nbytes;

	while (1)
	{
		nbytes = recv(sockfd, buf, buffsize, 0);
		if (program_closing) {
			break;
		}
		if (nbytes < 1) {

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
				threadlisten_exit(0,NULL);
			}
		}

		printf("\r%s\n-> ", buf);
		fflush(stdout);
	}

#ifdef _WIN32
	_endthreadex(0);
#else
	pthread_exit(NULL);
#endif
	return 0; // remove some warning, get another... meh
}


void closing_procedure(void)
{
	putchar('\n');
	pthread_t self_tid = pthread_self();
	dbg_print("in closing_procedure() from %s thread.\n", 
	          self_tid == threadmain_tid ? "main":"listen");
	
	program_closing = 1;

	if(sockfd != INVALID_SOCKET)
	{
	#ifdef _WIN32
		shutdown(sockfd, SD_BOTH);
	#else
		shutdown(sockfd, SHUT_RDWR);
	#endif

		closesocket(sockfd);
		sockfd = INVALID_SOCKET;
	}

	if(threadlisten_tid != 0 && self_tid != threadlisten_tid)
	{
	#ifdef _WIN32
		WSACleanup();
		WaitForSingleObject( threadlisten_tid, 500 );
		CloseHandle( threadlisten_tid );
	#else
		pthread_join(threadlisten_tid, NULL);
	#endif
		threadlisten_tid = 0;
	}
}

NORETURN void socket_disconnected(int signal)
{
	(void)signal; // SIGPIPE

	fputs("\nSocket disconnected\n", stderr);
	sleep(1);
	exit(1);
}

NORETURN void exit_program(int signal)
{
	(void)signal;
	exit(0); // calling exit(0) produces further call to closing_procedure()
}

NORETURN static void threadlisten_exit(int status, const char *msg)
{
	program_closing = 1;
	dbg_print("in threadlisten_exit()\n");
	if(msg)
		perror(msg);
	pthread_kill(threadmain_tid, SIGTERM);
	pthread_exit(NULL);
}

void print_help(void)
{
	print_version();
	puts("All commands start with a forward slash \"/\"");
	puts("");
	puts("Available commands:");
	puts("\t/help\tDisplay this message.");
	puts("\t/users\tShow info about connected users.");
	puts("\t/quit\tDisconnect from the server and exit.");
	puts("");
}

void print_version(void)
{
	puts("Chat client v0.1");
}

/*
https://stackoverflow.com/questions/421860/capture-characters-from-standard-input-without-waiting-for-enter-to-be-pressed
*/
