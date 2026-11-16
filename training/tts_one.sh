#!/bin/sh
# label voice rate text outbase
say -v "$2" -r "$3" -o "$5.aiff" "$4" && afconvert -f WAVE -d LEI16@16000 -c 1 "$5.aiff" "$5.wav" && rm -f "$5.aiff"
