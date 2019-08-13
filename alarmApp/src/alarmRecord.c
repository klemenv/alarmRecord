/*************************************************************************\
* Copyright (c) 2007 UChicago Argonne LLC, as Operator of Argonne
*     National Laboratory.
* Copyright (c) 2002 The Regents of the University of California, as
*     Operator of Los Alamos National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
\*************************************************************************/

/* Record Support Routines for Calculation records */
/*
 *      Author: Klemen Vodopivec
 *      Date:   8-9-2019
 */

#include <stdio.h>
#include <string.h>

#include "errlog.h"
#include "alarm.h"
#include "cantProceed.h"
#include "dbAccess.h"
#include "dbEvent.h"
#include "devSup.h"
#include "epicsTime.h"
#include "menuYesNo.h"
#include "recSup.h"
#include "recGbl.h"
#include "special.h"

#define GEN_SIZE_OFFSET
#include "alarmRecord.h"
#undef  GEN_SIZE_OFFSET
#include "epicsExport.h"

/* Create RSET - Record Support Entry Table */

#define report NULL
#define initialize NULL
static long init_record(dbCommon *, int);
static long process(dbCommon *);
#define special NULL
#define get_value NULL
#define cvt_dbaddr NULL
#define get_array_info NULL
#define put_array_info NULL
#define get_units NULL
#define get_precision NULL
#define get_enum_str NULL
#define get_enum_strs NULL
#define put_enum_str NULL
#define get_graphic_double NULL
#define get_ctrl_double NULL
#define get_alarm_double NULL

rset alarmRSET={
	RSETNUMBER,
	report,
	initialize,
	init_record,
	process,
	special,
	get_value,
	cvt_dbaddr,
	get_array_info,
	put_array_info,
	get_units,
	get_precision,
	get_enum_str,
	get_enum_strs,
	put_enum_str,
	get_graphic_double,
	get_ctrl_double,
	get_alarm_double
};
epicsExportAddress(rset, alarmRSET);

struct alarmRecordCtx {
    long alarmed;
};

static long init_record(dbCommon *pcommon, int pass)
{
    struct alarmRecord *prec = (struct alarmRecord *)pcommon;
    struct link *plink;
    char value[ALARM_STR_LEN];
    int i;

    if (pass == 0) {
        prec->rctx = (struct alarmRecordCtx *) callocMustSucceed(1, sizeof(struct alarmRecordCtx), "alarmRecord");
        return 0;
    }

    plink = &prec->inp1;
    for (i = 0; i < ALARM_NLINKS; i++, plink++) {
        if (dbLinkIsConstant(plink)) {
            recGblRecordError(S_dev_badInpType, (void *)prec, "alarm:init_record");
            return S_dev_badInpType;
        }
        recGblInitConstantLink(plink, DBF_STRING, value);
    }
    return 0;
}

static long process(dbCommon *pcommon)
{
    struct alarmRecord *prec = (struct alarmRecord *)pcommon;
    struct link *plink;
    char *pstr;
    epicsEnum16 *pen;
    epicsEnum16 *psevr;
    double *pdur;
    double *pdly;
    int i;
    int first;
    epicsTimeStamp timeLast;
    double timeDiff;
    long alarmed = 0;
    epicsEnum16 sevr = epicsSevNone;
    epicsEnum16 stat = epicsAlarmNone;
    char val[ALARM_STR_LEN] = "";

    prec->pact = TRUE;
    timeLast = prec->time;
    prec->val[0] = '\0'; // this will get overriden by the first alarm

    // Time since last processed, added to previously active alarmed durations
    recGblGetTimeStamp(prec);
    timeDiff = epicsTimeDiffInSeconds(&prec->time, &timeLast);

    plink = &prec->inp1;
    pstr = prec->str1;
    pen = &prec->en1;
    psevr = &prec->sev1;
    pdur = &prec->dur1;
    pdly = &prec->dly1;
    first = 1;
    for(i = 0; i < ALARM_NLINKS; i++, plink++, pen++, psevr++, pdur++, pdly++, pstr+=ALARM_STR_LEN) {

        if (!dbLinkIsConstant(plink) && *pen == menuYesNoYES) {
            char lval[ALARM_STR_LEN];
            epicsEnum16 lsev = epicsSevNone;
            epicsEnum16 lsta = epicsAlarmNone;

            if (dbGetAlarm(plink, &lsta, &lsev) != 0 || 
	            dbGetLink(plink, DBR_STRING, lval, 0, 0) != 0 ||
                lsev != epicsSevNone) {

// TODO: UDF but lsevr == epicsSevNone

                if (*pdur < 0) {
                    *pdur = 1;
                } else {
                    *pdur += timeDiff;
                }
                if (*pdur >= *pdly) {
                    alarmed |= (1 << i);
                }

fprintf(stderr, "ALARMED Link %d: val=%s duration=%.4fs sev=%d\n", i, lval, *pdur, sevr);

                // The first alarmed link drives this record value and status
                if (first) {
                    // Allow other links with lower pdly to trigger the alarm first
                    if (*pdur >= *pdly) {
                        first = 0;
                    }

                    if (lsev == epicsSevNone) {
                        // Probably CA link got disconnected
                        sprintf(lval, "Invalid link %d", i);
                        sevr = epicsSevInvalid;
                        stat = epicsAlarmLink;
                    } else if (*psevr != epicsSevNone) {
                        // Using SEVR from the record, but keep STAT
                        sevr = *psevr;
                        stat = lsta;
                    } else {
                        sevr = lsev;
                        stat = lsta;
                    }

                    // Use STR from the record if available,
                    // otherwise use link value directly
                    if (!*pstr) {
                        strncpy(val, lval, ALARM_STR_LEN);
                    } else if (strstr(pstr, "%s") == NULL) {
                        strncpy(val, pstr, ALARM_STR_LEN);
                    } else {
                        snprintf(val, ALARM_STR_LEN, pstr, lval);
                    }
                }
            } else {
                *pdur = -1;
            }
        } else {
            *pdur = -1;
        }
    }

    if (prec->rctx->alarmed != alarmed) {
        unsigned short monitor_mask;

        prec->rctx->alarmed = alarmed;
//        prec->nsta = stat;
//        prec->nsev = sevr;
        recGblSetSevr(prec, stat, sevr);
        strncpy(prec->val, val, ALARM_STR_LEN);

        monitor_mask = recGblResetAlarms(prec);
        monitor_mask |= DBE_VALUE | DBE_LOG | DBE_ALARM;
        db_post_events(prec, &prec->val, monitor_mask);
    }

    recGblFwdLink(prec);
    prec->pact = FALSE;
    return 0;
}
