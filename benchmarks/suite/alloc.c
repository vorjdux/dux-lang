#include <stdio.h>
#include <stdlib.h>
typedef struct { int x; int y; } Node;
int main(void) {
    int N = 1000000, i, sink = 0;
    for (i = 0; i < N; i++) {
        Node *n = (Node *)malloc(sizeof(Node));
        n->x = i; n->y = i + 1;
        sink += n->x;
        free(n);
    }
    printf("%d\n", sink);
    return 0;
}
