#!/bin/bash

filesdir=$1
searchstr=$2

if [ -z "$filesdir" ] || [ -z "$searchstr" ]
then
  echo "Usage: ./finder.sh <filesdir> <searchstr>"
  echo "  <filesdir> - directory path."
  echo "  <searchstr> - text string that will be searched for within the files."
  exit 1
fi

if [ ! -d "$filesdir" ]
then
  echo "The first argument should be a valid directory."
  exit 1
fi

X=$(grep -rl "$searchstr" "$filesdir" | wc -l)

Y=$(grep -r "$searchstr" "$filesdir" | wc -l)

echo  "The number of files are ${X} and the number of matching lines are ${Y}"
