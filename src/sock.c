/* $Id: sock.c,v 1.40 2004-02-18 20:18:53 rjkaes Exp $
 *
 * Sockets are created and destroyed here. When a new connection comes in from
 * a client, we need to copy the socket and the create a second socket to the
 * remote server the client is trying to connect to. Also, the listening
 * socket is created and destroyed here. Sounds more impressive than it
 * actually is.
 *
 * Copyright (C) 1998  Steven Young
 * Copyright (C) 1999,2004  Robert James Kaes (rjkaes@users.sourceforge.net)
 * Copyright (C) 2000  Chris Lightfoot (chris@ex-parrot.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include "tinyproxy.h"

#include "log.h"
#include "heap.h"
#include "network.h"
#include "sock.h"
#include "text.h"

/*
 * Bind the given socket to the supplied address.  The socket is
 * returned if the bind succeeded.  Otherwise, -1 is returned
 * to indicate an error.
 */
static int
bind_socket(int sockfd, const char* addr)
{
	struct addrinfo hints, *res, *ressave;

	assert(sockfd >= 0);
	assert(addr != NULL && strlen(addr) != 0);

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	/* The local port it not important */
	if (getaddrinfo(addr, NULL, &hints, &res) != 0)
		return -1;

	ressave = res;

	/* Loop through the addresses and try to bind to each */
	do {
		if (bind(sockfd, res->ai_addr, res->ai_addrlen) == 0)
			break; /* success */
	} while ((res = res->ai_next) != NULL);

	freeaddrinfo(ressave);
	if (res == NULL) /* was not able to bind to any address */
		return -1;

	return sockfd;
}

/*
 * Open a connection to a remote host.  It's been re-written to use
 * the getaddrinfo() library function, which allows for a protocol
 * independent implementation (mostly for IPv4 and IPv6 addresses.)
 */
int
opensock(const char* host, int port)
{
	int sockfd, n;
	struct addrinfo hints, *res, *ressave;
	char portstr[6];

	assert(host != NULL);
	assert(port > 0);

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	snprintf(portstr, sizeof(portstr), "%d", port);

	n = getaddrinfo(host, portstr, &hints, &res);
	if (n != 0) {
		log_message(LOG_ERR, "opensock: Could not retrieve info for %s",
			    host);
		return -1;
	}

	ressave = res;
	do {
		sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (sockfd < 0)
			continue; /* ignore this one */

		/* Bind to the specified address */
		if (config.bind_address) {
			if (bind_socket(sockfd, config.bind_address) < 0) {
				close(sockfd);
				continue; /* can't bind, so try again */
			}
		}

		if (connect(sockfd, res->ai_addr, res->ai_addrlen) == 0)
			break; /* success */

		close(sockfd);
	} while ((res = res->ai_next) != NULL);

	freeaddrinfo(ressave);
	if (res == NULL) {
		log_message(LOG_ERR,
			    "opensock: Could not establish a connection to %s",
			    host);
		return -1;
	}

	return sockfd;
}

/*
 * Set the socket to non blocking -rjkaes
 */
int
socket_nonblocking(int sock)
{
	int flags;

	assert(sock >= 0);

	flags = fcntl(sock, F_GETFL, 0);
	return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

/*
 * Set the socket to blocking -rjkaes
 */
int
socket_blocking(int sock)
{
	int flags;

	assert(sock >= 0);

	flags = fcntl(sock, F_GETFL, 0);
	return fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);
}

/*
 * Start listening to a socket. Create a socket with the selected port.
 * The size of the socket address will be returned to the caller through
 * the pointer, while the socket is returned as a default return.
 *	- rjkaes
 */
int
listen_sock(uint16_t port, socklen_t* addrlen)
{
	int listenfd;
	const int on = 1;
	struct sockaddr_in addr;

	assert(port > 0);
	assert(addrlen != NULL);

	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	if (config.ipAddr) {
		addr.sin_addr.s_addr = inet_addr(config.ipAddr);
	} else {
		addr.sin_addr.s_addr = inet_addr("0.0.0.0");
	}

	if (bind(listenfd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		log_message(LOG_ERR, "Unable to bind listening socket because of %s",
			    strerror(errno));
		return -1;
	}

	if (listen(listenfd, MAXLISTEN) < 0) {
		log_message(LOG_ERR, "Unable to start listening socket because of %s",
			    strerror(errno));
		return -1;
	}

	*addrlen = sizeof(addr);

	return listenfd;
}

/*
 * Return the peer's socket information.
 */
int
getpeer_information(int fd, char* ipaddr, char* string_addr)
{
	struct sockaddr sa;
	size_t salen = sizeof(struct sockaddr);

	assert(fd >= 0);
	assert(ipaddr != NULL);
	assert(string_addr != NULL);

	/* Set the strings to default values */
	ipaddr[0] = '\0';
	strlcpy(string_addr, "[unknown]", PEER_STRING_LENGTH);

	/* Look up the IP address */
	if (getpeername(fd, &sa, &salen) != 0)
		return -1;

	if (get_ip_string(&sa, ipaddr, PEER_IP_LENGTH) == NULL)
		return -1;

	/* Get the full host name */
	return getnameinfo(&sa, salen,
			   string_addr, PEER_STRING_LENGTH,
			   NULL, 0, 0);
}
