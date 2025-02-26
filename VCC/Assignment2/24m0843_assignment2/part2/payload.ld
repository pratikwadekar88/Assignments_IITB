SECTIONS
{
        .payload3a 0 : {
                guest3a = .;
                guest3a.img.o
                guest3a_end = .;
        }
        .payload3b 0 : AT(LOADADDR(.payload3a)+SIZEOF(.payload3a)) {
                guest3b = .;
                guest3b.img.o
                guest3b_end = .;
        }
        .payload4a 0 : AT(LOADADDR(.payload3b)+SIZEOF(.payload3b)) {
                guest4a = .;
                guest4a.img.o
                guest4a_end = .;
        }
        .payload4b 0 : AT(LOADADDR(.payload4a)+SIZEOF(.payload4a)) {
                guest4b = .;
                guest4b.img.o
                guest4b_end = .;
        }      
}
