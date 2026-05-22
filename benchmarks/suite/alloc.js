class Node {
    constructor(a, b) { this.x = a; this.y = b; }
}
const N = 1_000_000;
let sink = 0;
for (let i = 0; i < N; i++) {
    const n = new Node(i, i + 1);
    sink += n.x;
}
console.log(sink);
