const nums = Array.from({length: 20}, (_, k) => k);
let sum = 0n;
for (let i = 0; i < 2_000_000; i++)
    for (const v of nums)
        sum += BigInt(v);
console.log(sum.toString());
