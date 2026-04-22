CFLAGS=-shared -fPIC -O2 -fopenmp


all: libnest.so

libnest.so: main.c
	$(CC) $(CFLAGS) $^ -o $@

install:
	mkdir -p $(prefix)/lib
	cp ./libnest.so $(prefix)/lib/

.PHONY: clean
clean:
	rm -f main
