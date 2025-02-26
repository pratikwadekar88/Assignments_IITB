KDIR = /lib/modules/`uname -r`/build

kbuild:
	make -C $(KDIR) M=`pwd`
	gcc -o dev_user dev_user.c

clean:
	make -C $(KDIR) M=`pwd` clean
	rm dev_user
