#include "microbench.h"
#define FILE_SIZE (1LU*1024*1024*1024)
#define NB_FILES 20

char **pmem_maps;
char **dram_maps;
int *fds;

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

typedef struct {
    size_t id;
    size_t granularity;
    size_t nb_accesses;
    size_t ro;
} thread_t;

void *pmem_test(void *test) {
    
    thread_t* t = (thread_t*)(test);

   size_t id = t->id;
   size_t granularity = t->granularity;;
   pin_me_on(id);

   char *map = pmem_maps[id];

   init_seed();

   /* Allocate data to copy to the file */
   char *page_data = aligned_alloc(PAGE_SIZE, PAGE_SIZE);
   memset(page_data, 52, PAGE_SIZE);

   if(t->ro) {
      for(size_t i = 0; i < t->nb_accesses; i++) {
         uint64_t loc_rand = (lehmer64()/granularity*granularity) % (FILE_SIZE - granularity);
         memcpy(page_data, &map[loc_rand], granularity);
      }
   } else {
      for(size_t i = 0; i < t->nb_accesses; i++) {
         uint64_t loc_rand = (lehmer64()/granularity*granularity) % (FILE_SIZE - granularity);
         pmem_memcpy(&map[loc_rand], page_data, granularity, PMEM_F_MEM_NODRAIN|PMEM_F_MEM_NONTEMPORAL);
      }
       pmem_drain();
   }

   return NULL;
}
void *pmem_test_temp(void *test) {
    
    thread_t* t = (thread_t*)(test);

   size_t id = t->id;
   size_t granularity = t->granularity;;
   pin_me_on(id);

   char *map = pmem_maps[id];

   init_seed();

   /* Allocate data to copy to the file */
   char *page_data = aligned_alloc(PAGE_SIZE, PAGE_SIZE);
   memset(page_data, 52, PAGE_SIZE);

   if(t->ro) {
      for(size_t i = 0; i < t->nb_accesses; i++) {
         uint64_t loc_rand = (lehmer64()/granularity*granularity) % (FILE_SIZE - granularity);
         memcpy(page_data, &map[loc_rand], granularity);
      }
   } else {
      for(size_t i = 0; i < t->nb_accesses; i++) {
         uint64_t loc_rand = (lehmer64()/granularity*granularity) % (FILE_SIZE - granularity);
         pmem_memcpy(&map[loc_rand], page_data, granularity, PMEM_F_MEM_NODRAIN|PMEM_F_MEM_TEMPORAL);
      }
       pmem_drain();
   }

   return NULL;
}
void *dram_test(void *test) {
    thread_t* t = (thread_t*)(test);
   size_t id = t->id;
   size_t granularity = t->granularity;
   pin_me_on(id);

   char *map = dram_maps[id];

   init_seed();

   /* Allocate data to copy to the file */
   char *page_data = aligned_alloc(PAGE_SIZE, PAGE_SIZE);
   memset(page_data, 52, PAGE_SIZE);

   if(t->ro) {
      for(size_t i = 0; i < t->nb_accesses; i++) {
         uint64_t loc_rand = (lehmer64()/granularity*granularity) % (FILE_SIZE - granularity);
         memcpy(page_data, &map[loc_rand], granularity);
      }
   } else {
      for(size_t i = 0; i < t->nb_accesses; i++) {
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

void* launch1(void* z){
   declare_timer;
    size_t nthread = 10;
    pthread_t *threads = malloc(nthread * sizeof(*threads));

    size_t nb_accesses = 3000000;
    size_t granularity = 4096;
    if(granularity > 256)
        nb_accesses /= granularity/256;
    
    thread_t* tt = malloc(nthread*sizeof(thread_t));
    for(size_t i = 0; i < nthread; i++){
        tt[i] = (thread_t){
            .id = i,
            .granularity = granularity,
            .nb_accesses = nb_accesses,
            .ro = 0
        };
    }


    start_timer {
        for(size_t i = 0; i < nthread; i++)
            pthread_create(&threads[i], NULL, pmem_test_temp, (void*)(tt+i));
        for(size_t i = 0; i < nthread; i++)
            pthread_join(threads[i], NULL);
    } stop_timer("Launch1(PMEM): %ld memcpy %lu threads %lu granularity %s - %lu memcpy/s %lu MBs", nthread*nb_accesses, nthread, granularity, (tt[0].ro)?"Read":"Write", nthread*nb_accesses*1000000LU/elapsed, nthread*nb_accesses*granularity*1000000LU/elapsed/1024/1024);
    free(threads);
    free(tt);

    return NULL;
}
void* launch2(void* z){

   declare_timer;
    size_t nthread = 10;
    pthread_t *threads = malloc(nthread * sizeof(*threads));

    size_t nb_accesses = 3000000;
    size_t granularity = 4096;
    if(granularity > 256)
        nb_accesses /= granularity/256;
    
    thread_t* tt = malloc(nthread*sizeof(thread_t));

    for(size_t i = 0; i < nthread; i++){
        tt[i] = (thread_t) {
            .id = i,
            .granularity = granularity,
            .nb_accesses = nb_accesses,
            .ro = 0
        };
    }




    start_timer {
        for(size_t i = 0; i < nthread; i++)
            pthread_create(&threads[i], NULL, dram_test, (void*)(tt+i));
        for(size_t i = 0; i < nthread; i++)
            pthread_join(threads[i], NULL);
    } stop_timer("Launch2(DRAM): %ld memcpy %lu threads %lu granularity %s - %lu memcpy/s %lu MBs", nthread*nb_accesses, nthread, granularity, (tt[0].ro)?"Read":"Write", nthread*nb_accesses*1000000LU/elapsed, nthread*nb_accesses*granularity*1000000LU/elapsed/1024/1024);
    free(threads);
    free(tt);
    return NULL;
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


    pthread_t *rr = malloc(2 * sizeof(*rr));
    pthread_create(rr, NULL, launch1, NULL);
    pthread_create(rr+1, NULL, launch2, NULL);
    pthread_join(rr[0], NULL);
    pthread_join(rr[1], NULL);

   clean();
   return 0;
}

