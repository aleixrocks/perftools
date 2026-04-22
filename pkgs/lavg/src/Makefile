CFLAGS=-ggdb3
LDFLAGS=-lm

all: main

main: main.c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

install:
	mkdir -p $(prefix)/bin
	cp ./main $(prefix)/bin/lavg
	cp ./plot.py $(prefix)/bin/plot-lavg

.PHONY: clean
clean:
	rm -f main
