#ifndef UTILSASSERTS_H
#define UTILSASSERTS_H

#ifdef QTR_PPU_ASSERTS
#include <cassert>
#define ASSERT1(condition) assert(condition)
#define ASSERT2(condition, message) assert(condition)
#define ASSERT_SELECTOR(_2, _1, func, ...) func
#define ASSERT(...) ASSERT_SELECTOR(__VA_ARGS__, ASSERT2, ASSERT1)(__VA_ARGS__)
#define ASSERT_NO_ENTRY() assert(false)
#define ASSERT_CODE(code) do code while(0)
#else
#define ASSERT(...) ((void)0)
#define ASSERT_NO_ENTRY() ((void)0)
#define ASSERT_CODE(code) ((void)0)
#endif

#endif // UTILSASSERTS_H
