#!/bin/sh
set -ef

dist=dizzybox-x86_64
mkdir -p "$dist"

unset extraflags
if [ ${CC+t} ]; then
	printf 'Using configured compiler %s\n' "$CC"
elif CC="$(command -v zig)"; then
	printf 'Using zig cc version '; zig version
	extraflags='cc -target x86_64-linux-musl'
elif CC="$(command -v musl-clang)" ||
     CC="$(command -v musl-gcc)"; then
	printf 'zig cc unavailabe, falling back to %s\n' "$CC"
elif CC="$(command -v clang)" ||
     CC="$(command -v gcc)" ||
	 CC="$(command -v cc)"; then
	printf 'Warning: Falling back to %s' "$CC"
else
	printf 'No suitable C compiler found. Install one or specify by setting CC.\n'
	exit 1
fi

printf "Compiling...\n"

VERSION="$(git describe --tags --dirty)"
compile_flags="$(cat compile_flags.txt)"

(
	set -x
	# shellcheck disable=SC2046,SC2086
	"$CC" $extraflags dizzybox.c -o "$dist"/dizzybox $compile_flags -Werror -DVERSION="\"$VERSION\""
)

cp /usr/share/licenses/musl/COPYRIGHT "$dist"/MUSL_COPYRIGHT

cp COPYING "$dist"/COPYING

>"$dist"/LICENSE.txt cat<<EOF
dizzybox is free software, available under the GNU GPLv3 license; see COPYING.
This program is statically linked against musl, available under the MIT license;
see MUSL_COPYRIGHT.
EOF

tar -c "$dist" | xz -e > "$dist".txz

printf "\nSize stats:\n"
du -hs "$dist"/dizzybox "$dist".txz
