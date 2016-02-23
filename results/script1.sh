#!/bin/bash

i=0
n=10
touch temp

while [ $i -lt $n ]
do
	cat output1 | grep ^$i$ | wc -l >> temp
	i=$[$i+1]
done

sort -r temp > output1_final
rm temp
