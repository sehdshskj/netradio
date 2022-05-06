#CFLAGS+=I../include/
CFLAGS+=-pthread
all:client

client:client.c 
#	gcc client.c -o client $(CFLAGS)
	gcc client.c -o client -Wall $(CFLAGS)

clean:
	rm -rf *.o client