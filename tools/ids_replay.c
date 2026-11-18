#include <stdio.h>
#include <inttypes.h>
#include "ids_features.h"

#ifndef IDS_FEATURES_ONLY
#include "ids_log_format.h"
#include "ids_model.h"
#endif

/* Reads whitespace-separated observations from stdin. This tool has no network I/O. */
int main(void)
{
    IDS_History history = {0};
    char input[256];
    uint64_t ms;
    unsigned seq, size, code;
    char extra;

#ifdef IDS_FEATURES_ONLY
    puts(IDS_FEATURE_COLUMNS);
#else
    puts(IDS_AUDIT_COLUMNS);
#endif
    while (fgets(input, sizeof(input), stdin))
    {
        if (sscanf(input, "%" SCNu64 " %u %u %u %c", &ms, &seq, &size, &code, &extra) != 4 ||
            seq > 16383 || size < 8 || size > 65542 || code > 255)
        {
            fputs("Expected timestamp_ms sequence packet_size function_code\n", stderr);
            return 1;
        }

        float values[IDS_FEATURE_COUNT];
        bool ready = IDS_Observe(&history, ms, seq, size, code, values);
#ifdef IDS_FEATURES_ONLY
        if (ready)
        {
            printf("%.0f,%.0f,%.0f,%.0f,%.0f\n", values[0], values[1], values[2], values[3], values[4]);
        }
#else
        char note[96] = "Warm-up: header check only";
        bool passed = true;
        if (ready)
        {
            float score = IDS_ModelScore(values);
            passed = score <= IDS_MODEL_THRESHOLD;
            if (passed)
            {
                note[0] = '\0';
            }
            else
            {
                snprintf(note, sizeof(note), "Isolation Forest score %.4f exceeds %.4f",
                         score, IDS_MODEL_THRESHOLD);
            }
        }
        char line[160];
        IDS_FormatAudit(line, sizeof(line), ms, 0x18FA, passed, note);
        fputs(line, stdout);
#endif
    }
    return ferror(stdin) || ferror(stdout);
}
