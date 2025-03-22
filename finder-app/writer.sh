#!/bin/bash

writefile=$1
writestr=$2

if [ -z "$writefile" ] || [ -z "$writestr" ]
then
  echo "Usage: ./writer.sh <writefile> <writestr>"
  echo "  <writefile> - file path."
  echo "  <writestr> - text string that will be written to the file."
  exit 1
fi

writefiledir=$(dirname "$writefile")

mkdir -p "$writefiledir"

echo "$writestr" > "$writefile"
