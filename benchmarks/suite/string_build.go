package main

import (
	"fmt"
	"strings"
)

func main() {
	var b strings.Builder
	for i := 0; i < 50000; i++ {
		b.WriteByte('x')
	}
	fmt.Println(b.Len())
}
