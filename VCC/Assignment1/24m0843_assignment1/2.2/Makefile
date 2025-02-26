
# TARGETS = control_station soldier

# SOURCES = $(TARGETS:=.c)

# all: $(TARGETS)

# $(TARGETS): $(SOURCES)
# 	$(CC) $(CFLAGS) -o $@ $@.c

# clean:
# 	rm -f $(TARGETS)

KDIR = /lib/modules/`uname -r`/build
kbuild:
	make -C $(KDIR) M=`pwd`
clean:
	make -C $(KDIR) M=`pwd` clean