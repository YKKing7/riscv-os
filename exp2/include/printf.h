/*
 * printf 函数头文件
 * 格式化输出、光标控制、颜色输出
 */

#ifndef PRINTF_H
#define PRINTF_H

#include "types.h"

/* ============== ANSI 颜色定义 ============== */

/* 前景色 (文字颜色) */
#define COLOR_BLACK         30
#define COLOR_RED           31
#define COLOR_GREEN         32
#define COLOR_YELLOW        33
#define COLOR_BLUE          34
#define COLOR_MAGENTA       35
#define COLOR_CYAN          36
#define COLOR_WHITE         37

/* 高亮前景色 */
#define COLOR_BRIGHT_BLACK  90
#define COLOR_BRIGHT_RED    91
#define COLOR_BRIGHT_GREEN  92
#define COLOR_BRIGHT_YELLOW 93
#define COLOR_BRIGHT_BLUE   94
#define COLOR_BRIGHT_MAGENTA 95
#define COLOR_BRIGHT_CYAN   96
#define COLOR_BRIGHT_WHITE  97

/* 背景色 */
#define BG_BLACK            40
#define BG_RED              41
#define BG_GREEN            42
#define BG_YELLOW           43
#define BG_BLUE             44
#define BG_MAGENTA          45
#define BG_CYAN             46
#define BG_WHITE            47

/* 特殊样式 */
#define STYLE_RESET         0
#define STYLE_BOLD          1
#define STYLE_DIM           2
#define STYLE_UNDERLINE     4
#define STYLE_BLINK         5
#define STYLE_REVERSE       7

/* ============== 函数声明 ============== */

/* 基本输出 */
int printf(const char *fmt, ...);

/* 屏幕控制 */
void clear_screen(void);
void clear_line(void);
void goto_xy(int x, int y);

/* 颜色输出 */
void set_color(int color);
void reset_color(void);
int printf_color(int color, const char *fmt, ...);

#endif
