/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Portions Copyright 2006-2008 Message Systems, Inc. All rights reserved.
 */

/* #pragma ident	"@(#)vmem_mmap.c	1.2	05/06/08 SMI" */

#include "config.h"
#include <errno.h>

#if HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#ifdef HAVE_SYS_SYSMACROS_H
#include <sys/sysmacros.h>
#endif

#include <unistd.h>

#include "vmem_base.h"

#define	ALLOC_PROT	PROT_READ | PROT_WRITE | PROT_EXEC
#define	FREE_PROT	PROT_NONE

#define	ALLOC_FLAGS	MAP_PRIVATE | MAP_ANONYMOUS
#define	FREE_FLAGS	MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE

#ifdef MAP_ALIGN
#define	CHUNKSIZE	(64*1024)	/* 64 kilobytes */
#else
#define VMEM_MMAP_ENLARGE 1024
static size_t CHUNKSIZE;
static size_t vmem_mmap_heap_align_size = 128*1024;
size_t vmem_mmap_get_heap_size(){
	return vmem_mmap_heap_align_size;
}
#endif

static vmem_t *mmap_heap;
/*delete mmap to reduce mmap times*/
static void *
vmem_mmap_alloc(vmem_t *src, size_t size, int vmflags)
{
	void *ret;
	int old_errno = errno;

	ret = vmem_alloc(src, size, vmflags);
#ifndef _WIN32
	if (ret != NULL &&
	    mmap(ret, size, ALLOC_PROT, ALLOC_FLAGS | MAP_FIXED, -1, 0) ==
	    MAP_FAILED) {
		vmem_free(src, ret, size);
		vmem_reap();

		ASSERT((vmflags & VM_NOSLEEP) == VM_NOSLEEP);
		errno = old_errno;
		return (NULL);
	}
#endif

	errno = old_errno;
	return (ret);
}

static void
vmem_mmap_free(vmem_t *src, void *addr, size_t size)
{
	int old_errno = errno;
#ifdef _WIN32
	VirtualFree(addr, size, MEM_RELEASE);
#else
	(void) mmap(addr, size, FREE_PROT, FREE_FLAGS | MAP_FIXED, -1, 0);
#endif
	vmem_free(src, addr, size);
	vmem_reap();
	errno = old_errno;
}
#if !defined(MAP_ALIGN) && !defined(_WIN32)
#define vmem_mmap_align_ok(addr, align) (((unsigned long)(addr)) & ((unsigned long)(align) -1))
static unsigned long vmem_mmap_get_align(unsigned long addr, unsigned long align){
	unsigned long mask;
	mask = align - 1;
	return ((addr + mask) & (~mask));
}

static void* vmem_mmap_alloc_align_span(unsigned long size, unsigned long align){
	unsigned long bufsize = size + align;
	unsigned long start,end,start_align,end_align;
	void* buf;
	
	ASSERT(0 == size%align);
	buf = mmap(0, bufsize, FREE_PROT, FREE_FLAGS, -1, 0);
	if (MAP_FAILED == buf){
		vmem_reap();
		return NULL;
	}
	
	start = (unsigned long)buf;
	end = start + bufsize;
	
	start_align = vmem_mmap_get_align(start, align);
	if (start_align > start){
		munmap(buf, start_align - start);
	}
	end_align = start_align + size;
	if (end_align < end){
		munmap((void*)end_align, end-end_align);
	}
	
	ASSERT(0 == vmem_mmap_align_ok(start_align, vmem_mmap_get_heap_size()));
	return (void*)start_align;
}
#endif
static void *
vmem_mmap_top_alloc(vmem_t *src, size_t size, int vmflags)
{
	void *ret;
	void *buf;
	int old_errno = errno;
	size_t bufsize; 
	ret = vmem_alloc(src, size, VM_NOSLEEP);

	if (ret) {
		errno = old_errno;
		return (ret);
	}
	/*
	 * Need to grow the heap
	 */
#ifdef _WIN32
	buf = VirtualAlloc(NULL, size, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
	if (buf == NULL) buf = MAP_FAILED;
#elif defined(MAP_ALIGN)
	buf = mmap((void*)CHUNKSIZE, size, FREE_PROT, FREE_FLAGS | MAP_ALIGN,
			-1, 0);
#else
	bufsize = P2ROUNDUP(size, CHUNKSIZE);
	buf = vmem_mmap_alloc_align_span(bufsize, vmem_mmap_get_heap_size());
#endif

	if (buf != MAP_FAILED) {
		#if !defined(MAP_ALIGN) && !defined(_WIN32)
		ret = _vmem_extend_alloc(src, buf, bufsize, size, vmflags);
		#else
		ret = _vmem_extend_alloc(src, buf, size, size, vmflags);
		#endif
		if (ret != NULL)
			return (ret);
		else {
			(void) munmap(buf, size);
			errno = old_errno;
			return (NULL);
		}
	} else {
		/*
		 * Growing the heap failed.  The allocation above will
		 * already have called umem_reap().
		 */
		ASSERT((vmflags & VM_NOSLEEP) == VM_NOSLEEP);

		errno = old_errno;
		return (NULL);
	}
}

vmem_t *
vmem_mmap_arena(vmem_alloc_t **a_out, vmem_free_t **f_out)
{
#ifdef _WIN32
	SYSTEM_INFO info;
	size_t pagesize;
#else
	size_t pagesize = _sysconf(_SC_PAGESIZE);
#endif
	
#ifdef _WIN32
	GetSystemInfo(&info);
	pagesize = info.dwPageSize;
	CHUNKSIZE = info.dwAllocationGranularity;
#elif !defined(MAP_ALIGN)
	CHUNKSIZE = pagesize * VMEM_MMAP_ENLARGE;
#endif

	if (mmap_heap == NULL) {
		#if !defined(MAP_ALIGN) && !defined(_WIN32)
		mmap_heap = vmem_init("mmap_top", vmem_mmap_get_heap_size(),
		#else
		mmap_heap = vmem_init("mmap_top", CHUNKSIZE),
		#endif
		    vmem_mmap_top_alloc, vmem_free,
		    "mmap_heap", NULL, 0, pagesize,
		    vmem_mmap_alloc, vmem_mmap_free);
	}
	if (a_out != NULL)
		*a_out = vmem_mmap_alloc;
	if (f_out != NULL)
		*f_out = vmem_mmap_free;

	return (mmap_heap);
}
