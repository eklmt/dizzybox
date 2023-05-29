#!/bin/sh
set -ef

dist=dizzybox-x86_64
mkdir -p "$dist"

unset extraflags

if [ ${CC+t} ]; then
	printf 'Using configured compiler %s\n' "$CC"
	CC_VERSION="$CC"
elif CC="$(command -v zig)"; then
	printf 'Using zig cc version '; zig version
	extraflags='cc -target x86_64-linux-musl'
	CC_VERSION="zig cc $(zig version)"
elif CC="$(command -v musl-clang)" ||
     CC="$(command -v musl-gcc)"; then
	printf 'zig cc unavailabe, falling back to %s\n' "$CC"
	CC_VERSION="$(basename "$CC") $("$CC" -dumpversion)"
elif CC="$(command -v clang)" ||
     CC="$(command -v gcc)"; then
	printf 'Warning: Falling back to %s\n' "$CC"
	CC_VERSION="$(basename "$CC") $("$CC" -dumpversion)"
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
	"$CC" $extraflags dizzybox.c -o "$dist"/dizzybox $compile_flags -Werror -DVERSION="\"$VERSION [$CC_VERSION]\""
)

cp COPYING "$dist"/COPYING

tar -c "$dist" | xz -e > "$dist".txz

printf "\nSize stats:\n"
du -hs "$dist"/dizzybox "$dist".txz
