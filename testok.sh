#!/bin/bash

export POSIXLY_CORRECT=1

#INPUT="osoby-short.csv"
INPUT="osoby-short.csv.gz"
#INPUT="osoby-short.csv.bz2"
#CMD="cat"
CMD="zcat"
#CMD="bzcat"

LINES=0
OLDIFS=$IFS
IFS=','
while read id datum vek pohlavi kraj_nuts_kod okres_lau_kod nakaza_v_zahranici nakaza_zeme_csu_kod reportovano_khs
do
    (( LINES++ ))
    echo "id=$id, datum=$datum, vek=$vek"
    # tady je spousta dalsiho zpracovani
done <<< $($CMD $INPUT | head -n 6)
IFS=$OLDIFS

echo "lines=$LINES"

exit 0