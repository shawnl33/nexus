/* libmbedcrypto 가 부르는 vsnprintf_s.
 * GCC 16 의 stdio.h 는 이 함수를 static inline 으로만 두고 외부 심볼은 안 만든다.
 * 여기서 stdio.h 를 넣으면 그 인라인과 정의가 겹친다.
 */
#include <stdarg.h>
#include <stddef.h>

int vsnprintf(char *s, size_t n, const char *fmt, va_list ap);

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
