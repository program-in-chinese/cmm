// simple_string.c

#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include "std_port/std_port.h"
#include "std_template/simple_string.h"

namespace simple
{

// Create string
size_t string::snprintf(const char *fmt, size_t n, ...)
{
    if (n == 0)
        return 0;

    if (n > 1024)
        // Limit size to 1K
        n = 1024;

    char *buf = (char *)STD_ALLOCA(n);
    if (!buf)
        return 0;

    va_list va;
    va_start(va, n);
    STD_VSNPRINTF(buf, n, fmt, va);
    va_end(va);

    buf[n - 1] = 0;
    *this = buf;
    return length();
}

// BKDR Hash Function
string::string_hash_t string::hash_string(const char_t *c_str)
{
    string_hash_t seed = 131; // 31 131 1313 13131 131313 etc..
    string_hash_t hash = 0;
    int maxn = 64;

    while (*c_str && maxn--)
        hash = hash * seed + (*c_str++);

    return (hash & 0x7FFFFFFF);
}

} // End of namespace: simple
