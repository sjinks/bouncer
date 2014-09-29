/**
 * @file common.c
 * @date Aug 1, 2011
 * @author Volodymyr Kolesnykov <volodymyr@wildwolf.name>
 */

#include "common.h"
#include <fcntl.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <ctype.h>

#ifndef BOUNCER_PORT
#	define BOUNCER_PORT 10025
#endif

static const char* str_greeting = "554 5.3.2 HELLO FROM THE BOUNCER!\r\n";
static const char* str_quit     = "221 2.0.0 Bye.\r\n";
static const char* str_noop     = "250 2.0.0 OK.\r\n";
static const char* str_bad_seq  = "503 5.1.1 Bad sequence of commands.\r\n";
static const char* err_syntax   = "500 5.5.2 Syntax error.\r\n";
const char*        err_unavail  = "421 4.4.2 localhost.localdomain Closing transmission channel.\r\n";

volatile sig_atomic_t terminate = 0;
struct entry_t sockets[BOUNCER_MAX_EVENTS];
struct data_t data[BOUNCER_MAX_EVENTS];
int connections = 0;

/**
 * Sets the termination flag
 *
 * @brief SIGTERM/SIGINT/SIGQUIT handler
 */
static void signal_handler(int signal)
{
	terminate = 1;
}

/**
 * @brief Installs signal handlers
 * @see signal_handler()
 *
 * SIGPIPE is ignored, @c signal_handler() is set to handle SIGTERM, SIGINT and SIGQUIT
 */
void set_signals(void)
{
	struct sigaction sa;

	sa.sa_handler = SIG_IGN;
	sa.sa_flags   = SA_RESTART;
	sigfillset(&sa.sa_mask);
	sigaction(SIGPIPE, &sa, NULL);

	sa.sa_handler = signal_handler;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
}

/**
 * Returns the index of the socket @a sock in the list of connections
 *
 * @param sock Socket to find
 * @return Index of the socket, -1 on failure
 */
int find_socket(int sock)
{
	int i;

	for (i=0; i<connections; ++i) {
		if (sockets[i].sock == sock) {
			return i;
		}
	}

	return -1;
}

/**
 * @brief Drops privileges
 * @return -1 on failure, 0 on success
 * @see http://refspecs.freestandards.org/LSB_3.0.0/LSB-Core-generic/LSB-Core-generic/usernames.html
 *
 * Tries to change the user identity to @c nobody or @c daemon, then creates @c /var/run/bouncer directory, chnages the current directory to it and chroot()'s into it.
 */
int drop_privs(void)
{
	struct passwd* e;
	uid_t uid;
	gid_t gid;
	int res;

	e = getpwnam("nobody");
	if (!e) {
		/* Per LSB 3.0 'daemon' is a required name/group */
		e = getpwnam("daemon");
	}

	if (e) {
		uid = e->pw_uid;
		gid = e->pw_gid;
	}
	else {
		return -1;
	}

	if (
	       mkdir("/var/run/bouncer", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) < 0
	    || chroot("/var/run/bouncer") < 0
	    || chdir("/") < 0
	    || setgid(gid) < 0
	    || setuid(uid) < 0
	) {
		return -1;
	}

	return 0;
}

/**
 * @bried Makes the descriptor @a fd non-blocking
 * @param fd Descriptor
 * @return 0 on success, -1 on failure
 */
int make_nonblocking(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) {
		return -1;
	}

	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/**
 * @brief Creates a new non-blocking listening socket that is bound to @c localhost:10025
 * @return Socket descriptor on success, -1 on failure
 */
int create_socket(void)
{
	int sock;
	int res = 1;
	struct sockaddr_in sa;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		return -1;
	}

	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &res, sizeof(res)) < 0) {
		return -1;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sin_family      = AF_INET;
	sa.sin_port        = htons(BOUNCER_PORT);
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (
		   make_nonblocking(sock) < 0
		|| bind(sock, (struct sockaddr*)&sa, sizeof(sa)) < 0
		|| listen(sock, 512) < 0
		)
	{
		int e = errno;
		close(sock);
		errno = e;
		return -1;
	}

	return sock;
}

/**
 * @brief Wrapper around @c read() that handles @c EINTR gracefully
 * @param fd Descriptor
 * @param buf Pointer to the buffer to read the data into
 * @param size Number of bytes to read
 * @return -1 on failure, number of bytes actually read on success
 */
