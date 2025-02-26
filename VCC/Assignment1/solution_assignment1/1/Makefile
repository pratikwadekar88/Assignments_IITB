KDIR := /lib/modules/`uname -r`/build

UPROGS := test1 test2

all: kbuild $(UPROGS)

kbuild:
	make -C $(KDIR) M=`pwd`

$(UPROGS): %:  %.c
	gcc -o $@ $<

clean:
	make -C $(KDIR) M=`pwd` clean
	rm -f $(UPROGS)

.PHONY: all kbuild clean
