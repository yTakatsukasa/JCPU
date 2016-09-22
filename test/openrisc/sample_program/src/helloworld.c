void my_putc(char c) {
    *(volatile unsigned char *)(0x60000004) = c;
}

void my_puts(const char *s) {
    for( ; *s != '\0'; ++s) my_putc(*s);
}


int main(int argc, char*argv[])
{
    (void) argc;
    (void) argv;
    my_puts("Hello world\n");
    return 0;
}
