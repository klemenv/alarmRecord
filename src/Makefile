TOP=..
include $(TOP)/configure/CONFIG

LIBRARY_IOC = alarmRecord

USR_CXXFLAGS += -std=c++11 -Wno-deprecated-declarations -DUSE_TYPED_RSET

DBDINC += alarmRecord
alarmRecord_DBD += alarmRecord.dbd
alarmRecord_SRCS += alarmRecord.cpp

include $(TOP)/configure/RULES
