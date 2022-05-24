#ifndef MICROBENCH_H
#define MICROBENCH_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>
#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>
#include <numa.h>
#include <libpmem.h>
#include <emmintrin.h>

#define PAGE_SIZE (4LU*1024LU)

/*
 * Cute timer macros
 * Usage:
 * declare_timer;
 * start_timer {
 *   ...
 /1024/1024/1024* } stop_timer("Took %lu us", elapsed);
 */
#define declare_timer uint64_t elapsed; \
   struct timeval st, et;

#define start_timer gettimeofday(&st,NULL);

#define stop_timer(msg, args...) ;do { \
	   gettimeofday(&et,NULL); \
	   elapsed = ((et.tv_sec - st.tv_sec) * 1000000) + (et.tv_usec - st.tv_usec) + 1; \
	   printf("(%s,%d) [%6lums] " msg "\n", __FUNCTION__ , __LINE__, elapsed/1000, ##args); \
} while(0)

#define die(msg, args...) \
   do {                         \
      fprintf(stderr,"(%s,%d) " msg "\n", __FUNCTION__ , __LINE__, ##args); \
      fflush(stdout); \
      exit(-1); \
   } while(0)
#endif


/*
 * Foreach macro
 */
#define foreach(item, array) \
    for(int keep = 1, \
            count = 0,\
            size = sizeof (array) / sizeof *(array); \
        keep && count != size; \
        keep = !keep, count++) \
      for(item = *((array) + count); keep; keep = !keep)

/*
 * RDTSCLL
 */
#ifdef __x86_64__
#define rdtscll(val) {                                           \
       _mm_mfence(); \
       unsigned int __a,__d;                                        \
       asm volatile("rdtsc" : "=a" (__a), "=d" (__d));              \
       (val) = ((unsigned long)__a) | (((unsigned long)__d)<<32);   \
}
#else
#error "Compiling in 32b??"
#endif

