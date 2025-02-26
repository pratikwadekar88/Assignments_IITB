KDIR = /lib/modules/`uname -r`/build
CC = gcc
CFLAGS = -Wall -Wextra -g

all: kbuild test1 test2

kbuild:
	make -C $(KDIR) M=`pwd`

test1: test1.c
	$(CC) $(CFLAGS) -o test1 test1.c

test2: test2.c
	$(CC) $(CFLAGS) -o test2 test2.c

clean:
	make -C $(KDIR) M=`pwd` clean
	rm -f test1 test2
