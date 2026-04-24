#include "string.h"

int kstrcmp(const char* a, const char* b)
{
    while (*a && *b) {
        if (*a != *b)
            return *a - *b;
        a++;
        b++;
    }
    return *a - *b;
}