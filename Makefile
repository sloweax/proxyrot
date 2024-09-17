CC=cc
CFLAGS=-Wall -Wextra
LIBS=-lpthread
BINDSTPATH=/usr/local/bin

all: proxyrot

debug: CFLAGS+= -g
debug: all

%.o: %.c
	$(CC) $(CFLAGS) $< -c -o $@

proxyrot: proxyrot.o util.o socks5.o proxy.o
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

clean:
	rm -f *.o *.out proxyrot

install: all
	mkdir -p $(BINDSTPATH)
	install -m755 proxyrot $(BINDSTPATH)

uninstall:
	rm -f $(BINDSTPATH)/proxyrot

.PHONY: clean all install uninstall debug
