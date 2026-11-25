/*
 * Project configuration for FlashDB on the PC simulator.
 */

#ifndef _FDB_CFG_H_
#define _FDB_CFG_H_

#define FDB_USING_KVDB
#define FDB_USING_TSDB

/* No file mode: all I/O goes through FAL, so every erase/write is countable. */
#define FDB_USING_FAL_MODE

#define FDB_WRITE_GRAN          1

/* The lab prints its own table; database logs would drown it. */
#define FDB_PRINT(...)          do { } while (0)

#endif /* _FDB_CFG_H_ */
