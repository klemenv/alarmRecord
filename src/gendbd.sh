#!/bin/sh

# This is a helper script that generates the alarmRecord.dbd file from its 
# template file and populates the fields for the assigned number of links.
# This is mainly to prevent typos when editing a large number of links and
# is not meant to be run with every built. Instead, the generated
# alarmRecord.dbd file is considered static and is checked into repository.

NLINKS=${1:-10}

rm -f O.Common/alarmRecordFieldBlock* O.Common/alarmRecordFields.dbd
awk -v RS='}\n' -v ORS='}\n' -F'\n' '
{
close(out)
out="O.Common/alarmRecordFieldBlock" NR
print > out
}' alarmRecordFields.tmpl

for f in O.Common/alarmRecordFieldBlock*; do
	for i in `seq $NLINKS`; do
		sed "s/\$(I)/$i/g" $f >> O.Common/alarmRecordFields.dbd
	done
done

sed "/\$(FIELDS)/{
	s/\$(FIELDS)/%#define ALARMREC_NLINKS $NLINKS/
	r O.Common/alarmRecordFields.dbd
}" alarmRecord.tmpl > alarmRecord.dbd
