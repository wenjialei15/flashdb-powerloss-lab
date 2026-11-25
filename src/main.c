/*
 * Power-loss sweep for FlashDB.
 *
 * Both databases run on a simulated 64 KiB flash. For every erase/write the
 * workload performs, the experiment cuts power just before that operation,
 * reboots (RAM is cleared, flash is kept), and checks the surviving data.
 *
 * Invariants:
 *   KVDB  - every key still exists, and its value is either the old or the new
 *           one; a torn write must never produce anything else.
 *   TSDB  - every record that is still readable carries the payload that
 *           belongs to its timestamp, in strictly increasing time order.
 */

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fal.h>
#include <flashdb.h>

#include "sim_flash.h"

#define KEY_COUNT       96
#define UPDATED_COUNT   48
#define VAL_LEN         64

#define TS_COUNT        520
#define TS_LEN          16
#define TS_BASE         100000

static struct fdb_kvdb g_kvdb;
static struct fdb_tsdb g_tsdb;
static jmp_buf g_reboot;

static int g_failures;
static int g_single_cut;

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static void key_name(char *buf, size_t len, int i)
{
    snprintf(buf, len, "key%03d", i);
}

static void kv_value(char *buf, const char *tag, int i)
{
    snprintf(buf, VAL_LEN, "%s-%03d-0123456789abcdef0123456789abcdef01234567", tag, i);
}

static void ts_payload(uint8_t *buf, fdb_time_t ts)
{
    size_t i;

    for (i = 0; i < TS_LEN; i++) {
        buf[i] = (uint8_t)((ts * 31u + i * 7u) & 0xffu);
    }
}

static fdb_time_t ts_now(void)
{
    return TS_BASE;
}

/* ------------------------------------------------------------------------- */
/* KVDB                                                                      */
/* ------------------------------------------------------------------------- */

static int kv_boot(void)
{
    uint32_t sec_size = SIM_FLASH_SECTOR;

    /* A reboot loses all RAM state; the struct is zeroed, not deinitialised. */
    memset(&g_kvdb, 0, sizeof(g_kvdb));

    fdb_kvdb_control(&g_kvdb, FDB_KVDB_CTRL_SET_SEC_SIZE, &sec_size);
    return fdb_kvdb_init(&g_kvdb, "env", "kvdb", NULL, NULL) == FDB_NO_ERR ? 0 : -1;
}

static int kv_seed(void)
{
    char key[16];
    char val[VAL_LEN];
    int i;

    for (i = 0; i < KEY_COUNT; i++) {
        key_name(key, sizeof(key), i);
        kv_value(val, "old", i);
        if (fdb_kv_set(&g_kvdb, key, val) != FDB_NO_ERR) {
            return -1;
        }
    }
    return 0;
}

static int kv_workload(void)
{
    char key[16];
    char val[VAL_LEN];
    int i;

    for (i = 0; i < UPDATED_COUNT; i++) {
        key_name(key, sizeof(key), i);
        kv_value(val, "new", i);
        if (fdb_kv_set(&g_kvdb, key, val) != FDB_NO_ERR) {
            return -1;
        }
    }
    return 0;
}

static int kv_verify(void)
{
    char key[16], expect_old[VAL_LEN], expect_new[VAL_LEN];
    int i;
    int bad = 0;

    for (i = 0; i < KEY_COUNT; i++) {
        const char *got;

        key_name(key, sizeof(key), i);
        kv_value(expect_old, "old", i);
        kv_value(expect_new, "new", i);

        got = fdb_kv_get(&g_kvdb, key);
        if (got == NULL) {
            printf("    %s missing\n", key);
            bad++;
            continue;
        }

        if (i < UPDATED_COUNT) {
            if (strcmp(got, expect_old) != 0 && strcmp(got, expect_new) != 0) {
                printf("    %s = '%s' (expected old or new)\n", key, got);
                bad++;      /* neither old nor new: a torn write leaked through */
            }
        } else if (strcmp(got, expect_old) != 0) {
            printf("    %s = '%s' (expected '%s')\n", key, got, expect_old);
            bad++;          /* untouched key changed or disappeared */
        }
    }

    /* The database must still be usable after the recovery. */
    if (fdb_kv_set(&g_kvdb, "probe", "ok") != FDB_NO_ERR) {
        bad++;
    } else if (fdb_kv_get(&g_kvdb, "probe") == NULL) {
        bad++;
    }

    return bad;
}

