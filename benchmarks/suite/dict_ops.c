#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAP 262144

typedef struct { char key[16]; int val; int used; } Entry;
static Entry table[CAP];

static unsigned hash_key(const char *k) {
    unsigned h = 5381;
    while (*k) h = h * 33 ^ (unsigned char)*k++;
    return h;
}
static void ht_set(const char *k, int v) {
    unsigned h = hash_key(k) & (CAP - 1);
    while (table[h].used && strcmp(table[h].key, k) != 0) h = (h + 1) & (CAP - 1);
    strncpy(table[h].key, k, 15);
    table[h].val = v;
    table[h].used = 1;
}
static int ht_get(const char *k) {
    unsigned h = hash_key(k) & (CAP - 1);
    while (table[h].used && strcmp(table[h].key, k) != 0) h = (h + 1) & (CAP - 1);
    return table[h].used ? table[h].val : 0;
}

int main(void) {
    int N = 100000;
    char key[16];
    for (int i = 0; i < N; i++) {
        snprintf(key, sizeof(key), "k%d", i);
        ht_set(key, i);
    }
    long long sum = 0;
    for (int j = 0; j < N; j++) {
        snprintf(key, sizeof(key), "k%d", j);
        sum += ht_get(key);
    }
    printf("%lld\n", sum);
    return 0;
}
