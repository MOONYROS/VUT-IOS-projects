#!/bin/bash

export POSIXLY_CORRECT=1

#INPUT="osoby-short.csv"
#INPUT="osoby2.csv"
INPUT="osoby-short.csv.gz"
#INPUT="osoby-short.csv.bz2"
#CMD="cat"
CMD="zcat"
#CMD="bzcat"

LINES=0
#OLDIFS=$IFS
#IFS=','
while read -r line
do
    (( LINES++ ))
    echo $line
    id="$(echo $line | cut -d',' -f1)"
    datum="$(echo $line | cut -d',' -f2)"
    vek="$(echo $line | cut -d',' -f3)"
    #echo "id=$id, datum=$datum, vek=$vek"
    # tady je spousta dalsiho zpracovani
    #echo $line | awk -F "\"*,\"*" '{print "AWK LINE: id=",id=$1,"name=",datum=$2,"vek=",vek=$3}' 
    echo "PARSED: id=$id, datum=$datum, vek=$vek"
done <<< $($CMD $INPUT | head -n 6)
#IFS=$OLDIFS
echo "Lines=$LINES"

exit 0