static void kv_scenario(void)
{
    int total_ops;
    int cut;
    int failed_cuts = 0;

    sim_flash_init();
    if (fal_init() <= 0) {
        printf("KVDB: FAL init failed\n");
        g_failures++;
        return;
    }

    if (kv_boot() != 0 || kv_seed() != 0) {
        printf("KVDB: seed failed\n");
        g_failures++;
        return;
    }

    /* Snapshot the committed database; every trial starts from here. */
    sim_flash_snapshot();

    /* Baseline: how many flash operations does the update workload perform? */
    if (kv_boot() != 0) {
        printf("KVDB: reboot failed\n");
        g_failures++;
        return;
    }
    sim_flash_clear_ops();
    if (kv_workload() != 0) {
        printf("KVDB: workload failed\n");
        g_failures++;
        return;
    }
    total_ops = sim_flash_op_count();

    printf("== KVDB: update %d of %d keys ==\n", UPDATED_COUNT, KEY_COUNT);
    printf("baseline: %d flash ops, %d sectors, %d bytes per value\n\n",
           total_ops, (int)(24 * 1024 / SIM_FLASH_SECTOR), VAL_LEN);

    for (cut = 1; cut <= total_ops; cut++) {
        int bad;

        if (g_single_cut && cut != g_single_cut) {
            continue;
        }
        sim_flash_restore();
        if (kv_boot() != 0) {
            failed_cuts++;
            continue;
        }

        sim_flash_set_cut(cut, &g_reboot);
        if (setjmp(g_reboot) == 0) {
            kv_workload();
            continue;               /* no cut: cut index beyond the workload */
        }

        /* Power was cut. Disarm it before the reboot does its own I/O. */
        sim_flash_set_cut(0, NULL);

        if (kv_boot() != 0) {
            bad = 1;
        } else {
            bad = kv_verify();
        }

        if (bad) {
            failed_cuts++;
            printf("  cut %4d: %d problem(s)\n", cut, bad);
        }
    }

    printf("KVDB: %d cut points, %d failed\n\n", total_ops, failed_cuts);
    g_failures += failed_cuts;
}

/* ------------------------------------------------------------------------- */
/* TSDB                                                                      */
/* ------------------------------------------------------------------------- */

static int ts_boot(void)
{
    uint32_t sec_size = SIM_FLASH_SECTOR;
    uint8_t rollover = 1;

    memset(&g_tsdb, 0, sizeof(g_tsdb));

    fdb_tsdb_control(&g_tsdb, FDB_TSDB_CTRL_SET_SEC_SIZE, &sec_size);
    if (fdb_tsdb_init(&g_tsdb, "log", "tsdb", ts_now, TS_LEN, NULL) != FDB_NO_ERR) {
        return -1;
    }
    fdb_tsdb_control(&g_tsdb, FDB_TSDB_CTRL_SET_ROLLOVER, &rollover);
    return 0;
}

static int ts_workload(int count)
{
    uint8_t payload[TS_LEN];
    struct fdb_blob blob;
    int i;

    for (i = 0; i < count; i++) {
        fdb_time_t ts = TS_BASE + i;
        ts_payload(payload, ts);
        if (fdb_tsl_append_with_ts(&g_tsdb,
                                   fdb_blob_make(&blob, payload, sizeof(payload)),
                                   ts) != FDB_NO_ERR) {
            return -1;
        }
    }
    return 0;
}

