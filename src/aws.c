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
#include <signal.h>

#include "aws.h"
#include "utils/util.h"
#include "utils/debug.h"
#include "utils/sock_util.h"
#include "utils/w_epoll.h"

#define NR_EVENTS 1000


/* server socket file descriptor */
static int listenfd;

/* epoll file descriptor */
static int epollfd;

static io_context_t ctx;

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

	if (conn == NULL)
		return;
	dlog(LOG_DEBUG, "Entered prepare send reply header\n");
	conn->state = STATE_SENDING_HEADER;

	sprintf(conn->send_buffer, "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n", conn->file_size);
	dlog(LOG_DEBUG, "Send buffer has : %s\n", conn->send_buffer);
	conn->send_len = strlen(conn->send_buffer);
	dlog(LOG_DEBUG, "Exiting prepare send reply header\n");
}

static void connection_prepare_send_404(struct connection *conn)
{
	/* TODO: Prepare the connection buffer to send the 404 header. */
	dlog(LOG_DEBUG, "Entered Prepare send 404 header\n");
	if (conn == NULL)
		return;
	conn->state = STATE_SENDING_404;

	conn->send_len = strlen("HTTP/1.1 404 Not Found\r\n\r\n");
	memmove(conn->send_buffer, "HTTP/1.1 404 Not Found\r\n\r\n", conn->send_len + 1);
	dlog(LOG_DEBUG, "Exiting prepare send 404 header\n");
}

// asta e ok
static enum resource_type connection_get_resource_type(struct connection *conn)
{
	/* TODO: Get resource type depending on request path/filename. Filename should
	 * point to the static or dynamic folder.
	 */
	if (conn == NULL || conn->have_path == 0) {
		dlog(LOG_DEBUG, "404");

		return RESOURCE_TYPE_NONE;
	}

	if (strstr(conn->request_path, AWS_REL_STATIC_FOLDER) != NULL) {
		dlog(LOG_DEBUG, "Static path : %s\n", conn->request_path);

		return RESOURCE_TYPE_STATIC;
	}

	if (strstr(conn->request_path, AWS_REL_DYNAMIC_FOLDER) != NULL) {
		dlog(LOG_DEBUG, "Dynamic path : %s\n", conn->request_path);

		return RESOURCE_TYPE_DYNAMIC;
	}
	dlog(LOG_DEBUG, "404");

	return RESOURCE_TYPE_NONE;
}

struct connection *connection_create(int sockfd)
{
	/* TODO: Initialize connection structure on given socket. */

	struct connection *conn = malloc(sizeof(struct connection));

	DIE(conn == NULL, "malloc");

	conn->fd = 0;
	memset(conn->filename, 0, BUFSIZ);
	conn->sockfd = sockfd;
	conn->ctx = ctx;
	conn->piocb[0] = &conn->iocb;
	conn->file_size = 0;
	memset(conn->recv_buffer, 0, BUFSIZ);
	conn->recv_len = 0;
	memset(conn->send_buffer, 0, BUFSIZ);
	conn->send_len = 0;
	conn->send_pos = 0;
	conn->file_pos = 0;
	conn->async_read_len = 0;
	conn->have_path = 0;
	memset(conn->request_path, 0, BUFSIZ);
	conn->res_type = RESOURCE_TYPE_NONE;
	conn->state = STATE_INITIAL;
	return conn;
}

void connection_start_async_io(struct connection *conn)
{
	/* TODO: Start asynchronous operation (read from file).
	 * Use io_submit(2) & friends for reading data asynchronously.
	 */
	if (conn == NULL)
		return;

	size_t to_receive;

	if (conn->file_size <= BUFSIZ)
		to_receive = conn->file_size - conn->file_pos;
	else
		to_receive = BUFSIZ;

	io_prep_pread(&conn->iocb, conn->fd, conn->recv_buffer, to_receive, conn->file_pos);
	conn->piocb[0] = &conn->iocb;

	if (io_submit(conn->ctx, 1, &conn->piocb[0]) < 0) {
		dlog(LOG_DEBUG, "io_submit failed");
		return;
	}
	dlog(LOG_DEBUG, "async ongoing");
	conn->state = STATE_ASYNC_ONGOING;
}

void connection_remove(struct connection *conn)
{
	/* TODO: Remove connection handler. */

	if (conn == NULL)
		return;

	w_epoll_remove_ptr(epollfd, conn->sockfd, conn);
	close(conn->sockfd);
	close(conn->fd);
	free(conn);
}

