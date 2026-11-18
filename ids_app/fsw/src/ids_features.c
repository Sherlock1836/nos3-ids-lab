#include "ids_features.h"

bool IDS_Observe(IDS_History *history, uint64_t now_ms, uint16_t sequence,
                 unsigned size, unsigned function_code, float features[IDS_FEATURE_COUNT])
{
    bool ready = history->count > 0 && now_ms >= history->last_ms;
    if (!ready)
    {
        history->count = 0;
        history->head = 0;
    }

    while (history->count > 0 && now_ms - history->times[history->head] >= 1000)
    {
        history->head = (history->head + 1) % IDS_WINDOW_CAPACITY;
        --history->count;
    }
    if (history->count == IDS_WINDOW_CAPACITY)
    {
        history->head = (history->head + 1) % IDS_WINDOW_CAPACITY;
        --history->count;
    }
    unsigned tail = (history->head + history->count) % IDS_WINDOW_CAPACITY;
    history->times[tail] = now_ms;
    ++history->count;

    unsigned delta = ((unsigned)sequence + 16384u - history->sequence) & 0x3fffu;
    int signed_delta = delta >= 8192 ? (int)delta - 16384 : (int)delta;
    features[0] = ready ? (float)signed_delta : 0.0f;
    features[1] = ready ? (float)(now_ms - history->last_ms) : 0.0f;
    features[2] = (float)size;
    features[3] = (float)function_code;
    features[4] = (float)history->count;
    history->sequence = sequence;
    history->last_ms = now_ms;
    return ready;
}
