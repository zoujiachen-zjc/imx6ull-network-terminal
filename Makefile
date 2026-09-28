CC=gcc
test:test.c
	$(CC) -o $@ $^

server:server.c
	$(CC) -o $@ $^
client:client.c
	$(CC) -o $@ $^
test_3:test_3.c
	$(CC) -o $@ $^

clean:
	rm -rf *.o