TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# xxxRecord.h will be created from xxxRecord.dbd

DBDINC += alarmRecord

# <name>.dbd will be created from <name>Include.dbd
#DBD += alarmRecord.dbd

#=============================

alarm_SRCS += scalerRecord.c
alarm_LIBS += $(EPICS_BASE_IOC_LIBS)

#=============================
# build an ioc application for testing

PROD_IOC = alarmApp

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
alarmApp_SRCS_DEFAULT += alarm_registerRecordDeviceDriver.cpp
alarmApp_SRCS_DEFAULT += stdMain.cpp
alarmApp_SRCS_vxWorks += -nil-
#The following adds support from base/src/vxWorks
alarmApp_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

alarmApp_LIBS += std seq pv asyn $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
