#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "../src/wal/wal.h"
#include "../src/event/event.h"
#include "../src/utils/crc.h"

#define TEST_WAL_MAGIC 0x57414C31u

struct TestWalEntryHeader {
    uint32_t magic;
    uint32_t length;
    uint64_t lsn;
    uint32_t crc32;
};

uint8_t *event_serialize(Event *e, size_t *out_size) {
    size_t text_len = (e && e->text) ? strlen(e->text) + 1 : 0;
    size_t total = sizeof(Event) + text_len;
    uint8_t *buf = (uint8_t *)malloc(total);
    assert(buf != NULL);

    Event copy = *e;
    copy.text = NULL;
    copy.old_text = NULL;
    copy.old_children = NULL;
    memcpy(buf, &copy, sizeof(copy));
    if (text_len > 0) {
        memcpy(buf + sizeof(Event), e->text, text_len);
    }

    if (out_size) {
        *out_size = total;
    }
    return buf;
}

Event *event_deserialize(uint8_t *buf, size_t buf_size) {
    if (!buf || buf_size < sizeof(Event)) {
        return NULL;
    }

    Event *e = (Event *)calloc(1, sizeof(Event));
    assert(e != NULL);
    memcpy(e, buf, sizeof(Event));
    e->old_text = NULL;
    e->old_children = NULL;

    size_t text_len = buf_size - sizeof(Event);
    if (text_len > 0) {
        e->text = (char *)calloc(1, text_len);
        assert(e->text != NULL);
        memcpy(e->text, buf + sizeof(Event), text_len);
    }

    return e;
}

int event_validate(Event *e) {
    return (e == NULL) ? -1 : 0;
}

const char *event_type_to_string(EventType type) {
    switch (type) {
        case EVENT_BEGIN_TRANSACTION:
            return "EVENT_BEGIN_TRANSACTION";
        case EVENT_COMMIT_TRANSACTION:
            return "EVENT_COMMIT_TRANSACTION";
        case EVENT_UPDATE_TEXT:
            return "EVENT_UPDATE_TEXT";
        default:
            return "EVENT_OTHER";
    }
}

static Event *new_event(EventType type, uint64_t node_id, const char *text) {
    Event *e = (Event *)calloc(1, sizeof(Event));
    assert(e != NULL);
    e->type = type;
    e->node_id = node_id;
    if (text) {
        e->text = strdup(text);
        assert(e->text != NULL);
    }
    return e;
}

static void free_event(Event *event) {
    if (!event) {
        return;
    }
    free(event->text);
    free(event);
}

typedef struct ReplayCapture {
    EventType types[16];
    int count;
} ReplayCapture;

typedef struct RawCaptureEntry {
    uint64_t lsn;
    EventType type;
} RawCaptureEntry;

typedef struct RawCapture {
    RawCaptureEntry entries[16];
    int count;
} RawCapture;

static int read_full_local(int fd, void *buf, size_t n) {
    uint8_t *p = (uint8_t *)buf;
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(fd, p + off, n - off);
        if (r == 0) {
            return -1;
        }
        assert(r > 0);
        off += (size_t)r;
    }
    return 0;
}

static void capture_raw_entries(const char *path, RawCapture *capture) {
    int fd = open(path, O_RDONLY);
    assert(fd >= 0);
    capture->count = 0;

    while (1) {
        struct TestWalEntryHeader hdr;
        ssize_t n = read(fd, &hdr, sizeof(hdr));
        if (n == 0) {
            break;
        }
        assert(n == (ssize_t)sizeof(hdr));
        assert(hdr.magic == TEST_WAL_MAGIC);

        uint8_t *payload = (uint8_t *)malloc(hdr.length);
        assert(payload != NULL);
        assert(read_full_local(fd, payload, hdr.length) == 0);
        assert(crc32_ieee(payload, hdr.length) == hdr.crc32);

        Event *event = event_deserialize(payload, hdr.length);
        assert(event != NULL);
        assert(capture->count < (int)(sizeof(capture->entries) / sizeof(capture->entries[0])));
        capture->entries[capture->count].lsn = hdr.lsn;
        capture->entries[capture->count].type = event->type;
        capture->count++;

        free_event(event);
        free(payload);
    }

    close(fd);
}

static int capture_event(Event *event, void *ctx) {
    ReplayCapture *capture = (ReplayCapture *)ctx;
    assert(capture->count < (int)(sizeof(capture->types) / sizeof(capture->types[0])));
    capture->types[capture->count++] = event->type;
    return 0;
}

static off_t file_size(const char *path) {
    struct stat st;
    assert(stat(path, &st) == 0);
    return st.st_size;
}

static void append_event_or_die(Wal *wal, Event *event) {
    assert(event != NULL);
    assert(wal_append(wal, event) == 0);
    free_event(event);
}

