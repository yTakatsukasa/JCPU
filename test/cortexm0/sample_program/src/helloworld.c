volatile unsigned char *const PUTC_ADDR = (void *)0x60000004;
volatile unsigned char *const END_ADDR = (void *)0x80000000;
//unsigned char * PUTC_ADDR = (void *)0x40000000;

void put_str(const char *str)
{
    for(; *str != '\0'; ++str){
        *PUTC_ADDR = *str;
    }
}

void main()
{
    put_str("Hello, world\n");
    *END_ADDR = 0;
}

void _start(){
    main();
}
