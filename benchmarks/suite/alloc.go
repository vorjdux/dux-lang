package main

import "fmt"

type Node struct{ x, y int }

func main() {
	N, sink := 1_000_000, 0
	for i := 0; i < N; i++ {
		n := &Node{i, i + 1}
		sink += n.x
	}
	fmt.Println(sink)
}