struct ts_check {
    int count;
    int bad;
    fdb_time_t last;
};

static bool ts_cb(fdb_tsl_t tsl, void *arg)
{
    struct ts_check *check = arg;
    struct fdb_blob blob;
    uint8_t payload[TS_LEN];
    uint8_t expect[TS_LEN];
    size_t i;

    if (tsl->status != FDB_TSL_WRITE) {
        check->count++;
        return false;           /* rolled-over / deleted record, no payload to trust */
    }

    fdb_blob_read((fdb_db_t)&g_tsdb,
                  fdb_tsl_to_blob(tsl, fdb_blob_make(&blob, payload, sizeof(payload))));

    ts_payload(expect, tsl->time);
    for (i = 0; i < TS_LEN; i++) {
        if (payload[i] != expect[i]) {
            check->bad++;
            break;
        }
    }

    if (tsl->time <= check->last) {
        check->bad++;               /* order must survive a reboot */
    }
    check->last = tsl->time;
    check->count++;

    return false;
}

static int ts_verify(void)
{
    struct ts_check check;

    memset(&check, 0, sizeof(check));
    check.last = (fdb_time_t)-1;

    fdb_tsl_iter(&g_tsdb, ts_cb, &check);

    if (check.bad != 0) {
        return check.bad;
    }

    /* Still writable? */
    {
        uint8_t payload[TS_LEN];
        struct fdb_blob blob;
        ts_payload(payload, TS_BASE + TS_COUNT + 1);
        if (fdb_tsl_append_with_ts(&g_tsdb,
                                   fdb_blob_make(&blob, payload, sizeof(payload)),
                                   TS_BASE + TS_COUNT + 1) != FDB_NO_ERR) {
            return 1;
        }
    }

    return 0;
}

static void ts_scenario(void)
{
    int total_ops;
    int cut;
    int failed_cuts = 0;

    sim_flash_init();
    if (fal_init() <= 0) {
        printf("TSDB: FAL init failed\n");
        g_failures++;
        return;
    }

    if (ts_boot() != 0) {
        printf("TSDB: init failed\n");
        g_failures++;
        return;
    }
    sim_flash_snapshot();

    if (ts_boot() != 0) {
        printf("TSDB: reboot failed\n");
        g_failures++;
        return;
    }
    sim_flash_clear_ops();
    if (ts_workload(TS_COUNT) != 0) {
        printf("TSDB: workload failed\n");
        g_failures++;
        return;
    }
    total_ops = sim_flash_op_count();

    printf("== TSDB: append %d records, 3 sectors ==\n", TS_COUNT);
    printf("baseline: %d flash ops\n\n", total_ops);

    for (cut = 1; cut <= total_ops; cut++) {
        int bad;

        sim_flash_restore();
        if (ts_boot() != 0) {
            failed_cuts++;
            continue;
        }

        sim_flash_set_cut(cut, &g_reboot);
        if (setjmp(g_reboot) == 0) {
            ts_workload(TS_COUNT);
            continue;
        }

        sim_flash_set_cut(0, NULL);

        if (ts_boot() != 0) {
            bad = 1;
        } else {
            bad = ts_verify();
        }

        if (bad) {
            failed_cuts++;
            printf("  cut %4d: %d problem(s)\n", cut, bad);
        }
    }

    printf("TSDB: %d cut points, %d failed\n\n", total_ops, failed_cuts);
    g_failures += failed_cuts;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 2 && strcmp(argv[1], "cut") == 0) {
        g_single_cut = atoi(argv[2]);
    }

    printf("FlashDB power-loss lab\n");
    printf("device: %d KiB, %d B sectors\n\n",
           (int)(SIM_FLASH_SIZE / 1024), (int)SIM_FLASH_SECTOR);

    kv_scenario();
    if (!g_single_cut) {
        ts_scenario();
    }

    printf("TOTAL: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
