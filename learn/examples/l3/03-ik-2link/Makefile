CFLAGS = -Wall -Wextra -O2
all: demo
demo: main.c
	$(CC) $(CFLAGS) -o $@ $< -lm
run: demo
	./demo
clean:
	rm -f demo
.PHONY: all run clean
