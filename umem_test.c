#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "./sys/vmem_impl_user.h"
#include "vmem_base.h"
#include "umem.h"

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

int main(int argc, char*argv[])
{
	char*p;
	umem_cache_t* cache;
	cache = umem_cache_create("mrdotKcache", 1000, 0, NULL, NULL, NULL, NULL, NULL,0);	     if (!cache){
		printf("cache create failed\n");
		return 0;
	}
	p = umem_cache_alloc(cache, 0);
	if (!p){
		printf("cache alloc failed\n");
		return 0;
	}
	memset(p, 0x1, 1000);
	printf("success cache:%p alloc:%p quantum:%ld\n",cache,p,cache->cache_arena->vm_quantum);
	return 0;
}



