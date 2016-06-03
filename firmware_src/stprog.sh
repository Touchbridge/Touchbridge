#!/bin/bash
#
OBJFILE=$1
REMOTE=$2
if [ -z "$OBJFILE" ]; then
    echo Usage: $0 file [remote_programmer_uri]
    exit 1
fi
if [ -z "$REMOTE" ]; then
    REMOTE="localhost:4242"
fi

TEMPFOO=`basename $0`
TMPFILE=`mktemp /tmp/${TEMPFOO}.XXXXXX` || exit 1
cat > $TMPFILE <<EOF
target extended-remote ${REMOTE}
load
EOF
arm-none-eabi-gdb -n --batch --command=$TMPFILE $OBJFILE
rm $TMPFILE
