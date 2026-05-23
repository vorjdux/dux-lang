#include <cstdio>
#include <string>
int main() {
    std::string s;
    s.reserve(50000);
    for (int i = 0; i < 50000; i++) s += 'x';
    printf("%zu\n", s.size());
}
