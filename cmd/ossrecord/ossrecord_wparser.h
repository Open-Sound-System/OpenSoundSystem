#ifndef _OSSRECORD_WPARSER_H
#define _OSSRECORD_WPARSER_H

int format2obits (int);
int write_head (void);

#ifdef OSS_BIG_ENDIAN
#define BE_INT(x) x
#define BE_SH(x) x
#define LE_INT(x) bswap(x)
#define LE_SH(x) bswaps(x)
#else
#define BE_INT(x) bswap(x)
#define BE_SH(x) bswaps(x)
#define LE_INT(x) x
#define LE_SH(x) x
#endif /* OSS_BIG_ENDIAN */

#endif
