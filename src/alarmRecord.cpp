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
#include <list>
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
static void lateInitCallback(epicsCallback *arg);
static long processRecord(dbCommon *);
static long updateRecordField(DBADDR *addr, int after);
static void checkDelaysCallback(epicsCallback *arg);

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
    bool initialized = false;
    epicsCallback lateInitCb;
    epicsCallback checkDelaysCb;

    struct AlarmEvent {
        epicsEnum16 severity = epicsSevNone;
        epicsTime time = epicsTimeStamp({ .secPastEpoch = 0, .nsec = 0 });
    };
    std::list<AlarmEvent> prevAlarms[ALARMREC_NLINKS];
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
    bool haveDelays = false;
    for (int i = 0; i < ALARMREC_NLINKS; i++) {
        auto inp = &rec->inp1 + i;
        auto dbc = &rec->dbc1 + i;
        auto dly = &rec->dly1 + i;
        char value[ALARMREC_STR_LEN];

        recGblInitConstantLink(inp, DBF_STRING, value);

        if (*dbc <= 1 && *dly > 0) {
            haveDelays = true;
        }
    }

    callbackSetCallback(lateInitCallback, &rec->ctx->lateInitCb);
    callbackSetPriority(0, &rec->ctx->lateInitCb);
    callbackSetUser(rec, &rec->ctx->lateInitCb);
    callbackRequestDelayed(&rec->ctx->lateInitCb, 1.0);

    callbackSetCallback(checkDelaysCallback, &rec->ctx->checkDelaysCb);
    callbackSetPriority(0, &rec->ctx->checkDelaysCb);
    callbackSetUser(rec, &rec->ctx->checkDelaysCb);
    if (haveDelays) {
        // Schedule processing of the record regardless of PINI and CP links
        callbackRequestDelayed(&rec->ctx->checkDelaysCb, 1.0);
    }

    return 0;
}

static void lateInitCallback(epicsCallback *arg)
{
    void *r;
    callbackGetUser(r, arg);
    auto rec = reinterpret_cast<alarmRecord*>(r);
    // We don't let record process until CA links are settled. This is needed
    // because we don't always turn disconnected links into alarmed state.
    // So when the record processes initially, the CA links are likely not
    // connected yet and they could trigger unintentional alarm.
    rec->ctx->initialized = true;
}

