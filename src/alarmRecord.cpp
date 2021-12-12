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
#include "alarmString.h"
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

static long initRecord(dbCommon *, int);
static long processRecord(dbCommon *);
static long updateRecordField(DBADDR *addr, int after);
static void checkLinksCallback(epicsCallback *arg);
static void checkLinks(alarmRecord *rec);

rset alarmRSET = {
    .number = RSETNUMBER,
    .report = NULL,
    .init = NULL,
    .init_record = RECSUPFUN_CAST initRecord,
    .process = RECSUPFUN_CAST processRecord,
    .special = RECSUPFUN_CAST updateRecordField,
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

static std::string replaceSubstring(const std::string& text, const std::string& pattern, const std::string& replacement, bool all=false)
{
    std::string out;
    size_t last = 0;
    size_t pos = text.find(pattern);
    while (pos != text.npos) {
        out += text.substr(last, pos-last);
        out += replacement;
        last = pos + pattern.length();
        if (!all) {
            break;
        }
        pos = text.find(pattern, last);
    }
    out += text.substr(last);
    return out;
}

static long initRecord(dbCommon *common, int pass)
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
        auto type = &rec->ty1 + i;
        char value[ALARMREC_STR_LEN];

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
            *type = alarmrecLINKTYPE_LOCAL;

            if (!dbIsLinkConnected(inp)) {
                errlogPrintf("alarmRecord: %s.INP%c in no-vo disco state\n", rec->name, i+'A');
            }
        }

        rec->ctx->prevAlarms[i].resize(*dbc < 1 ? 1 : *dbc);
    }

    callbackSetCallback(checkLinksCallback, &rec->ctx->checkLinkCb);
    callbackSetPriority(0, &rec->ctx->checkLinkCb);
    callbackSetUser(rec, &rec->ctx->checkLinkCb);

    return 0;
}

static long processRecord(dbCommon *common)
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
        auto osv = &rec->osv1 + i;
        auto dbi = &rec->dbi1 + i;
        auto dbc = &rec->dbc1 + i;
        auto str = &rec->str1 + i;
        auto sev = &rec->sev1 + i;
        auto act = &rec->act1 + i;
        epicsEnum16 severity = epicsSevNone;
        epicsEnum16 status = epicsAlarmNone;
        char value[ALARMREC_STR_LEN];

        if (dbLinkIsConstant(inp)) {
            continue;
        }

        prevAlarms.resize(*dbc < 1 ? 1 : *dbc);

        if (dbGetAlarm(inp, &status, &severity) != 0 || dbGetLink(inp, DBR_STRING, value, 0, 0) != 0) {
            status = epicsAlarmLink;
            severity = epicsSevInvalid;
        } else if (severity < (*msv+1)) {
            severity = epicsSevNone;
            status = epicsAlarmNone;
        }

        if (severity >= (*msv+1)) {
            bool alarmed = (*dbc <= 1);
            if (*dbc > 1 && *sev < (*msv+1)) {
                prevAlarms.advance();
                prevAlarms.current().time = now;
                prevAlarms.current().severity = severity;

                if (prevAlarms.last().time != invalidTime) {
                    double elapsed = (prevAlarms.current().time - prevAlarms.last().time);
                    if (elapsed <= *dbi) {
                        alarmed = true;
                    }
                }
            }

            if (alarmed && recsevr == epicsSevNone && *en == menuYesNoYES) {
                recsevr = (*osv != epicsSevNone ? *osv : severity);
                recstat = status;
                if (!*str) {
                    recval = value;
                } else {
                    recval = *str;
                    recval = replaceSubstring(recval, "%VAL%", value);
                    recval = replaceSubstring(recval, "%SEVR%", epicsAlarmSeverityStrings[severity]);
                    recval = replaceSubstring(recval, "%STAT%", epicsAlarmConditionStrings[status]);
                }
            }
        }

        if (severity != *sev) {
            if (severity < (*msv+1)) {
                *act = epicsSevNone;
            } else if (*osv != epicsSevNone) {
                *act = *osv;
            } else {
                *act = severity;
            }
            db_post_events(rec, act, DBE_VALUE);
        }

        *sev = severity;
    }

    bool skip_post = false;
    if (rec->en == 0) {
        recsevr = epicsSevNone;
        recstat = epicsAlarmNone;
        recval = "disabled";
    } else if (rec->lch == 1 && rec->sevr != epicsSevNone && rec->stat != epicsAlarmUDF) {
        skip_post = true;
    }

    if (!skip_post) {
        if (rec->sevr != recsevr || rec->stat != recstat || rec->val != recval) {
            recGblSetSevr(rec, recstat, recsevr);
            strncpy(rec->val, recval.c_str(), ALARMREC_STR_LEN);
            rec->val[ALARMREC_STR_LEN-1] = 0;

            auto monitor_mask = recGblResetAlarms(rec);
            monitor_mask |= DBE_VALUE | DBE_LOG | DBE_ALARM;
            db_post_events(rec, &rec->val, monitor_mask);
        }
    }

    recGblFwdLink(rec);
    rec->pact = FALSE;

    return 0;
}

