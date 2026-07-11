#!/bin/sh
set -eu

base_url=https://www.netlib.org/f2c
directory_name=netlib-f2c

direct_files='changes
00lastchange
README
f2c.ps
f2c.pdf
fc
getopt.c'

for command in wget tar unzip gzip mktemp grep; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "error: required command not found: $command" >&2
    exit 1
  fi
done

case $0 in
  */*) script_ref=$0 ;;
  *) script_ref=$(command -v "$0") ;;
esac

script_dir=$(CDPATH= cd "$(dirname "$script_ref")" && pwd -P)
script_path=$script_dir/$(basename "$script_ref")
working_dir=$(pwd -P)

clean_directory() {
  directory=$1
  preserved_file=${2:-}

  for entry in \
    "$directory"/.[!.]* \
    "$directory"/..?* \
    "$directory"/*
  do
    if [ ! -e "$entry" ] && [ ! -L "$entry" ]; then
      continue
    fi
    if [ -n "$preserved_file" ] && [ "$entry" = "$preserved_file" ]; then
      continue
    fi
    rm -rf "$entry"
  done
}

if [ "${working_dir##*/}" = "$directory_name" ]; then
  destination=$working_dir
  preserved_script=
  if [ "$script_dir" = "$destination" ]; then
    preserved_script=$script_path
  fi
  clean_directory "$destination" "$preserved_script"
else
  destination=$working_dir/$directory_name
  rm -rf "$destination"
  mkdir -p "$destination"
fi

temporary_dir=$(mktemp -d "$destination/.download.XXXXXX")
cleanup() {
  rm -rf "$temporary_dir"
}
trap cleanup EXIT HUP INT TERM

download() {
  file=$1
  wget \
    --quiet \
    --show-progress \
    --output-document="$temporary_dir/$file" \
    "$base_url/$file"
}

for file in $direct_files; do
  download "$file"
done
download src.tgz
download libf2c.zip

gzip -t "$temporary_dir/src.tgz"
unzip -tq "$temporary_dir/libf2c.zip" >/dev/null

if tar -tzf "$temporary_dir/src.tgz" \
    | grep -Eq '(^/|(^|/)\.\.(/|$))'; then
  echo 'error: src.tgz contains an unsafe path' >&2
  exit 1
fi

if unzip -Z1 "$temporary_dir/libf2c.zip" \
    | grep -Eq '(^/|(^|/)\.\.(/|$))'; then
  echo 'error: libf2c.zip contains an unsafe path' >&2
  exit 1
fi

for file in $direct_files; do
  mv "$temporary_dir/$file" "$destination/$file"
done

tar -xzf "$temporary_dir/src.tgz" -C "$destination"
mkdir "$destination/libf2c"
unzip -q "$temporary_dir/libf2c.zip" -d "$destination/libf2c"

rm -f "$destination/src/README"

echo "f2c files are ready in $destination"
