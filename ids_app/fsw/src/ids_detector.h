#ifndef IDS_DETECTOR_H
#define IDS_DETECTOR_H

#include <stddef.h>
#include "ids_features.h"

#define IDS_IDLE_RESET_MS 5000
#define IDS_PROFILE_COLUMNS "timestamp,message_id," IDS_FEATURE_COLUMNS ",model_score,decision"

typedef struct
{
    unsigned code;
    unsigned size;
} IDS_Command;

typedef enum
{
    IDS_ACCEPT,
    IDS_UNKNOWN_CODE,
    IDS_WRONG_SIZE
} IDS_ContractResult;

typedef struct
{
    double (*score)(const float *features);
    double threshold;
    unsigned short_gap_ms;
    unsigned busy_count;
    unsigned rate_ceiling;
} IDS_Profile;

typedef enum
{
    IDS_TIMING_PASS,
    IDS_TIMING_WARMUP,
    IDS_TIMING_UNCALIBRATED,
    IDS_TIMING_RATE,
    IDS_TIMING_OUTLIER
} IDS_TimingResult;

IDS_ContractResult IDS_CheckContract(const IDS_Command *commands, size_t count,
                                     unsigned code, unsigned size, unsigned *expected_size);
IDS_TimingResult IDS_CheckTiming(const IDS_Profile *profile, const float features[IDS_FEATURE_COUNT],
                                 double *score);
bool IDS_ObserveTiming(IDS_History *history, uint64_t now_ms, uint16_t sequence,
                       unsigned size, unsigned code, float features[IDS_FEATURE_COUNT]);
const char *IDS_TimingName(IDS_TimingResult result);

#endif
