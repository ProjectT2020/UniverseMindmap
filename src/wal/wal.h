#ifndef WAL_H
#define WAL_H

#include <stdint.h>

#include "../event/event_types.h"

extern int disable_wal;

typedef struct Wal {
    int fd;
    char *path;
    int sync_count_interval; // how many entries to write before next fsync, 0 means no count-based sync
    uint64_t last_sync_lsn; // last synchronized LSN
    int sync_time_interval; // how many seconds to sync, 0 means no time-based sync
    uint64_t last_sync_time; // last synchronization timestamp (seconds)
    uint64_t checkpoint_lsn; // latest checkpoint LSN
    uint64_t checkpoint_offset; // latest checkpoint offset
    uint64_t next_lsn;
    uint64_t end_offset;
} Wal;


Wal* wal_open(const char *path);
void wal_close(Wal *wal);

int wal_append(Wal *wal, Event *e);

int wal_replay(
    Wal *wal,
    uint64_t *out_last_lsn,
    int (*on_payload)(Event *event, void *ctx),
    void *ctx
);

/**
 * truncate all record with lsn < given lsn
 */
int wal_truncate_commited(Wal *wal, uint64_t lsn);

#endif // WAL_H
