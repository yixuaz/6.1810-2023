#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    char *ptr = (char *)0x0;
    printf("%c\n", *ptr);  // deference null
    exit(0);
}