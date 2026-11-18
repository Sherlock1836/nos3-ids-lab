#ifndef IDS_FEATURES_H
#define IDS_FEATURES_H

#include <stdbool.h>
#include <stdint.h>

#define IDS_WINDOW_CAPACITY 32
#define IDS_FEATURE_COUNT 5
#define IDS_FEATURE_COLUMNS "seq_delta,delta_t_ms,packet_size,function_code,packets_last_second"

typedef struct
{
    uint64_t times[IDS_WINDOW_CAPACITY];
    unsigned head;
    unsigned count;
    uint16_t sequence;
    uint64_t last_ms;
} IDS_History;

/* One history per monitored stream. Returns false during warm-up or clock reset. */
bool IDS_Observe(IDS_History *history, uint64_t now_ms, uint16_t sequence,
                 unsigned size, unsigned function_code, float features[IDS_FEATURE_COUNT]);

#endif
