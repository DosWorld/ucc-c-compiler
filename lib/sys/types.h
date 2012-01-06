#ifndef __SYS_TYPES_H
#define __SYS_TYPES_H

#ifdef __x86_64__
#  define __WORDSIZE    64
#else
#  define __WORDSIZE    32
#endif

typedef signed   char   int8_t;

typedef unsigned char  uint8_t;
//typedef signed   short  int16_t;
//typedef unsigned short uint16_t;

typedef signed   int    int32_t;
typedef unsigned int   uint32_t;

#ifdef __x86_64__
//typedef signed   long   int64_t;
//typedef unsigned long  uint64_t;
#endif


typedef unsigned int  size_t;
typedef          int ssize_t;

#endif
