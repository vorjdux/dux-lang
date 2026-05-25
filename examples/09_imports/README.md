# Imports in Dux

## Standard Library Modules

Dux ships with three built-in modules:

| Module | Functions |
|--------|-----------|
| `math` | `sqrt`, `pow`, `floor`, `ceil`, `abs`, `log`, `log2`, `sin`, `cos`, `min`, `max` |
| `str`  | `concat`, `from_int`, `from_double`, `length`, `slice` |
| `io`   | `println`, `print`, `readline` |

Import a module at the top of your file:

```dux
import math
import str
import io
```

Then call functions with dot notation:

```dux
double x = math.sqrt(9.0)    # 3.0
str s    = str.from_int(42)  # "42"
io.println("hello")
```

## User-File Imports

The import syntax supports dotted paths for file-based modules:

```dux
import utils
import geometry.shapes
import myapp.models.user
```

This feature is on the roadmap - the syntax is parsed and validated today,
and the multi-file compilation pass will resolve these paths at link time.
