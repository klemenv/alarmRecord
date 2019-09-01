TOP=../..

include $(TOP)/configure/CONFIG

PROD_IOC = test
DBD += test.dbd

test_DBD += base.dbd
test_DBD += alarmRecord.dbd
test_LIBS += alarmRecord

test_SRCS += test_registerRecordDeviceDriver.cpp

test_SRCS_DEFAULT += testMain.cpp
test_SRCS_vxWorks += -nil-

test_LIBS += alarmRecord
test_LIBS += $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES
