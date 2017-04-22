#!/bin/bash
#
OBJFILE=$1
if [ -z "$OBJFILE" ]; then
    echo Usage: $0 file [programmer_device]
    exit 1
fi

if [ -z "$2" ]; then
    PROGDEV1=/dev/cu.usbmodemD5DE96E1
    PROGDEV2=/dev/cu.usbmodemE2C4CAC1
    PROGDEV3=/dev/ttyACM0
    PROGDEV4=/dev/cu.usbmodemD5DCDFD1
    if [ -e $PROGDEV1 ]; then
        PROGDEV=$PROGDEV1
    elif [ -e $PROGDEV2 ]; then
        PROGDEV=$PROGDEV2
    elif [ -e $PROGDEV3 ]; then
        PROGDEV=$PROGDEV3
    elif [ -e $PROGDEV4 ]; then
        PROGDEV=$PROGDEV4
    fi
else
    PROGDEV=$2
fi

if [ -z $PROGDEV ]; then
    echo "No programmer found or specified."
    exit 1
fi

if [ ! -e $PROGDEV ]; then
    echo "Couldn't find programmer device $PROGDEV"
    exit 1
fi

echo "Using programmer at $PROGDEV"
TEMPFOO=`basename $0`
TMPFILE=`mktemp /tmp/${TEMPFOO}.XXXXXX` || exit 1
cat > $TMPFILE <<EOF
target extended-remote ${PROGDEV}
mon connect_srst enable
mon swdp_scan
mon swdp_scan
attach 1
load
start
detach
EOF
arm-none-eabi-gdb -n --batch --command=$TMPFILE $OBJFILE
rm $TMPFILE
