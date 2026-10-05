#!/bin/sh
# Builds vcc.wasm from the VCC core plus the port glue.
set -e
cd "$(dirname "$0")"
mkdir -p out obj
CXX="clang++ --target=wasm32-wasi --sysroot=/usr"
FLAGS="-std=c++17 -O2 -fno-exceptions -fno-rtti -DNDEBUG -Icore -Icompat -Wno-everything"
SRCS="core/mc6809.cpp core/hd6309.cpp core/tcc1014graphics.cpp core/tcc1014mmu.cpp
core/tcc1014registers.cpp core/mc6821.cpp core/iobus.cpp core/coco3.cpp core/keyboard.cpp
core/keyboardLayout.cpp core/joystickinput.cpp core/fd502/wd1793.cpp core/fd502/distortc.cpp
core/ssc/tms7000.cpp core/ssc/ay8913.cpp core/ssc/sp0256.cpp core/ssc/ssc_core.cpp core/gmc/sn76496.cpp
port/vfs.cpp port/pak.cpp port/tape.cpp port/host.cpp"
OBJS=""
for s in $SRCS; do
	o=obj/$(echo $s | tr / _).o
	if [ ! -f $o ] || [ $s -nt $o ] || [ -n "$REBUILD" ]; then
		$CXX $FLAGS -c $s -o $o &
	fi
	OBJS="$OBJS $o"
done
wait
clang --target=wasm32-wasi --sysroot=/usr -O2 -c port/cxa.c -o obj/cxa.o
$CXX -O2 -mexec-model=reactor $OBJS obj/cxa.o -o out/vcc.wasm -Wl,--strip-debug -Wl,-z,stack-size=1048576
ls -la out/vcc.wasm
