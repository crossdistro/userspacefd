CFLAGS=-ggdb -Wall -Werror -std=c99 -Ilib
LDFLAGS=-lpthread

all: test_waitfd test_epoll
test_waitfd: test_waitfd.o lib/waitfd.o
	gcc $(LDFLAGS) -o $@ $^
test_epoll: test_epoll.o lib/epoll.o
	gcc $(LDFLAGS) -o $@ $^
clean:
	rm waitfd *.o
