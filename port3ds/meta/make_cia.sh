#!/bin/sh
#
# Packs heretic.elf into an installable .cia with the HOME menu icon and
# banner.  Needs bannertool and makerom on the PATH; does not need
# devkitARM, so CI can run it on a newer system than the devkitARM
# container (makerom's Linux build wants a recent glibc).
#
# Run from port3ds/:
#   sh meta/make_cia.sh ELF CIA TITLE DESCRIPTION AUTHOR MAJOR MINOR MICRO
#

set -e

elf=$1 cia=$2 title=$3 desc=$4 author=$5
major=${6:-0} minor=${7:-0} micro=${8:-0}

tmp=$(mktemp -d)
trap 'rm -fr "$tmp"' EXIT

bannertool makebanner -i meta/banner.png -a meta/banner.wav \
    -o "$tmp/banner.bnr"
bannertool makesmdh -s "$title" -l "$desc" -p "$author" \
    -i meta/icon.png -o "$tmp/icon.icn"
makerom -f cia -o "$cia" -target t -exefslogo \
    -elf "$elf" -rsf meta/heretic.rsf \
    -icon "$tmp/icon.icn" -banner "$tmp/banner.bnr" \
    -major "$major" -minor "$minor" -micro "$micro"

echo "built ... $cia"
