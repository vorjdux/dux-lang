#include "duxrt.h"
#include <cxxabi.h>
#include <typeinfo>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern "C" {

/* ── Exception object ────────────────────────────────────────────────────── */

DuxException* duxrt_exception_new(const char* type, const char* msg) {
    DuxException* e = (DuxException*)malloc(sizeof(DuxException));
    if (!e) abort();
    e->type_name = type ? type : "Exception";
    e->message   = msg  ? strdup(msg) : NULL;
    e->line      = 0;
    e->file      = NULL;
    return e;
}

void duxrt_exception_free(DuxException* e) {
    if (!e) return;
    free(e->message);
    free(e);
}

/*
 * Throw a DuxException via the Itanium C++ ABI.
 *
 * The thrown "object" is a DuxException* stored inside the __cxa exception
 * buffer (sizeof(void*) bytes).  Catch-sides use `landingpad catch ptr null`
 * (catch-all), so type_info is stored for bookkeeping only — it is never
 * consulted for type-matching.
 *
 * After __cxa_begin_catch the catch codegen receives a void** pointing to the
 * stored DuxException*; it loads through it to get the actual pointer.
 */
void duxrt_throw(DuxException* e) {
    if (!e) {
        fputs("duxrt: throw(null)\n", stderr);
        abort();
    }
    void** storage = (void**)abi::__cxa_allocate_exception(sizeof(void*));
    *storage = e;
    // typeid(void*) is always available from libstdc++; it is only stored
    // in the exception header, never matched against (catch ptr null catches all).
    abi::__cxa_throw(storage,
                     const_cast<std::type_info*>(&typeid(void*)),
                     nullptr);
}

} // extern "C"
