# Dux Memory and Ownership Model

This document describes how Dux manages memory for each category of value:
where values live (stack vs. heap), who is responsible for freeing them, and
what the programmer must do to avoid leaks or use-after-free bugs.

All information here is derived from the runtime library
(`src/runtime/duxrt.h`, `src/runtime/str.c`, `src/runtime/list.c`,
`src/runtime/dict.c`) and the code generator (`src/codegen/codegen.cpp`).

---

## 1. Primitive types (`int`, `long`, `double`, `real`, `bool`)

**Value semantics — stack-allocated.**

The code generator lowers primitives directly to LLVM scalar types:

| Dux type | LLVM IR type |
|----------|-------------|
| `bool`   | `i1`        |
| `int`    | `i32`       |
| `long`   | `i64`       |
| `real`   | `float`     |
| `double` | `double`    |

Primitives live in CPU registers or on the stack frame; they are never
heap-allocated and require no garbage collection or manual deallocation.
Assignment copies the value.

---

## 2. Strings (`str`)

**Reference-counted heap allocation via `DuxStr`.**

Every `str` value is a pointer to a heap-allocated `DuxStr` struct:

```c
typedef struct DuxStr {
    _Atomic int32_t  refcount;   /* -1 = immortal literal; >0 = live; 0 = freed */
    int32_t          len;        /* number of bytes (not including NUL terminator) */
    char*            ext;        /* NULL = data inline in FAM; non-NULL = separate heap buffer */
    char             data[];     /* inline storage for short strings (len <= 63) */
} DuxStr;
```

**Hybrid allocation strategy:**
- Short strings (up to 63 bytes): a single `malloc(sizeof(DuxStr) + len + 1)`,
  data stored in the flexible-array member (`data`), `ext == NULL`.
- Long strings (64+ bytes): `malloc(sizeof(DuxStr))` for the header only;
  `ext` points to a separately allocated heap buffer.

**Reference-count operations** (from `src/runtime/str.c`):

| Function | Description |
|---|---|
| `duxrt_str_new(data, len)` | Allocate a new `DuxStr` with `refcount = 1`. |
| `duxrt_str_retain(s)` | Increment `refcount` atomically (no-op for immortals). Returns `s`. |
| `duxrt_str_release(s)` | Decrement `refcount`; when it reaches 0, free both `ext` (if non-NULL) and the header. |

**String literals** compiled into the binary are immortal (`refcount = -1`)
and are never freed.  The codegen emits them as LLVM global constants.

The programmer does **not** need to call `delete` on strings.  The code
generator inserts `duxrt_str_retain` / `duxrt_str_release` calls automatically
at assignments, function entries, returns, and scope exits.  Explicit `delete s`
on a `str` variable emits `duxrt_str_release`.

**Current limitation:** there is no cycle detection for reference-counted
strings.  If two strings somehow reference each other (not possible through
normal Dux operations) they would leak.  In practice this cannot happen with
the current type system.

---

## 3. Class instances

**Heap-allocated with `malloc`; freed by `delete` or destructor.**

A `new T(args...)` expression:

1. Calls `malloc(sizeof(T_struct))` where `T_struct` is the LLVM struct type
   for class `T`.
2. Stores the vtable pointer at struct index 0.
3. Applies any field default initializers.
4. Calls the constructor function (if present).

The result is a raw pointer.  **There is no garbage collector.**  The
programmer (or the Dux destructor mechanism) is responsible for freeing every
class instance.

**RAII destructors:**

Declare a `~ClassName()` method to release resources:

```dux
class Buffer {
    # ...
    ~Buffer() {
        # free internal resources here
    }
}
```

The code generator automatically inserts destructor calls when a class
instance goes out of scope (at the end of a function or block).  Destructor
calls are emitted in **reverse declaration order** so nested resources are
released inner-first.

**Explicit deallocation with `delete`:**

```dux
Buffer b = new Buffer()
# ... use b ...
delete b   # calls ~Buffer() then free(b); sets b = null
```

After `delete`, the pointer is set to `null` to prevent double-free if the
RAII cleanup fires again at scope exit.

**No garbage collection:** Dux has no GC.  If you allocate an object and lose
all references to it without calling `delete`, the memory leaks.

---

## 4. Lists and Dicts

**Reference semantics — pointers to heap structures.**

`list` and `dict` values are **reference-counted** heap structures, mirroring
the `DuxStr` model.  The runtime structs carry an atomic reference count:

