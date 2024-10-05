// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <sys/eventfd.h>
#include <libaio.h>
#include <errno.h>
// #include <linux/aio_abi.h>

#include "aws.h"
#include "utils/util.h"
#include "utils/debug.h"
#include "utils/sock_util.h"
#include "utils/w_epoll.h"

/* server socket file descriptor */
static int listenfd;

/* epoll file descriptor */
static int epollfd;

char *buffer;
off_t offset;

static int aws_on_path_cb(http_parser *p, const char *buf, size_t len)
{
	struct connection *conn = (struct connection *)p->data;

	memcpy(conn->request_path, buf, len);
	conn->request_path[len] = '\0';
	conn->have_path = 1;

	return 0;
}

static void connection_prepare_send_reply_header(struct connection *conn)
{
	/* TODO: Prepare the connection buffer to send the reply header. */
	ssize_t bytes_sent;
	int rc;
	char abuffer[64];

	rc = get_peer_address(conn->sockfd, abuffer, 64);
	if (rc < 0) {
		ERR("get_peer_address");
		goto remove_connection;
	}
	dlog(LOG_INFO, "response: %s\n", conn->send_buffer);

	bytes_sent = send(conn->sockfd, conn->send_buffer, conn->send_len, 0);
	if (bytes_sent < 0) {		/* error in communication */
		dlog(LOG_ERR, "send: Error in communication to %s\n", abuffer);
		goto remove_connection;
	}
	if (bytes_sent == 0) {		/* connection closed */
		dlog(LOG_INFO, "send: Connection closed to %s\n", abuffer);
		goto remove_connection;
	}

	while (bytes_sent > 0) {
		dlog(LOG_INFO, "se trim iar\n");
		conn->send_len -= bytes_sent;
		bytes_sent = send(conn->sockfd, conn->send_buffer, conn->send_len, 0);
		dlog(LOG_INFO, "bytes sent: %ld\n", bytes_sent);
		if (bytes_sent < 0) {		/* error in communication */
			dlog(LOG_ERR, "send: Error in communication to %s\n", abuffer);
			goto remove_connection;
		}
	}

	return;

remove_connection:
	rc = w_epoll_remove_ptr(epollfd, conn->sockfd, conn);
	DIE(rc < 0, "w_epoll_remove_ptr");

	/* remove current connection */
	conn->state = STATE_CONNECTION_CLOSED;
	if (conn->fd != -1)
		close(conn->fd);
	connection_remove(conn);

	// return STATE_CONNECTION_CLOSED;
}

static enum resource_type connection_get_resource_type(struct connection *conn)
{
	/* TODO: Get resource type depending on request path/filename. Filename should
	 * point to the static or dynamic folder.
	 */

	if (strstr(conn->request_path, "dynamic") != NULL) {
		conn->res_type = RESOURCE_TYPE_DYNAMIC;
		return RESOURCE_TYPE_DYNAMIC;
	}
	if (strstr(conn->request_path, "static") != NULL) {
		conn->res_type = RESOURCE_TYPE_STATIC;
		return RESOURCE_TYPE_STATIC;
	}
	conn->res_type = RESOURCE_TYPE_NONE;
	return RESOURCE_TYPE_NONE;
}


struct connection *connection_create(int sockfd)
{
	/* TODO: Initialize connection structure on given socket. */

	struct connection *conn = malloc(sizeof(*conn));

	DIE(conn == NULL, "malloc");

	conn->sockfd = sockfd;
	memset(conn->recv_buffer, 0, BUFSIZ);
	memset(conn->send_buffer, 0, BUFSIZ);

	return conn;
	return NULL;
}

void connection_start_async_io(struct connection *conn)
{
	/* TODO: Start asynchronous operation (read from file).
	 * Use io_submit(2) & friends for reading data asynchronously.
	 */


	buffer = malloc(conn->file_size);
	conn->eventfd = eventfd(0, 0);
	memset(&conn->iocb, 0, sizeof(struct iocb));

	io_prep_pread(&conn->iocb, conn->fd, buffer, conn->file_size, 0);
	conn->piocb[0] = &conn->iocb;
	io_set_eventfd(&conn->iocb, conn->eventfd);
	int rc = w_epoll_add_ptr_in(epollfd, conn->eventfd, conn);

	DIE(rc < 0, "w_epoll_add_ptr_in");

	rc = io_setup(1, &conn->ctx);
	DIE(rc < 0, "io_setup");

	if (io_submit(conn->ctx, 1, &conn->piocb[0]) < 0) {
		perror("io_submit");
		exit(EXIT_FAILURE);
	}
}

