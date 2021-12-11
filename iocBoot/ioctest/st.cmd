#!../../bin/linux-x86_64/test

< envPaths

cd "${TOP}"

## Register all support components
dbLoadDatabase "dbd/test.dbd"
test_registerRecordDeviceDriver pdbbase

## Load record instances
dbLoadRecords("db/test.db")

cd "${TOP}/iocBoot/${IOC}"

iocInit()
