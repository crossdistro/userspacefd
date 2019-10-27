CPPFLAGS = -D_GNU_SOURCE -DMSG_DEBUG
CFLAGS = -ggdb -pg -Wall -Werror -std=c99 -I.
LDFLAGS = -pg
TARGETS = test_msg

all: $(TARGETS)
test_msg.o msg.o: debug.h
test_msg: test_msg.o msg.o
	gcc $(LDFLAGS) -o $@ $^
clean:
	rm $(TARGETS) *.o
check: all
	./test_msg