ssize_t safe_read(int fd, void* buf, size_t size)
{
	ssize_t nread;
	do {
		nread = read(fd, buf, size);
	} while (-1 == nread && EINTR == errno);

	return nread;
}

/**
 * @brief Wrapper around @c write() that handles @c EINTR gracefully
 * @param fd Descriptor
 * @param buf Pointer to the buffer to write the data from
 * @param size Number of bytes to write
 * @return -1 on failure, number of bytes actually written on success
 */
ssize_t safe_write(int fd, const void* buf, size_t size)
{
	ssize_t written;
	do {
		written = write(fd, buf, size);
	} while (-1 == written && EINTR == errno);

	return written;
}

int do_write(struct data_t* x, struct entry_t* e, const void* buf, size_t len, enum state_t state)
{
	time_t now = time(NULL);

	if (!x->write_buf) {
		x->write_buf = buf;
		x->towrite   = len;
	}

	ssize_t n = safe_write(e->sock, x->write_buf + x->nwritten, x->towrite);
	if (n == x->towrite) {
		x->towrite   = 0;
		x->write_buf = NULL;
		x->state     = state;
		e->timeout   = now + 300;
	}
	else if (n > 0) {
		x->nwritten += n;
		x->towrite  -= n;
		e->timeout   = now + 300;
	}
	else if ((EAGAIN != errno && EWOULDBLOCK != errno) || !n) {
		x->state = ERR;
	}
	else {
		return 1;
	}

	return 0;
}

int process_event(int sock, const int flags)
{
	int idx = find_socket(sock);
	if (idx < 0) {
		return 1;
	}

	struct entry_t e = sockets[idx];
	struct data_t* x = e.data;
	int done = 0;

	while (!done) {
		time_t now = time(NULL);
		switch (x->state) {
			case S0:
				done = !(flags & BOUNCER_CAN_WRITE) || do_write(x, &e, str_greeting, strlen(str_greeting), S1);
				break;

			case S1:
				if (flags & BOUNCER_CAN_READ) {
					ssize_t n = safe_read(sock, x->read_buf + x->nread, 512 - x->nread);
					if (n > 0) {
						x->nread += n;
						e.timeout = now + 300;
					}
					else if ((EAGAIN != errno && EWOULDBLOCK != errno) || !n) {
						x->state = ERR;
					}
					else {
						done = !x->nread;
					}

					char* pos = memchr(x->read_buf, '\n', x->nread);
					if (pos) {
						*pos = 0;
						char* p;
						for (p=x->read_buf; p!=pos; ++p) {
							if (isspace(*p)) {
								*p = 0;
								break;
							}
							else {
								*p = toupper(*p);
							}
						}

						if (!strcmp(x->read_buf, "QUIT")) {
							x->state = S2;
						}
						else if (!strcmp(x->read_buf, "NOOP")) {
							x->state = S3;
						}
						else if (!x->read_buf[0]) {
							x->state = S4;
						}
						else {
							x->state = S5;
						}

						memcpy(x->read_buf, pos+1, x->nread - (pos - x->read_buf + 1));
						x->nread = x->nread - (pos - x->read_buf + 1);
					}
					else if (512 == x->nread) {
						x->state = S4;
						x->nread = 0;
					}
				}
				else {
					done = 1;
				}

				break;

			case S2:
				done = !(flags & BOUNCER_CAN_WRITE) || do_write(x, &e, str_quit, strlen(str_quit), FIN);
				break;

			case S3:
				done = !(flags & BOUNCER_CAN_WRITE) || do_write(x, &e, str_noop, strlen(str_noop), S1);
				break;

			case S4:
				done = !(flags & BOUNCER_CAN_WRITE) || do_write(x, &e, err_syntax, strlen(err_syntax), S1);
				break;

			case S5:
				done = !(flags & BOUNCER_CAN_WRITE) || do_write(x, &e, str_bad_seq, strlen(str_bad_seq), S1);
				break;

			case FIN:
				return 1;

			case ERR:
				if (flags & BOUNCER_CAN_WRITE) {
					do_write(x, &e, err_unavail, strlen(err_unavail), ERR);
				}

				return 1;
		}
	}

	return 0;
}
