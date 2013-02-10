#ifndef __DCLOAD_TYPES_H__
#define __DCLOAD_TYPES_H__

#include <stdint.h>

/* dcload dirent */

typedef struct {
  uint32_t            d_ino;  /* inode number */
  int32_t           d_off;  /* offset to the next dirent */
  uint16_t  d_reclen;/* length of this record */
  uint8_t   d_type;         /* type of file */
  char            d_name[256];    /* filename */
} dcload_dirent_t;

/* dcload stat */

typedef struct {
  uint16_t st_dev;
  uint16_t st_ino;
  int32_t st_mode;
  uint16_t st_nlink;
  uint16_t st_uid;
  uint16_t st_gid;
  uint16_t st_rdev;
  int32_t st_size;
  int32_t st_atime_priv;
  int32_t st_spare1;
  int32_t st_mtime_priv;
  int32_t st_spare2;
  int32_t st_ctime_priv;
  int32_t st_spare3;
  int32_t st_blksize;
  int32_t st_blocks;
  int32_t st_spare4[2];
} dcload_stat_t;

#endif

