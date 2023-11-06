#include "kernel/types.h"
#include "user/user.h"


int main(int argc, char *argv[]) {
    vmprint(1);
    char *long_content = malloc(2076656);
    long_content[0] = 'c';
    printf("content:%c\n", long_content[0]);
    vmprint(1);
    char *long_content2 = malloc(2097152-16);
    long_content2[0] = 'b';
    printf("content:%c\n", long_content2[0]);
    vmprint(1);
    char *long_content3 = malloc(2097152);
    long_content3[0] = 'a';
    printf("content:%c\n", long_content3[0]);
    vmprint(1);
    char *long_content4 = malloc(4096 - 16);
    long_content4[0] = 'd';
    printf("content:%c\n", long_content4[0]);
    vmprint(1);
    char *long_content5 = malloc(2097152 - 32 - 4096);
    long_content5[0] = 'e';
    printf("content:%c\n", long_content5[0]);
    vmprint(1);

    free(long_content5);
    free(long_content4);
    free(long_content3);
    free(long_content2);
    free(long_content);
    
    exit(0);
}