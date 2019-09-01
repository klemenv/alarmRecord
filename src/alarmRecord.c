/*************************************************************************\
* alarmRecord is distributed subject to a Software License Agreement found
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

static long init_record(dbCommon *pcommon, int pass)
{
    struct alarmRecord *prec = (struct alarmRecord *)pcommon;
    struct link *plink;
    char value[ALARM_STR_LEN];
    int i;

    if (pass == 1) {
        plink = &prec->inp1;
        for (i = 0; i < ALARM_NLINKS; i++, plink++) {
            if (dbLinkIsConstant(plink)) {
                recGblRecordError(S_dev_badInpType, (void *)prec, "alarm:init_record");
                return S_dev_badInpType;
            }
            recGblInitConstantLink(plink, DBF_STRING, value);
        }
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
    epicsInt32 *pcnt;
    epicsInt32 *pmdc;
    int i;
    epicsTimeStamp timeLast;
    double timeDiff;
    epicsEnum16 sevr = epicsSevNone;
    epicsEnum16 stat = epicsAlarmNone;
    char val[ALARM_STR_LEN] = "";
    double mindur = 0.0;

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
    pcnt = &prec->cnt1;
    pmdc = &prec->mdc1;
    for(i = 0; i < ALARM_NLINKS; i++, plink++, pen++, psevr++, pdur++, pdly++, pcnt++, pmdc++, pstr+=ALARM_STR_LEN) {
        char lval[ALARM_STR_LEN];
        epicsEnum16 lsev = epicsSevNone;
        epicsEnum16 lsta = epicsAlarmNone;

        if (dbGetAlarm(plink, &lsta, &lsev) == 0 &&
            dbGetLink(plink, DBR_STRING, lval, 0, 0) == 0 &&
            lsev == epicsSevNone) {
            
            *pcnt = 0;
            *pdur = -1;

        } else {
            // This link is in alarm mode, investigate why and decide
            // whether alarm needs to propagate to record level

            // Do alarm accounting
            *pcnt += 1;
            if (*pdur < 0) {
                *pdur = 0;
            } else {
                *pdur += timeDiff;
            }

            if (!dbLinkIsConstant(plink) && *pen == menuYesNoYES &&
                *pdur >= *pdly && *pcnt >= *pmdc) {
// TODO: UDF but lsevr == epicsSevNone

                // This link is now effectively in alarm, is it the strongest?
                if (lsev == epicsSevNone) {
                    snprintf(lval, ALARM_STR_LEN, "link %d disconnected", i);
                    sevr = epicsSevInvalid;
                    stat = epicsAlarmLink;

                } else if (*pdur < mindur) {
                    mindur = *pdur;
                    if (*psevr != epicsSevNone) {
                        // Using SEVR from the record, but keep STAT
                        lsev = *psevr;
                    }
                    if (lsev > sevr) {
                        sevr = lsev;
                        stat = lsta;
                    }
                }

                // Use STR from the record if available,
                // otherwise use link's value directly
                if (!*pstr) {
                    strncpy(val, lval, ALARM_STR_LEN);
                } else if (strstr(pstr, "%s") != NULL) {
                    snprintf(val, ALARM_STR_LEN, pstr, lval);
                } else {
                    strncpy(val, pstr, ALARM_STR_LEN);
                }
            }
        }
    }

    if (sevr != epicsSevNone || stat != epicsAlarmNone) {
        unsigned short monitor_mask;

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