static void test_recover_truncates_uncommitted_transaction(void) {
    char path[] = "/tmp/um_wal_recover_txn_XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);

    Wal *wal = wal_open(path);
    assert(wal != NULL);

    append_event_or_die(wal, new_event(EVENT_BEGIN_TRANSACTION, 0, NULL));
    append_event_or_die(wal, new_event(EVENT_UPDATE_TEXT, 100, "committed"));
    append_event_or_die(wal, new_event(EVENT_COMMIT_TRANSACTION, 0, NULL));

    append_event_or_die(wal, new_event(EVENT_BEGIN_TRANSACTION, 0, NULL));
    append_event_or_die(wal, new_event(EVENT_UPDATE_TEXT, 101, "uncommitted"));

    off_t size_before_recovery = file_size(path);
    wal_close(wal);

    Wal *recovered = wal_open(path);
    assert(recovered != NULL);

    off_t size_after_recovery = file_size(path);
    assert(size_after_recovery < size_before_recovery);

    ReplayCapture capture = {0};
    uint64_t last_lsn = 0;
    assert(wal_replay(recovered, &last_lsn, capture_event, &capture) == 0);

    assert(capture.count == 1);
    assert(capture.types[0] == EVENT_UPDATE_TEXT);
    assert(last_lsn == 3);
    assert(recovered->next_lsn == 4);

    wal_close(recovered);
    assert(unlink(path) == 0);
}

static void test_recover_keeps_non_transaction_tail_entries(void) {
    char path[] = "/tmp/um_wal_recover_plain_XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);

    Wal *wal = wal_open(path);
    assert(wal != NULL);

    append_event_or_die(wal, new_event(EVENT_UPDATE_TEXT, 1, "a"));
    append_event_or_die(wal, new_event(EVENT_UPDATE_TEXT, 2, "b"));

    off_t size_before_recovery = file_size(path);
    wal_close(wal);

    Wal *recovered = wal_open(path);
    assert(recovered != NULL);

    off_t size_after_recovery = file_size(path);
    assert(size_after_recovery == size_before_recovery);

    ReplayCapture capture = {0};
    uint64_t last_lsn = 0;
    assert(wal_replay(recovered, &last_lsn, capture_event, &capture) == 0);

    assert(capture.count == 2);
    assert(capture.types[0] == EVENT_UPDATE_TEXT);
    assert(capture.types[1] == EVENT_UPDATE_TEXT);
    assert(last_lsn == 2);
    assert(recovered->next_lsn == 3);

    wal_close(recovered);
    assert(unlink(path) == 0);
}

static void test_truncate_commited_keeps_reference_plain_entry(void) {
    char path[] = "/tmp/um_wal_truncate_plain_XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);

    Wal *wal = wal_open(path);
    assert(wal != NULL);

    append_event_or_die(wal, new_event(EVENT_UPDATE_TEXT, 1, "a"));
    append_event_or_die(wal, new_event(EVENT_UPDATE_TEXT, 2, "b"));
    append_event_or_die(wal, new_event(EVENT_UPDATE_TEXT, 3, "c"));

    assert(wal_truncate_commited(wal, 2) == 0);

    RawCapture capture = {0};
    capture_raw_entries(path, &capture);
    assert(capture.count == 2);
    assert(capture.entries[0].lsn == 2);
    assert(capture.entries[0].type == EVENT_UPDATE_TEXT);
    assert(capture.entries[1].lsn == 3);
    assert(capture.entries[1].type == EVENT_UPDATE_TEXT);

    wal_close(wal);
    assert(unlink(path) == 0);
}

static void test_truncate_commited_removes_commit_after_transaction_checkpoint(void) {
    char path[] = "/tmp/um_wal_truncate_txn_XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);

    Wal *wal = wal_open(path);
    assert(wal != NULL);

    append_event_or_die(wal, new_event(EVENT_UPDATE_TEXT, 1, "before"));
    append_event_or_die(wal, new_event(EVENT_BEGIN_TRANSACTION, 0, NULL));
    append_event_or_die(wal, new_event(EVENT_UPDATE_TEXT, 2, "inside"));
    append_event_or_die(wal, new_event(EVENT_COMMIT_TRANSACTION, 0, NULL));
    append_event_or_die(wal, new_event(EVENT_UPDATE_TEXT, 3, "after"));

    assert(wal_truncate_commited(wal, 3) == 0);

    RawCapture capture = {0};
    capture_raw_entries(path, &capture);
    assert(capture.count == 2);
    assert(capture.entries[0].lsn == 3);
    assert(capture.entries[0].type == EVENT_UPDATE_TEXT);
    assert(capture.entries[1].lsn == 5);
    assert(capture.entries[1].type == EVENT_UPDATE_TEXT);
    assert(wal->last_sync_lsn == 4);

    wal_close(wal);
    assert(unlink(path) == 0);
}