void connection_remove(struct connection *conn)
{
	/* TODO: Remove connection handler. */
	close(conn->sockfd);
	conn->state = STATE_CONNECTION_CLOSED;
	free(conn);
}

void handle_new_connection(void)
{
	/* TODO: Handle a new connection request on the server socket. */

	static int sockfd;
	socklen_t addrlen = sizeof(struct sockaddr_in);
	struct sockaddr_in addr;
	struct connection *conn;
	int rc;

	/* TODO: Accept new connection. */

	sockfd = accept(listenfd, (SSA *) &addr, &addrlen);
	DIE(sockfd < 0, "accept");

	/* TODO: Set socket to be non-blocking. */

	int flags = fcntl(sockfd, F_GETFL, 0);

	fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);


	/* TODO: Instantiate new connection handler. */

	conn = connection_create(sockfd);

	/* TODO: Add socket to epoll. */

	rc = w_epoll_add_ptr_in(epollfd, sockfd, conn);
	DIE(rc < 0, "w_epoll_add_in");

	/* TODO: Initialize HTTP_REQUEST parser. */
	http_parser_init(&conn->request_parser, HTTP_REQUEST);
}

void receive_data(struct connection *conn)
{
	/* TODO: Receive message on socket.
	 * Store message in recv_buffer in struct connection.
	 */

	ssize_t bytes_recv;
	int rc;
	char abuffer[64];

	rc = get_peer_address(conn->sockfd, abuffer, 64);
	if (rc < 0) {
		ERR("get_peer_address");
		goto remove_connection;
	}

	int offset = 0;

	while (1) {
		bytes_recv = recv(conn->sockfd, conn->recv_buffer + offset, BUFSIZ, 0);
		if (bytes_recv < 0) { /* error in communication */
			dlog(LOG_ERR, "Error in communication from: %s\n", abuffer);
			goto remove_connection;
		}
		if (bytes_recv == 0) { /* connection closed */
			dlog(LOG_INFO, "Connection closed from: %s\n", abuffer);
			goto remove_connection;
		}
		offset += bytes_recv;
		if (strstr(conn->recv_buffer, "\r\n\r\n") != NULL)
			break;
	}

	dlog(LOG_DEBUG, "Received message from: %s\n", abuffer);

	conn->recv_len = strlen(conn->recv_buffer);
	conn->state = STATE_REQUEST_RECEIVED; //?

	return;

remove_connection:
	rc = w_epoll_remove_ptr(epollfd, conn->sockfd, conn);
	DIE(rc < 0, "w_epoll_remove_ptr");

	/* remove current connection */
	conn->state = STATE_CONNECTION_CLOSED;
}

int connection_open_file(struct connection *conn)
{
	/* TODO: Open file and update connection fields. */
	struct stat st;


	int fd = open(conn->filename, O_WRONLY | O_CREAT | O_TRUNC);

	if (fd != -1) {
		stat(conn->filename, &st);
		conn->file_size = st.st_size;


		sprintf(conn->send_buffer,
		"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n", conn->file_size);
		conn->send_len = strlen(conn->send_buffer);
	} else {
		sprintf(conn->send_buffer, "HTTP/1.0 404 Not Found\r\n\r\n");
		conn->send_len = strlen(conn->send_buffer);
		fstat(conn->fd, &st);
	}

	return fd;
	return -1;
}

void connection_complete_async_io(struct connection *conn)
{
	/* TODO: Complete asynchronous operation; operation returns successfully.
	 * Prepare socket for sending.
	 */
	struct io_event *events;

	memset(events, 0, sizeof(events));
	int ret;

	ret = io_getevents(conn->ctx, 1, 1, events, NULL);
	dlog(LOG_INFO, "ret e %d\n", ret);

	if (ret < 1) {
		perror("io_getevents");
		exit(EXIT_FAILURE);
	}

	if (events[0].res < 0) {
		perror("Asynchronous I/O error");
		exit(EXIT_FAILURE);
	}
}

