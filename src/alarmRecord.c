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

typedef struct ProcessTimeNode {
    struct ProcessTimeNode *prev;
    struct ProcessTimeNode *next;
    epicsTimeStamp time;
} ProcessTimeNode;

struct alarmRecordCtx {
    int initPost;
    struct {
        epicsEnum16 sevr;
        epicsEnum16 stat;
        ProcessTimeNode *procTimes;
    } links[ALARM_NLINKS];
    long alarmMask;
};

static long init_record(dbCommon *pcommon, int pass)
{
    struct alarmRecord *prec = (struct alarmRecord *)pcommon;
    struct link *plink;
    epicsInt32 *pcnt;
    char value[ALARM_STR_LEN];
    int i, j;

    if (pass == 0) {
        prec->rctx = (struct alarmRecordCtx *) callocMustSucceed(1, sizeof(struct alarmRecordCtx), "alarmRecord");
        return 0;
    }

    plink = &prec->inp1;
    pcnt = &prec->cnt1;
    for (i = 0; i < ALARM_NLINKS; i++, plink++, pcnt++) {
        if (dbLinkIsConstant(plink)) {
            continue;
        }

        recGblInitConstantLink(plink, DBF_STRING, value);

        if (*pcnt < 1) {
            *pcnt = 1;
        }
        for (j = 0; j < *pcnt; j++) {
            ProcessTimeNode *node = (ProcessTimeNode*) callocMustSucceed(1, sizeof(ProcessTimeNode), "alarmRecord");
            if (!prec->rctx->links[i].procTimes) {
                node->next = node;
                node->prev = node;
            } else {
                node->next = prec->rctx->links[i].procTimes;
                node->prev = prec->rctx->links[i].procTimes->prev;
                prec->rctx->links[i].procTimes->prev->next = node;
                prec->rctx->links[i].procTimes->prev = node;
            }
            prec->rctx->links[i].procTimes = node;
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
    double *pdly;
    double *pint;
    epicsInt32 *pcnt;
    int i;
    epicsEnum16 sevr = epicsSevNone; // New record severity
    epicsEnum16 stat = (prec->rctx->initPost == 0 ? epicsAlarmUDF : epicsAlarmNone); // New record status
    epicsEnum16 tsev = epicsSevNone; // Highest newly triggered severity
    char val[ALARM_STR_LEN] = "no alarm"; // New record string value
    long alarmMask = 0;
    epicsTimeStamp invalidTime = { 0, 0 };

    prec->pact = TRUE;
    recGblGetTimeStamp(prec);

    plink = &prec->inp1;
    pstr = prec->str1;
    pen = &prec->en1;
    psevr = &prec->sev1;
    pdly = &prec->dly1;
    pcnt = &prec->cnt1;
    pint = &prec->int1;
    for(i = 0; i < ALARM_NLINKS; i++, plink++, pen++, psevr++, pdly++, pcnt++, pint++, pstr+=ALARM_STR_LEN) {
        char lval[ALARM_STR_LEN];
        epicsEnum16 lsev = epicsSevNone;
        epicsEnum16 lsta = epicsAlarmNone;

        if (dbLinkIsConstant(plink) || *pen == menuYesNoNO) {
            continue;
        }

        if (dbGetAlarm(plink, &lsta, &lsev) == 0 &&
            dbGetLink(plink, DBR_STRING, lval, 0, 0) == 0 &&
            lsev == epicsSevNone && lsta == epicsAlarmNone) {

//fprintf(stderr, "Link %d: SEVR=%d STAT=%d\n", i, lsev, lsta);

            prec->rctx->links[i].sevr = epicsSevNone;
            prec->rctx->links[i].stat = epicsAlarmNone;

            if (*pcnt == 1) {
                prec->rctx->links[i].procTimes->time = invalidTime;
            }

        } else {

            // This link is in alarm mode, investigate why and decide
            // whether alarm needs to propagate to record level
            int effective = 0;
            int triggered = 0;
            double duration = epicsTimeDiffInSeconds(&prec->time, &prec->rctx->links[i].procTimes->time);

//fprintf(stderr, "Link %d: SEVR=%d STAT=%d\n", i, lsev, lsta);

            // Adjust severity and status
            if (lsev == epicsSevNone && lsta == epicsAlarmNone) {
                lsev = epicsSevInvalid;
                lsta = epicsAlarmLink;
                snprintf(lval, ALARM_STR_LEN, "link %d disconnected", i);
            }
            if (*psevr != epicsSevNone) {
                // Using SEVR from the record, but keep STAT
                lsev = *psevr;
            }

            triggered |= (prec->rctx->links[i].sevr != lsev);
            triggered |= (prec->rctx->links[i].stat != lsta);

            if (triggered) {
fprintf(stderr, "Link %d triggered\n", i);
                prec->rctx->links[i].procTimes->time = prec->time;
                if (*pcnt == 1) {
                    duration = 0;
                } else {
                    prec->rctx->links[i].procTimes = prec->rctx->links[i].procTimes->next;
                }
            }

            if (*pcnt == 1) {
                effective = (duration >= *pdly);
            } else if (prec->rctx->links[i].sevr == lsev && prec->rctx->links[i].stat == lsta) {
                effective = (duration < *pint);
            } else {
                // SEVR or STAT chaged, we may need to update VAL too
                effective = 1;
            }
            prec->rctx->links[i].sevr = lsev;
            prec->rctx->links[i].stat = lsta;

            if (effective) {
                int highest = 0;
fprintf(stderr, "Link %d effective\n", i);

                alarmMask |= (1 << i);

                highest = (lsev > sevr);
                if (triggered && lsev > tsev) {
                    tsev = lsev;
                    highest = 1;
                }

                if (highest) {
                    sevr = lsev;
                    stat = lsta;

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
    }

    if (alarmMask != prec->rctx->alarmMask || prec->rctx->initPost == 0) {
        unsigned short monitor_mask;

fprintf(stderr, "Posting value\n");
        prec->rctx->initPost = 1;
        prec->rctx->alarmMask = alarmMask;

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
