package main

import "fmt"

func main() {
	var sum int64
	const N = 500_000
	for i := 0; i < N; i++ {
		s := fmt.Sprintf("item=%d", i)
		sum += int64(len(s))
	}
	fmt.Println(sum)
}