void handle_new_connection(void)
{
	/* TODO: Handle a new connection request on the server socket. */
	SSA client;
	socklen_t client_len = sizeof(SSA);

	/* TODO: Accept new connection. */
	int new_conn = accept(listenfd, (SSA *)&client, &client_len);

	/* TODO: Set socket to be non-blocking. */
	int flags = fcntl(new_conn, F_GETFL, 0);

	fcntl(new_conn, F_SETFL, flags | O_NONBLOCK);

	/* TODO: Instantiate new connection handler. */
	struct connection *conn = connection_create(new_conn);

	/* TODO: Add socket to epoll. */
	w_epoll_add_ptr_in(epollfd, new_conn, conn);

	/* TODO: Initialize HTTP_REQUEST parser. */
	http_parser_init(&conn->request_parser, HTTP_REQUEST);
	conn->request_parser.data = conn;
}

void receive_data(struct connection *conn)
{
	/* TODO: Receive message on socket.
	 * Store message in recv_buffer in struct connection.
	 */

	if (conn == NULL)
		return;
	conn->state = STATE_RECEIVING_DATA;
	dlog(LOG_DEBUG, "Receiving data\n");

	ssize_t bytes_read = recv(conn->sockfd, conn->recv_buffer + conn->recv_len, BUFSIZ, 0);

	if (bytes_read > 0) {
		conn->recv_len += bytes_read;
		dlog(LOG_DEBUG, "Received %ld bytes\n", bytes_read);
	} else if (bytes_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
		dlog(LOG_DEBUG, "Socket is not ready for receiving more data, try again later\n");
	}

	if (strstr(conn->recv_buffer, "\r\n\r\n") != NULL) {
		dlog(LOG_DEBUG, "Received entire header : %s\n", conn->recv_buffer);
		conn->state = STATE_REQUEST_RECEIVED;
	}
}

int connection_open_file(struct connection *conn)
{
	/* TODO: Open file and update connection fields. */
	if (conn == NULL)
		return -1;

	char path[BUFSIZ];

	path[0] = '\0';
	strcat(path, AWS_DOCUMENT_ROOT);
	path[strlen(path) - 1] = '\0';
	strcat(path, conn->request_path);
	memcpy(conn->request_path, path, strlen(path));

	dlog(LOG_DEBUG, "Opening file de la %s, initial fd = %d\n", conn->request_path, conn->fd);

	conn->fd = open(conn->request_path, O_RDONLY);
	if (conn->fd > 0) {
		dlog(LOG_DEBUG, "File opened : %s, fd = %d\n", conn->request_path, conn->fd);
	} else {
		dlog(LOG_DEBUG, "File cannot open\n");
		connection_prepare_send_404(conn);
		conn->state = STATE_SENDING_404;
		return -1;
	}
	struct stat file_stat;

	fstat(conn->fd, &file_stat);
	conn->file_size = file_stat.st_size;
	dlog(LOG_DEBUG, "File size: %ld\n", conn->file_size);

	return 0;
}

void connection_complete_async_io(struct connection *conn)
{
	/* TODO: Complete asynchronous operation; operation returns successfully.
	 * Prepare socket for sending.
	 */

	if (conn == NULL)
		return;

	struct io_event events[1];

	int num_events = io_getevents(conn->ctx, 1, 1, events, NULL);

	if (num_events < 0) {
		dlog(LOG_DEBUG, "io_getevents failed");
		return;
	}

	if (events[0].res < 0) {
		dlog(LOG_DEBUG, "Asynchronous I/O operation failed\n");
		return;
	}

	size_t bytes_read = events[0].res;

	conn->send_len = bytes_read;
	conn->file_pos += bytes_read;
	if (bytes_read == 0) {
		dlog(LOG_DEBUG, "bytes read e 0\n");
		conn->state = STATE_DATA_SENT;
		return;
	}
	conn->send_pos = 0;
	memcpy(conn->send_buffer, conn->recv_buffer, bytes_read);
	conn->state = STATE_SENDING_DATA;
}

