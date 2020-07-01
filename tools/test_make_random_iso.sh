#!/bin/sh
# 
# Make a random ISO, used by all of the test programs
# Make sure that it is more than 3 pages in length (at least 48MB)

# This file is a work of a US government employee and as such is in the Public domain.
# Simson L. Garfinkel, March 12, 2012
unset AFFLIB_PASSPHRASE

SCRIPT=`realpath "$0"`
SCRIPTPATH=`dirname "$SCRIPT"`
MANPATH="$SCRIPTPATH/../man"

if test "x" = "x$1" ;
  then echo usage: $0 filename
  exit 1
fi

echo Making the random ISO $1
/bin/rm -f "$1"
touch "$1"
dd if=/dev/urandom of="$1" bs=65536 count=24   2>/dev/null
dd if=/dev/zero    bs=16777216 count=2 >> "$1" 2>/dev/null
for i in 1 2 3 4 5 6 7 8 9 0 ; do \
    for fn in "$MANPATH"/* ; do \
        cat "$fn" >> "$1" ;
    done ; \
done
ls -l "$1"
openssl md5 "$1"
exit 0

