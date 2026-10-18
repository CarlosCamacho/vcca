#include <stdlib.h>
void* __cxa_allocate_exception(unsigned long n){ (void)n; abort(); }
void __cxa_throw(void*a,void*b,void(*c)(void*)){ (void)a;(void)b;(void)c; abort(); }
