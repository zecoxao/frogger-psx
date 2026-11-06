#!/bin/bash
echo "int main(void){return 0;}" > /tmp/t32.c
if gcc -m32 /tmp/t32.c -o /tmp/t32 2>/tmp/m32.log; then
    echo "M32_GCC=OK"
else
    echo "M32_GCC=FAIL"
    head -4 /tmp/m32.log
fi
echo -n "SDL2_i386: "
ls /usr/lib/i386-linux-gnu/libSDL2-2.0.so* 2>/dev/null || echo "MISSING"
echo -n "GL_i386: "
ls /usr/lib/i386-linux-gnu/libGL.so* 2>/dev/null || echo "MISSING"
echo -n "foreign_arch: "
dpkg --print-foreign-architectures 2>/dev/null || echo "none"
