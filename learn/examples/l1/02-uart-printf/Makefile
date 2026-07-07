CFLAGS = -Wall -Wextra -O2 -DHOST_SIM
all: demo
demo: main.c
	$(CC) $(CFLAGS) -o $@ $<
run: demo
	./demo
clean:
	rm -f demo
.PHONY: all run clean