int parse_header(struct connection *conn)
{
	/* TODO: Parse the HTTP header and extract the file path. */
	/* Use mostly null settings except for on_path callback. */

	if (conn == NULL)
		return -1;

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
		.on_message_complete = 0};

	size_t nparsed = http_parser_execute(&conn->request_parser, &settings_on_path,
										 conn->recv_buffer, conn->recv_len);

	if (nparsed > 0)
		dlog(LOG_DEBUG, "Parsed %ld bytes si request path e: %s\n", nparsed, conn->request_path);

	return nparsed;
}

enum connection_state connection_send_static(struct connection *conn)
{
	/* TODO: Send static data using sendfile(2). */

	if (conn == NULL)
		return STATE_CONNECTION_CLOSED;

	dlog(LOG_DEBUG, "Sunt in send static, fd = %d\n", conn->fd);

	off_t offset = conn->file_pos;
	ssize_t bytes_sent;

	bytes_sent = sendfile(conn->sockfd, conn->fd, &offset, conn->file_size - conn->file_pos);

	if (bytes_sent <= 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return STATE_SENDING_DATA;
		else
			return STATE_CONNECTION_CLOSED;
	}

	conn->file_pos += bytes_sent;

	if (conn->file_pos >= conn->file_size)
		return STATE_DATA_SENT;
	else
		return STATE_SENDING_DATA;
}

int connection_send_data(struct connection *conn)
{
	/* May be used as a helper function. */
	/* TODO: Send as much data as possible from the connection send buffer.
	 * Returns the number of bytes sent or -1 if an error occurred
	 */
	if (conn == NULL)
		return -1;

	if (conn->send_len == 0 || conn->send_pos >= conn->send_len)
		return 0;

	size_t to_send = conn->send_len - conn->send_pos;

	ssize_t bytes_sent = send(conn->sockfd, conn->send_buffer + conn->send_pos, to_send, 0);

	if (bytes_sent > 0) {
		char sent_string[BUFSIZ];

		memcpy(sent_string, conn->send_buffer + conn->send_pos, bytes_sent);
		sent_string[bytes_sent] = '\0';
		dlog(LOG_DEBUG, "AM TRIMIS : %ld bytes :\n", bytes_sent);
		conn->send_pos += bytes_sent;

		return bytes_sent;
	} else if (bytes_sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
		return 0;
	} else {
		return -1;
	}
}

int connection_send_dynamic(struct connection *conn)
{
	/* TODO: Read data asynchronously.
	 * Returns 0 on success and -1 on error.
	 */

	if (conn == NULL || conn->fd < 0)
		return -1;

	if (conn->state != STATE_ASYNC_ONGOING)
		connection_start_async_io(conn);

	else
		connection_complete_async_io(conn);

	return 0;
}

void handle_input(struct connection *conn)
{
	/* TODO: Handle input information: may be a new message or notification of
	 * completion of an asynchronous I/O operation.
	 */

	if (conn == NULL) {
		dlog(LOG_DEBUG, "Connection is NULL\n");
		return;
	}

	switch (conn->state) {
	case 0:
		dlog(LOG_DEBUG, "Initial state\n");
		receive_data(conn);
		break;

	case STATE_RECEIVING_DATA:
		dlog(LOG_DEBUG, "Receiving data\n");
		receive_data(conn);
		break;

	case STATE_CONNECTION_CLOSED:
		connection_remove(conn);
		break;

	default:
		break;
	}
}

