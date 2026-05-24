total = 0
N = 500_000
for i in range(N):
    s = f"item={i}"
    total += len(s)
print(total)
