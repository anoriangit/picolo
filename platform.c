
// general picolo platform support code

#include <malloc.h>

size_t P_GetTotalHeap(void) {
   extern char __StackLimit, __bss_end__;
   return &__StackLimit  - &__bss_end__;
}

size_t P_GetFreeHeap(void) {
   struct mallinfo m = mallinfo();
   return P_GetTotalHeap() - m.uordblks;
}


// platform.c