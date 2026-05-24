#include <cstdio>
#include <unordered_map>
#include <string>
int main() {
    const int N = 100000;
    std::unordered_map<std::string, int> d;
    d.reserve(N * 2);
    char key[16];
    for (int i = 0; i < N; i++) {
        snprintf(key, sizeof(key), "k%d", i);
        d[key] = i;
    }
    long long sum = 0;
    for (int j = 0; j < N; j++) {
        snprintf(key, sizeof(key), "k%d", j);
        sum += d[key];
    }
    printf("%lld\n", sum);
}
