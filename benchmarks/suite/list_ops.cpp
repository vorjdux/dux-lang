#include <cstdio>
#include <vector>
int main() {
    std::vector<int> nums;
    for (int k = 0; k < 20; k++) nums.push_back(k);
    long long sum = 0;
    for (int i = 0; i < 2000000; i++)
        for (int v : nums)
            sum += v;
    printf("%lld\n", sum);
}
