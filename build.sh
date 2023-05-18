#!/bin/sh
dist=dizzybox-x86_64

mkdir "$dist"
musl-clang dizzybox.c -o "$dist"/dizzybox `cat compile_flags.txt` -Werror
cp /usr/share/licenses/musl/COPYRIGHT "$dist"/MUSL_COPYRIGHT
cp COPYING "$dist"/COPYING
tee "$dist"/LICENSE.txt << EOF
dizzybox is free software, available under the GNU GPLv3 license; see COPYING.
This program is statically linked against musl, available under the MIT license;
see MUSL_COPYRIGHT.
EOF

tar -cv "$dist" | xz -e > "$dist".txz
