
TARGETS = control_station soldier

obj-m += chardev.o

SOURCES = $(TARGETS:=.c)
KDIR = /lib/modules/`uname -r`/build

all: $(TARGETS) kbuild

$(TARGETS): $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $@.c

kbuild:
	make -C $(KDIR) M=`pwd` modules

clean:
	rm -f $(TARGETS)
	make -C $(KDIR) M=`pwd` clean