static void test_truncate_commited_rejects_non_terminal_transaction_lsn(void) {
    char path[] = "/tmp/um_wal_truncate_txn_invalid_XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);

    Wal *wal = wal_open(path);
    assert(wal != NULL);

    append_event_or_die(wal, new_event(EVENT_BEGIN_TRANSACTION, 0, NULL));
    append_event_or_die(wal, new_event(EVENT_UPDATE_TEXT, 10, "first"));
    append_event_or_die(wal, new_event(EVENT_UPDATE_TEXT, 11, "second"));
    append_event_or_die(wal, new_event(EVENT_COMMIT_TRANSACTION, 0, NULL));
    append_event_or_die(wal, new_event(EVENT_UPDATE_TEXT, 12, "after"));

    RawCapture before = {0};
    capture_raw_entries(path, &before);

    assert(wal_truncate_commited(wal, 2) == -1);

    RawCapture after = {0};
    capture_raw_entries(path, &after);
    assert(after.count == before.count);
    for (int i = 0; i < before.count; i++) {
        assert(after.entries[i].lsn == before.entries[i].lsn);
        assert(after.entries[i].type == before.entries[i].type);
    }

    wal_close(wal);
    assert(unlink(path) == 0);
}

static void test_truncate_commited_nested_transaction_keeps_reference_and_drops_trailing_commits(void) {
    char path[] = "/tmp/um_wal_truncate_nested_ok_XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);

    Wal *wal = wal_open(path);
    assert(wal != NULL);

    append_event_or_die(wal, new_event(EVENT_UPDATE_TEXT, 1, "before"));
    append_event_or_die(wal, new_event(EVENT_BEGIN_TRANSACTION, 0, NULL));
    append_event_or_die(wal, new_event(EVENT_UPDATE_TEXT, 2, "outer"));
    append_event_or_die(wal, new_event(EVENT_BEGIN_TRANSACTION, 0, NULL));
    append_event_or_die(wal, new_event(EVENT_UPDATE_TEXT, 3, "inner-last"));
    append_event_or_die(wal, new_event(EVENT_COMMIT_TRANSACTION, 0, NULL));
    append_event_or_die(wal, new_event(EVENT_COMMIT_TRANSACTION, 0, NULL));
    append_event_or_die(wal, new_event(EVENT_UPDATE_TEXT, 4, "after"));

    // LSN 5 is inside nested transactions and is terminal for both levels.
    assert(wal_truncate_commited(wal, 5) == 0);

    RawCapture capture = {0};
    capture_raw_entries(path, &capture);
    assert(capture.count == 2);
    assert(capture.entries[0].lsn == 5);
    assert(capture.entries[0].type == EVENT_UPDATE_TEXT);
    assert(capture.entries[1].lsn == 8);
    assert(capture.entries[1].type == EVENT_UPDATE_TEXT);

    wal_close(wal);
    assert(unlink(path) == 0);
}

static void test_truncate_commited_nested_transaction_rejects_non_terminal_lsn(void) {
    char path[] = "/tmp/um_wal_truncate_nested_bad_XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);

    Wal *wal = wal_open(path);
    assert(wal != NULL);

    append_event_or_die(wal, new_event(EVENT_BEGIN_TRANSACTION, 0, NULL));
    append_event_or_die(wal, new_event(EVENT_UPDATE_TEXT, 10, "outer-a"));
    append_event_or_die(wal, new_event(EVENT_BEGIN_TRANSACTION, 0, NULL));
    append_event_or_die(wal, new_event(EVENT_UPDATE_TEXT, 11, "inner-last"));
    append_event_or_die(wal, new_event(EVENT_COMMIT_TRANSACTION, 0, NULL));
    append_event_or_die(wal, new_event(EVENT_UPDATE_TEXT, 12, "outer-b"));
    append_event_or_die(wal, new_event(EVENT_COMMIT_TRANSACTION, 0, NULL));

    RawCapture before = {0};
    capture_raw_entries(path, &before);

    // LSN 4 is terminal for inner txn but not terminal for outer txn.
    assert(wal_truncate_commited(wal, 4) == -1);

    RawCapture after = {0};
    capture_raw_entries(path, &after);
    assert(after.count == before.count);
    for (int i = 0; i < before.count; i++) {
        assert(after.entries[i].lsn == before.entries[i].lsn);
        assert(after.entries[i].type == before.entries[i].type);
    }

    wal_close(wal);
    assert(unlink(path) == 0);
}

int main(void) {
    test_recover_truncates_uncommitted_transaction();
    test_recover_keeps_non_transaction_tail_entries();
    test_truncate_commited_keeps_reference_plain_entry();
    test_truncate_commited_removes_commit_after_transaction_checkpoint();
    test_truncate_commited_rejects_non_terminal_transaction_lsn();
    test_truncate_commited_nested_transaction_keeps_reference_and_drops_trailing_commits();
    test_truncate_commited_nested_transaction_rejects_non_terminal_lsn();

    printf("[PASS] wal scan/recover tests\n");
    return 0;
}
