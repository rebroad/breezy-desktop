CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -fPIC -O2
LDFLAGS = -shared

OBJS = breezy_math.o
LIB = libbreezy_math.so
HEADER = breezy_math.h

all: $(LIB)

$(LIB): $(OBJS)
	$(CC) $(LDFLAGS) -o $(LIB) $(OBJS)

$(OBJS): %.o: %.c $(HEADER)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(LIB)

install: $(LIB)
	# Not installing by default - library is used directly via source

.PHONY: all clean install

