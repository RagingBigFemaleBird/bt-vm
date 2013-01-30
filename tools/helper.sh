#!/bin/sh

cat > /tmp/foo.S <<EOF
 .text
 .arm
 .word $1
EOF

as  /tmp/foo.S -o /tmp/foo.o
echo "ARM:  " `objdump -d /tmp/foo.o | grep "   0:"`
echo "Thumb:" `objdump --disassembler-options=force-thumb -d     /tmp/foo.o | grep "   0:"`
rm -rf /tmp/foo.o /tmp/foo.S
