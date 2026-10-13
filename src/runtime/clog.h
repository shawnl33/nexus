#ifndef TR_CLOG_H
#define TR_CLOG_H

/* 콘솔 한 줄: "HH:MM:SS  태그  내용". 상태 줄만 맞추고 오류 문장은 그대로 둔다. */

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static inline int tr_clog_format(char *buf, size_t cap, int hour, int min, int sec,
                                 const char *tag, const char *msg) {
    if (buf == 0 || cap == 0) {
        return -1;
    }
    return snprintf(buf, cap, "%02d:%02d:%02d  %s  %s\n", hour, min, sec, tag != 0 ? tag : "",
                    msg != 0 ? msg : "");
}

static inline void tr_clog(const char *tag, const char *fmt, ...) {
    char msg[480];
    va_list ap;
    va_start(ap, fmt);
    if (fmt == 0) {
        msg[0] = '\0';
    } else {
        vsnprintf(msg, sizeof(msg), fmt, ap);
    }
    va_end(ap);
    time_t now = time(0);
    struct tm snapshot;
    struct tm *tm = localtime(&now);
    if (tm != 0) {
        snapshot = *tm;
        tm = &snapshot;
    }
    char line[560];
    tr_clog_format(line, sizeof(line), tm != 0 ? tm->tm_hour : 0, tm != 0 ? tm->tm_min : 0,
                   tm != 0 ? tm->tm_sec : 0, tag, msg);
    fputs(line, stdout);
    fflush(stdout);
}

#endif
