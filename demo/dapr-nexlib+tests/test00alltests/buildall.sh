#!/bin/sh
for i in ../test*
do
	cd $i
	make clean
	make
	cd -
done
