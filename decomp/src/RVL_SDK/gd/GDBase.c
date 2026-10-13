#include "revolution/gd/GDBase.h"

GDLObj* __GDCurrentDL = NULL;
static GDOverflowCallback overflowcb = NULL;

void GDInitGDLObj(GDLObj* pDL, void* pStart, u32 length) {
    pDL->start = pStart;
    pDL->ptr = (u8*)pStart;
    pDL->top = (u8*)pStart + length;
    pDL->length = length;
}

void GDFlushCurrToMem() {
    DCFlushRange(__GDCurrentDL->start, __GDCurrentDL->length);
}

void GDPadCurr32() {
    u32 n = ((u32)__GDCurrentDL->ptr & 31);
    if (n) {
        for (; n < 32; n++) {
            __GDWrite(0);
        }
    }
}

void GDOverflowed() {
#ifdef TARGET_PC
    {
        static int sReported = 0;
        if (sReported++ < 5) {
            OSReport("GD: display list overflow (start %p, length %u, at +%d)\n", (void*)__GDCurrentDL->start, (unsigned)__GDCurrentDL->length,
                     (int)(__GDCurrentDL->ptr - __GDCurrentDL->start));
        }
    }
#endif
    if (overflowcb) {
        (*overflowcb)();
    }
}
