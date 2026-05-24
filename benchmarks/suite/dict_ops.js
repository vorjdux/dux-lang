const N = 100_000;
const d = new Map();
for (let i = 0; i < N; i++) d.set(`k${i}`, i);
let sum = 0n;
for (let j = 0; j < N; j++) sum += BigInt(d.get(`k${j}`));
console.log(sum.toString());
