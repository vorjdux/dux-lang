#include <cstdio>
struct Node { int x, y; };
int main() {
    int N = 1000000, sink = 0;
    for (int i = 0; i < N; i++) {
        Node *n = new Node{i, i+1};
        sink += n->x;
        delete n;
    }
    printf("%d\n", sink);
}
