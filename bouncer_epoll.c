/**
 * @file bouncer_epoll.c
 * @date Jul 31, 2011
 * @author Vladimir Kolesnikov <vladimir@free-sevastopol.com>
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "common.h"

static const char* err_timeout  = "421 4.4.2 localhost.localdomain Timeout exceeded.\r\n";

/**
 * Tries to accept he incoming connection
 *
 * @param listener The socket the server listens to
 * @param eh @c epoll descriptor
 * @return The descriptor of the new connection, -1 on failure (@c errno will hold the error code)
 */
static int accept_socket(int listener, int eh)
{
	int conn = accept(listener, NULL, 0);
	if (conn >= 0) {
		if (connections >= BOUNCER_MAX_EVENTS-1) {
			shutdown(conn, SHUT_RDWR);
			close(conn);
			errno = ECONNABORTED;
			return -1;
		}

		struct epoll_event ev;
		if (make_nonblocking(conn) >= 0) {
			ev.data.fd = conn;
			ev.events  = EPOLLIN | EPOLLOUT | EPOLLET;
			if (epoll_ctl(eh, EPOLL_CTL_ADD, conn, &ev) >= 0) {
				time_t now = time(NULL);
				sockets[connections].sock         = conn;
				sockets[connections].data         = &data[connections];
				sockets[connections].timeout      = now + 300; /* 4.5.3.2.7. Server Timeout: 5 Minutes. */
				sockets[connections].hard_timeout = now + 900; /* We are not a mail server after all - the client should disconnect as soon as it gets 554 */

				data[connections].state     = S0;
				data[connections].nread     = 0;
				data[connections].nwritten  = 0;
				data[connections].towrite   = 0;
				data[connections].write_buf = NULL;
				++connections;
				return conn;
			}
		}

		int e = errno;
		close(conn);
		errno = e;
	}

	return -1;
}


/**
 * Writes a NULL-terminated @a msg (if it is not NULL) to @a sock, gracefully closes the connection and frees all associated resources
 *
 * @param sock Socket descriptor
 * @param eh @c epoll descriptor
 * @param msg Message to send before close
 */
static void close_socket(int sock, int eh, const char* msg)
{
	int idx;
	struct epoll_event ev;

	if (msg) {
		safe_write(sock, msg, strlen(msg));
	}

	epoll_ctl(eh, EPOLL_CTL_DEL, sock, &ev);
	shutdown(sock, SHUT_RDWR);
	close(sock);

	idx = find_socket(sock);
	if (idx >= 0) {
		if (idx != connections - 1) {
			sockets[idx]      = sockets[connections-1];
			data[idx]         = data[connections-1];
			sockets[idx].data = &data[idx];
		}

		--connections;
	}
}

/**
 * Main event loop
 *
 * @param eh @c epoll descriptor
 * @param listener The socket the server listens to
 */
static void event_loop(int eh, int listener)
{
	struct epoll_event events[BOUNCER_MAX_EVENTS];
	int i;

	while (!terminate) {
		int nfds = epoll_wait(eh, events, BOUNCER_MAX_EVENTS, 1);
		if (nfds < 0) {
			if (EINTR != errno) {
				perror("epoll_wait");
				break;
			}
		}

		for (i=0; i<nfds; ++i) {
			int sock = events[i].data.fd;
			if (sock == listener) {
				accept_socket(listener, eh);
				/* There's nothing bad if accept() failed: if this is our fault, the client will retry later */
			}
			else if (events[i].events & (EPOLLERR | EPOLLHUP)) {
				close_socket(sock, eh, NULL);
			}
			else {
				int e = 0;
				if (events[i].events & EPOLLIN) {
					e = BOUNCER_CAN_READ;
				}

				if (events[i].events & EPOLLOUT) {
					e |= BOUNCER_CAN_WRITE;
				}

				if (process_event(sock, e)) {
					close_socket(sock, eh, NULL);
				}
			}
		}

		if (!terminate) {
			time_t now = time(NULL);
			for (i=connections-1; i>=0; --i) {
				if (sockets[i].timeout <= now || sockets[i].hard_timeout <= now) {
					close_socket(sockets[i].sock, eh, err_timeout);
				}
			}
		}
	}

	for (i=connections-1; i>=0; --i) {
		close_socket(sockets[i].sock, eh, err_unavail);
	}
}

int main(int argc, char** argv)
{
	int eh;
	int sock;
	struct epoll_event ev;

	eh = epoll_create(20);
	if (eh < 0) {
		perror("epoll_create");
		return EXIT_FAILURE;
	}

	sock = create_socket();
	if (sock < 0) {
		perror("create_socket");
		close(eh);
		return EXIT_FAILURE;
	}

	ev.data.fd = sock;
	ev.events  = EPOLLIN;
	if (epoll_ctl(eh, EPOLL_CTL_ADD, sock, &ev) < 0) {
		perror("epoll_ctl");
		close(eh);
		close(sock);
		return EXIT_FAILURE;
	}

	set_signals();

	if (drop_privs() < 0) {
		perror("drop_privs");
		close(eh);
		close(sock);
		return EXIT_FAILURE;
	}

	event_loop(eh, sock);

	close(eh);
	close(sock);
	return EXIT_SUCCESS;
}
