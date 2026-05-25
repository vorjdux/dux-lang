#include <cstdio>
#include <string>
int main() {
    long long sum = 0;
    const int N = 500000;
    for (int i = 0; i < N; i++) {
        std::string s = std::string("item=") + std::to_string(i);
        sum += (long long)s.size();
    }
    printf("%lld\n", sum);
}
