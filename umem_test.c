#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "./sys/vmem_impl_user.h"
#include "vmem_base.h"
#include "umem.h"
#include <sys/mman.h>

//#define UMEM_STANDALONE 1
#include "umem_impl.h"

int main2(int argc, char *argv[])
{
  char *foo;

  umem_startup(NULL, 0, 0, NULL, NULL);

  foo = umem_alloc(32, UMEM_DEFAULT);

  strcpy(foo, "hello there");

  printf("Hello %s\n", foo);

  umem_free(foo, 32);

  return EXIT_SUCCESS;
}
extern vmem_t *vmem_heap;
unsigned int g_num;
unsigned int g_size;
static unsigned long get_align(unsigned long addr, unsigned long align){
	unsigned long mask;
	mask = align - 1;
	return ((addr + mask) & (~mask));
}

void memory_tight_test(unsigned int num, unsigned int size){
    char** buf;
    int i;
	umem_cache_t* cache;
    unsigned long len = get_align(num*sizeof(void*), 4096);

    buf = (char**)mmap(NULL, len, PROT_READ | PROT_WRITE , MAP_PRIVATE | MAP_ANONYMOUS ,-1, 0);
    if (MAP_FAILED == buf){
        return;
    }
    memset(buf, 0, len);
    cache = umem_cache_create("mrdotKcache", size, 0, NULL, NULL, NULL, NULL, NULL,0);	
    if (!cache){
		printf("cache create failed\n");
        return;
    }
    printf("success num:%u size:%u cache:%p quantum:%ld\n",
        num, size, cache,cache->cache_arena->vm_quantum);

    for (i=0; i<num; i++){
	    buf[i] = umem_cache_alloc(cache, 0);
    	if (i +1 == num){
            printf("umem_cache_alloc %d %p\n", i, buf[i]);
    	}
        memset(buf[i], 0x2, size);
    }
    sleep(10);
    printf("sleep 10s then free all memory\n");
    for (i=0; i<num; i++){
	    umem_cache_free(cache, buf[i]);
    	if (i +1 == num){
            printf("umem_cache_free %d %p\n", i, buf[i]);
    	}
    }

    /*show meminfo*/
    vmem_memory_stats();

    while(1){
        if(vmem_heap->vm_kstat.vk_mem_inuse > num*size){
            printf("Start reclaiming memory.\n");
            umem_trig_reapmem_start();
            sleep(1);
        }else{
            printf("Stop reclaiming memory.\n");
            umem_trig_reapmem_stop();
            break;
        }
    }
    /*show meminfo*/
    vmem_memory_stats();
    munmap(buf, len);
	return;
}
int main(int argc, char*argv[])
{
	char*p;

    if (argc != 3){
        printf("useage: ./umem_test num size\n");
        return 0;
    }
    g_num = atoi(argv[1]);
    g_size = atoi(argv[2]);

    memory_tight_test(g_num, g_size);
    return 0;
}



