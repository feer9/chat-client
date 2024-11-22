#ifndef _CLIENT_H
#define _CLIENT_H

#ifdef _WIN32
  #include <WinSock2.h>
  #include <WS2tcpip.h>
  #include <process.h>
  #include <Windows.h>
#else /* unix */
  #include <unistd.h>
  #include <pthread.h>
  #include <sys/socket.h>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <signal.h>
  #include <errno.h>
#endif

#include <sys/types.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
//#include <locale.h>
#include <openssl/ssl.h>
#include <openssl/err.h>


#if defined(__GNUC__) || defined(__MINGW32__) || defined(__MINGW64__)
#define NORETURN __attribute__((__noreturn__))
#elif defined(_MSC_VER)
#define NORETURN __declspec(noreturn)
#endif

/* some win <-> unix translations/compatibility layer */
#ifndef _WIN32 /* __unix__ */
  #define SOCKET int
  #define INVALID_SOCKET	-1
  #define SOCKET_ERROR   -1
  #define THREAD_RET_T void*
  #define closesocket(sockfd) close(sockfd)
  #define gai_strerrorA(errcode) gai_strerror(errcode)
  #define STDCALL
  #define TXRX_SZ size_t
#else /* windows */
  #define sleep(s) Sleep(s*1000)
  #define pthread_t HANDLE
  #define STDCALL __stdcall
  #define THREAD_RET_T unsigned
  #define TXRX_SZ int
  #ifdef _WIN64
	#define ssize_t __int64
  #else
	#define ssize_t long
  #endif
#endif

#define PORT_DEFAULT_STR "27007"

#if 1 /*defined(NDEBUG)*/
  #define dbg_print(...)
#else
  #define dbg_print(...) do{fprintf(stderr, __VA_ARGS__); fflush(stdout);}while(0)
#endif

size_t getLine(char* buf, int buf_sz);
NORETURN void socket_disconnected(int signal);
void closing_procedure(void);
NORETURN void exit_program(int signal);
void wakeup(int signal);
void print_help(void);
void print_version(void);
int open_socket(const char* address, const char* port);
void set_signals(void);
static void console(void);
static pthread_t start_thread(THREAD_RET_T (* func)(void *), void *data);
static THREAD_RET_T STDCALL thread_listen(void* data);
NORETURN static void threadlisten_exit(int status, const char *msg);

#endif // _CLIENT_H
