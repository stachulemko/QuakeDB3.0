#ifndef CONFIG_H
#define CONFIG_H

/* Maximum number of columns per table */
#define MAX_COLUMNS          32
/* Maximum column name length (including null terminator) */
#define MAX_COL_NAME_LEN     64
/* Maximum string value length in a tuple cell (including null terminator) */
#define MAX_STR_LEN          128
/* Maximum number of tuples per 8 kb block */
#define MAX_TUPLES_PER_BLOCK 64
/* Maximum number of blocks per table */
#define MAX_BLOCKS           1024
/* Data block size in bytes */
#define BLOCK_SIZE           8192

#define BLOCK_FREE_SPACE  2000

#define BLOCK_USABLE_SIZE  (BLOCK_SIZE - BLOCK_FREE_SPACE)

#define DATA_TABLE_PATH "/home/stas/dev/QuakeDB3.0B/core/data"

#define INDEX_TABLE_PATH "/home/stas/dev/QuakeDB3.0B/core/indexes"

#ifndef VIEW_MODE
#define VIEW_MODE 1    // 1 - Repeatable Read , 2 - Read Committed
#endif

#define RESULT_SPACE 10

#define MAX_INDEX_BLOCKS 12  // number of block have to be divide by 3 wthout change

#define M 4

#define btreeFreeSpace 2000

#define btreeBufforSize 4

#define mvccBufforSize 10  // number of txn statuses in one MVCC buffer window

#define timeExceedTransaction 100 // in seconds
#endif
