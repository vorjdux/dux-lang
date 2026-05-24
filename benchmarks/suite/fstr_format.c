#include <stdio.h>
int main(void) {
    char buf[32];
    long long sum = 0;
    int N = 500000;
    for (int i = 0; i < N; i++) {
        int n = snprintf(buf, sizeof(buf), "item=%d", i);
        sum += n;
    }
    printf("%lld\n", sum);
    return 0;
}
