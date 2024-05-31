#!/bin/bash
slaves=$(head -n 1 slaves.txt)
n=$1
port=2001
cores=2
limit=$((port+slaves+-1))
for ((i = port; i <= limit; i++))
do
	gnome-terminal -- ./testing $n $i 1
done
echo All done

