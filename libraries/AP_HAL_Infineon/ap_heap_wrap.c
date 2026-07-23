#include <reent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"{
#endif

// void *__real__malloc_r(struct _reent *r, size_t size);
// void *__real__calloc_r(struct _reent *r, size_t n, size_t size);
// void __real__free_r(struct _reent *r, void *ptr);

// void *__wrap__malloc_r(struct _reent *r, size_t size)
// {
//     return __real__malloc_r(r, size);
// }

// void *__wrap__calloc_r(struct _reent *r, size_t n, size_t size)
// {
//     return __real__calloc_r(r, n, size);
// }

// void __wrap__free_r(struct _reent *r, void *ptr)
// {
//     __real__free_r(r, ptr);
// }

// void *calloc(size_t nmemb, size_t size)
// {
//     return NULL;//return pvPortCalloc(nmemb, size);
// }
// void *malloc(size_t size)
// {
//     return NULL;//return pvPortMalloc(size);
// }
// void free(void *ptr)
// {
//     //vPortFree(ptr);
// }
// caddr_t _sbrk(int incr)
// {
//     return (caddr_t)ENOMEM;
// }

#ifdef __cplusplus
}
#endif
