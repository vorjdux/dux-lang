# Welcome to Dux Lang
## An experimental new programming language

### Why do you create another programming language?
- We like of new challanges
- Why not?
- We saw a lot of pragram languages with crazy syntax sugars but.. really?
- Out aim is to create a fusion between the elegance of Python and the strenght of C++ without so much crazy syntax (just a little bit :D)
- Compiled! Yeahhh \nn/_

## Dependences
- libbobcat-dev : sudo apt install libbobcat-dev
- autotools-dev : sudo apt install autotools-dev
- flexc++       : sudo apt install flexc++
- bisonc++      : sudo apt install bisonc++


## Editing and renegerating from dux.lexer
```
$ ./lexgen.sh
```

## Editing and renegerating from dux.grammar
```
$ ./gragen.sh
```

## Compile process
```
$ autoreconf -vfi
$ ./configure
$ make
$ sudo make install
```
