#define _GNU_SOURCE
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/select.h>
#include <errno.h>
#include <assert.h>
#include <sys/epoll.h>

struct _epoll_data {
	int epfd;
	int pipefd;
};

struct _select_fds {
	fd_set readfds;
	fd_set writefds;
	fd_set exceptfds;
};

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
struct epoll_event *matrix[FD_SETSIZE][FD_SETSIZE] = { { 0 } };

static int
_wait(int epfd, const struct timespec *timespec, const sigset_t *sigmask, struct _select_fds *fds)
{
	int maxfd = 0;

	if (fds == NULL)
		fds = alloca(sizeof *fds);
	if (fds == NULL)
		return ENOMEM;

	memset(fds, 0, sizeof *fds);

	for (int fd = 0; fd < FD_SETSIZE; fd++) {
		struct epoll_event *source;

		pthread_mutex_lock(&mutex);

		source = matrix[epfd][fd];
		if (!source) {
			pthread_mutex_unlock(&mutex);
			continue;
		}

		if (fd > maxfd)
			maxfd = fd;

		if (source->events & EPOLLIN)
			FD_SET(fd, &fds->readfds);
		if (source->events & EPOLLOUT)
			FD_SET(fd, &fds->writefds);
		if (source->events & EPOLLERR)
			FD_SET(fd, &fds->exceptfds);

		pthread_mutex_unlock(&mutex);
	}

	return pselect(maxfd + 1, &fds->readfds, &fds->writefds, &fds->exceptfds, timespec, sigmask);
}

void *
_epoll(void *arg)
{
	struct _epoll_data *data = arg;
	int status;
	ssize_t size;
	int one = 1;

	while ((status = _wait(data->epfd, NULL, NULL, NULL)) > -1) {
		size = write(data->pipefd, &one, sizeof one);
		assert(size == sizeof one);
	}


	// When not ready:
	// TODO: wait for at least one file descriptor to get ready
	// TODO: wait for a new file descriptor to be added, retart waiting with the new fd; can use eventfd for that


	// When ready:
	// TODO: wait until the event is picked up and start again to avoid tight loop



	/* We rely on the whole message being transferred at once. */
	close(data->pipefd);

	free(data);
	return NULL;
}

int
epoll_create1(int flags)
{
	struct _epoll_data *data;
	pthread_t thread;
	int pipefd[2];
	int status;

	data = malloc(sizeof *data);
	if (!data)
		goto fail_alloc;

	status = pipe2(pipefd, O_DIRECT | (flags & (O_CLOEXEC | O_NONBLOCK)));
	if (status == -1)
		goto fail_pipe;

	data->epfd = pipefd[0];
	data->pipefd = pipefd[1];

	pthread_mutex_lock(&mutex);

	matrix[data->epfd][data->epfd] = calloc(1, sizeof *matrix[0][0]);

	pthread_mutex_unlock(&mutex);

	status = pthread_create(&thread, NULL, _epoll, data);
	if (status == -1)
		goto fail_thread;

	return pipefd[0];
fail_thread:
	close(pipefd[0]);
	close(pipefd[1]);
fail_pipe:
	free(data);
fail_alloc:
	return -1;
}

int
epoll_create(int size)
{
	if (size <= 0) {
		errno = EINVAL;
		return -1;
	}

	return epoll_create1(0);
}

static int
_modify(int epfd, int fd, const struct epoll_event *event)
{
	struct epoll_event **slot;

	// TODO: check event data for unsupported flags
	// TODO: signal added file descriptor

	pthread_mutex_lock(&mutex);

	slot = &matrix[epfd][fd];

	if (*slot)
		free(*slot);

	*slot = malloc(sizeof **slot);
	if (!*slot) {
		pthread_mutex_unlock(&mutex);
		return ENOMEM;
	}

	memcpy(*slot, event, sizeof **slot);

	pthread_mutex_unlock(&mutex);

	return 0;
}

static int
_epoll_ctl(int epfd, int op, int fd, const struct epoll_event *event)
{
	if (epfd < 0 || epfd >= FD_SETSIZE || fd < 0 || fd >= FD_SETSIZE)
		return EBADF;

	if (epfd == fd)
		return EINVAL;

	if (!matrix[epfd][epfd])
		return EINVAL;

	switch (op) {
	case EPOLL_CTL_ADD:
		if (matrix[epfd][fd])
			return EEXIST;
		return _modify(epfd, fd, event);
	case EPOLL_CTL_MOD:
		if (matrix[epfd][fd])
			return ENOENT;
		return _modify(epfd, fd, event);
		break;
	case EPOLL_CTL_DEL:
		if (matrix[epfd][fd])
			return ENOENT;
		return _modify(epfd, fd, NULL);
		break;
	default:
		return EINVAL;
	}
}

int
epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
	int status;

	status = _epoll_ctl(epfd, op, fd, event);
	if (status == 0)
		return 0;
	else {
		errno = status;
		return -1;
	}
}

static struct timespec *
_int2ts_r(int timeout, struct timespec *tsbuf)
{
	switch (timeout) {
	case -1:
		return NULL;
	default:
		tsbuf->tv_sec = timeout;
		tsbuf->tv_nsec = 0;
		return tsbuf;
	}
}

int
epoll_pwait(int epfd, struct epoll_event *revents, int maxevents, int timeout, const sigset_t *sigmask)
{
	int count = 0;
	struct timespec tsbuf;
	struct _select_fds fds;

	_wait(epfd, _int2ts_r(timeout, &tsbuf), sigmask, &fds);
	// TODO: Check status.

	for (int fd = 0; fd < FD_SETSIZE; fd++) {
		int events = 0;

		if (count >= maxevents)
			break;
		if (!matrix[epfd][fd])
			continue;

		if (FD_ISSET(fd, &fds.readfds))
			events |= EPOLLIN;
		if (FD_ISSET(fd, &fds.writefds))
			events |= EPOLLOUT;
		if (FD_ISSET(fd, &fds.exceptfds))
			events |= EPOLLERR;

		if (events == 0)
			continue;

		revents[count].events = events;
		revents[count].data = matrix[epfd][fd]->data;
		count++;
	}

	return count;
}

int
epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
	return epoll_pwait(epfd, events, maxevents, timeout, NULL);
}

