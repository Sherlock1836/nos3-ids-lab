#include "cfe.h"
#include "ci_lab_msg.h"
#include "ci_lab_msgids.h"
#include "sample_msg.h"
#include "sample_msgids.h"
#include "ids_detector.h"
#include "ids_log_format.h"
#include "ids_profiles.h"

enum { IDS_INFO = 1, IDS_ERROR, IDS_ANOMALY };
enum { IDS_MAX_LOG_ROWS = 8192 };

typedef struct
{
    const char *path;
    osal_id_t id;
    unsigned rows;
} IDS_Log;

static IDS_Log Audit = { .path = "/ram/ids_audit_log.csv" };
static IDS_Log Features = { .path = "/ram/ids_features.csv" };

#define IDS_COUNT(array) (sizeof(array) / sizeof((array)[0]))

static const IDS_Command SampleCommands[] = {
    {SAMPLE_NOOP_CC, sizeof(SAMPLE_NoArgs_cmd_t)},
    {SAMPLE_RESET_COUNTERS_CC, sizeof(SAMPLE_NoArgs_cmd_t)},
    {SAMPLE_ENABLE_CC, sizeof(SAMPLE_NoArgs_cmd_t)},
    {SAMPLE_DISABLE_CC, sizeof(SAMPLE_NoArgs_cmd_t)},
    {SAMPLE_CONFIG_CC, sizeof(SAMPLE_Config_cmd_t)},
};

static const IDS_Command CiLabCommands[] = {
    {CI_LAB_NOOP_CC, sizeof(CI_LAB_NoopCmd_t)},
    {CI_LAB_RESET_COUNTERS_CC, sizeof(CI_LAB_ResetCountersCmd_t)},
};

typedef struct
{
    unsigned mid;
    const IDS_Command *commands;
    size_t command_count;
    const IDS_Profile *profile;
    IDS_History history;
    uint64_t last_alert_ms;
    bool alert_sent;
} IDS_Stream;

static IDS_Stream Streams[] = {
    {.mid = SAMPLE_CMD_MID, .commands = SampleCommands, .command_count = IDS_COUNT(SampleCommands),
     .profile = &IDS_SampleProfile},
    {.mid = CI_LAB_CMD_MID, .commands = CiLabCommands, .command_count = IDS_COUNT(CiLabCommands),
     .profile = &IDS_CiLabProfile},
};

void IDS_AppMain(void);

static void CloseLog(IDS_Log *log)
{
    if (OS_ObjectIdDefined(log->id))
    {
        OS_close(log->id);
        log->id = OS_OBJECT_ID_UNDEFINED;
    }
}

static void WriteLog(IDS_Log *log, const char *line, int length, size_t capacity)
{
    if (!OS_ObjectIdDefined(log->id))
    {
        return;
    }
    if (length <= 0 || (size_t)length >= capacity ||
        OS_write(log->id, line, (size_t)length) != length)
    {
        CFE_EVS_SendEvent(IDS_ERROR, CFE_EVS_EventType_ERROR, "IDS log write failed: %s", log->path);
        CloseLog(log);
        return;
    }
    if (++log->rows > IDS_MAX_LOG_ROWS)
    {
        CFE_EVS_SendEvent(IDS_ERROR, CFE_EVS_EventType_ERROR, "IDS log reached row limit: %s", log->path);
        CloseLog(log);
    }
}

static void OpenLog(IDS_Log *log, const char *header, size_t size)
{
    log->id = OS_OBJECT_ID_UNDEFINED;
    log->rows = 0;
    int32 status = OS_OpenCreate(&log->id, log->path,
                                 OS_FILE_FLAG_CREATE | OS_FILE_FLAG_TRUNCATE, OS_WRITE_ONLY);
    if (status == OS_SUCCESS)
    {
        WriteLog(log, header, (int)size - 1, size);
    }
    else
    {
        CFE_EVS_SendEvent(IDS_ERROR, CFE_EVS_EventType_ERROR, "IDS log open failed: %s (%ld)",
                          log->path, (long)status);
    }
}

