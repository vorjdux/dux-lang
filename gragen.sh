#!/usr/bin/env sh

GRAINC_DIR='include/grammar'
GRASRC_DIR='src/grammar'
GRAGEN_DIR='gen/grammar'

rm -rf "$GRACEN/ParserBase.h"

bisonc++ $GRASRC_DIR/dux.grammar --target-directory=$GRAGEN_DIR

for entry in $GRAGEN_DIR/*.cpp
do
	filename=$(echo $entry | sed 's/.*\/\(.*\)/\1/g')
	echo ">>> Renegerating: $entry -> $GRASRC_DIR/$filename"
	echo "/**\n** This file is generated in gen/grammar.\n** If you need to change it, please change there.\n**/" > "$GRASRC_DIR/$filename"
	cat "$entry" | sed 's/\(\#include \"\)\(Parser.*\.h\)\(\"\)/\1grammar\/\2\3/g' >> "$GRASRC_DIR/$filename"
done


for entry in "$GRAGEN_DIR"/*.h
do
	filename=$(echo $entry | sed 's/.*\/\(.*\)/\1/g')
	echo ">>> Renegerating: $entry -> $GRAINC_DIR/$filename"
        echo "/**\n** This header file is generated in gen/grammar.\n** If you need to change it, please change there.\n**/" > "$GRAINC_DIR/$filename"
        cat "$entry" | sed 's/\(\#include \"\)\(Parser.*\.h\)\(\"\)/\1grammar\/\2\3/g' | sed 's/\(\#include \"\)\(\.\.\/\)\(Parser.*\.h\)\(\"\)/\1grammar\/\3\4/g' >> "$GRAINC_DIR/$filename"
done
