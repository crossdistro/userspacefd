#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include <msg.h>
#include <debug.h>

struct _message {
	char msg[65536];
	size_t size;
	int pid;
	int sock;
	bool accepted; /* Already passed to the caller. */

	struct _message *next;
};

struct _channel {
	int pid;
	int sock;

	/* Linked list of messages waiting for Receive or Reply. */
	struct _message *pending;
};

static int
_getpid(void)
{
	static int pid = -1;

	if (pid == -1)
		pid = getpid();

	return pid;
}

static struct _channel *
_listen(void)
{
	struct sockaddr_un sun = { .sun_family = AF_UNIX };
	int ret;

	/* TODO: Not thread-safe. Consider using a thread-local channel
	 * together with `gettid()` rather than a global channel with
	 * `getpid()`.
	 */
	static struct _channel channel = {
		.sock = -1,
	};

	if (channel.sock != -1)
		return &channel;

	channel.pid = _getpid();

	channel.sock = socket(sun.sun_family, SOCK_SEQPACKET, 0);
	if (channel.sock == -1)
		return NULL;

	snprintf(sun.sun_path + 1, sizeof sun.sun_path - 1, "qnx4compat.msg.%d", channel.pid);
	ret = bind(channel.sock, (const struct sockaddr *) &sun, sizeof sun);
	if (ret == -1)
		goto fail;

	ret = listen(channel.sock, SOMAXCONN);
	if (ret == -1)
		goto fail;

	debug("Listening for clients (pid=%d fd=%d).", channel.pid, channel.sock);
	return &channel;
fail:
	debug("Listening failed.");
	close(channel.sock);
	channel.sock = -1;
	return NULL;
}

static int
_accept(struct _channel *channel, int *peer)
{
	struct ucred ucred;
	socklen_t len;
	int sock;
	int ret;

	sock = accept(channel->sock, NULL, NULL);
	if (sock == -1)
		goto fail;

	len = sizeof ucred;
	ret = getsockopt(sock, SOL_SOCKET, SO_PEERCRED, &ucred, &len);
	if (ret == -1)
		goto fail;

	if (peer)
		*peer = ucred.pid;

	debug("Accepted connection (pid=%d peer=%d fd=%d).", channel->pid, ucred.pid, sock);
	return sock;
fail:
	debug("Accept failed: %s", strerror(errno));
	return -1;
}

static int
_connect(int pid)
{
	struct sockaddr_un sun = { .sun_family = AF_UNIX };
	int sock;
	int ret;

	snprintf(sun.sun_path + 1, sizeof sun.sun_path - 1, "qnx4compat.msg.%d", pid);

	sock = socket(sun.sun_family, SOCK_SEQPACKET, 0);
	if (sock == -1)
		goto fail_sock;

	ret = connect(sock, (const struct sockaddr *) &sun, sizeof sun);
	if (ret == -1)
		goto fail;

	debug("Connected to server (pid=%d fd=%d).", _getpid(), sock);
	return sock;
fail:
	close(sock);
fail_sock:
	debug("Connection failed: %s", strerror(errno));
	return -1;
}

static ssize_t
_send(int fd, const void *buf, size_t len)
{
	ssize_t ret;

	ret = send(fd, buf, len, 0);

	if (ret > 0)
		debug("Sent %zd of %zd characters (pid=%d fd=%d).", ret, len, _getpid(), fd);
	else
		debug("Send failed: %s", strerror(errno));

	return ret;
}

static ssize_t
_recv(int fd, void *buf, size_t len)
{
	ssize_t ret;

	ret = recv(fd, buf, len, 0);

	if (ret > 0)
		debug("Received %zd of %zd characters (pid=%d fd=%d).", ret, len, _getpid(), fd);
	else if (ret == 0)
		debug("Receive failed due to the closed endpoint.");
	else
		debug("Receive failed: %s", strerror(errno));

	return ret;
}

static void
_close(int fd)
{
	debug("Socket closed (fd=%d).", fd);
	close(fd);
}

/*static void
_copy_to_iovec(struct iovec *iov, int parts, const char *buffer)
{
	size_t offset = 0;

	while (parts--) {
		memcpy(iov->iov_base, buffer + offset, iov->iov_len);
		offset += iov->iov_len;
		iov++;
	}
}*/

int
Send(int pid, const void *smsg, void *rmsg, size_t smsglen, size_t rmsglen)
{
	int sock;
	int ret;

	sock = _connect(pid);
	if (sock == -1)
		return -1;

	ret = _send(sock, smsg, smsglen);
	if (ret == -1)
		goto fail;

	/* TODO: Consider handling priority inversion. */

	ret = _recv(sock, rmsg, rmsglen);
	if (ret == -1)
		goto fail;
	if (ret == 0) {
		errno = EINVAL;
		goto fail;
	}

	_close(sock);
	return 0;
fail:
	_close(sock);
	return -1;
}


static struct _message *
_retrieve_message(struct _channel *channel, int fd, int peer)
{
	struct _message *message;
	ssize_t ret;

	message = malloc(sizeof *message);
	if (!message)
		return NULL;

	/* Receive one message from socket. */
	ret = _recv(fd, &message->msg, sizeof message->msg);
	if (ret == -1)
		goto fail;
	if (ret == 0) {
		/* Client gone. */
		errno = ESRCH;
		goto fail;
	}
	message->pid = peer;
	message->size = ret;
	message->sock = fd;

	/* Cache the message. */
	message->next = channel->pending;
	channel->pending = message;

	return message;
fail:
	free(message);
	return NULL;
}

static int
_peek_message(struct _message *message, void *smsg, size_t smsglen)
{
	/* Accept message if not accepted yet. */
	message->accepted = true;

	/* Keep size in limits. */
	if (smsglen > message->size)
		smsglen = message->size;

	/* Pass data to the caller. */
	memcpy(smsg, message->msg, smsglen);

	return message->pid;
}

int
Receive(int pid, void *smsg, size_t smsglen)
{
	struct _channel *channel;
	struct _message *message;
	int sock;
	int peer;

	/* TODO: We might want to translate socket errors. */
	channel = _listen();
	if (!channel)
		return -1;

	/* First look at already retrieved messages. */
	for (message = channel->pending; message; message = message->next) {
		if (pid) {
			/* Peek message data from a specific client if available. */
			if (message->pid == pid)
				return _peek_message(message, smsg, smsglen);
		} else {
			/* Peek message data from any unaccepted message. */
			if (!message->accepted)
				return _peek_message(message, smsg, smsglen);
		}
	};

	/* Retreive messages from the socket until there's one for the caller. */
	do {
		/* Wait for new client and accept. */
		sock = _accept(channel, &peer);
		if (sock == -1)
			return -1;

		/* Retrieve and enqueue the message from socket. */
		message = _retrieve_message(channel, sock, peer);
		if (!message)
			return -1;

	} while (pid && message->pid != pid);

	/* Peek message data from the new message. */
	return _peek_message(message, smsg, smsglen);
}

int
Reply(int pid, const void *rmsg, size_t rmsglen)
{
	struct _channel *channel = _listen();
	struct _message **message;
	int ret;

	for (message = &channel->pending; *message; message = &(*message)->next)
		if ((*message)->pid == pid)
			break;
	if (!*message) {
		errno = -ESRCH;
		return -1;
	}

	ret = _send((*message)->sock, rmsg, rmsglen);
	if (ret == -1)
		return -1;

	/* Clean up successfully replied message. */
	_close((*message)->sock);
	*message = (*message)->next;

	/* Report success */
	return 0;
}
