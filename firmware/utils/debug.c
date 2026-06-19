/*
 * debug.c — 调试输出实现
 *
 * Phase 0 使用 Semihosting (通过 SWD 输出到调试器)
 * 后续 Phase 可选配 UART printf。
 */
#include "debug.h"
#include <stdarg.h>

/* Semihosting 调用号 */
#define SYS_WRITE0  0x04

/* Semihosting: 通过 BKPT 指令发送字符串到调试器 */
static void semihost_write0(const char *s) {
    __asm__ volatile(
        "mov r0, #0x04\n"   /* SYS_WRITE0 */
        "mov r1, %[str]\n"
        "bkpt #0xAB\n"
        :
        : [str] "r"(s)
        : "r0", "r1"
    );
}

static log_level_t g_log_level = LOG_INFO;

void debug_init(void) {
    /* Phase 0: 无需特殊初始化, Semihosting 由调试器处理 */
}

void debug_printf(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    
    /* 简单 vsnprintf 模拟 (嵌入式中常用循环格式化) */
    char *p = buf;
    const char *f = fmt;
    while (*f) {
        if (*f == '%' && *(f+1) == 's') {
            const char *s = va_arg(args, const char *);
            while (*s) *p++ = *s++;
            f += 2;
        } else if (*f == '%' && *(f+1) == 'd') {
            int d = va_arg(args, int);
            if (d < 0) { *p++ = '-'; d = -d; }
            char tmp[12];
            int i = 0;
            do { tmp[i++] = '0' + (d % 10); d /= 10; } while (d);
            while (i) *p++ = tmp[--i];
            f += 2;
        } else if (*f == '%' && (*(f+1) == 'x' || *(f+1) == 'X')) {
            unsigned int x = va_arg(args, unsigned int);
            char hex[] = "0123456789ABCDEF";
            char tmp[12];
            int i = 0;
            do { tmp[i++] = hex[x & 0xF]; x >>= 4; } while (x);
            *p++ = '0'; *p++ = 'x';
            while (i) *p++ = tmp[--i];
            f += 2;
        } else if (*f == '%' && *(f+1) == 'l' && *(f+2) == 'u') {
            unsigned long lu = va_arg(args, unsigned long);
            char tmp[12];
            int i = 0;
            do { tmp[i++] = '0' + (lu % 10); lu /= 10; } while (lu);
            while (i) *p++ = tmp[--i];
            f += 3;
        } else if (*f == '\\' && *(f+1) == 'n') {
            *p++ = '\n';
            f += 2;
        } else {
            *p++ = *f++;
        }
    }
    *p = '\0';
    va_end(args);
    
    semihost_write0(buf);
}

void debug_log(log_level_t level, const char *fmt, ...) {
    if (level > g_log_level) return;
    
    static const char *prefix[] = {"[ERR] ", "[WRN] ", "[INF] ", "[DBG] "};
    semihost_write0(prefix[level]);
    
    char buf[256];
    va_list args;
    va_start(args, fmt);
    /* Same format loop as debug_printf, simplified for log prefix */
    char *p = buf;
    const char *f = fmt;
    while (*f) {
        if (*f == '%' && *(f+1) == 's') {
            const char *s = va_arg(args, const char*);
            while (*s) *p++ = *s++;
            f += 2;
        } else if (*f == '%' && *(f+1) == 'd') {
            int d = va_arg(args, int);
            if (d < 0) { *p++ = '-'; d = -d; }
            char tmp[12]; int i = 0;
            do { tmp[i++] = '0'+(d%10); d/=10; } while(d);
            while(i) *p++ = tmp[--i];
            f += 2;
        } else if (*f == '\\' && *(f+1) == 'n') {
            *p++ = '\n'; f += 2;
        } else {
            *p++ = *f++;
        }
    }
    *p = '\0';
    va_end(args);
    
    semihost_write0(buf);
}

void debug_set_level(log_level_t level) {
    g_log_level = level;
}
