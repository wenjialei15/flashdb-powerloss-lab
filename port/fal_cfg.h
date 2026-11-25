/*
 * FAL configuration for the PC-side simulation.
 *
 * One 64 KiB NOR-like device with 4 KiB sectors. Partitions are declared as a
 * static table, exactly like on a real board.
 */

#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

/* Silence FAL info/error output before its header picks a default. */
#define FAL_PRINTF(...) do { } while (0)

#include <fal_def.h>

#define FAL_PART_HAS_TABLE_CFG

extern struct fal_flash_dev sim_flash0;

#define FAL_FLASH_DEV_TABLE       \
{                                 \
    &sim_flash0,                  \
}

#define FAL_PART_TABLE                                                          \
{                                                                               \
    {FAL_PART_MAGIC_WORD, "kvdb", "simflash",     0, 24 * 1024, 0},            \
    {FAL_PART_MAGIC_WORD, "tsdb", "simflash", 24 * 1024, 12 * 1024, 0},        \
}

#endif /* _FAL_CFG_H_ */
