/*
 * debug.h — 调试输出 + 断言
 */
#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h>

/* ===== 日志级别 ===== */
typedef enum {
    LOG_ERROR = 0,
    LOG_WARN  = 1,
    LOG_INFO  = 2,
    LOG_DEBUG = 3,
} log_level_t;

/* 初始化调试输出 */
void debug_init(void);

/* printf 重定向 (通过 SWO 或 Semihosting) */
void debug_printf(const char *fmt, ...);

/* 条件日志 */
void debug_log(log_level_t level, const char *fmt, ...);

/* 设置日志级别 */
void debug_set_level(log_level_t level);

/* ===== 断言 ===== */
#ifdef NDEBUG
#define ASSERT(expr) ((void)0)
#else
#define ASSERT(expr) do { \
    if (!(expr)) { \
        debug_printf("ASSERT FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        while(1) { __asm__ volatile("nop"); } \
    } \
} while(0)
#endif /* NDEBUG */

#endif /* DEBUG_H */
