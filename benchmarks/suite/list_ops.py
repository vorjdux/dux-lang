nums = list(range(20))
total = 0
for _ in range(2_000_000):
    for v in nums:
        total += v
print(total)
