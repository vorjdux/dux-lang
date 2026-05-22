#include <stdio.h>
int main(void) {
    long long sum = 0, i, N = 50000000LL;
    for (i = 0; i < N; i++) sum += i * 2LL + 1LL;
    printf("%lld\n", sum);
    return 0;
}
