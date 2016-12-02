/**
 * @file common.h
 * @date Aug 1, 2011
 * @author Volodymyr Kolesnykov <volodymyr@wildwolf.name>
 */

#ifndef BOUNCER_COMMON_H_
#define BOUNCER_COMMON_H_

#include <unistd.h>
#include <signal.h>
#include <time.h>

#ifndef BOUNCER_MAX_EVENTS
#	define BOUNCER_MAX_EVENTS 1024
#endif

#define BOUNCER_CAN_READ  1
#define BOUNCER_CAN_WRITE 2

/**
 * S0: Send 554, move to S1
 * S1: Read data;
 * - if data is QUIT, move to S2;
 * - if data is NOOP, move to S3;
 * - if line is too long or empty, move to S4;
 * - otherwise move to S5
 * S2: Send 221, move to FIN
 * S3: Send 250, move to S1
 * S4: Send 500, move to S1
 * S5: Send 503, move to S1
 * FIN: Close socket
 * Sx: On error move to ERR
 * ERR: Report error, close socket
 */
enum state_t { S0, S1, S2, S3, S4, S5, FIN, ERR };

struct data_t {
	size_t nread;
	size_t nwritten;
	size_t towrite;
	const char* write_buf;
	char read_buf[516]; /* 4.5.3.1.4. Command Line: The maximum total length of a command line including the command word and the <CRLF> is 512 octets.*/
	enum state_t state;
};

struct entry_t {
	struct data_t* data;
	time_t timeout;
	time_t hard_timeout;
	int sock;
};

extern volatile sig_atomic_t terminate;
extern struct entry_t sockets[BOUNCER_MAX_EVENTS];
extern struct data_t data[BOUNCER_MAX_EVENTS];
extern int connections;
extern const char* err_unavail;

int drop_privs(void);
void set_signals(void);
int make_nonblocking(int fd);
int create_socket(void);
ssize_t safe_read(int fd, void* buf, size_t size);
ssize_t safe_write(int fd, const void* buf, size_t size);
int process_event(int sock, const int flags);
int find_socket(int sock);

#endif /* COMMON_H_ */
