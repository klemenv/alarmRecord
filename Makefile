#Makefile at top of application tree
TOP = .
include $(TOP)/configure/CONFIG
DIRS += src
DIRS += testApp
DIRS += iocBoot
include $(TOP)/configure/RULES_TOP
