/*************************************************************************\
* alarmRecord is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/* Record Support Routines for Calculation records */
/*
 *      Author: Klemen Vodopivec
 *      Date:   8-9-2019
 */

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "alarm.h"
#include "callback.h"
#include "cantProceed.h"
#include "dbAccess.h"
#include "dbEvent.h"
#include "devSup.h"
#include "epicsTime.h"
#include "epicsVersion.h"
#include "errlog.h"
#include "menuYesNo.h"
#include "recSup.h"
#include "recGbl.h"

#define GEN_SIZE_OFFSET
#include "alarmRecord.h"
#undef  GEN_SIZE_OFFSET
#include "epicsExport.h"

#include "CircularList.h"

#ifdef VERSION_INT
#  if EPICS_VERSION_INT < VERSION_INT(3,16,0,2)
#    define dbLinkIsConstant(lnk) ((lnk)->type == CONSTANT)
#    define RECSUPFUN_CAST (RECSUPFUN)
#  else
#    define RECSUPFUN_CAST
#  endif
#else
#  define dbLinkIsConstant(lnk) ((lnk)->type == CONSTANT)
#  define RECSUPFUN_CAST (RECSUPFUN)
#endif

static long init_record(dbCommon *, int);
static long process_record(dbCommon *);
static void checkLinksCallback(epicsCallback *arg);
static void checkLinks(alarmRecord *rec);

rset alarmRSET = {
    .number = RSETNUMBER,
    .report = NULL,
    .init = NULL,
    .init_record = RECSUPFUN_CAST init_record,
    .process = RECSUPFUN_CAST process_record,
    .special = NULL,
    .get_value = NULL,
    .cvt_dbaddr = NULL,
    .get_array_info = NULL,
    .put_array_info = NULL,
    .get_units = NULL,
    .get_precision = NULL,
    .get_enum_str = NULL,
    .get_enum_strs = NULL,
    .put_enum_str = NULL,
    .get_graphic_double = NULL,
    .get_control_double = NULL,
    .get_alarm_double = NULL,
};
epicsExportAddress(rset, alarmRSET);

struct alarmRecordCtx {
    std::string noAlarmStr;
    struct AlarmEvent {
        epicsEnum16 severity = epicsSevNone;
        epicsTime time = epicsTimeStamp({ .secPastEpoch = 0, .nsec = 0 });
    };
    CircularList<AlarmEvent> prevAlarms[ALARMREC_NLINKS];
    enum {
        NO_CA_LINKS,
        CA_LINKS_ALL_OK,
        CA_LINKS_NOT_OK,
    } caLinkStat{NO_CA_LINKS};
    bool cbScheduled{false};
    epicsCallback checkLinkCb;
};

static long init_record(dbCommon *common, int pass)
{
    auto rec = reinterpret_cast<struct alarmRecord *>(common);

    if (pass == 0) {
        auto buffer = callocMustSucceed(1, sizeof(struct alarmRecordCtx), "alarmRecord");
        rec->ctx = new (buffer) alarmRecordCtx;
        return 0;
    }

    rec->ctx->noAlarmStr = rec->val;
    rec->ctx->caLinkStat = alarmRecordCtx::NO_CA_LINKS;
    for (int i = 0; i < ALARMREC_NLINKS; i++) {
        auto inp = &rec->inp1 + i;
        auto dbc = &rec->dbc1 + i;
        auto en  = &rec->en1  + i;
        auto type = &rec->ty1 + i;
        char value[ALARMREC_STR_LEN];

        if (dbLinkIsConstant(inp)) {
            *en = 0;
            continue;
        }

        recGblInitConstantLink(inp, DBF_STRING, value);

        if (dbLinkIsConstant(inp)) {
            *type = alarmrecLINKTYPE_CONST;
        } else if (dbLinkIsVolatile(inp)) {
            if (dbIsLinkConnected(inp)) {
                *type = alarmrecLINKTYPE_EXT;
                if (rec->ctx->caLinkStat == alarmRecordCtx::NO_CA_LINKS) {
                    rec->ctx->caLinkStat = alarmRecordCtx::CA_LINKS_ALL_OK;
                }
            } else {
                *type = alarmrecLINKTYPE_EXT_NC;
                rec->ctx->caLinkStat = alarmRecordCtx::CA_LINKS_NOT_OK;
            }
        } else {
            /* PV must reside on this ioc */
            *type = alarmrecLINKTYPE_LOCAL;

            if (!dbIsLinkConnected(inp)) {
                errlogPrintf("alarmRecord: %s.INP%c in no-vo disco state\n", rec->name, i+'A');
            }
        }

        rec->ctx->prevAlarms[i].resize(std::max(1U, *dbc));
    }

    callbackSetCallback(checkLinksCallback, &rec->ctx->checkLinkCb);
    callbackSetPriority(0, &rec->ctx->checkLinkCb);
    callbackSetUser(rec, &rec->ctx->checkLinkCb);

    return 0;
}

