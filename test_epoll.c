#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#include <sys/epoll.h>

int
main(int argc, char **argv)
{
	char *data = "my test string";
	char buf[16];
	int pipefd[2];
	int epfd;
	int status;
	struct epoll_event event = { .events = EPOLLIN, .data = { .ptr = data } };
	struct epoll_event revent;

	status = pipe2(pipefd, O_DIRECT);
	assert(status == 0);

	status = write(pipefd[1], data, strlen(data));
	assert(status == strlen(data));

	epfd = epoll_create1(0);
	assert(epfd > 2);

	status = epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd[0], &event);
	assert(status == 0);

	status = epoll_wait(epfd, &revent, 1, -1);
	assert(status == 1);\
	assert(revent.events == EPOLLIN);
	assert(revent.data.ptr == data);

	status = read(pipefd[0], buf, sizeof buf);
	assert(status == strlen(data));

	status = epoll_wait(epfd, &revent, 1, 0);
	assert(status == 0);

	return EXIT_SUCCESS;
}
