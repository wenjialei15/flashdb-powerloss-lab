/*
 * 64 KiB NOR-like flash device for the PC build.
 *
 * Every erase/write goes through here, which gives two things the real
 * hardware cannot offer: an exact operation counter, and the ability to
 * "cut power" just before operation N without performing it.
 */

#ifndef SIM_FLASH_H
#define SIM_FLASH_H

#include <setjmp.h>
#include <stdint.h>

#define SIM_FLASH_SIZE      (64u * 1024u)
#define SIM_FLASH_SECTOR    4096u

void sim_flash_init(void);

/* Snapshot / restore the whole device, for replaying one workload many times. */
void sim_flash_snapshot(void);
void sim_flash_restore(void);

/*
 * Arm a power cut before the next `op`-th erase/write. `op` <= 0 disables it.
 * The longjmp target must stay alive until the operation completes.
 */
void sim_flash_set_cut(int op, jmp_buf *env);

int  sim_flash_op_count(void);
void sim_flash_clear_ops(void);
int  sim_flash_cut_fired(void);
int  sim_flash_cut_kind(void);
long sim_flash_cut_off(void);
size_t sim_flash_cut_size(void);

const uint8_t *sim_flash_mem(void);

#endif /* SIM_FLASH_H */
