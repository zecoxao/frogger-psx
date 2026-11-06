#!/bin/bash
# List undefined symbols across all compiled objects, minus what host libc
# will satisfy at link. This is the runtime surface the port must provide.
cd "$(dirname "$0")/../.."
objs=$(find port/build -name '*.o')
# symbols DEFINED somewhere in the 86 objects (any non-U type)
nm $objs 2>/dev/null | grep -vE ' [Uw] ' | awk 'NF>=3{print $3}' | sort -u > /tmp/defined.txt
# symbols referenced-but-undefined
nm $objs 2>/dev/null | grep ' U ' | awk '{print $2}' | sort -u > /tmp/undef_raw.txt

# crude libc filter (host provides these)
libc='^(printf|fprintf|sprintf|puts|putchar|puts|memcpy|memset|memmove|memcmp|memchr|strcpy|strncpy|strcat|strncat|strcmp|strncmp|strlen|strchr|strrchr|rand|srand|abs|qsort|atoi|atol|longjmp|setjmp|_setjmp|bzero|bcopy|stderr|stdout|stdin)$'

# external = undefined AND not defined internally AND not libc
comm -23 /tmp/undef_raw.txt /tmp/defined.txt | grep -vE "$libc" > /tmp/ext.txt
echo "=== true external symbols (PSX runtime surface): $(wc -l < /tmp/ext.txt) ==="
echo "=== of which gte_ ops: $(grep -c 'gte_' /tmp/ext.txt) ==="
echo
echo "--- full external list ---"
cat /tmp/ext.txt
