CFLAGS = -Wall -Werror -O2 -Werror=array-bounds=0

all: emu emu1 emu2 emu3 emu4 guesta.bin guestb.bin guest1a.bin guest1b.bin guest2a.bin guest2b.bin 

emu:
	gcc emu.c -o emu -lpthread -g

emu1:
	gcc emu1.c -o emu1 -lpthread -g

emu2:
	gcc emu2.c -o emu2 -lpthread -g

emu3: emu3.o payload.o
	$(CC) $^ -o $@

emu4: emu4.o payload.o
	$(CC) $^ -o $@

payload.o: payload.ld guest3a.img.o guest3b.img.o guest4a.img.o guest4b.img.o
	$(LD) -r -T $< -o $@

guesta.bin: guesta.o
	ld -m elf_i386 --oformat binary -N -e _start -Ttext 0x10000 -o guesta.bin guesta.o

guestb.bin: guestb.o
	ld -m elf_i386 --oformat binary -N -e _start -Ttext 0x10000 -o guestb.bin guestb.o

guesta.o: guesta.s
	as -32 guesta.s -o guesta.o

guestb.o: guestb.s
	as -32 guestb.s -o guestb.o

guest1a.bin: guest1a.o
	ld -m elf_i386 --oformat binary -N -e _start -Ttext 0x10000 -o guest1a.bin guest1a.o

guest1b.bin: guest1b.o
	ld -m elf_i386 --oformat binary -N -e _start -Ttext 0x10000 -o guest1b.bin guest1b.o

guest2a.bin: guest2a.o
	ld -m elf_i386 --oformat binary -N -e _start -Ttext 0x10000 -o guest2a.bin guest2a.o

guest2b.bin: guest2b.o
	ld -m elf_i386 --oformat binary -N -e _start -Ttext 0x10000 -o guest2b.bin guest2b.o

guest1a.o: guest1a.s
	as -32 guest1a.s -o guest1a.o

guest1b.o: guest1b.s
	as -32 guest1b.s -o guest1b.o

guest2a.o: guest2a.s
	as -32 guest2a.s -o guest2a.o

guest2b.o: guest2b.s
	as -32 guest2b.s -o guest2b.o

guest3a.o: guest3a.c
	$(CC) $(CFLAGS) -m32 -ffreestanding -fno-pic -c -o $@ $^

guest3a.img: guest3a.o
	$(LD) -T guest.ld -m elf_i386 $^ -o $@

guest3b.o: guest3b.c
	$(CC) $(CFLAGS) -m32 -ffreestanding -fno-pic -c -o $@ $^

guest3b.img: guest3b.o
	$(LD) -T guest.ld -m elf_i386 $^ -o $@

guest4a.o: guest4a.c
	$(CC) $(CFLAGS) -m32 -ffreestanding -fno-pic -c -o $@ $^

guest4a.img: guest4a.o
	$(LD) -T guest.ld -m elf_i386 $^ -o $@

guest4b.o: guest4b.c
	$(CC) $(CFLAGS) -m32 -ffreestanding -fno-pic -c -o $@ $^

guest4b.img: guest4b.o
	$(LD) -T guest.ld -m elf_i386 $^ -o $@

%.img.o: %.img
	$(LD) -b binary -r $^ -o $@

.PHONY: clean
clean:
	$(RM) emu emu1 emu2 emu3 emu4 *.o *.img *.img.o *.bin \
