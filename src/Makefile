TOP=..
include $(TOP)/configure/CONFIG

LIBRARY_IOC = alarmRecord

USR_CXXFLAGS += -std=c++17 -Wno-deprecated-declarations -DUSE_TYPED_RSET
#USR_CXXFLAGS += -O0 -g -ggdb

DBDINC += alarmRecord
alarmRecord_DBD += alarmRecord.dbd
alarmRecord_SRCS += alarmRecord.cpp

include $(TOP)/configure/RULES

$(COMMON_DIR)/alarmRecord.dbd:
	../gendbd.sh $(ALARMREC_NLINKS) > $@