int parse_header(struct connection *conn)
{
	/* TODO: Parse the HTTP header and extract the file path. */
	/* Use mostly null settings except for on_path callback. */
	http_parser_settings settings_on_path = {
		.on_message_begin = 0,
		.on_header_field = 0,
		.on_header_value = 0,
		.on_path = aws_on_path_cb,
		.on_url = 0,
		.on_fragment = 0,
		.on_query_string = 0,
		.on_body = 0,
		.on_headers_complete = 0,
		.on_message_complete = 0
	};

	conn->request_parser.data = conn;

	size_t parsed = http_parser_execute(&conn->request_parser, &settings_on_path, conn->recv_buffer, conn->recv_len);

	return 0;
}

enum connection_state connection_send_static(struct connection *conn)
{
	/* TODO: Send static data using sendfile(2). */

	ssize_t bytes_sent;
	int rc;
	char abuffer[64];

	// rc = get_peer_address(conn->sockfd, abuffer, 64);
	if (rc < 0) {
		ERR("get_peer_address");
		goto remove_connection;
	}


	off_t offset = 0;
	int total_sent = 0;

	while (1) {
		bytes_sent = sendfile(conn->sockfd, conn->fd, &offset, conn->file_size - total_sent);
		if (bytes_sent < 0) { /* error in communication */
			dlog(LOG_ERR, "Error in communication to %s\n", abuffer);

			goto remove_connection;
		}
		if (bytes_sent == 0) { /* connection closed */
			dlog(LOG_INFO, "Connection closed to %s\n", abuffer);
			goto remove_connection;
		}
		total_sent += bytes_sent;
		if (total_sent >= conn->file_size)
			break;
	}

	rc = w_epoll_remove_ptr(epollfd, conn->sockfd, conn);
	DIE(rc < 0, "w_epoll_remove_ptr");

	connection_remove(conn);
	return STATE_CONNECTION_CLOSED;

	dlog(LOG_DEBUG, "Sending message to %s\n", abuffer);

	printf("--\n%s--\n", conn->send_buffer);

	/* all done - remove out notification */
	rc = w_epoll_update_ptr_in(epollfd, conn->sockfd, conn);
	DIE(rc < 0, "w_epoll_update_ptr_in");

	conn->state = STATE_DATA_SENT;
	if (conn->fd != -1)
		close(conn->fd);
	connection_remove(conn);

	return STATE_DATA_SENT;

remove_connection:
		rc = w_epoll_remove_ptr(epollfd, conn->sockfd, conn);
		DIE(rc < 0, "w_epoll_remove_ptr");

		/* remove current connection */
		if (conn->fd != -1)
			close(conn->fd);
		connection_remove(conn);

		return STATE_CONNECTION_CLOSED;

		return STATE_NO_STATE;
	}

int connection_send_data(struct connection *conn)
{
	/* May be used as a helper function. */
	/* TODO: Send as much data as possible from the connection send buffer.
	 * Returns the number of bytes sent or -1 if an error occurred
	 */
	return -1;
}


int connection_send_dynamic(struct connection *conn)
{
	/* TODO: Read data asynchronously.
	 * Returns 0 on success and -1 on error.
	 */
	connection_start_async_io(conn);
	connection_complete_async_io(conn);



	int bytes_sent = send(conn->sockfd, buffer, conn->file_size, 0);

	if (bytes_sent < 0) {		/* error in communication */
		dlog(LOG_ERR, "send: Error in communication\n");
		goto remove_connection;
	}
	if (bytes_sent == 0) {		/* connection closed */
		dlog(LOG_INFO, "send: Connection closed\n");
		goto remove_connection;
	}
	while (bytes_sent > 0) {
		conn->file_size -= bytes_sent;
		bytes_sent = send(conn->sockfd, conn->send_buffer, conn->file_size, 0);

		if (bytes_sent < 0)		/* error in communication */
			dlog(LOG_ERR, "send: Error in communication\n");
	}
	io_destroy(conn->ctx);
	conn->ctx = 0;
	close(conn->eventfd);
	conn->eventfd = -1;

	int rc = w_epoll_remove_ptr(epollfd, conn->sockfd, conn);

	DIE(rc < 0, "w_epoll_remove_ptr");


	conn->state = STATE_DATA_SENT;
	if (conn->fd != -1)
		close(conn->fd);
	connection_remove(conn);

	return 0;

remove_connection:
	rc = w_epoll_remove_ptr(epollfd, conn->sockfd, conn);
	DIE(rc < 0, "w_epoll_remove_ptr");

	/* remove current connection */
	if (conn->fd != -1)
		close(conn->fd);
	connection_remove(conn);
	return -1;
}