static long updateRecordField(DBADDR *addr, int after)
{
    if (after == 0) {
        return 0;
    }

    auto field = dbGetFieldIndex(addr);
    auto rec = reinterpret_cast<alarmRecord*>(addr->precord);

    if (field == alarmRecordEN) {
        if (rec->en == 1) {
            strncpy(rec->val, rec->ctx->noAlarmStr.c_str(), ALARMREC_STR_LEN);
            recGblSetSevr(rec, epicsAlarmNone, epicsSevNone);
            auto monitor_mask = recGblResetAlarms(rec);
            monitor_mask |= DBE_VALUE | DBE_LOG | DBE_ALARM;
            db_post_events(rec, &rec->val, monitor_mask);
        }
        return 0;
    } else if (field == alarmRecordCLR) {
        if (rec->clr == 1) {
            strncpy(rec->val, rec->ctx->noAlarmStr.c_str(), ALARMREC_STR_LEN);
            recGblSetSevr(rec, epicsAlarmNone, epicsSevNone);
            auto monitor_mask = recGblResetAlarms(rec);
            monitor_mask |= DBE_VALUE | DBE_LOG | DBE_ALARM;
            db_post_events(rec, &rec->val, monitor_mask);
        }
        return 0;
    } else if (field >= alarmRecordINP1 && field < (alarmRecordINP1+ALARMREC_NLINKS)) {
        auto i = field - alarmRecordINP1;
        auto link = &rec->inp1 + i;
        auto value = &rec->str1 + i;
        auto type = &rec->ty1 + i;

        recGblInitConstantLink(link, DBF_DOUBLE, value);

        if (dbLinkIsConstant(link)) {
            db_post_events(rec, value, DBE_VALUE);
            *type = alarmrecLINKTYPE_CONST;
        } else if (dbLinkIsVolatile(link)) {
            if (dbIsLinkConnected(link)) {
                *type = alarmrecLINKTYPE_EXT;
            } else {
                *type = alarmrecLINKTYPE_EXT_NC;
                if (!rec->ctx->cbScheduled) {
                    callbackRequestDelayed(&rec->ctx->checkLinkCb, .5);
                    rec->ctx->cbScheduled = true;
                    rec->ctx->caLinkStat = alarmRecordCtx::CA_LINKS_NOT_OK;
                }
            }
        } else {
            *type = alarmrecLINKTYPE_LOCAL;

            if (!dbIsLinkConnected(link)) {
                errlogPrintf("alarmRecord: %s.INP%c in no-vo diso state\n", rec->name, i);
            }
        }
        db_post_events(rec, type, DBE_VALUE);
        return 0;
    } else if (field >= alarmRecordDBC1 && field < (alarmRecordDBC1+ALARMREC_NLINKS)) {
        auto i = field - alarmRecordDBC1;
        auto value = &rec->dbc1 + i;
        return (*value < 1 ? S_db_badField : 0);
    } else {
        return S_db_badChoice;
    }
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
