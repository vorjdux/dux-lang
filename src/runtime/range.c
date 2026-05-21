#include "duxrt.h"

DuxRange duxrt_range_excl(int64_t start, int64_t end) {
    DuxRange r = { start, end, 1, 0 };
    return r;
}

DuxRange duxrt_range_incl(int64_t start, int64_t end) {
    DuxRange r = { start, end, 1, 1 };
    return r;
}

int64_t duxrt_range_len(DuxRange r) {
    int64_t n = r.end - r.start;
    if (r.inclusive) n++;
    return n < 0 ? 0 : n;
}
