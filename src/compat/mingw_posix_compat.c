/* third_party/mingw-prefix 의 libzmq 는 win32 스레드 GCC 로 만들어져
 * __gthr_win32_* 를 부른다. WinLibs POSIX GCC 에는 그 심볼이 없다.
 * libmbedcrypto 는 옛 UCRT 의 vsnprintf_s 를 부른다. GCC 16 은 그 이름을 안 준다.
 * 이 파일은 POSIX 스레드 MinGW 에서만 링크한다. win32 스레드 GCC 는 이미 갖고 있다.
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

struct tr_abs_time {
    long long sec;
    long nsec;
};

static long long
tr_now_ms(void)
{
    FILETIME ft;
    ULARGE_INTEGER u;
    GetSystemTimeAsFileTime(&ft);
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (long long)(u.QuadPart / 10000ULL) - 11644473600000LL;
}

void
__gthr_win32_mutex_init_function(CRITICAL_SECTION *mutex)
{
    InitializeCriticalSection(mutex);
}

void
__gthr_win32_mutex_destroy(CRITICAL_SECTION *mutex)
{
    DeleteCriticalSection(mutex);
}

int
__gthr_win32_mutex_lock(CRITICAL_SECTION *mutex)
{
    EnterCriticalSection(mutex);
    return 0;
}

int
__gthr_win32_mutex_unlock(CRITICAL_SECTION *mutex)
{
    LeaveCriticalSection(mutex);
    return 0;
}

int
__gthr_win32_cond_timedwait(CONDITION_VARIABLE *cond, CRITICAL_SECTION *mutex,
                            const struct tr_abs_time *abs_timeout)
{
    DWORD ms = INFINITE;
    if (abs_timeout) {
        long long delta = abs_timeout->sec * 1000 + abs_timeout->nsec / 1000000 - tr_now_ms();
        if (delta <= 0) return ETIMEDOUT;
        if (delta > 0xffffffffLL) delta = 0xffffffffLL;
        ms = (DWORD)delta;
    }
    if (!SleepConditionVariableCS(cond, mutex, ms))
        return GetLastError() == ERROR_TIMEOUT ? ETIMEDOUT : EINVAL;
    return 0;
}

int
vsnprintf_s(char *buffer, size_t size_of_buffer, size_t count,
            const char *format, va_list argptr)
{
    size_t n;
    if (size_of_buffer == 0) return -1;
    /* _TRUNCATE */
    n = count == (size_t)-1 ? size_of_buffer : count + 1;
    if (n > size_of_buffer) n = size_of_buffer;
    return vsnprintf(buffer, n, format, argptr);
}
