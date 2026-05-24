N = 100_000
d = {}
for i in range(N):
    d[f"k{i}"] = i
total = sum(d[f"k{j}"] for j in range(N))
print(total)
