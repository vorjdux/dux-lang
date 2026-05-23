#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define N 50000
int main(void) {
    char *buf = (char *)malloc(N + 1);
    buf[0] = '\0';
    for (int i = 0; i < N; i++) {
        buf[i] = 'x';
        buf[i+1] = '\0';
    }
    printf("%zu\n", strlen(buf));
    free(buf);
    return 0;
}
