#!/usr/bin/env bash
# Cross-language benchmark runner
# Measures: compile time (where applicable) + 3-run best wall-clock runtime
set -euo pipefail

SUITE="$(cd "$(dirname "$0")/suite" && pwd)"
BINDIR="/tmp/dux_bench_bins"
DUXC="$(cd "$(dirname "$0")/.." && pwd)/build/dux"
mkdir -p "$BINDIR"

# ── helpers ──────────────────────────────────────────────────────────────────

wall() { date +%s%3N; }   # milliseconds

# Run a command 3 times, print best elapsed ms
best_of_3() {
    local best=999999999
    for _r in 1 2 3; do
        local t0 t1 elapsed
        t0=$(wall)
        "$@" > /dev/null 2>&1
        t1=$(wall)
        elapsed=$(( t1 - t0 ))
        (( elapsed < best )) && best=$elapsed
    done
    echo "$best"
}

# Time a compilation command, print elapsed ms
compile_time() {
    local t0 t1
    t0=$(wall)
    "$@" > /dev/null 2>&1
    t1=$(wall)
    echo $(( t1 - t0 ))
}

fmt_ms() {
    local ms=$1
    if (( ms >= 10000 )); then printf "%6.2f s " "$(echo "scale=2; $ms/1000" | bc)"
    elif (( ms >= 1000 )); then printf "%6.2f s " "$(echo "scale=2; $ms/1000" | bc)"
    else printf "%5d ms " "$ms"
    fi
}

print_row() {
    local lang="$1" compile_ms="$2" run_ms="$3"
    local compile_str run_str
    if [[ "$compile_ms" == "-" ]]; then
        compile_str="    n/a   "
    else
        compile_str=$(fmt_ms "$compile_ms")
    fi
    run_str=$(fmt_ms "$run_ms")
    printf "  %-10s  %10s  %10s\n" "$lang" "$compile_str" "$run_str"
}

header() {
    echo ""
    echo "▶ $1"
    printf "  %-10s  %10s  %10s\n" "language" "compile" "run (best/3)"
    printf "  %-10s  %10s  %10s\n" "----------" "----------" "------------"
}

# ── pre-compile all compiled languages ───────────────────────────────────────

echo "=== Compiling binaries ==="

for bench in math_loop fib string_build alloc list_ops dict_ops fstr_format; do
    echo -n "  $bench ... "

    ct_c=$(compile_time gcc   -O2 -o "$BINDIR/${bench}_c"   "$SUITE/${bench}.c")
    ct_cpp=$(compile_time g++ -O2 -o "$BINDIR/${bench}_cpp" "$SUITE/${bench}.cpp")
    ct_go=$(compile_time go build -o "$BINDIR/${bench}_go"  "$SUITE/${bench}.go")
    ct_dux=$(compile_time "$DUXC" --compile -O2 "$SUITE/${bench}.dux" -o "$BINDIR/${bench}_dux")

    echo "done  (C: ${ct_c}ms  C++: ${ct_cpp}ms  Go: ${ct_go}ms  Dux: ${ct_dux}ms)"

    # stash compile times for the report
    eval "CT_C_${bench}=${ct_c}"
    eval "CT_CPP_${bench}=${ct_cpp}"
    eval "CT_GO_${bench}=${ct_go}"
    eval "CT_DUX_${bench}=${ct_dux}"
done

echo ""
echo "=== Running benchmarks (3 runs each, best time reported) ==="

# ── math_loop ────────────────────────────────────────────────────────────────

header "math loop  (50 M iterations: sum += i*2+1)"

for lang in c cpp go dux; do
    run_ms=$(best_of_3 "$BINDIR/math_loop_${lang}")
    ct_var="CT_${lang^^}_math_loop"
    print_row "$lang" "${!ct_var}" "$run_ms"
done
print_row "node"   "-" "$(best_of_3 node "$SUITE/math_loop.js")"
print_row "python" "-" "$(best_of_3 python3 "$SUITE/math_loop.py")"

# ── fib ──────────────────────────────────────────────────────────────────────

header "recursive fibonacci(35)"

for lang in c cpp go dux; do
    run_ms=$(best_of_3 "$BINDIR/fib_${lang}")
    ct_var="CT_${lang^^}_fib"
    print_row "$lang" "${!ct_var}" "$run_ms"
done
print_row "node"   "-" "$(best_of_3 node "$SUITE/fib.js")"
print_row "python" "-" "$(best_of_3 python3 "$SUITE/fib.py")"

# ── string_build ─────────────────────────────────────────────────────────────

header "string build  (50 K char appends)"

for lang in c cpp go dux; do
    run_ms=$(best_of_3 "$BINDIR/string_build_${lang}")
    ct_var="CT_${lang^^}_string_build"
    print_row "$lang" "${!ct_var}" "$run_ms"
done
print_row "node"   "-" "$(best_of_3 node "$SUITE/string_build.js")"
print_row "python" "-" "$(best_of_3 python3 "$SUITE/string_build.py")"

# ── alloc ────────────────────────────────────────────────────────────────────

header "heap alloc/free  (1 M object cycles)"

for lang in c cpp go dux; do
    run_ms=$(best_of_3 "$BINDIR/alloc_${lang}")
    ct_var="CT_${lang^^}_alloc"
    print_row "$lang" "${!ct_var}" "$run_ms"
done
print_row "node"   "-" "$(best_of_3 node "$SUITE/alloc.js")"
print_row "python" "-" "$(best_of_3 python3 "$SUITE/alloc.py")"

# ── list_ops ─────────────────────────────────────────────────────────────────

header "list element iteration  (2 M passes × 20 int elements = 40 M reads)"

for lang in c cpp go dux; do
    run_ms=$(best_of_3 "$BINDIR/list_ops_${lang}")
    ct_var="CT_${lang^^}_list_ops"
    print_row "$lang" "${!ct_var}" "$run_ms"
done
print_row "node"   "-" "$(best_of_3 node "$SUITE/list_ops.js")"
print_row "python" "-" "$(best_of_3 python3 "$SUITE/list_ops.py")"

# ── dict_ops ─────────────────────────────────────────────────────────────────

header "dict ops  (100 K inserts + 100 K lookups, string keys)"

for lang in c cpp go dux; do
    run_ms=$(best_of_3 "$BINDIR/dict_ops_${lang}")
    ct_var="CT_${lang^^}_dict_ops"
    print_row "$lang" "${!ct_var}" "$run_ms"
done
print_row "node"   "-" "$(best_of_3 node "$SUITE/dict_ops.js")"
print_row "python" "-" "$(best_of_3 python3 "$SUITE/dict_ops.py")"

# ── fstr_format ──────────────────────────────────────────────────────────────

header "string interpolation  (500 K f-string builds)"

for lang in c cpp go dux; do
    run_ms=$(best_of_3 "$BINDIR/fstr_format_${lang}")
    ct_var="CT_${lang^^}_fstr_format"
    print_row "$lang" "${!ct_var}" "$run_ms"
done
print_row "node"   "-" "$(best_of_3 node "$SUITE/fstr_format.js")"
print_row "python" "-" "$(best_of_3 python3 "$SUITE/fstr_format.py")"

echo ""
echo "=== Done ==="
