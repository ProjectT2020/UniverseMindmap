#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <inttypes.h>

#include "wal.h"
#include "event/event.h"
#include "utils/logging.h"
#include "utils/crc.h"

#define WAL_MAGIC 0x57414C31u /* "WAL1" */

int disable_wal = 0; // global flag to disable WAL 

// typedef struct {
//     uint64_t lsn;
//     uint64_t offset;
// } WalPosition;

struct WalEntryHeader {
    uint32_t magic;        // 0x57414C31 ("WAL1")
    uint32_t length;       // payload length (not include header / crc)
    uint64_t lsn;          // monotonically increasing
    uint32_t crc32;        // CRC of the payload
};

/**
 * return: -1 on *error* other than EINTR, or read *less than* n bytes
 *       0 on success (read exactly n bytes)    
 */
static int read_full(int fd, void *buf, size_t n) {
    uint8_t *p = (uint8_t *)buf;
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(fd, p + off, n - off);
        if (r == 0) return -1;
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)r;
    }
    return 0;
}

static int write_full(int fd, const void *buf, size_t n) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, p + off, n - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

/**
 * scan WAL file for integrity
 * get last LSN and end offset, truncate if there are partial/corrupted entries at the end
 */
static int wal_scan_and_recover(int fd, uint64_t *out_last_lsn, uint64_t *out_end_offset) {
    if (lseek(fd, 0, SEEK_SET) < 0) return -1;

    uint64_t last_lsn = 0;

    uint64_t next_valid_transaction_offset = 0;
    bool in_transaction = false;
    while (1) {
        struct WalEntryHeader hdr;
        ssize_t r = read(fd, &hdr, sizeof(hdr));
        if (r == 0) break; /* EOF */
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if ((size_t)r < sizeof(hdr)) {
            /* partial header at tail -> truncate */
            break;
        }

        if (hdr.magic != WAL_MAGIC) {
            break;
        }

        if (hdr.length == 0 || hdr.length > (32u * 1024u * 1024u)) {
            break;
        }

        uint8_t *payload = (uint8_t *)malloc(hdr.length);
        if (!payload) return -1;
        if (read_full(fd, payload, hdr.length) != 0) {
            free(payload);
            break;
        }

        uint32_t crc = crc32_ieee(payload, (size_t)hdr.length);
        if (crc != hdr.crc32) {
            free(payload);
            break;
        }
        Event *event = event_deserialize(payload, (size_t)hdr.length);
        free(payload);
        if (!event) {
            break;
        }
        if(event->type == EVENT_BEGIN_TRANSACTION){
            in_transaction = true;
        } else if (event->type == EVENT_COMMIT_TRANSACTION){
            in_transaction = false;
        }
        if (!in_transaction) {
            next_valid_transaction_offset = (off_t)lseek(fd, 0, SEEK_CUR);
            last_lsn = hdr.lsn;
        }
        free(event);
    }

    /* truncate to last good offset */
    if (ftruncate(fd, (off_t)next_valid_transaction_offset) != 0) {
        return -1;
    }

    if (out_last_lsn) *out_last_lsn = last_lsn;
    if (out_end_offset) *out_end_offset = next_valid_transaction_offset;
    return 0;
}

Wal* wal_open(const char *path) {
    if (!path) return NULL;

    int fd = open(path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        return NULL;
    }
    log_debug("Opened WAL file at %s (fd=%d)\n", path, fd);

    uint64_t last_lsn = 0;
    uint64_t end_offset = 0;
    if (wal_scan_and_recover(fd, &last_lsn, &end_offset) != 0) {
        log_error("Failed to scan and recover WAL file: %s\n", path);
        close(fd);
        return NULL;
    }

    if (lseek(fd, (off_t)end_offset, SEEK_SET) < 0) {
        close(fd);
        return NULL;
    }

    Wal *wal = (Wal *)calloc(1, sizeof(Wal));
    if (!wal) {
        close(fd);
        return NULL;
    }
    wal->path = strdup(path);
    wal->fd = fd;
    wal->sync_count_interval = 1; // sync every entry by default
    wal->sync_time_interval = 0; // disable time interval, force sync every entry
    wal->last_sync_time = 0;
    wal->next_lsn = last_lsn + 1;
    wal->end_offset = end_offset;
    return wal;
}

