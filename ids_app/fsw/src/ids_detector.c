#include "ids_detector.h"

IDS_ContractResult IDS_CheckContract(const IDS_Command *commands, size_t count,
                                     unsigned code, unsigned size, unsigned *expected_size)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (commands[i].code == code)
        {
            *expected_size = commands[i].size;
            return size == *expected_size ? IDS_ACCEPT : IDS_WRONG_SIZE;
        }
    }
    return IDS_UNKNOWN_CODE;
}

bool IDS_ObserveTiming(IDS_History *history, uint64_t now_ms, uint16_t sequence,
                       unsigned size, unsigned code, float features[IDS_FEATURE_COUNT])
{
    if (history->count && now_ms >= history->last_ms && now_ms - history->last_ms >= IDS_IDLE_RESET_MS)
    {
        history->count = 0;
    }
    return IDS_Observe(history, now_ms, sequence, size, code, features);
}

IDS_TimingResult IDS_CheckTiming(const IDS_Profile *profile, const float features[IDS_FEATURE_COUNT],
                                 double *score)
{
    if (!profile->score)
    {
        return IDS_TIMING_UNCALIBRATED;
    }
    *score = profile->score(features);
    if (features[4] > profile->rate_ceiling)
    {
        return IDS_TIMING_RATE;
    }
    if (*score > profile->threshold && features[1] < profile->short_gap_ms &&
        features[4] >= profile->busy_count)
    {
        return IDS_TIMING_OUTLIER;
    }
    return IDS_TIMING_PASS;
}

const char *IDS_TimingName(IDS_TimingResult result)
{
    switch (result)
    {
        case IDS_TIMING_PASS: return "PASS";
        case IDS_TIMING_WARMUP: return "WARMUP";
        case IDS_TIMING_UNCALIBRATED: return "UNCALIBRATED";
        case IDS_TIMING_RATE: return "RATE";
        case IDS_TIMING_OUTLIER: return "OUTLIER";
    }
    return "UNKNOWN";
}
