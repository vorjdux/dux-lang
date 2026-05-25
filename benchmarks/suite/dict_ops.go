package main

import "fmt"

func main() {
	const N = 100_000
	d := make(map[string]int, N)
	for i := 0; i < N; i++ {
		d[fmt.Sprintf("k%d", i)] = i
	}
	var sum int64
	for j := 0; j < N; j++ {
		sum += int64(d[fmt.Sprintf("k%d", j)])
	}
	fmt.Println(sum)
}