```c
/* DuxList: reference-counted dynamic array of void* */
typedef struct DuxList {
    atomic_int  refcount;  /* 1 = single owner; >1 = shared */
    int32_t     _pad;
    int64_t     len;
    int64_t     cap;
    void**      data;      /* heap-allocated element array */
} DuxList;

/* DuxDict: reference-counted open-addressing hash map, char* keys */
typedef struct DuxDict {
    atomic_int    refcount;
    int32_t       _pad;
    int64_t       len;
    int64_t       cap;
    int64_t       tomb;
    DuxDictEntry* entries;
} DuxDict;
```

**Lifecycle rules:**

| Operation | Effect |
|-----------|--------|
| `list x = [...]` / `dict d = {}` | Allocates with `refcount = 1`. RAII cleanup registered for `x` / `d`. |
| `list y = x` | Retains `x` (`refcount++`). RAII registered for `y` too. |
| Scope exit (RAII) | Releases each in-scope list/dict (`refcount--`); frees when `refcount` reaches 0. |
| `delete x` | Releases `x`'s reference (`refcount--`) and nulls the slot. Subsequent RAII cleanup for `x` is a **no-op** (null check). |
| Returned from a function | The return value is retained before scope cleanup; caller receives `refcount = 1`. |

**Example — no explicit delete required:**

```dux
list names = ["Alice", "Bob", "Carol"]
# names is automatically released at end of scope (refcount → 0 → freed)

list a = ["x", "y"]
list b = a           # refcount → 2
delete a             # refcount → 1; b is still valid
println(b[0])        # "x"
# b released by RAII at scope exit (refcount → 0 → freed)
```

**`delete` and RAII coexist safely.**  Calling `delete` before scope exit
releases the reference immediately and zeroes the slot.  When scope exits,
RAII sees a null pointer and skips the release — no double-free.

---

## 5. Function parameters

| Type category | Passing convention |
|---|---|
| `int`, `long`, `double`, `real`, `bool` | Passed by value (copied into the callee's stack frame). |
| `str` | Passed as a `DuxStr*` pointer; the caller retains ownership. The callee should not release the pointer unless it was explicitly retained. |
| Class instances | Passed as a raw pointer (by reference). Mutations inside the callee are visible to the caller. |
| `list`, `dict` | Passed as a pointer (by reference). Same aliasing semantics as assignment. |

---

## 6. Current limitations

- **No borrow checker.** Dux does not track ownership or lifetimes.
  Use-after-free and double-free are possible if `delete` is used incorrectly.

- **No cycle detection.** The reference-counting scheme for `str`, `list`, and
  `dict` cannot detect reference cycles.  In practice, Dux expressions cannot
  form cycles between these types through normal language constructs.

- **No cycle detection for reference-counted strings.** The reference-counting
  scheme for `str` cannot detect reference cycles.  In practice, `str` values
  cannot form cycles through normal Dux expressions.

- **No generational or tracing GC.** Large programs allocating many short-lived
  class instances should rely on destructors for cleanup; lists and dicts are
  freed automatically by RAII but cycles (if they could form) would leak.

- **Single-ownership assumption.** The runtime assumes at most one "owner"
  per class instance at any given time.  Sharing pointers across threads
  without synchronization can cause data races.

---

## 7. Best practices

### Use destructors for RAII-style cleanup

Define `~ClassName()` to release any resources your class owns.  The compiler
inserts the call automatically when the instance goes out of scope, so you
rarely need explicit `delete` for class instances:

```dux
class FileHandle {
    # (internal file descriptor via extern)
    FileHandle(str path) { /* open */ }
    ~FileHandle()        { /* close */ }
}

void process(str path) {
    FileHandle f = new FileHandle(path)
    # ... f is automatically closed at end of scope
}
```

### Use `defer` for cleanup of non-class resources

`defer` runs a block of statements when the enclosing scope exits, even if a
`return` or exception occurs:

```dux
void write_data(str path) {
    list lines = ["a", "b", "c"]
    # lines is released automatically by RAII at end of scope
    # Use defer only when early-exit cleanup is needed for OTHER resources:
    defer {
        delete lines   # safe no-op if already released; frees early if needed
    }
    # ... work with lines ...
}
```

### Avoid accessing `delete`d pointers

After `delete obj`, the slot is set to `null`.  Accessing it afterwards
is undefined behavior.  Null-check before use if lifetime is uncertain:

```dux
if obj != null {
    obj.method()
}
```

### Lists and dicts: RAII is automatic

Lists and dicts are freed automatically at scope exit via reference counting.
`delete` is optional — use it only for **early release** (e.g. freeing a large
list before a long computation):

```dux
list result = compute()
# ... use result ...
delete result   # optional: frees now; RAII at scope exit is already a no-op
# ... long computation without result in memory ...
```

If `delete` is omitted, RAII releases the list when the scope exits.
Either way there is no double-free: `delete` nulls the slot so subsequent
RAII cleanup is skipped.
