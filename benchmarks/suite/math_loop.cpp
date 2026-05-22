#include <cstdio>
int main() {
    long long sum = 0, N = 50000000LL;
    for (long long i = 0; i < N; i++) sum += i * 2LL + 1LL;
    printf("%lld\n", sum);
}