void wal_close(Wal *wal) {
    if (!wal) return;
    if (wal->fd >= 0) {
        (void)fsync(wal->fd);
        close(wal->fd);
        wal->fd = -1;
    }
    free(wal);
}

/**
 * return: 0 on success, -1 on error
 */
int wal_append(Wal *wal, Event *e) {
    if(disable_wal){
        return 0;
    }
    if(debuging){
        event_validate(e);
    }
    if (!wal || wal->fd < 0 || !e) return -1;

    size_t payload_size = 0;
    uint8_t *payload = event_serialize(e, &payload_size);
    if (!payload) return -1;

    log_debug("[wal_append] Appending event: type=%d, lsn=%lu, payload_size=%zu", e->type, wal->next_lsn, payload_size);

    // prepare header
    struct WalEntryHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = WAL_MAGIC;
    hdr.length = (uint32_t)payload_size;
    hdr.lsn = wal->next_lsn;
    hdr.crc32 = crc32_ieee(payload, payload_size);

    // prepare offset
    off_t start = lseek(wal->fd, 0, SEEK_END);
    if (start < 0) return -1;
    if (start != (off_t)wal->end_offset) return -1;

    // write header + payload
    if (write_full(wal->fd, &hdr, sizeof(hdr)) != 0) {
        log_error("[wal_append] Failed to write header");
        return -1;
    }
    if (write_full(wal->fd, payload, payload_size) != 0) {
        log_error("[wal_append] Failed to write payload");
        return -1;
    }

    log_debug("[wal_append] Written to WAL file, wal->end_offset=%lu", wal->end_offset);

    // fsync if needed
    int need_fsync = 0;
    int delta_count = wal->next_lsn - wal->last_sync_lsn;
    if (delta_count >= wal->sync_count_interval) {
        need_fsync = 1;
    }
    int delta_time = (int)(time(NULL) - wal->last_sync_time);
    if (wal->sync_time_interval > 0 && delta_time >= wal->sync_time_interval) {
        need_fsync = 1;
    }
    // smartly skip fsync if writing very frequently with small payloads
    if(delta_count > delta_time * 1000 && delta_count < 1000){
        need_fsync = 0;
    }
    if (need_fsync) {
        if (fsync(wal->fd) != 0) {
            log_error("[wal_append] Failed to fsync WAL file");
            return -1;
        }
        wal->last_sync_lsn = wal->next_lsn;
        wal->last_sync_time = (uint64_t)time(NULL);
        log_debug("[wal_append] WAL fsynced at lsn=%lu", wal->last_sync_lsn);
    }

    wal->next_lsn++;
    wal->end_offset = (uint64_t)start + (uint64_t)sizeof(hdr) + (uint64_t)payload_size;
    assert(wal->end_offset == (uint64_t)lseek(wal->fd, 0, SEEK_CUR));
    assert(wal->end_offset == (uint64_t)lseek(wal->fd, 0, SEEK_END));
    free(payload);
    log_debug("[wal_append] Success: next_lsn=%lu, event{%s, parent_id=%d, node_id=%d, next_sibling_id=%d, old_parent=%d, old_next_sibling_id=%d}",
         wal->next_lsn, event_type_to_string(e->type), 
         e->parent_id, e->node_id, e->next_sibling_id, e->old_parent, e->old_next_sibling_id

        );
    return 0;
}

/**
 * on_payload: return 0 to continue, non-zero to stop
 */
