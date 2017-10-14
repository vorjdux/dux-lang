#!/usr/bin/env sh

flexc++ src/lexer/dux.lexer --target-directory=gen/lexer/
cp gen/lexer/Scanner*.cpp src/lexer
cp gen/lexer/Scanner*.h include/lexer
