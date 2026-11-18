#ifndef IDS_LOG_FORMAT_H
#define IDS_LOG_FORMAT_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#define IDS_AUDIT_COLUMNS "timestamp,message_id,status,note"

static inline int IDS_FormatAudit(char *line, size_t capacity, uint64_t ms,
                                  unsigned mid, bool passed, const char *note)
{
    return snprintf(line, capacity, "%" PRIu64 ".%03u,0x%04X,%s,%s\n",
                    ms / 1000, (unsigned)(ms % 1000), mid, passed ? "PASS" : "FAIL", note);
}

#endif
