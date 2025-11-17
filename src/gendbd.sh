#!/bin/sh

# This is a helper script that generates the alarmRecord.dbd file from its 
# template file and populates the fields for the assigned number of links.
# This is mainly to prevent typos when editing a large number of links and
# is not meant to be run with every built. Instead, the generated
# alarmRecord.dbd file is considered static and is checked into repository.

NLINKS=${1:-25}

rm -f O.Common/alarmRecordFieldBlock* O.Common/alarmRecordFields.dbd

cat << EOF
menu(alarmrecLINKTYPE) {
	choice(alarmrecLINKTYPE_EXT_NC,"External PV Not Connected")
	choice(alarmrecLINKTYPE_EXT,"External PV OK")
	choice(alarmrecLINKTYPE_LOCAL,"Local PV")
	choice(alarmrecLINKTYPE_CONST,"Constant/Disabled")
}

recordtype(alarm) {
	include "dbCommon.dbd"
	field(CTX,DBF_NOACCESS) {
		prompt("Record Private")
		special(SPC_NOMOD)
		interest(4)
		extra("struct alarmRecordCtx *ctx")
	}
	field(EN,DBF_MENU) {
		prompt("Record alarm output enable")
		special(SPC_MOD)
		pp(TRUE)
		initial("1")
		menu(menuYesNo)
	}
	field(LCH,DBF_MENU) {
		prompt("Enable latching mode")
		pp(TRUE)
		initial("1")
		menu(menuYesNo)
	}
	field(CLR,DBF_UCHAR) {
		prompt("Clear latched alarm")
		special(SPC_MOD)
		initial("1")
		pp(TRUE)
		menu(menuYesNo)
	}
	field(VAL,DBF_STRING) {
		prompt("Current Value")
		special(SPC_NOMOD)
		asl(ASL0)
		initial("OK")
		size(40)
	}

	%#define ALARMREC_STR_LEN 40
	%#define ALARMREC_NLINKS $NLINKS
EOF

for i in `seq $NLINKS`; do
	cat << EOF
	field(INP$i,DBF_INLINK) {
		prompt("Input $i")
		promptgroup(GUI_CALC)
		special(SPC_MOD)
		interest(1)
	}
EOF
done
for i in `seq $NLINKS`; do
	cat << EOF
	field(TY$i,DBF_MENU) {
		prompt("Link $i type")
		special(SPC_NOMOD)
		interest(1)
		menu(alarmrecLINKTYPE)
		initial("1")
	}
EOF
done
for i in `seq $NLINKS`; do
	cat << EOF
	field(STR$i,DBF_STRING) {
		prompt("String $i")
		promptgroup(GUI_OUTPUT)
		asl(ASL0)
		initial("%VAL%")
		pp(TRUE)
		size(40)
	}
EOF
done
for i in `seq $NLINKS`; do
	cat << EOF
	field(EN$i,DBF_MENU) {
		prompt("Enable link $i")
		promptgroup(GUI_INPUTS)
		pp(TRUE)
		menu(menuYesNo)
	}
EOF
done
for i in `seq $NLINKS`; do
	cat << EOF
	field(ACT$i,DBF_MENU) {
		prompt("Link alarm active $i")
		promptgroup(GUI_INPUTS)
		special(SPC_NOMOD)
		initial("0")
		menu(menuAlarmSevr)
	}
EOF
done
for i in `seq $NLINKS`; do
	cat << EOF
	field(OSV$i,DBF_MENU) {
		prompt("Override severity $i")
		menu(menuAlarmSevr)
	}
EOF
done
for i in `seq $NLINKS`; do
	cat << EOF
	field(MSV$i,DBF_MENU) {
		prompt("Minimum severity $i")
		promptgroup(GUI_ALARMS)
		pp(TRUE)
		interest(1)
		menu(menuAlarmSevr)
	}
EOF
done
for i in `seq $NLINKS`; do
	cat << EOF
	field(DBI$i,DBF_DOUBLE) {
		prompt("Debounce interval $i")
		initial("0.0")
		pp(TRUE)
	}
EOF
done
for i in `seq $NLINKS`; do
	cat << EOF
	field(DBC$i,DBF_LONG) {
		prompt("Debounce count limit $i")
		special(SPC_MOD)
		initial("1")
		pp(TRUE)
	}
EOF
done
for i in `seq $NLINKS`; do
	cat << EOF
	field(DLY$i,DBF_LONG) {
		prompt("Delay interval $i")
		initial("0")
		pp(TRUE)
	}
EOF
done
for i in `seq $NLINKS`; do
	cat << EOF
	field(SEV$i,DBF_MENU) {
		prompt("Link $i last severity")
		special(SPC_NOMOD)
		menu(menuAlarmSevr)
	}
EOF
done

cat << EOF
}
EOF
