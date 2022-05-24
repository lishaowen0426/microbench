#include "microbench.h"
#define FILE_SIZE (1LU*1024*1024*1024)
#define NB_FILES 10

char **pmem_maps;
char **dram_maps;
int *fds;
size_t granularity;
size_t nb_accesses;
size_t ro;

static __thread __uint128_t g_lehmer64_state;

static void init_seed(void) {
   g_lehmer64_state = rand();
}

static uint64_t lehmer64() {
   g_lehmer64_state *= 0xda942042e4dd58b5;
   return g_lehmer64_state >> 64;
}

void pin_me_on(int core) {
   cpu_set_t cpuset;
   pthread_t thread = pthread_self();

   CPU_ZERO(&cpuset);
   CPU_SET(core, &cpuset);

   int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
   if (s != 0)
      printf("Cannot pin thread on core %d\n", core);
}

void *pmem_test(void *test) {
   size_t id = (size_t)test;
   pin_me_on(id);

   char *map = pmem_maps[id];

   init_seed();

   /* Allocate data to copy to the file */
   char *page_data = aligned_alloc(PAGE_SIZE, PAGE_SIZE);
   memset(page_data, 52, PAGE_SIZE);

   if(ro) {
      for(size_t i = 0; i < nb_accesses; i++) {
         uint64_t loc_rand = (lehmer64()/granularity*granularity) % (FILE_SIZE - granularity);
         memcpy(page_data, &map[loc_rand], granularity);
      }
   } else {
      for(size_t i = 0; i < nb_accesses; i++) {
         uint64_t loc_rand = (lehmer64()/granularity*granularity) % (FILE_SIZE - granularity);
         pmem_memcpy(&map[loc_rand], page_data, granularity, PMEM_F_MEM_NODRAIN|PMEM_F_MEM_NONTEMPORAL);
      }
       pmem_drain();
   }

   return NULL;
}
void *dram_test(void *test) {
   size_t id = (size_t)test;
   pin_me_on(id);

   char *map = dram_maps[id];

   init_seed();

   /* Allocate data to copy to the file */
   char *page_data = aligned_alloc(PAGE_SIZE, PAGE_SIZE);
   memset(page_data, 52, PAGE_SIZE);

   if(ro) {
      for(size_t i = 0; i < nb_accesses; i++) {
         uint64_t loc_rand = (lehmer64()/granularity*granularity) % (FILE_SIZE - granularity);
         memcpy(page_data, &map[loc_rand], granularity);
      }
   } else {
      for(size_t i = 0; i < nb_accesses; i++) {
         uint64_t loc_rand = (lehmer64()/granularity*granularity) % (FILE_SIZE - granularity);
         memcpy(&map[loc_rand], page_data, granularity);
      }
   }

   return NULL;
}

void *create_files(void *data) {
   char *path;
   int id = (size_t)data;

   asprintf(&path, "/pmem0/test%d", id);

   int fd = open(path,  O_RDWR | O_CREAT | O_DIRECT, 0777);
   if(fd == -1)
      die("Cannot open %s\n", path);
   fallocate(fd, 0, 0, FILE_SIZE);
   fds[id] = fd;

   char *map = pmem_map_file(path, 0, 0, 0777, NULL, NULL);
   if(!pmem_is_pmem(map, FILE_SIZE))
      die("File %s is not in pmem?!", path);
  if(map == NULL){
    die("pmem_mmap failed");
  }
   memset(map, 0, FILE_SIZE);

   pmem_maps[id] = map;

   // create dram map
    
   map = mmap(NULL, FILE_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);
   if(map == MAP_FAILED){
    die("mmap failed");
   }
   dram_maps[id] = map;
   return NULL;
}

void clean(){
    
   char *path;
    for(int i = 0; i < NB_FILES; i++){
        asprintf(&path, "/pmem0/test%d", i);
        pmem_unmap(pmem_maps[i], FILE_SIZE);
        close(fds[i]);
        remove(path);
        munmap(dram_maps[i], FILE_SIZE);
    }

    free(path);

}

int main(int argc, char **argv) {
   pmem_maps = malloc(NB_FILES*sizeof(*pmem_maps));
   dram_maps = malloc(NB_FILES*sizeof(*dram_maps));
   fds = malloc(NB_FILES*sizeof(*fds));

   /* Prepare files */
   pthread_t *ithreads = malloc(NB_FILES * sizeof(*ithreads));
   for(size_t i = 0; i < NB_FILES; i++)
      pthread_create(&ithreads[i], NULL, create_files, (void*)i);
   for(size_t i = 0; i < NB_FILES; i++)
      pthread_join(ithreads[i], NULL);
    free(ithreads);
   /* Bench latency */
   declare_timer;
   size_t ros[] = { 0, 1 };
   //size_t granularities[] = { 8, 64, 256, 512, 1024, 4096 };
   size_t granularities[] = { 256, 4096 };
   //size_t nthread, nthreads[] = { 1, 2, 4, 6, 8, 10, 12, 14, 20 };
   size_t nthread, nthreads[] = { 10 };
   foreach(ro, ros) {
      foreach(granularity, granularities) {
         foreach(nthread, nthreads) {
            pthread_t *threads = malloc(nthread * sizeof(*threads));
            start_timer {
               nb_accesses = 3000000;
               if(granularity > 256)
                  nb_accesses /= granularity/256;
               for(size_t i = 0; i < nthread; i++)
                  pthread_create(&threads[i], NULL, pmem_test, (void*)i);
               for(size_t i = 0; i < nthread; i++)
                  pthread_join(threads[i], NULL);
            } stop_timer("Total(PMEM): %ld memcpy %lu threads %lu granularity %lu readonly - %lu memcpy/s %lu MBs", nthread*nb_accesses, nthread, granularity, ro, nthread*nb_accesses*1000000LU/elapsed, nthread*nb_accesses*granularity*1000000LU/elapsed/1024/1024);


            start_timer {
               nb_accesses = 3000000;
               if(granularity > 256)
                  nb_accesses /= granularity/256;
               for(size_t i = 0; i < nthread; i++)
                  pthread_create(&threads[i], NULL, dram_test, (void*)i);
               for(size_t i = 0; i < nthread; i++)
                  pthread_join(threads[i], NULL);
            } stop_timer("Total(dram): %ld memcpy %lu threads %lu granularity %lu readonly - %lu memcpy/s %lu MBs", nthread*nb_accesses, nthread, granularity, ro, nthread*nb_accesses*1000000LU/elapsed, nthread*nb_accesses*granularity*1000000LU/elapsed/1024/1024);

            free(threads);
         }
      }
   }

   clean();
   return 0;
}

