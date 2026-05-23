class Node:
    __slots__ = ('x', 'y')
    def __init__(self, a, b):
        self.x = a
        self.y = b

N = 1_000_000
sink = 0
for i in range(N):
    n = Node(i, i + 1)
    sink += n.x
print(sink)
