#ifndef P2MALLOC_H
#define P2MALLOC_H

void *p2malloc(int size);
void  p2free(void *ptr);
int   p2allocated(void);
int   p2totmem(void);
void  p2reset(void);   /* reset all state; needed between tests since each was a fresh process in xv6 */

#endif /* P2MALLOC_H */
