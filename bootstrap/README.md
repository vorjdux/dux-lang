# Dux Stage 2 — Self-Hosting Bootstrap (M6)

This directory contains the first milestone toward Dux self-hosting: a Dux
program that can parse Dux source code and dump its AST, compiled by the
C++ stage-1 compiler.

## Components

- `dux_stage2.dux` — tokenizer + recursive-descent parser + AST printer,
  written entirely in Dux
- `bootstrap.sh` — builds `dux_stage2` using the C++ compiler, then runs
  it against `examples/hello_world.dux`

## Usage

```bash
cd bootstrap
./bootstrap.sh          # build stage2 and test it

# Or manually:
../build/dux --compile dux_stage2.dux -o dux_stage2
./dux_stage2 --dump-ast ../examples/hello_world.dux
./dux_stage2 --dump-ast ../tests/run_ok/hello.dux
./dux_stage2 --version
./dux_stage2 -h
```

## What this demonstrates

`dux_stage2` is a non-trivial Dux program (~600 lines) that:

1. Reads a `.dux` source file using `io.file`
2. Tokenizes it (Dux lexer class — handles keywords, operators, strings,
   comments including `#`, `//`, `/* */`, and `"""..."""` docstrings)
3. Parses it with a recursive-descent parser (functions, classes, imports,
   namespaces, enums, control flow, expressions)
4. Prints the AST in the same canonical format as the C++ `ast::Printer`

The output of `dux_stage2 --dump-ast hello_world.dux` exactly matches
the output of `dux --dump-ast hello_world.dux` for supported constructs.

## Known limitations

- Generic type parameters in class/function declarations are not printed
  (round-trip is approximate for generic code)
- `match` statement pretty-printing is simplified
- The AST dump is a pretty-print, not a serialized tree format

## Next steps (toward full self-hosting)

- Port the semantic analysis pass to Dux
- Port codegen bindings to emit LLVM IR via Dux FFI
- `dux_stage3`: compiled by `dux_stage2`, can compile `hello_world.dux`
