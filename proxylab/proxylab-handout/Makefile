#
# Makefile for Proxy Lab 
#
# You may modify this file any way you like (except for the handin
# rule). Autolab will execute the command "make" on your specific 
# Makefile to build your proxy from sources.
#
CC = gcc
CFLAGS = -g -Werror
LDFLAGS = -lpthread

all: proxy proxy_concurr

csapp.o: csapp.c csapp.h
	$(CC) $(CFLAGS) -c csapp.c

cache.o: cache.c csapp.h
	$(CC) $(CFLAGS) -c cache.c

proxy.o: proxy.c csapp.h
	$(CC) $(CFLAGS) -c proxy.c

proxy: proxy.o csapp.o cache.o
	$(CC) $(CFLAGS) proxy.o cache.o csapp.o -o proxy $(LDFLAGS)

proxy_concurr.o: proxy_concurr.c csapp.h
	$(CC) $(CFLAGS) -c proxy_concurr.c

proxy_concurr: proxy_concurr.o csapp.o
	$(CC) $(CFLAGS) proxy_concurr.o csapp.o -o proxy_concurr $(LDFLAGS)

# Creates a tarball in ../proxylab-handin.tar that you should then
# hand in to Autolab. DO NOT MODIFY THIS!
handin:
	(make clean; cd ..; tar czvf proxylab-handin.tar.gz proxylab-handout)

clean:
	rm -f *~ *.o proxy core *.tar *.zip *.gzip *.bzip *.gz


