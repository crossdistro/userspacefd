#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <stdio.h>

#include <msg.h>

#define ARRAY_SIZE(a) (sizeof (a) / sizeof *(a))

#define debug(msg, ...) fprintf(stderr, "%s %d: " __FILE__ ":%d: " msg "\n", __FUNCTION__, getpid(), __LINE__, ##__VA_ARGS__)
#define assert_true(condition, ...) if (!(condition)) { fprintf(stderr, "%s %d: " __FILE__ ":%d: Assertion %s failed.\n", __FUNCTION__, getpid(), __LINE__, #condition); abort(); }

#define N 100

void
client(int server)
{
	char smsg[256] = "Hello Server!";
	char rmsg[64];
	int ret;

	ret = Send(server, &smsg, &rmsg, sizeof smsg, sizeof rmsg);
	assert_true(ret == 0);
	assert_true(strcmp(rmsg, "Hello Client!") == 0);
}

void
server(void)
{
	char smsg[1024];
	char rmsg[32] = "Hello Client!";
	int clients[N];
	int i;
	int ret;

	/* Receive calls from clients */
	for (i = 0; i < N; i++) {
		int ret;

		/* First peek a small part of a new message. */
		memset(smsg, 0, sizeof smsg);
		ret = Receive(0, smsg, 5);
		assert_true(ret > 0);
		assert_true(strcmp(smsg, "Hello") == 0);
		clients[i] = ret;

		/* Now retrieve a bigger part */
		memset(smsg, 0, sizeof smsg);
		ret = Receive(clients[i], smsg, sizeof smsg);
		assert_true(ret > 0);
		assert_true(strcmp(smsg, "Hello Server!") == 0);
		assert_true(ret == clients[i]);
	}

	/* Reply to clients */
	for (i = 0; i < N; i++) {
		ret = Reply(clients[i], &rmsg, sizeof rmsg);
		assert_true(ret == 0);
	}
}

int
main(int argc, char **argv)
{
	int i;
	int pid;
	/* TODO: Think about process and thread ids. */
	int srvpid = getpid();

	for (i = 0; i < N; i++) {
		pid = fork();
		if (pid == 0) {
			sleep(1);
			client(srvpid);
			exit(EXIT_SUCCESS);
		}
		assert_true(pid > 0);
	}

	server();

	for (i = 0; i < N; i++) {
		int wstatus;

		pid = wait(&wstatus);
		assert_true(WIFEXITED(wstatus));
		assert_true(WEXITSTATUS(wstatus) == 0);
		assert_true(pid > 0);
	}

	return EXIT_SUCCESS;
}
