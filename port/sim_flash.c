#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fal.h>
#include "sim_flash.h"

/* FAL log sink, selected with -DFAL_PRINTF=sim_noop_printf. */
int sim_noop_printf(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}

static uint8_t g_flash[SIM_FLASH_SIZE];
static uint8_t g_pristine[SIM_FLASH_SIZE];

static int g_ops;
static int g_cut_at;
static int g_cut_fired;
static jmp_buf *g_cut_env;
static int g_cut_kind;
static long g_cut_off;
static size_t g_cut_size;

void sim_flash_init(void)
{
    memset(g_flash, 0xff, sizeof(g_flash));
    memset(g_pristine, 0xff, sizeof(g_pristine));
    g_ops = 0;
    g_cut_at = 0;
    g_cut_fired = 0;
    g_cut_env = NULL;
}

void sim_flash_snapshot(void)
{
    memcpy(g_pristine, g_flash, sizeof(g_flash));
}

void sim_flash_restore(void)
{
    memcpy(g_flash, g_pristine, sizeof(g_flash));
    g_ops = 0;
    g_cut_fired = 0;
    g_cut_at = 0;
    g_cut_env = NULL;
}

void sim_flash_set_cut(int op, jmp_buf *env)
{
    g_cut_at = op;
    g_cut_env = env;
    g_ops = 0;
    g_cut_fired = 0;
}

int sim_flash_op_count(void)
{
    return g_ops;
}

void sim_flash_clear_ops(void)
{
    g_ops = 0;
}

int sim_flash_cut_fired(void)
{
    return g_cut_fired;
}

int sim_flash_cut_kind(void)
{
    return g_cut_kind;
}

long sim_flash_cut_off(void)
{
    return g_cut_off;
}

size_t sim_flash_cut_size(void)
{
    return g_cut_size;
}

const uint8_t *sim_flash_mem(void)
{
    return g_flash;
}

/* ------------------------------------------------------------------------- */
/* FAL device ops                                                            */
/* ------------------------------------------------------------------------- */

static int sim_init(void)
{
    return 0;
}

static int sim_read(long offset, uint8_t *buf, size_t size)
{
    if (offset < 0 || (size_t)offset > SIM_FLASH_SIZE ||
        size > SIM_FLASH_SIZE - (size_t)offset) {
        return -1;
    }

    memcpy(buf, &g_flash[offset], size);
    return 0;
}

static int sim_write(long offset, const uint8_t *buf, size_t size)
{
    if (offset < 0 || (size_t)offset > SIM_FLASH_SIZE ||
        size > SIM_FLASH_SIZE - (size_t)offset) {
        return -1;
    }

    g_ops++;
    if (g_cut_at > 0 && g_ops >= g_cut_at) {
        g_cut_fired = 1;
        longjmp(*g_cut_env, 1);
    }

    /* NOR semantics: a write can only clear bits, never set them. */
    {
        size_t i;
        for (i = 0; i < size; i++) {
            g_flash[offset + i] &= buf[i];
        }
    }
    return 0;
}

static int sim_erase(long offset, size_t size)
{
    if (offset < 0 || (size_t)offset > SIM_FLASH_SIZE ||
        size > SIM_FLASH_SIZE - (size_t)offset) {
        return -1;
    }

    g_ops++;
    if (g_cut_at > 0 && g_ops >= g_cut_at) {
        g_cut_fired = 1;
        g_cut_kind = 1;
        g_cut_off = offset;
        g_cut_size = size;
        longjmp(*g_cut_env, 1);
    }

    memset(&g_flash[offset], 0xff, size);
    return 0;
}

struct fal_flash_dev sim_flash0 =
{
    .name       = "simflash",
    .addr       = 0,
    .len        = SIM_FLASH_SIZE,
    .blk_size   = SIM_FLASH_SECTOR,
    .ops        = { sim_init, sim_read, sim_write, sim_erase },
    .write_gran = 1,
};
