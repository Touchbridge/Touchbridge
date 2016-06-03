#!/bin/bash
#

# For OS X - check if GNU tar available
# (brew install gnu-tar)
GTAR=$(which gtar)
if [ -z "$GTAR" ]; then
    TAR=tar
else
    TAR=$GTAR
fi

if [ -z "$1" ]; then
    PI_IP=james@jamespi
else
    PI_IP=$1
fi

# Archive target of symlinks (for tbg_protocol.h)
$TAR ch Makefile *.c *.h | ssh ${PI_IP} "(mkdir -p Touchbridge && cd Touchbridge && tar x && make)"
