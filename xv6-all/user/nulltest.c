#include "kernel/types.h"
#include "user/user.h"
#include "kernel/memlayout.h"

int main(int argc, char *argv[]) {
    char *ptr = NULL;
    printf("%c\n", *ptr);  // deference null
    exit(0);
}