static long process_record(dbCommon *common)
{
    auto rec = reinterpret_cast<struct alarmRecord *>(common);
    static epicsTime invalidTime = epicsTimeStamp({ .secPastEpoch = 0, .nsec = 0 });
    epicsTime now = epicsTime::getCurrent();

    epicsEnum16 recsevr = epicsSevNone;
    epicsEnum16 recstat = epicsAlarmNone;
    std::string recval = rec->ctx->noAlarmStr;

    rec->pact = TRUE;
    recGblGetTimeStamp(rec);

    if (rec->ctx->caLinkStat != alarmRecordCtx::NO_CA_LINKS) {
        checkLinks(rec);
    }

    for(int i = 0; i < ALARMREC_NLINKS; i++) {
        auto& prevAlarms = rec->ctx->prevAlarms[i];
        auto inp = &rec->inp1 + i;
        auto en  = &rec->en1  + i;
        auto msv = &rec->msv1 + i;
        auto dbi = &rec->dbi1 + i;
        auto dbc = &rec->dbc1 + i;
        auto dly = &rec->dly1 + i;
        auto str = &rec->str1 + i;
        auto sev = &rec->sev1 + i;
        auto act = &rec->act1 + i;

        if (dbLinkIsConstant(inp) || *en == menuYesNoNO || rec->en == menuYesNoNO) {
            prevAlarms.current().time = invalidTime;
            prevAlarms.previous().time = invalidTime;
            prevAlarms.last().time = invalidTime;
            continue;
        }

        prevAlarms.resize(std::max(1U, *dbc));

        epicsEnum16 severity = epicsSevNone;
        epicsEnum16 status = epicsAlarmNone;
        char value[ALARMREC_STR_LEN];

        if (dbGetAlarm(inp, &status, &severity) != 0 || dbGetLink(inp, DBR_STRING, value, 0, 0) != 0) {
            status = epicsAlarmLink;
            severity = epicsSevInvalid;
            snprintf(value, ALARMREC_STR_LEN, "link %d disconnected", i);
        }

        if (severity < *msv || severity == 0) {
            // not triggered
            if (rec->ctx->prevAlarms[i].current().time != invalidTime) {
                rec->ctx->prevAlarms[i].advance();
                rec->ctx->prevAlarms[i].current().time = invalidTime;
            }
            *act = 0;
            continue;
        }
        *act = 1;

        // The link is in alarm, check for how long or how many times
        bool triggered = false;
        if (prevAlarms.current().time == invalidTime) {
            // Newly triggered alarm
            prevAlarms.current().time = now;
            prevAlarms.current().severity = severity;
        } else if (severity > prevAlarms.current().severity) {
            // Just record current severity, but don't count as a new occurance
            prevAlarms.current().severity = severity;
        }

        if (*dbc < 1) {
            // link is in alarm && not using debounce => check delay
            double elapsed = (now - prevAlarms.current().time);
            triggered = (elapsed >= *dly);
        } else {
            double elapsed = (prevAlarms.current().time - prevAlarms.last().time);
            triggered = (elapsed <= *dbi);
        }

        if (triggered) {
            if (*sev != epicsSevNone) {
                severity = *sev;
            }

            if (severity > recsevr) {
                recsevr = severity;
                recstat = status;

                // Use STR from the record if available,
                // otherwise use link's value directly
                if (!*str) {
                    recval = value;
                } else if (strstr(*str, "%s") != NULL) {
                    char v[ALARMREC_STR_LEN+1];
                    snprintf(v, ALARMREC_STR_LEN, *str, value);
                    recval = v;
                } else {
                    recval = *str;
                }
            }
        }
    }

    bool post = false;
    if (rec->lch == 0) {
        if (rec->sevr != recsevr || rec->stat != recstat || rec->val != recval) {
            post = true;
        }
    } else {
        if (rec->clr != 0) {
            post = true;
            rec->clr = 0;
        }
        if (rec->sevr != epicsSevNone) {
            // don't post if already alarming
        } else if (rec->sevr != recsevr || rec->stat != recstat || rec->val != recval) {
            post = true;
        }
    }

    if (post) {
        recGblSetSevr(rec, recstat, recsevr);
        strncpy(rec->val, recval.c_str(), ALARMREC_STR_LEN);
        rec->val[ALARMREC_STR_LEN-1] = 0;

        auto monitor_mask = recGblResetAlarms(rec);
        monitor_mask |= DBE_VALUE | DBE_LOG | DBE_ALARM;
        db_post_events(rec, &rec->val, monitor_mask);
    }

    recGblFwdLink(rec);
    rec->pact = FALSE;

    return 0;
}

