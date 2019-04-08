CC = gcc

CFLAGS += -c -std=gnu99 -Wall -Werror -Wshadow -Wextra -O2 -D_FORTIFY_SOURCE=2
CFLAGS += -fstack-protector-all -D_GNU_SOURCE -MP -MMD 

LDFLAGS += -lz

S_SOURCES = $(wildcard src/sender/*.c)
R_SOURCES = $(wildcard src/receiver/*.c)
C_SOURCES = $(wildcard src/common/*.c)

S_OBJECTS = $(S_SOURCES:.c=.o)
R_OBJECTS = $(R_SOURCES:.c=.o)
C_OBJECTS = $(C_SOURCES:.c=.o)

# -MMD implicitely creates these
DEPS = $(S_SOURCES:.c=.d) $(R_SOURCES:.c=.d) $(C_SOURCES:.c=.d)

SENDER = sender
RECEIVER = receiver

all: $(C_OBJECTS) $(SENDER) $(RECEIVER)

debug: CFLAGS += -D_DEBUG -g -Wno-unused-parameter -fno-omit-frame-pointer
debug: clean all
		
$(SENDER): $(S_OBJECTS) $(C_OBJECTS)
		$(CC) $(S_OBJECTS) $(C_OBJECTS) -o $@ $(LDFLAGS)

$(RECEIVER): $(R_OBJECTS) $(C_OBJECTS) 
		$(CC) $(R_OBJECTS) $(C_OBJECTS) -o $@ $(LDFLAGS)

.c.o:
		$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)
		
.PHONY: clean mrproper

clean:
	@rm -f $(R_OBJECTS) $(C_OBJECTS) $(S_OBJECTS) $(DEPS)

mrproper:
	@rm -f $(SENDER) $(RECEIVER)

tests: all
	@./tests/run_tests.sh

# include the dependencies makefiles
-include $(DEPS)