static bool CheckCommand(const CFE_MSG_Message_t *message, IDS_Stream *stream, uint64_t now,
                         char *note, size_t note_size)
{
    CFE_MSG_Size_t size;
    CFE_MSG_Type_t type;
    CFE_MSG_SequenceCount_t sequence;
    CFE_MSG_FcnCode_t code;
    bool secondary;
    if (CFE_MSG_GetType(message, &type) != CFE_SUCCESS || type != CFE_MSG_Type_Cmd ||
        CFE_MSG_GetSize(message, &size) != CFE_SUCCESS || size < sizeof(CFE_MSG_CommandHeader_t) ||
        CFE_MSG_GetHasSecondaryHeader(message, &secondary) != CFE_SUCCESS || !secondary ||
        CFE_MSG_GetSequenceCount(message, &sequence) != CFE_SUCCESS ||
        CFE_MSG_GetFcnCode(message, &code) != CFE_SUCCESS)
    {
        snprintf(note, note_size, "Invalid command header");
        return false;
    }

    unsigned expected_size;
    IDS_ContractResult contract = IDS_CheckContract(stream->commands, stream->command_count,
                                                     code, size, &expected_size);
    if (contract == IDS_UNKNOWN_CODE)
    {
        snprintf(note, note_size, "Unknown function code %u", (unsigned)code);
        return false;
    }
    if (contract == IDS_WRONG_SIZE)
    {
        snprintf(note, note_size, "Function %u declared %u bytes. Expected %u",
                 (unsigned)code, (unsigned)size, expected_size);
        return false;
    }
    bool checksum_valid;
    if (CFE_MSG_ValidateChecksum(message, &checksum_valid) != CFE_SUCCESS || !checksum_valid)
    {
        snprintf(note, note_size, "Command checksum failed");
        return false;
    }

    float values[IDS_FEATURE_COUNT];
    IDS_TimingResult result = IDS_TIMING_WARMUP;
    double score = 0;
    if (IDS_ObserveTiming(&stream->history, now, sequence, size, code, values))
    {
        result = IDS_CheckTiming(stream->profile, values, &score);
        char row[160];
        char score_text[32] = "";
        if (result != IDS_TIMING_UNCALIBRATED)
        {
            snprintf(score_text, sizeof(score_text), "%.6f", score);
        }
        int length = snprintf(row, sizeof(row), "%" PRIu64 ".%03u,0x%04X,%.0f,%.0f,%.0f,%.0f,%.0f,%s,%s\n",
                              now / 1000, (unsigned)(now % 1000), stream->mid, values[0], values[1],
                              values[2], values[3], values[4], score_text, IDS_TimingName(result));
        WriteLog(&Features, row, length, sizeof(row));
    }
    switch (result)
    {
        case IDS_TIMING_RATE:
            snprintf(note, note_size, "Receive rate %.0f/s exceeds %u/s", values[4], stream->profile->rate_ceiling);
            return false;
        case IDS_TIMING_OUTLIER:
            snprintf(note, note_size, "Timing outlier %.4f exceeds %.4f. Gap %.0f ms at %.0f/s",
                     score, stream->profile->threshold, values[1], values[4]);
            return false;
        case IDS_TIMING_WARMUP:
            snprintf(note, note_size, "Warm-up: command contract passed");
            break;
        case IDS_TIMING_UNCALIBRATED:
            snprintf(note, note_size, "Command contract passed. Timing profile unavailable");
            break;
        case IDS_TIMING_PASS:
            break;
    }
    return true;
}

static void Inspect(const CFE_MSG_Message_t *message)
{
    CFE_SB_MsgId_t id;
    if (CFE_MSG_GetMsgId(message, &id) != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(IDS_ERROR, CFE_EVS_EventType_ERROR, "IDS could not decode message ID");
        return;
    }
    unsigned mid = (unsigned)CFE_SB_MsgIdToValue(id);
    IDS_Stream *stream = NULL;
    for (size_t i = 0; i < IDS_COUNT(Streams); ++i)
    {
        if (Streams[i].mid == mid)
        {
            stream = &Streams[i];
            break;
        }
    }
    if (!stream)
    {
        return;
    }

    CFE_TIME_SysTime_t met = CFE_TIME_GetMET();
    uint64_t now = (uint64_t)met.Seconds * 1000 + ((uint64_t)met.Subseconds * 1000 >> 32);
    char note[104] = "";
    char line[160];
    bool passed = CheckCommand(message, stream, now, note, sizeof(note));
    int length = IDS_FormatAudit(line, sizeof(line), now, mid, passed, note);
    WriteLog(&Audit, line, length, sizeof(line));
    if (!passed && (!stream->alert_sent || now < stream->last_alert_ms ||
                    now - stream->last_alert_ms >= 1000))
    {
        CFE_EVS_SendEvent(IDS_ANOMALY, CFE_EVS_EventType_ERROR, "MID 0x%04X: %s",
                          mid, note);
        stream->last_alert_ms = now;
        stream->alert_sent = true;
    }
}

void IDS_AppMain(void)
{
    uint32 run = CFE_ES_RunStatus_APP_RUN;
    CFE_SB_PipeId_t pipe;
    int32 status = CFE_EVS_Register(NULL, 0, CFE_EVS_EventFilter_BINARY);
    if (status == CFE_SUCCESS)
    {
        status = CFE_SB_CreatePipe(&pipe, 32, "IDS_PIPE");
    }
    for (size_t i = 0; i < IDS_COUNT(Streams) && status == CFE_SUCCESS; ++i)
    {
        status = CFE_SB_SubscribeEx(CFE_SB_ValueToMsgId(Streams[i].mid), pipe,
                                   CFE_SB_DEFAULT_QOS, 32);
    }
    if (status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("IDS initialization failed: %ld\n", (long)status);
        CFE_ES_ExitApp(CFE_ES_RunStatus_APP_ERROR);
        return;
    }

    OpenLog(&Audit, IDS_AUDIT_COLUMNS "\n", sizeof(IDS_AUDIT_COLUMNS "\n"));
    OpenLog(&Features, IDS_PROFILE_COLUMNS "\n", sizeof(IDS_PROFILE_COLUMNS "\n"));
    for (size_t i = 0; i < IDS_COUNT(Streams); ++i)
    {
        CFE_EVS_SendEvent(IDS_INFO, CFE_EVS_EventType_INFORMATION, "IDS watching MID 0x%04X (%s timing profile)",
                          Streams[i].mid, Streams[i].profile->score ? "active" : "no");
    }
    while (CFE_ES_RunLoop(&run))
    {
        CFE_SB_Buffer_t *buffer;
        status = CFE_SB_ReceiveBuffer(&buffer, pipe, 500);
        if (status == CFE_SUCCESS)
        {
            Inspect(&buffer->Msg);
        }
        else if (status != CFE_SB_TIME_OUT)
        {
            CFE_EVS_SendEvent(IDS_ERROR, CFE_EVS_EventType_ERROR, "IDS receive failed: %ld", (long)status);
            run = CFE_ES_RunStatus_APP_ERROR;
        }
    }
    CloseLog(&Features);
    CloseLog(&Audit);
    CFE_ES_ExitApp(run);
}