int wal_replay(
    Wal *wal,
    uint64_t *out_last_lsn,
    int (*on_payload)(Event *event, void *ctx),
    void *ctx
){
    if (!wal || wal->fd < 0 || !on_payload) return -1;
    uint64_t checkpoint_lsn = wal->checkpoint_lsn;

    log_debug("[wal_replay] Starting replay: checkpoint_lsn=%lu, wal_next_lsn=%lu\n", checkpoint_lsn, wal->next_lsn);

    if (lseek(wal->fd, 0, SEEK_SET) < 0) return -1;

    int entry_count = 0;
    while (1) {
        struct WalEntryHeader hdr;
        int r = read_full(wal->fd, &hdr, sizeof(hdr));
        if (r == -1) {
            log_debug("[wal_replay] End of WAL file, replayed %d entries\n", entry_count);
            return 0;// end of reading
        }

        if (hdr.magic != WAL_MAGIC) {
            log_debug("[wal_replay] Bad magic: 0x%08x\n", hdr.magic);
            return -1;
        }
        if (hdr.length == 0 || hdr.length > (32u * 1024u * 1024u)) return -1;
        
        if(hdr.lsn <= checkpoint_lsn){
            // skip
            log_debug("[wal_replay] Skipping entry with lsn=%lu (checkpoint_lsn=%lu)\n", hdr.lsn, checkpoint_lsn);
            if(lseek(wal->fd, (off_t)hdr.length, SEEK_CUR) < 0) return -1;
            continue;
        }

        
        uint8_t *payload = (uint8_t *)malloc(hdr.length);
        if (!payload) return -1;
        if (read_full(wal->fd, payload, hdr.length) != 0) {
            free(payload);
            return -1;
        }
        uint32_t crc = crc32_ieee(payload, (size_t)hdr.length);
        if (crc != hdr.crc32) {
            free(payload);
            return -1;
        }

        Event *event = event_deserialize(payload, (size_t)hdr.length);
        free(payload);
        if (!event) {
            log_debug("[wal_replay] Failed to deserialize event at lsn=%lu\n", hdr.lsn);
            return -1;
        }
        log_debug("[wal_replay] Replaying entry: lsn=%lu, length=%u, entry_count=%d, event{%s, parent_id=%d, node_id=%d, }\n",
             hdr.lsn, hdr.length, entry_count, event_type_to_string(event->type), event->parent_id, event->node_id);
        if(event->type == EVENT_BEGIN_TRANSACTION ||
           event->type == EVENT_COMMIT_TRANSACTION){
            log_debug("[wal_replay] Skipping transaction control event: type=%d\n", event->type);
        }else{
            int cb = on_payload(event, ctx);
            if (cb != 0) {// on_payload return non-zero to stop
                *out_last_lsn = hdr.lsn;
                log_debug("[wal_replay] Callback returned non-zero, stopping at lsn=%lu\n", hdr.lsn);
                fprintf(stderr, "[ERROR] [wal_replay] Callback returned non-zero, stopping at lsn=%llu\n", hdr.lsn);
                return cb;
            }
        }
        free(event);
        *out_last_lsn = hdr.lsn;
        wal->next_lsn = hdr.lsn + 1;
        entry_count++;
    }
    return 0;
}

// static int read_next_entry_header(int fd, uint64_t *out_offset, struct WalEntryHeader *out_hdr) {
//     off_t curr = lseek(fd, *out_offset, SEEK_CUR);
//     if (curr < 0) return -1;

//     struct WalEntryHeader hdr;
//     ssize_t r = read_full(fd, &hdr, sizeof(hdr));
//     if (r == -1) {
//         return -1;
//     }

//     if (hdr.magic != WAL_MAGIC) {
//         return -1;
//     }

//     if (out_offset) *out_offset += sizeof(hdr) + hdr.length;
//     if (out_hdr) *out_hdr = hdr;
//     return 0;

// }

// static int read_next_entry(int fd, uint64_t *out_offset, struct WalEntryHeader *hdr, uint8_t **out_payload) {
//     off_t curr = lseek(fd, *out_offset, SEEK_CUR);
//     if (curr < 0) return -1;

//     ssize_t r = read_full(fd, hdr, sizeof(*hdr));
//     if (r == -1) {
//         return -1;
//     }

