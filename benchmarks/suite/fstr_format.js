let sum = 0;
const N = 500_000;
for (let i = 0; i < N; i++) {
    const s = `item=${i}`;
    sum += s.length;
}
console.log(sum);
