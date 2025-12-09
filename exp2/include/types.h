/*
 * 基本数据类型定义
 * 适用于 RISC-V 64 位架构
 */

#ifndef TYPES_H
#define TYPES_H

/* 无符号整数类型 */
typedef unsigned char       uint8;
typedef unsigned short      uint16;
typedef unsigned int        uint32;
typedef unsigned long       uint64;

/* 有符号整数类型 */
typedef signed char         int8;
typedef signed short        int16;
typedef signed int          int32;
typedef signed long         int64;

/* 简写类型 */
typedef unsigned int        uint;
typedef unsigned short      ushort;
typedef unsigned char       uchar;

/* 指针大小类型 */
typedef uint64              uintptr;
typedef int64               intptr;
typedef uint64              size_t;

/* 空指针 */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* 布尔常量 */
#define TRUE  1
#define FALSE 0

#endif
