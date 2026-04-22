CFLAGS=-shared -fPIC -O2


all: libpthtree.so

libpthtree.so: main.c
	$(CC) $(CFLAGS) $^ -o $@

install:
	mkdir -p $(prefix)/lib $(prefix)/bin
	cp ./libpthtree.so $(prefix)/lib/
	cp ./pthtree.sh $(prefix)/bin/pthtree

.PHONY: clean
clean:
	rm -f libpthtree.so
