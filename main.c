#include <stdio.h>
#include <unistd.h>

int main(void) {
    printf("Hello from PS5! pid=%d uid=%d\n", getpid(), getuid());
    return 0;
}