static long processRecord(dbCommon *common)
{
    auto rec = reinterpret_cast<struct alarmRecord *>(common);
    bool haveDelays = false;
    epicsEnum16 recsevr = (rec->lch > 0 ? rec->sevr : epicsSevNone);
    epicsEnum16 recstat = (rec->lch > 0 ? rec->stat : epicsAlarmNone);
    std::string recval  = (rec->lch > 0 ? rec->val  : rec->ctx->noAlarmStr);

    rec->pact = TRUE;
    recGblGetTimeStamp(rec);

    if (rec->ctx->initialized && rec->en == menuYesNoYES) {

        for(int i = 0; i < ALARMREC_NLINKS; i++) {
            auto& prevAlarms = rec->ctx->prevAlarms[i];
            auto inp = &rec->inp1 + i;
            auto en  = &rec->en1  + i;
            auto msv = &rec->msv1 + i;
            auto osv = &rec->osv1 + i;
            auto dbi = &rec->dbi1 + i;
            auto dbc = &rec->dbc1 + i;
            auto dly = &rec->dly1 + i;
            auto str = &rec->str1 + i;
            auto sev = &rec->sev1 + i;
            auto act = &rec->act1 + i;
            epicsEnum16 severity = epicsSevNone;
            epicsEnum16 status = epicsAlarmNone;
            char value[128];
            char egu[32];

            if (dbLinkIsConstant(inp)) {
                continue;
            }

            if (*dbc <= 1 && *dly > 0) {
                haveDelays = true;
            }

            if (dbGetAlarm(inp, &status, &severity) != 0 || dbGetLink(inp, DBR_STRING, value, 0, 0) != 0 || dbGetUnits(inp, egu, sizeof(egu)) != 0) {
                status = epicsAlarmLink;
                severity = epicsSevInvalid;
            } else if (severity < *msv) {
                severity = epicsSevNone;
                status = epicsAlarmNone;
            }

            bool alarmed = false;
            if (*en == menuYesNoYES && severity >= *msv) {
                // We support two modes:
                // * debounce mode, must be triggered DBCx>1 times in DBIx seconds
                // * latch mode, must stay active for certain DLYx seconds

                if (*dbc > 1) { // debounce mode
                    if (*sev == epicsSevNone) {
                        // alarm triggered, now check how many times
                        prevAlarms.push_back({severity, epicsTime::getCurrent()});
                        while (prevAlarms.size() > static_cast<size_t>(*dbc)) {
                            prevAlarms.pop_front();
                        }
                        double elapsed = (prevAlarms.back().time - prevAlarms.front().time);
                        if (prevAlarms.size() == static_cast<size_t>(*dbc) && (elapsed <= *dbi || *dbc == 1)) {
                            alarmed = true;
                        }
                    }
                } else { // latch mode
                    if (*sev == epicsSevNone) {
                        prevAlarms.clear();
                        prevAlarms.push_back({severity, epicsTime::getCurrent()});
                    } else if (prevAlarms.empty() == false) {
                        double elapsed = (epicsTime::getCurrent() - prevAlarms.front().time);
                        if (elapsed >= *dly) {
                            alarmed = true;
                        }
                    }
                }
            }

            if (alarmed == true) {
                if (severity != epicsSevNone && *osv != epicsSevNone) {
                    severity = *osv;
                }

                if (severity > recsevr) {
                    std::string text = *str;
                    text = replaceSubstring(text, "%VAL%", value, true);
                    text = replaceSubstring(text, "%EGU%", egu, true);
                    recval = text;
                    recsevr = severity;
                    recstat = status;
                }
                if (*act != severity) {
                    *act = severity;
                    db_post_events(rec, act, DBE_VALUE);
                }
            } else {
                if (*act != epicsSevNone) {
                    *act = epicsSevNone;
                    db_post_events(rec, act, DBE_VALUE);
                }
            }

            *sev = severity;
        }
    } // rec->en == menuYesNoYES

    if (rec->stat == epicsAlarmUDF) {
        recsevr = epicsSevNone;
        recstat = epicsAlarmNone;
        recval = "OK";
    }

    if (rec->en == menuYesNoNO) {
        recsevr = epicsSevNone;
        recstat = epicsAlarmNone;
        recval = "disabled";
    }

    if (recval != rec->val || recsevr != rec->sevr) {
        recGblSetSevr(rec, recstat, recsevr);
        strncpy(rec->val, recval.c_str(), ALARMREC_STR_LEN);
        rec->val[ALARMREC_STR_LEN-1] = 0;

        auto monitor_mask = recGblResetAlarms(rec);
        monitor_mask |= DBE_VALUE | DBE_LOG | DBE_ALARM;
        db_post_events(rec, &rec->val, monitor_mask);
    }

    recGblFwdLink(rec);
    rec->pact = FALSE;

    if (haveDelays) {
        callbackRequestDelayed(&rec->ctx->checkDelaysCb, 1.0);
    }

    return 0;
}

static void checkDelaysCallback(epicsCallback *arg)
{
    void *r;
    callbackGetUser(r, arg);
    auto rec = reinterpret_cast<alarmRecord*>(r);

    dbScanLock(reinterpret_cast<dbCommon*>(rec));
    processRecord(reinterpret_cast<dbCommon*>(rec));
    dbScanUnlock(reinterpret_cast<dbCommon*>(rec));
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
            rec->clr = 0;
            auto monitor_mask = recGblResetAlarms(rec);
            monitor_mask |= DBE_VALUE | DBE_LOG | DBE_ALARM;
            db_post_events(rec, &rec->val, monitor_mask);
        }
        return 0;
    } else if (field >= alarmRecordDBC1 && field < (alarmRecordDBC1+ALARMREC_NLINKS)) {
        auto i = field - alarmRecordDBC1;
        auto value = &rec->dbc1 + i;
        return (*value < 1 ? S_db_badField : 0);
    } else {
        return S_db_badChoice;
    }
}
