#define _GNU_SOURCE
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <assert.h>

#include "waitfd.h"

struct waitfd {
	int pid;
	int pipeout;
};

void *
_waitfd(void *arg)
{
	struct waitfd *data = arg;
	siginfo_t siginfo = { 0 };
	int status;
	ssize_t size;

	status = waitid(P_PID, data->pid, &siginfo, WEXITED);
	assert(status == 0);
	size = write(data->pipeout, &siginfo, sizeof siginfo);
	/* We rely on the whole message being transferred at once. */
	assert(size == sizeof siginfo);
	close(data->pipeout);

	free(data);
	return NULL;
}

// TODO: What about the rest of waitid input arguments?
int
waitfd(pid_t pid, int flags)
{
	struct waitfd *data;
	pthread_t thread;
	int pipefd[2];
	int status;

	data = malloc(sizeof *data);
	if (!data)
		goto fail_alloc;

	status = pipe2(pipefd, O_DIRECT | (flags & (O_CLOEXEC | O_NONBLOCK)));
	if (status == -1)
		goto fail_pipe;
	
	data->pid = pid;
	data->pipeout = pipefd[1];

	status = pthread_create(&thread, NULL, _waitfd, data);
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
