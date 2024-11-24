#include "client.h"

static SOCKET sockfd = INVALID_SOCKET;
static pthread_t threadlisten_tid = 0;
static pthread_t threadmain_tid = 0;
static atomic_bool program_closing = false;
static SSL *ssl = NULL;
static SSL_CTX *ctx = NULL;
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
	dbg_print("PID: %d\n", getpid());
	printf("Attempting to connect to %s:%s\n", argv[1], port);

	init_SSL();
	if (open_socket(argv[1], port) != 0) {
		fprintf(stderr, "Closing client.\n");
		exit(1);
	}
	connect_SSL();
	// TODO: normalizar el manejo de errores. Cerrar adecuadamente en cada caso.

	set_mainthread_signals();

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
		sleep_ms(500);
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

void set_mainthread_signals(void)
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

	sigaddset(&sa.sa_mask, SIGUSR1);
	sigaddset(&sa.sa_mask, SIGUSR2);
	if(pthread_sigmask(SIG_BLOCK, &sa.sa_mask, NULL) != 0) {
		perror("pthread_sigmask");
		exit(1);
	}

#endif
	atexit(closing_procedure);
}

void set_listenthread_signals(void)
{
#ifdef _WIN32
	// ???
#else
	// Block all signals from listen thread, receive everything from main thread
	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGTERM);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGPIPE);
	sigaddset(&sigset, SIGUSR1);
	sigaddset(&sigset, SIGUSR2);

	if(pthread_sigmask(SIG_BLOCK, &sigset, NULL) != 0) {
		perror("pthread_sigmask");
		exit(1);
	}
#endif
}

void connect_SSL(void)
{
	ssl = SSL_new(ctx);
	if(ssl == NULL)
		exit(EXIT_FAILURE);

	if(SSL_set_fd(ssl, sockfd) != 1)
		exit(EXIT_FAILURE);

	if (SSL_connect(ssl) <= 0)
	{
		ERR_print_errors_fp(stderr);
		exit(1);
	}
	else
	{
		printf("Connected to server via SSL.\n");
		/* For the transparent negotiation to succeed, the ssl must have been 
		initialized to client or server mode. This is being done by calling 
		SSL_set_connect_state(3) or SSL_set_accept_state() before the first 
		call to a write function.*/
	}
}

void init_SSL(void)
{
	SSL_library_init();
	SSL_load_error_strings();

	const SSL_METHOD *method = SSLv23_client_method();
	ctx = SSL_CTX_new(method);
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
		//	nbytes = send(sockfd, buf, len + 1, 0);
			nbytes = SSL_write(ssl, buf, len+1);
			if (nbytes < 1) {
				// See SSL_get_error()
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
			if(errno == 0 || (errno == EINTR && atomic_load(&program_closing))) {
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
	if(pthread_create(&id, NULL, func, data) != 0) {
		perror("pthread_create");
		exit(1);
	}
	return id;
#endif
}

static THREAD_RET_T STDCALL thread_listen(void* data)
{
	(void)data;
	char buf[256];
	const TXRX_SZ buffsize = sizeof(buf);
	ssize_t nbytes;

	set_listenthread_signals();

	while (1)
	{
	//	nbytes = recv(sockfd, buf, buffsize, 0);
		nbytes = SSL_read(ssl, buf, buffsize);
		if (atomic_load(&program_closing)) {
			break;
		}
		if (nbytes < 1) {
			int error = SSL_get_error(ssl, nbytes);

			if (error == SSL_ERROR_ZERO_RETURN) {
                fputs("\rConnection closed by host.\n", stderr);
				threadlisten_exit(0,NULL);
                break;
            } else if (error == SSL_ERROR_SYSCALL) {
                dbg_print("SSL_read interrupted by signal.\n");
                break;
            } else {
                ERR_print_errors_fp(stderr);
				threadlisten_exit(0,NULL);
                break;
            }
			/* TODO: test errors in Windows */
		/*	if (nbytes == -1) {
			#ifdef _WIN32
				int error = WSAGetLastError();
				if(!(error == WSAESHUTDOWN || error == WSAEINTR))
					fprintf(stderr, "\rrecv: %d\n", error);
			#else
				perror("\rrecv");
			#endif
				continue;
			} */
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
	write(STDOUT_FILENO, "\n", 1);
	pthread_t self_tid = pthread_self();	
	atomic_store(&program_closing, true);

	// TODO: can't call printf() after exit() safely. Replace with write()

	if(ssl)
	{
		dbg_print("Closing SSL... ");

		/* To finish the connection properly, we send a "close notify" alert to
		the server. In most cases, we have to wait for the same message from the
		server, and perform the call again. */
		int ret = SSL_shutdown(ssl);
		if (ret < 0) {
			ERR_print_errors_fp(stderr);
			_exit(EXIT_FAILURE);
		}
		else if (ret == 0) {
			sleep_ms(100);
			if (SSL_shutdown(ssl) != 1) {
				dbg_print("[ NOK ]\n");
			}
			else {
				dbg_print("[ OK ]\n");
			}
		}else {
			dbg_print("[ OK ]\n");
		}

		SSL_free(ssl);
		ssl = NULL;
	}

	if(ctx)
	{
		dbg_print("Closing SSL Context... ");
		SSL_CTX_free(ctx);
		ctx = NULL;
		dbg_print("[ OK ]\n");
	}

	if(sockfd != INVALID_SOCKET)
	{
		dbg_print("Closing socket... ");
		shutdown(sockfd, SHUT_RDWR);
		closesocket(sockfd);
		sockfd = INVALID_SOCKET;
		dbg_print("[ OK ]\n");
	}

	if(threadlisten_tid != 0 && self_tid != threadlisten_tid)
	{
		dbg_print("Joining listen thread... ");
	#ifdef _WIN32
		WSACleanup();
		WaitForSingleObject( threadlisten_tid, 500 );
		CloseHandle( threadlisten_tid );
	#else
		pthread_join(threadlisten_tid, NULL);
	#endif
		threadlisten_tid = 0;
		dbg_print("[ OK ]\n");
	}
}

NORETURN void socket_disconnected(int signal)
{
	(void)signal; // SIGPIPE

	fputs("\nSocket disconnected\n", stderr);
	sleep_ms(500);
	exit(1);
}

NORETURN void exit_program(int signal)
{
	(void)signal;
	exit(0); // calling exit(0) produces further call to closing_procedure()
}

NORETURN static void threadlisten_exit(int status, const char *msg)
{
	atomic_store(&program_closing, true);
	if(msg)
		perror(msg);
	pthread_kill(threadmain_tid, SIGTERM);
	pthread_exit(NULL);
}

#ifndef _WIN32
void sleep_ms(int ms)
{
	struct timespec tim, rem;
	tim.tv_sec = ms / 1000;
	tim.tv_nsec = (ms % 1000) * 1E6;

	if(nanosleep(&tim , &rem) == -1)
	{
		if (errno == EINTR)
			sleep_ms(rem.tv_sec*1000 + rem.tv_nsec/1E6);
		
		else
			perror("nanosleep");
	}
}
#endif

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
