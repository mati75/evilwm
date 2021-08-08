#!/bin/sh

in="$1"
out="$2"

test -z "$out" && { echo "usage: $0 infile outfile" >&2; exit 1; }

groff -Tpdf -P-pa4 -t -mpdfmark -mandoc "$in" > "$out"
