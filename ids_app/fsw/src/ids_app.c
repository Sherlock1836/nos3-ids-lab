#include "cfe.h"
#include "sample_msgids.h"
#include "ids_features.h"
#include "ids_log_format.h"
#include "ids_model.h"

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
static IDS_History History;
static uint64_t LastAlertMs;
static bool AlertSent;

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

static void Inspect(const CFE_MSG_Message_t *message)
{
    CFE_TIME_SysTime_t met = CFE_TIME_GetMET();
    uint64_t now = (uint64_t)met.Seconds * 1000 + ((uint64_t)met.Subseconds * 1000 >> 32);
    CFE_MSG_Size_t size;
    CFE_MSG_Type_t type;
    CFE_MSG_SequenceCount_t sequence;
    CFE_MSG_FcnCode_t code;
    bool secondary;
    bool passed = false;
    char note[96] = "Command header could not be decoded";
    char line[160];

    if (CFE_MSG_GetType(message, &type) == CFE_SUCCESS && type == CFE_MSG_Type_Cmd &&
        CFE_MSG_GetSize(message, &size) == CFE_SUCCESS && size >= sizeof(CFE_MSG_CommandHeader_t) &&
        CFE_MSG_GetHasSecondaryHeader(message, &secondary) == CFE_SUCCESS && secondary &&
        CFE_MSG_GetSequenceCount(message, &sequence) == CFE_SUCCESS &&
        CFE_MSG_GetFcnCode(message, &code) == CFE_SUCCESS)
    {
        float values[IDS_FEATURE_COUNT];
        bool ready = IDS_Observe(&History, now, sequence, size, code, values);
        passed = true;
        if (ready)
        {
            int length = snprintf(line, sizeof(line), "%.0f,%.0f,%.0f,%.0f,%.0f\n",
                                  values[0], values[1], values[2], values[3], values[4]);
            WriteLog(&Features, line, length, sizeof(line));
            double score = IDS_ModelScore(values);
            passed = score <= IDS_MODEL_THRESHOLD;
            note[0] = '\0';
            if (!passed)
            {
                snprintf(note, sizeof(note), "Isolation Forest score %.4f exceeds %.4f",
                         score, IDS_MODEL_THRESHOLD);
            }
        }
        else
        {
            snprintf(note, sizeof(note), "Warm-up: header check only");
        }
    }

    int length = IDS_FormatAudit(line, sizeof(line), now, SAMPLE_CMD_MID, passed, note);
    WriteLog(&Audit, line, length, sizeof(line));
    if (!passed && (!AlertSent || now < LastAlertMs || now - LastAlertMs >= 1000))
    {
        CFE_EVS_SendEvent(IDS_ANOMALY, CFE_EVS_EventType_ERROR, "MID 0x%04X: %s",
                          (unsigned)SAMPLE_CMD_MID, note);
        LastAlertMs = now;
        AlertSent = true;
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
    if (status == CFE_SUCCESS)
    {
        status = CFE_SB_SubscribeEx(CFE_SB_ValueToMsgId(SAMPLE_CMD_MID), pipe,
                                   CFE_SB_DEFAULT_QOS, 32);
    }
    if (status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("IDS initialization failed: %ld\n", (long)status);
        CFE_ES_ExitApp(CFE_ES_RunStatus_APP_ERROR);
        return;
    }

    OpenLog(&Audit, IDS_AUDIT_COLUMNS "\n", sizeof(IDS_AUDIT_COLUMNS "\n"));
    OpenLog(&Features, IDS_FEATURE_COLUMNS "\n", sizeof(IDS_FEATURE_COLUMNS "\n"));
    CFE_EVS_SendEvent(IDS_INFO, CFE_EVS_EventType_INFORMATION, "IDS watching MID 0x%04X",
                      (unsigned)SAMPLE_CMD_MID);
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
