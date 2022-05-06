CFLAGS+= -Wall
CFLAGS+=-pthread
all:server
server:server.o thr_channel.o thr_list.o medialib.o mytbf.o thr_msgcntl.o
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

clean:
	rm *.o server -rf