TOP=..
include $(TOP)/configure/CONFIG

BOB += alarmRecord.bob

include $(TOP)/configure/RULES

alarmRecord.bob: ../alarmRecord.tmpl.bob
	../genopi.sh $< $(ALARMREC_NLINKS) > $@
