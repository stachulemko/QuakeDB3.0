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

#define FREE_SPACE  2000

#define BLOCK_USABLE_SIZE  BLOCK_SIZE - FREE_SPACE  

#endif
