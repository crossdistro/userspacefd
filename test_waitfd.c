#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <poll.h>
#include <fcntl.h>
#include <assert.h>

#include <waitfd.h>

int
main(int argc, char **argv)
{
	pid_t pid;

	pid = fork();
	switch (pid) {
	case -1:
		perror("fork");
		exit(EXIT_FAILURE);
	case 0:
		sleep(1);
		exit(42);
	default:
		{
			struct pollfd pollfd = { .fd = -1, .events = POLLIN };
			int status;
			siginfo_t siginfo;

			pollfd.fd = waitfd(pid, O_NONBLOCK);

			status = poll(&pollfd, 1, -1);
			switch (status) {
			case -1:
				perror("poll");
				exit(EXIT_FAILURE);
			case 0:
				abort();
			default:
				{
					int size;

					size = read(pollfd.fd, &siginfo, sizeof siginfo);
					assert(size == sizeof siginfo);
					assert(siginfo.si_code == CLD_EXITED);
					assert(siginfo.si_status == 42);
					exit(EXIT_SUCCESS);
				}
			}
		}
	}
}
