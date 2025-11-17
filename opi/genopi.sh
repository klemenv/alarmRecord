#!/bin/bash

# This is a helper script that generates the alarmRecord.opi file from its
# template file and populates the fields for the assigned number of links.

TEMPLATE_FILE=${1:-alarmRecord.tmpl.bob}
NLINKS=${2:-10}

# We're assuming single 'group' widget in .bob file
sed -n '/.*group/,${p;/^  <\/widget>/q}' $TEMPLATE_FILE > alarmRecordLink.tmp
orig_y=`grep -m1 "<y>" alarmRecordLink.tmp | sed 's/.*>\(.*\)<.*/\1/'`
orig_height=`grep -m1 "<height>" alarmRecordLink.tmp | sed 's/.*>\(.*\)<.*/\1/'`

# Expand the group as many time as there are links
rm -f alarmRecordLinks.tmp
for i in `seq $NLINKS`; do
	let "y=orig_y+(i-1)*orig_height+(i-1)*5"
	sed -e "s/\$(L)/$i/g" -e "0,/<y>$orig_y/s/<y>$orig_y/<y>$y/" alarmRecordLink.tmp >> alarmRecordLinks.tmp
done

# Plug the expanded groups to the main .bob file
sed -e '/^.*group/,/^  <\/widget>/c%GROUP%' $TEMPLATE_FILE | sed -e '/^%GROUP%/r alarmRecordLinks.tmp' -e '/%GROUP/d'

#rm -f alarmRecordLink.tmp alarmRecordLinks.tmp
