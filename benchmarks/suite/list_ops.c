#include <stdio.h>
int main(void) {
    int nums[20];
    for (int k = 0; k < 20; k++) nums[k] = k;
    long long sum = 0;
    for (int i = 0; i < 2000000; i++)
        for (int k = 0; k < 20; k++)
            sum += nums[k];
    printf("%lld\n", sum);
    return 0;
}