void handle_output(struct connection *conn)
{
	/* TODO: Handle output information: may be a new valid requests or notification of
	 * completion of an asynchronous I/O operation or invalid requests.
	 */

	if (conn == NULL)
		return;

	switch (conn->state) {
	case STATE_REQUEST_RECEIVED:
		dlog(LOG_DEBUG, "Request received\n");
		parse_header(conn);
		dlog(LOG_DEBUG, "Request parsed\n");
		conn->res_type = connection_get_resource_type(conn);
		dlog(LOG_DEBUG, "\nResource type: %d\n", conn->res_type);

		if (conn->res_type == RESOURCE_TYPE_STATIC || conn->res_type == RESOURCE_TYPE_DYNAMIC) {
			if (connection_open_file(conn) == -1)
				break;

			dlog(LOG_DEBUG, "File opened, fd = %d\n", conn->fd);
			dlog(LOG_DEBUG, "prepare send reply header\n");
			connection_prepare_send_reply_header(conn);
			dlog(LOG_DEBUG, "reply header prepared\n");
		} else {
			dlog(LOG_DEBUG, "prepare send 404\n");
			connection_prepare_send_404(conn);
			dlog(LOG_DEBUG, "404 prepared\n");
		}
		break;

	case STATE_SENDING_HEADER:
		if (connection_send_data(conn) < 0) {
			conn->state = STATE_CONNECTION_CLOSED;
		} else if (conn->send_pos == conn->send_len) {
			conn->state = STATE_HEADER_SENT;
			conn->send_buffer[0] = '\0';
			conn->send_len = 0;
			conn->send_pos = 0;
		}
		break;

	case STATE_SENDING_404:
		if (connection_send_data(conn) < 0) {
			conn->state = STATE_CONNECTION_CLOSED;
		} else if (conn->send_pos == conn->send_len) {
			conn->state = STATE_404_SENT;
			conn->send_len = 0;
			conn->send_pos = 0;
			dlog(LOG_DEBUG, "sent 404 : %s\n", conn->send_buffer);
		}
		break;

	case STATE_HEADER_SENT:
		conn->state = STATE_SENDING_DATA;
		break;

	case STATE_404_SENT:
		conn->state = STATE_CONNECTION_CLOSED;
		break;

	case STATE_SENDING_DATA:
		if (conn->res_type == RESOURCE_TYPE_STATIC) {
			conn->state = connection_send_static(conn);
		} else if (conn->res_type == RESOURCE_TYPE_DYNAMIC) {
			if (conn->file_pos == 0) {
				dlog(LOG_INFO, "file pos e 0 si fac async start\n");
				connection_send_dynamic(conn);
				break;
			}

			dlog(LOG_INFO, "trimit cu pos %ld si len %ld\n", conn->send_pos, conn->send_len);

			if (connection_send_data(conn) == 0)
				connection_send_dynamic(conn);

		} else {
			dlog(LOG_INFO, "Connection closed");
			conn->state = STATE_CONNECTION_CLOSED;
		}
		break;

	case STATE_ASYNC_ONGOING:
		connection_send_dynamic(conn);
		break;
	case STATE_DATA_SENT:
		conn->state = STATE_CONNECTION_CLOSED;
		break;

	case STATE_CONNECTION_CLOSED:
		connection_remove(conn);
		break;
	default:
		break;
	}
}

void handle_client(uint32_t event, struct connection *conn)
{
	/* TODO: Handle new client. There can be input and output connections.
	 * Take care of what happened at the end of a connection.
	 */
	if (conn == NULL) {
		dlog(LOG_DEBUG, "Connection is NULL\n");
		return;
	}
	if (event & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
		dlog(LOG_DEBUG, "Socket error or hang-up\n");
		connection_remove(conn);
		return;
	}

	if (event & EPOLLIN) {
		handle_input(conn);
		if (conn != NULL && (conn->state == STATE_REQUEST_RECEIVED)) {
			w_epoll_remove_ptr(epollfd, conn->sockfd, conn);
			w_epoll_add_ptr_out(epollfd, conn->sockfd, conn);
		}
	}

	if (event & EPOLLOUT) {
		dlog(LOG_DEBUG, "Entering handle_output\n");
		handle_output(conn);
	}
}

int main(void)
{
	int rc;

	/* TODO: Initialize asynchronous operations. */

	ctx = 0;
	rc = io_setup(NR_EVENTS, &ctx);
	DIE(rc < 0, "io_setup");

	/* TODO: Initialize multiplexing. */
	epollfd = w_epoll_create();
	DIE(epollfd < 0, "w_epoll_create");

	/* TODO: Create server socket. */
	listenfd = tcp_create_listener(AWS_LISTEN_PORT, DEFAULT_LISTEN_BACKLOG);
	DIE(listenfd < 0, "tcp_create_listener");

	/* TODO: Add server socket to epoll object*/
	rc = w_epoll_add_fd_in(epollfd, listenfd);
	DIE(rc < 0, "w_epoll_add_fd_in");

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

		if (rev.data.fd == listenfd && (rev.events & EPOLLIN)) {
			dlog(LOG_DEBUG, "New connection\n");
			handle_new_connection();
		} else {
			if (rev.events & EPOLLIN) {
				dlog(LOG_DEBUG, "Readable client socket\n");
				handle_client(rev.events, rev.data.ptr);
			} else if (rev.events & EPOLLOUT) {
				dlog(LOG_DEBUG, "Writable client socket\n");
				handle_client(rev.events, rev.data.ptr);
			}
		}
	}
	close(listenfd);

	return 0;
}
