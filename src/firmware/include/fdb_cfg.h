#ifndef _FDB_CFG_H_
#define _FDB_CFG_H_

#define FDB_USING_KVDB
#define FDB_USING_FAL_MODE

/* W25Q64 NOR flash supports 1-bit write granularity semantics. */
#define FDB_WRITE_GRAN 1

/* Keep FlashDB logs quiet on bare-metal unless explicitly enabled. */
#define FDB_PRINT(...) do { } while (0)

#endif /* _FDB_CFG_H_ */
