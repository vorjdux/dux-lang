package main

import "fmt"

func main() {
	nums := make([]int, 20)
	for k := range nums {
		nums[k] = k
	}
	var sum int64
	for i := 0; i < 2_000_000; i++ {
		for _, v := range nums {
			sum += int64(v)
		}
	}
	fmt.Println(sum)
}
