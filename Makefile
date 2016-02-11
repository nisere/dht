CC=gcc
CFLAGS=-Wall
#CFLAGS+= -DTEST_DISTRIBUTION
#CFLAGS+= -DTEST_THROUGHPUT
#CFLAGS+= -DTEST_AVAILABILITY
LDFLAGS=-lcrypto -lmemcached

dht:
	$(CC) $(CFLAGS) dht.c -o dht $(LDFLAGS)

clean:
	rm dht
