#!/usr/bin/env sh

LEXINC_DIR='include/lexer'
LEXSRC_DIR='src/lexer'
LEXGEN_DIR='gen/lexer'

flexc++ $LEXSRC_DIR/dux.lexer --target-directory=$LEXGEN_DIR

for entry in $LEXGEN_DIR/*.cpp
do
	filename=$(echo $entry | sed 's/.*\/\(.*\)/\1/g')
	echo ">>> Renegerating: $entry -> $LEXSRC_DIR/$filename"
	echo "/**\n** This file is generated in gen/lexer.\n** If you need to change it, please change there.\n**/" > "$LEXSRC_DIR/$filename"
	cat "$entry" | sed 's/\(\#include \"\)\(ScannerImpl.h\)\(\"\)/\1lexer\/\2\3/g' >> "$LEXSRC_DIR/$filename"
done


for entry in "$LEXGEN_DIR"/*.h
do
	filename=$(echo $entry | sed 's/.*\/\(.*\)/\1/g')
	echo ">>> Renegerating: $entry -> $LEXINC_DIR/$filename"
        echo "/**\n** This header file is generated in gen/lexer.\n** If you need to change it, please change there.\n**/" > "$LEXINC_DIR/$filename"
        cat "$entry" >> "$LEXINC_DIR/$filename"
done
