CC = gcc

CFLAGS += -c -std=gnu99 -Wall -Werror -Wshadow -Wextra -O2 -D_FORTIFY_SOURCE=2
CFLAGS += -fstack-protector-all -D_GNU_SOURCE -MP -MMD -g -D_DEBUG

LDFLAGS += -lcunit -lz

_EXCLUDE = main.c
SOURCES = $(wildcard *.c) $(wildcard ../src/common/*.c) ../src/receiver/receive.c
OBJECTS = $(SOURCES:.c=.o)
	
all: clean exec test_exec
	./test_exec

test_exec: $(OBJECTS) 
	$(CC) -o test_exec $(LDFLAGS) $(OBJECTS)

.PHONY: clean mrproper test_exec

exec:
	@make -C .. debug

clean:
	@rm -f $(OBJECTS)

mrproper:
	@rm -f test_exec