void connection_send(struct connection *conn)
{
	if (conn->res_type == RESOURCE_TYPE_STATIC)
		connection_send_static(conn);
	else if (conn->res_type == RESOURCE_TYPE_DYNAMIC)
		connection_send_dynamic(conn);
}


void handle_client(uint32_t event, struct connection *conn)
{
	/* TODO: Handle new client. There can be input and output connections.
	 * Take care of what happened at the end of a connection.
	 */

	receive_data(conn);
	if (conn->state == STATE_CONNECTION_CLOSED)
		return;

	parse_header(conn);

	if (connection_get_resource_type(conn) == RESOURCE_TYPE_STATIC) {
		char *path = strrchr(conn->request_path, '/');

		if (path != NULL)
			sprintf(conn->filename, "%s%s", AWS_REL_STATIC_FOLDER, path + 1);
		else
			sprintf(conn->filename, "%s%s", AWS_REL_STATIC_FOLDER, conn->request_path);
	} else if (connection_get_resource_type(conn) == RESOURCE_TYPE_DYNAMIC) {
		char *path = strrchr(conn->request_path, '/');

		if (path != NULL)
			sprintf(conn->filename, "%s%s", AWS_REL_DYNAMIC_FOLDER, path + 1);
		else
			sprintf(conn->filename, "%s%s", AWS_REL_DYNAMIC_FOLDER, conn->request_path);
	}

	conn->fd = connection_open_file(conn);

	connection_prepare_send_reply_header(conn);


	int rc = w_epoll_update_ptr_inout(epollfd, conn->sockfd, conn);

	DIE(rc < 0, "w_epoll_add_ptr_inout");
	}

int main(void)
{
	int rc;

	/* TODO: Initialize asynchronous operations. */

	/* TODO: Initialize multiplexing. */
	epollfd = w_epoll_create();
	DIE(epollfd < 0, "w_epoll_create");

	/* TODO: Create server socket. */
	listenfd = tcp_create_listener(AWS_LISTEN_PORT, DEFAULT_LISTEN_BACKLOG);
	DIE(listenfd < 0, "tcp_create_listener");


	/* TODO: Add server socket to epoll object*/
	rc = w_epoll_add_fd_in(epollfd, listenfd);
	DIE(rc < 0, "w_epoll_add_fd_in");

	// dlog(LOG_DEBUG, "New connection\n");

	/* Uncomment the following line for debugging. */
	dlog(LOG_INFO, "Server waiting for connections on port %d\n", AWS_LISTEN_PORT);

	/* server main loop */
	while (1) {
		struct epoll_event rev;

		/* TODO: Wait for events. */

		rc = w_epoll_wait_infinite(epollfd, &rev);
		DIE(rc < 0, "w_epoll_wait_infinite");



		/* TODO: Switch event types; consider
		 *   - new connection requests (on server socket)
		 *   - socket communication (on connection sockets)
		 */


		if (rev.data.fd == listenfd) {
			dlog(LOG_DEBUG, "New connection\n");
			if (rev.events & EPOLLIN)
				handle_new_connection();
		} else {
			if (rev.events & EPOLLIN) {
				dlog(LOG_DEBUG, "New message\n");
				handle_client(rev.events, rev.data.ptr);
			}
			if (rev.events & EPOLLOUT) {
				dlog(LOG_DEBUG, "Ready to send message\n");
				connection_send_static(rev.data.ptr);
			}
		}
	}

	return 0;
}
