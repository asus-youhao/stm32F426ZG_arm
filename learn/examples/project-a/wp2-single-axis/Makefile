CFLAGS = -Wall -Wextra -O2
all: demo
demo: main.c
	$(CC) $(CFLAGS) -o $@ $<
run: demo
	./demo
clean:
	rm -f demo
.PHONY: all run clean
