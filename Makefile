CC=cc
CFLAGS=-Wall -Wextra -g
LIBS=-lpthread

all: proxyrot

%.o: %.c
	$(CC) $(CFLAGS) $< -c -o $@

proxyrot: proxyrot.o util.o
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

clean:
	rm -f *.o *.out proxyrot

.PHONY: clean all