//     if (hdr->magic != WAL_MAGIC) {
//         return -1;
//     }

//     uint8_t *payload = (uint8_t *)malloc(hdr->length);
//     if (!payload) return -1;
//     if (read_full(fd, payload, hdr->length) != 0) {
//         free(payload);
//         return -1;
//     }

//     uint32_t crc = crc32_ieee(payload, (size_t)hdr->length);
//     if (crc != hdr->crc32) {
//         free(payload);
//         return -1;
//     }

//     if (out_offset) *out_offset += sizeof(*hdr) + hdr->length;
//     if (out_payload) *out_payload = payload;
//     else free(payload);
//     return 0;
// }

/**
 * wal_truncate_commited() - Delete WAL entries persisted by a checkpoint.
 *
 * Removes all entries with LSN strictly less than the given reference LSN.
 * If that LSN falls inside a transaction, it must refer to the last
 * non-control event in that transaction, and the trailing COMMIT record
 * is truncated as part of the same atomic unit so the remaining WAL does
 * not start with a dangling COMMIT.
 *
 * Returns:
 *   0  - Success
 *   -1 - Error
 */
int wal_truncate_commited(Wal *wal, uint64_t lsn) {
    if (!wal || wal->fd < 0) {
        log_error("[wal_truncate_commited] invalid wal");
        return -1;
    }

    if (lsn <= 0) {
        log_debug("[wal_truncate_commited] invalid lsn, skipping");
        return 0;
    }

    log_debug("[wal_truncate_commited] Truncating entries with LSN < %"PRIu64, lsn);

    // Seek to beginning to read all entries
    if (lseek(wal->fd, 0, SEEK_SET) < 0) {
        log_error("[wal_truncate_commited] initial lseek failed");
        return -1;
    }

    // Create temporary file
    char temp_path[256];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", wal->path);

    int temp_fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (temp_fd < 0) {
        log_error("[wal_truncate_commited] failed to open temp file");
        return -1;
    }

    // Iterate through all entries and preserve the reference entry and later ones.
    off_t offset = 0;
    off_t new_end_offset = 0;
    int entries_kept = 0;
    int entries_removed = 0;

    bool in_transaction = false;
    bool waiting_for_commit = false;
    uint64_t truncated_through_lsn = 0;

    while (offset < (off_t)wal->end_offset) {
        struct WalEntryHeader hdr;
        ssize_t n = pread(wal->fd, &hdr, sizeof(hdr), offset);
        if (n != (ssize_t)sizeof(hdr)) {
            break; // Reached EOF or read error
        }

        if (hdr.magic != WAL_MAGIC) {
            log_debug("[wal_truncate_commited] invalid magic at offset %lld", (long long)offset);
            break;
        }

        if (hdr.length == 0 || hdr.length > (32u * 1024u * 1024u)) {
            log_error("[wal_truncate_commited] invalid entry length: %u", hdr.length);
            break;
        }

        // Total entry size = header + payload
        uint32_t total_entry_size = sizeof(hdr) + hdr.length;

        uint8_t *entry_buf = (uint8_t *)malloc(total_entry_size);
        if (!entry_buf) {
            log_error("[wal_truncate_commited] failed to allocate %u bytes", total_entry_size);
            close(temp_fd);
            unlink(temp_path);
            return -1;
        }

        n = pread(wal->fd, entry_buf, total_entry_size, offset);
        if (n != (ssize_t)total_entry_size) {
            log_error("[wal_truncate_commited] failed to read complete entry");
            free(entry_buf);
            break;
        }

        struct WalEntryHeader *read_hdr = (struct WalEntryHeader *)entry_buf;
        uint64_t entry_lsn = read_hdr->lsn;
        uint8_t *payload = entry_buf + sizeof(*read_hdr);
        uint32_t crc = crc32_ieee(payload, (size_t)read_hdr->length);
        if (crc != read_hdr->crc32) {
            log_error("[wal_truncate_commited] crc mismatch at lsn=%" PRIu64, entry_lsn);
            free(entry_buf);
            break;
        }

        Event *event = event_deserialize(payload, (size_t)read_hdr->length);
        if (!event) {
            log_error("[wal_truncate_commited] failed to deserialize entry at lsn=%" PRIu64, entry_lsn);
            free(entry_buf);
            break;
        }

        bool entry_is_begin = event->type == EVENT_BEGIN_TRANSACTION;
        bool entry_is_commit = event->type == EVENT_COMMIT_TRANSACTION;
        bool keep_entry = false;

        if (waiting_for_commit) {
            if (entry_is_commit) {
                entries_removed++;
                truncated_through_lsn = entry_lsn;
                waiting_for_commit = false;
            } else {
                log_error("[wal_truncate_commited] checkpoint LSN %" PRIu64 " is not the last event before COMMIT", lsn);
                free(event);
                free(entry_buf);
                close(temp_fd);
                unlink(temp_path);
                return -1;
            }
        } else if (entry_lsn < lsn) {
            entries_removed++;
            truncated_through_lsn = entry_lsn;
        } else if (entry_lsn == lsn) {
            if (entry_is_begin || entry_is_commit) {
                log_error("[wal_truncate_commited] reference LSN %" PRIu64 " must reference a non-control event", lsn);
                free(event);
                free(entry_buf);
                close(temp_fd);
                unlink(temp_path);
                return -1;
            }

            if (in_transaction) {
                keep_entry = true;
                waiting_for_commit = true;
            } else {
                keep_entry = true;
            }
        } else {
            keep_entry = true;
        }

        if (keep_entry) {
            if (write_full(temp_fd, entry_buf, total_entry_size) != 0) {
                log_error("[wal_truncate_commited] failed to write to temp file");
                free(event);
                free(entry_buf);
                close(temp_fd);
                unlink(temp_path);
                return -1;
            }
            new_end_offset += total_entry_size;
            entries_kept++;
        } else {
            log_debug("[wal_truncate_commited] Removing entry with LSN %"PRIu64, entry_lsn);
        }

        if (entry_is_begin) {
            if (in_transaction) {
                log_error("[wal_truncate_commited] nested transaction begin at lsn=%" PRIu64, entry_lsn);
                free(event);
                free(entry_buf);
                close(temp_fd);
                unlink(temp_path);
                return -1;
            }
            in_transaction = true;
        } else if (entry_is_commit) {
            if (!in_transaction && !waiting_for_commit) {
                log_error("[wal_truncate_commited] unexpected commit at lsn=%" PRIu64, entry_lsn);
                free(event);
                free(entry_buf);
                close(temp_fd);
                unlink(temp_path);
                return -1;
            }
            in_transaction = false;
        }

        free(event);
        free(entry_buf);
        offset += total_entry_size;
    }

    if (waiting_for_commit) {
        log_error("[wal_truncate_commited] checkpoint LSN %" PRIu64 " ends inside an unclosed transaction", lsn);
        close(temp_fd);
        unlink(temp_path);
        return -1;
    }

    close(temp_fd);

    // Replace original file with temp file
    if (rename(temp_path, wal->path) < 0) {
        log_error("[wal_truncate_commited] rename failed");
        unlink(temp_path);
        return -1;
    }

    // Reopen the file
    if (wal->fd >= 0) close(wal->fd);
    wal->fd = open(wal->path, O_RDWR | O_CREAT | O_APPEND, 0644);
    if (wal->fd < 0) {
        log_error("[wal_truncate_commited] failed to reopen wal file");
        return -1;
    }

    // Update state
    wal->end_offset = (uint64_t)new_end_offset;
    wal->last_sync_lsn = truncated_through_lsn;
    wal->checkpoint_lsn = lsn;

    log_debug("[wal_truncate_commited] Success: removed %d entries, kept %d entries, new end_offset=%"PRIu64,
              entries_removed, entries_kept, wal->end_offset);
    return 0;
}