static void checkLinksCallback(epicsCallback *arg)
{
    void *r;
    callbackGetUser(r, arg);
    auto rec = reinterpret_cast<alarmRecord*>(r);

    dbScanLock(reinterpret_cast<dbCommon*>(rec));
    rec->ctx->cbScheduled = false;
    checkLinks(rec);
    dbScanUnlock(reinterpret_cast<dbCommon*>(rec));
}

static void checkLinks(alarmRecord *rec)
{
    if (rec->tpro == 1) {
        printf("alarmRecord(%s):checkLinks()\n", rec->name);
    }

    rec->ctx->caLinkStat = alarmRecordCtx::NO_CA_LINKS;
    for (size_t i = 0; i<ALARMREC_NLINKS; i++) {
        auto link = &rec->inp1 + i;
        auto type = &rec->ty1 + i;
        auto linkEn = &rec->en1  + i;

        if (*linkEn == menuYesNoYES && dbLinkIsVolatile(link)) {
            if (rec->ctx->caLinkStat == alarmRecordCtx::NO_CA_LINKS) {
                rec->ctx->caLinkStat = alarmRecordCtx::CA_LINKS_ALL_OK;
            }

            auto stat = dbIsLinkConnected(link);
            if (stat == 0) {
                rec->ctx->caLinkStat = alarmRecordCtx::CA_LINKS_NOT_OK;
                if (*type != alarmrecLINKTYPE_EXT_NC) {
                    db_post_events(rec, type, DBE_VALUE);
                    *type = alarmrecLINKTYPE_EXT_NC;
                }
            } else if (*type == alarmrecLINKTYPE_EXT_NC) {
                *type = alarmrecLINKTYPE_EXT;
                db_post_events(rec, type, DBE_VALUE);
            }
        }
    }

    if (rec->ctx->caLinkStat == alarmRecordCtx::CA_LINKS_NOT_OK && !rec->ctx->cbScheduled) {
        /* Schedule another epicsCallback */
        rec->ctx->cbScheduled = true;
        callbackRequestDelayed(&rec->ctx->checkLinkCb, .5);
    }
}