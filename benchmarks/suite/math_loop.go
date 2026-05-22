package main

import "fmt"

func main() {
	var sum int64
	var N int64 = 50_000_000
	for i := int64(0); i < N; i++ {
		sum += i*2 + 1
	}
	fmt.Println(sum)
}
