/*************************************************************************/ /*!
@File
@Title          Implementation of PMR functions for OS managed memory
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Part of the memory management.  This module is responsible for
                implementing the function callbacks for physical memory borrowed
                from that normally managed by the operating system.
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

/* include5/ */
#include "img_types.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"

/* services/server/include/ */
#include "osfunc.h"
#include "mm.h"
#include "mutils.h"
#include "pdump_physmem.h"
#include "pdump_km.h"
#include "pmr.h"
#include "pmr_impl.h"
#include "devicemem_server_utils.h"

/* ourselves */
#include "physmem_osmem.h"

#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/mm_types.h>
#include <linux/vmalloc.h>
#include <linux/genalloc.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/tlbflush.h>
#if defined(CONFIG_X86)
#include <asm/cacheflush.h>
#endif
#if defined(__arm__) || defined (__metag__)
#include "osfunc.h"
#endif

struct _PMR_OSPAGEARRAY_DATA_ {
    /*
      uiNumPages:

      number of "pages" (a.k.a. macro pages, compound pages, higher
      order pages, etc...)
    */
    IMG_UINT32 uiNumPages;

    /*
      uiLog2PageSize;

      size of each "page" -- this would normally be the same as
      PAGE_SHIFT, but we support the idea that we may allocate pages
      in larger chunks for better contiguity, using order>0 in the
      call to alloc_pages()
    */
    IMG_UINT32 uiLog2PageSize;

    /*
      the pages thusly allocated...  N.B.. One entry per compound page,
      where compound pages are used.
    */
    struct page **pagearray;

    /*
      for pdump...
    */
    IMG_BOOL bPDumpMalloced;
    IMG_HANDLE hPDumpAllocInfo;

    /*
      record at alloc time whether poisoning will be required when the
      PMR is freed.
    */
    IMG_BOOL bZero;
    IMG_BOOL bPoisonOnFree;
    IMG_BOOL bPoisonOnAlloc;
    IMG_BOOL bHasOSPages;
    IMG_BOOL bOnDemand;
    /*
	 The cache mode of the PMR (required at free time)
	*/
    IMG_UINT32 ui32CPUCacheFlags;
	IMG_BOOL bUnsetMemoryType;
};

/*
 We assume the total area space for PVRSRV_HAP_WRITECOMBINE is fewer than 4MB.
If it's more than 4MB, it fails over to vmalloc automatically.
 */
#if defined(CONFIG_GENERIC_ALLOCATOR) && defined(CONFIG_X86)
#define POOL_SIZE	(4*1024*1024)
static struct gen_pool *pvrsrv_pool_writecombine;
static char *pool_start;

static void init_pvr_pool(void)
{
	struct vm_struct *tmp_area;
	int ret = -1;

	pvrsrv_pool_writecombine = gen_pool_create(PAGE_SHIFT, -1);
	if (!pvrsrv_pool_writecombine) {
		printk(KERN_ERR "%s: create pvrsrv_pool failed\n",
				__func__);
		return ;
	}

	/* Reserve space in the vmalloc vm range */
	tmp_area = __get_vm_area(POOL_SIZE, VM_ALLOC,
			VMALLOC_START, VMALLOC_END);
	pool_start = tmp_area->addr;

	if (!pool_start) {
		printk(KERN_ERR "%s:No vm space to create POOL\n",
				__func__);
		gen_pool_destroy(pvrsrv_pool_writecombine);
		pvrsrv_pool_writecombine = NULL;
		return ;
	} else {
		/* Add our reserved space into the pool */
		ret = gen_pool_add(pvrsrv_pool_writecombine,
			(unsigned long) pool_start, POOL_SIZE, -1);
		if (ret) {
			printk(KERN_ERR "%s:could not remainder pool\n",
					__func__);
			gen_pool_destroy(pvrsrv_pool_writecombine);
			pvrsrv_pool_writecombine = NULL;
			vfree(pool_start);
			pool_start = NULL;
			return ;
			}
	}
	return ;
}

static inline IMG_BOOL vmap_from_pool(IMG_VOID *pvCPUVAddr)
{
	IMG_CHAR *pcTmp = pvCPUVAddr;
	if ((pcTmp >= pool_start) && (pcTmp <= (pool_start + POOL_SIZE)))
	{
		return IMG_TRUE;
	}
	return IMG_FALSE;
}

#endif	/* defined(CONFIG_GENERIC_ALLOCATOR) && defined(CONFIG_X86) */

static void DisableOOMKiller(void)
{
	/* PF_DUMPCORE is treated by the VM as if the OOM killer was disabled.
	 *
	 * As oom_killer_disable() is an inline, non-exported function, we
	 * can't use it from a modular driver. Furthermore, the OOM killer
	 * API doesn't look thread safe, which `current' is.
	 */
	WARN_ON(current->flags & PF_DUMPCORE);
	current->flags |= PF_DUMPCORE;
}

static void EnableOOMKiller(void)
{
	current->flags &= ~PF_DUMPCORE;
}

static IMG_VOID
_PoisonPages(struct page *page,
             IMG_UINT32 uiOrder,
             const IMG_CHAR *pacPoisonData,
             IMG_SIZE_T uiPoisonSize)
{
    void *kvaddr;
    IMG_UINT32 uiSrcByteIndex;
    IMG_UINT32 uiDestByteIndex;
    IMG_UINT32 uiSubPageIndex;
    IMG_CHAR *pcDest;

    uiSrcByteIndex = 0;
    for (uiSubPageIndex = 0; uiSubPageIndex < (1U << uiOrder); uiSubPageIndex++)
    {
        kvaddr = kmap(page + uiSubPageIndex);

        pcDest = kvaddr;

        for(uiDestByteIndex=0; uiDestByteIndex<PAGE_SIZE; uiDestByteIndex++)
        {
            pcDest[uiDestByteIndex] = pacPoisonData[uiSrcByteIndex];
            uiSrcByteIndex++;
            if (uiSrcByteIndex == uiPoisonSize)
            {
                uiSrcByteIndex = 0;
            }
        }
        kunmap(page + uiSubPageIndex);
    }
}

static const IMG_CHAR _AllocPoison[] = "^PoIsOn";
static const IMG_UINT32 _AllocPoisonSize = 7;
static const IMG_CHAR _FreePoison[] = "<DEAD-BEEF>";
static const IMG_UINT32 _FreePoisonSize = 11;

static PVRSRV_ERROR
_AllocOSPageArray(PMR_SIZE_T uiSize,
        IMG_UINT32 uiLog2PageSize,
        IMG_BOOL bZero,
        IMG_BOOL bPoisonOnAlloc,
        IMG_BOOL bPoisonOnFree,
        IMG_BOOL bOnDemand,
        IMG_UINT32 ui32CPUCacheFlags,
		struct _PMR_OSPAGEARRAY_DATA_ **ppsPageArrayDataPtr)
{
    PVRSRV_ERROR eError;
    IMG_VOID *pvData;
    IMG_UINT32 uiNumPages;

    struct page **ppsPageArray;
    struct _PMR_OSPAGEARRAY_DATA_ *psPageArrayData;

    if (uiSize >= 0x1000000000ULL)
    {
        PVR_DPF((PVR_DBG_ERROR,
                 "physmem_osmem_linux.c: Do you really want 64GB of physical memory in one go?  This is likely a bug"));
        eError = PVRSRV_ERROR_INVALID_PARAMS;
        goto e_freed_pvdata;
    }

    PVR_ASSERT(PAGE_SHIFT <= uiLog2PageSize);

    if ((uiSize & ((1ULL << uiLog2PageSize) - 1)) != 0)
    {
        eError = PVRSRV_ERROR_PMR_NOT_PAGE_MULTIPLE;
        goto e_freed_pvdata;
    }

    /* Use of cast below is justified by the assertion that follows to
       prove that no significant bits have been truncated */
    uiNumPages = (IMG_UINT32)(((uiSize-1)>>uiLog2PageSize) + 1);
    PVR_ASSERT(((PMR_SIZE_T)uiNumPages << uiLog2PageSize) == uiSize);

    pvData = kmalloc(sizeof(struct _PMR_OSPAGEARRAY_DATA_) +
                     sizeof(struct page *) * uiNumPages,
                     GFP_KERNEL);
    if (pvData == IMG_NULL)
    {
        PVR_DPF((PVR_DBG_ERROR,
                 "physmem_osmem_linux.c: OS refused the memory allocation for the table of pages.  Did you ask for too much?"));
        eError = PVRSRV_ERROR_INVALID_PARAMS;
        goto e_freed_pvdata;
    }
    PVR_ASSERT(pvData != IMG_NULL);

    psPageArrayData = pvData;
    ppsPageArray = pvData + sizeof(struct _PMR_OSPAGEARRAY_DATA_);
    psPageArrayData->pagearray = ppsPageArray;
    psPageArrayData->uiLog2PageSize = uiLog2PageSize;
    psPageArrayData->uiNumPages = uiNumPages;
    psPageArrayData->bZero = bZero;
    psPageArrayData->ui32CPUCacheFlags = ui32CPUCacheFlags;
 	psPageArrayData->bPoisonOnAlloc = bPoisonOnAlloc;
 	psPageArrayData->bPoisonOnFree = bPoisonOnFree;
 	psPageArrayData->bHasOSPages = IMG_FALSE;
 	psPageArrayData->bOnDemand = bOnDemand;

    psPageArrayData->bPDumpMalloced = IMG_FALSE;

	psPageArrayData->bUnsetMemoryType = IMG_FALSE;

	*ppsPageArrayDataPtr = psPageArrayData;

	return PVRSRV_OK;

e_freed_pvdata:
   PVR_ASSERT(eError != PVRSRV_OK);
   return eError;

}


static PVRSRV_ERROR
_AllocOSPages(struct _PMR_OSPAGEARRAY_DATA_ **ppsPageArrayDataPtr)
{
    /* Allocate a bunch of physical memory.  Must be whole number of
       pages worth */

    PVRSRV_ERROR eError;
    IMG_UINT32 uiOrder;
    IMG_UINT32 uiPageIndex;
	IMG_UINT32 ui32CPUCacheFlags;

    struct _PMR_OSPAGEARRAY_DATA_ *psPageArrayData = *ppsPageArrayDataPtr;
    struct page **ppsPageArray = psPageArrayData->pagearray;

    unsigned int gfp_flags;
#if defined (CONFIG_X86) || defined(__arm__) || defined (__metag__)
	IMG_PVOID pvPageVAddr;
#endif

    PVR_ASSERT(!psPageArrayData->bHasOSPages);

    uiOrder = psPageArrayData->uiLog2PageSize - PAGE_SHIFT;
    ui32CPUCacheFlags = psPageArrayData->ui32CPUCacheFlags;

    gfp_flags = GFP_KERNEL | __GFP_NOWARN | __GFP_NOMEMALLOC;

#if defined(CONFIG_X86)
    gfp_flags |= __GFP_DMA32;
#else
    gfp_flags |= __GFP_HIGHMEM;
#endif

    if (psPageArrayData->bZero)
    {
        gfp_flags |= __GFP_ZERO;
    }

    /* Allocate pages one at a time.  Note that the _device_ memory
       page size may be different from the _host_ cpu page size - we
       have a concept of a minimum contiguity requirement, which must
       be sufficient to meet the requirement of both device and host
       page size (and possibly other devices or other external
       constraints).  We are allocating ONE "minimum contiguity unit"
       (in practice, generally a _device_ page, but not necessarily)
       at a time, by asking the OS for 2**uiOrder _host_ pages at a
       time. */
    for (uiPageIndex = 0;
         uiPageIndex < psPageArrayData->uiNumPages;
         uiPageIndex++)
    {
        /* For now we don't support compound pages */
        PVR_ASSERT(uiOrder == 0);

        DisableOOMKiller();
        ppsPageArray[uiPageIndex] = alloc_pages(gfp_flags, uiOrder);
        EnableOOMKiller();

#if defined (CONFIG_X86)
		if (ppsPageArray[uiPageIndex] != IMG_NULL)
		{
			/*
				On X86 if we already have a mapping we need to change the mode of
				current mapping before we map it ourselves
			*/
			pvPageVAddr = page_address(ppsPageArray[uiPageIndex]);
			if (pvPageVAddr != NULL)
			{
				int ret;

				switch (ui32CPUCacheFlags)
				{
					case PVRSRV_MEMALLOCFLAG_CPU_UNCACHED:
							ret = set_memory_uc((unsigned long)pvPageVAddr, 1);
							if (ret)
							{
								eError = PVRSRV_ERROR_UNABLE_TO_SET_CACHE_MODE;
								 __free_pages(ppsPageArray[uiPageIndex], uiOrder);
								 ppsPageArray[uiPageIndex] = IMG_NULL;
							}
							psPageArrayData->bUnsetMemoryType = IMG_TRUE;
							break;

					case PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE:
							ret = set_memory_wc((unsigned long)pvPageVAddr, 1);
							if (ret)
							{
								eError = PVRSRV_ERROR_UNABLE_TO_SET_CACHE_MODE;
								 __free_pages(ppsPageArray[uiPageIndex], uiOrder);
								 ppsPageArray[uiPageIndex] = IMG_NULL;
							}
							psPageArrayData->bUnsetMemoryType = IMG_TRUE;
							break;

					case PVRSRV_MEMALLOCFLAG_CPU_CACHED:
							break;

					default:
							break;
				}
			}
		}
#endif
#if defined (__arm__) || defined (__metag__)
		/*
		On ARM kernels we can be given pages which still remain in the cache.
		In order to make sure that the data we write through our mappings
		doesn't get over written by later cache evictions we invalidate the
		pages that get given to us.

		Note:
		This still seems to be true if we request cold pages, it's just less
		likely to be in the cache.
		*/
		if (ppsPageArray[uiPageIndex] != IMG_NULL)
		{
			pvPageVAddr = kmap(ppsPageArray[uiPageIndex]);

			if (ui32CPUCacheFlags != PVRSRV_MEMALLOCFLAG_CPU_CACHED)
			{
				IMG_CPU_PHYADDR sCPUPhysAddrStart, sCPUPhysAddrEnd;

				sCPUPhysAddrStart.uiAddr = page_to_phys(ppsPageArray[uiPageIndex]);
				sCPUPhysAddrEnd.uiAddr = sCPUPhysAddrStart.uiAddr + PAGE_SIZE;

				/* If we're zeroing, we need to make sure the cleared memory is pushed out
					of the cache before the cache lines are invalidated */
				if(psPageArrayData->bZero)
					OSFlushCPUCacheRangeKM(pvPageVAddr,
												pvPageVAddr + PAGE_SIZE,
												sCPUPhysAddrStart,
												sCPUPhysAddrEnd);
				else
					OSInvalidateCPUCacheRangeKM(pvPageVAddr,
												pvPageVAddr + PAGE_SIZE,
												sCPUPhysAddrStart,
												sCPUPhysAddrEnd);
			}
			kunmap(ppsPageArray[uiPageIndex]);
		}
#endif
        if (ppsPageArray[uiPageIndex] == IMG_NULL)
        {
            PVR_DPF((PVR_DBG_ERROR,
                     "physmem_osmem_linux.c: alloc_pages failed to honour request at %d of %d",
                     uiPageIndex,
                     psPageArrayData->uiNumPages));
            for(--uiPageIndex;uiPageIndex < psPageArrayData->uiNumPages;--uiPageIndex)
            {
#if defined(CONFIG_X86)
				pvPageVAddr = page_address(ppsPageArray[uiPageIndex]);
				if (psPageArrayData->bUnsetMemoryType == IMG_TRUE)
				{
					int ret;

					ret = set_memory_wb((unsigned long)pvPageVAddr, 1);
					if (ret)
					{
						PVR_DPF((PVR_DBG_ERROR, "%s: Failed to reset page attribute", __FUNCTION__));
					}
				}
#endif
                __free_pages(ppsPageArray[uiPageIndex], uiOrder);
            }
            eError = PVRSRV_ERROR_PMR_FAILED_TO_ALLOC_PAGES;
            goto e_freed_pages;
        }

        /* Can't ask us to zero it and poison it */
        PVR_ASSERT(!psPageArrayData->bZero || !psPageArrayData->bPoisonOnAlloc);

        if (psPageArrayData->bPoisonOnAlloc)
        {
            _PoisonPages(ppsPageArray[uiPageIndex],
                         uiOrder,
                         _AllocPoison,
                         _AllocPoisonSize);
        }
    }

    /* OS Pages have been allocated */
    psPageArrayData->bHasOSPages = IMG_TRUE;

    PVR_DPF((PVR_DBG_MESSAGE, "physmem_osmem_linux.c: allocated OS memory for PMR @0x%p", psPageArrayData));

    return PVRSRV_OK;

    /*
      error exit paths follow:
    */

e_freed_pages:
    PVR_ASSERT(eError != PVRSRV_OK);
    return eError;
}

static PVRSRV_ERROR
_FreeOSPagesArray(struct _PMR_OSPAGEARRAY_DATA_ *psPageArrayData)
{
    kfree(psPageArrayData);

    PVR_DPF((PVR_DBG_MESSAGE, "physmem_osmem_linux.c: freed OS memory for PMR @0x%p", psPageArrayData));

    return PVRSRV_OK;
}

static PVRSRV_ERROR
_FreeOSPages(struct _PMR_OSPAGEARRAY_DATA_ *psPageArrayData)
{
    PVRSRV_ERROR eError;
    IMG_UINT32 uiNumPages;
    IMG_UINT32 uiOrder;
    IMG_UINT32 uiPageIndex;
    struct page **ppsPageArray;
#if defined (CONFIG_X86)
    IMG_PVOID pvPageVAddr;
#endif

	PVR_ASSERT(psPageArrayData->bHasOSPages);

    ppsPageArray = psPageArrayData->pagearray;

    uiNumPages = psPageArrayData->uiNumPages;

    uiOrder = psPageArrayData->uiLog2PageSize - PAGE_SHIFT;

	for (uiPageIndex = 0;
		 uiPageIndex < uiNumPages;
		 uiPageIndex++)
	{
		if (psPageArrayData->bPoisonOnFree)
		{
			_PoisonPages(ppsPageArray[uiPageIndex],
						 uiOrder,
						 _FreePoison,
						 _FreePoisonSize);
		}
#if defined(CONFIG_X86)
		if (psPageArrayData->bUnsetMemoryType == IMG_TRUE)
		{
			int ret;

			pvPageVAddr = page_address(ppsPageArray[uiPageIndex]);
			ret = set_memory_wb((unsigned long) pvPageVAddr, 1);
			if (ret)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Failed to reset page attribute", __FUNCTION__));
			}
		}
#endif
		__free_pages(ppsPageArray[uiPageIndex], uiOrder);
	}

    eError = PVRSRV_OK;

    psPageArrayData->bHasOSPages = IMG_FALSE;

    return eError;
}

/*
 *
 * Implementation of callback functions
 *
 */

/* destructor func is called after last reference disappears, but
   before PMR itself is freed. */
static PVRSRV_ERROR
PMRFinalizeOSMem(PMR_IMPL_PRIVDATA pvPriv
                 //struct _PMR_OSPAGEARRAY_DATA_ *psOSPageArrayData
                 )
{
    PVRSRV_ERROR eError;
    struct _PMR_OSPAGEARRAY_DATA_ *psOSPageArrayData;

    psOSPageArrayData = pvPriv;

    /* Conditionally do the PDump free, because if CreatePMR failed we
       won't have done the PDump MALLOC.  */
    if (psOSPageArrayData->bPDumpMalloced)
    {
        PDumpPMRFree(psOSPageArrayData->hPDumpAllocInfo);
    }

    /*  We can't free pages until now. */
    if (psOSPageArrayData->bHasOSPages)
    {
		eError = _FreeOSPages(psOSPageArrayData);
		PVR_ASSERT (eError == PVRSRV_OK); /* can we do better? */
    }

    eError = _FreeOSPagesArray(psOSPageArrayData);
    PVR_ASSERT (eError == PVRSRV_OK); /* can we do better? */

    return PVRSRV_OK;
}

/* callback function for locking the system physical page addresses.
   This function must be called before the lookup address func. */
static PVRSRV_ERROR
PMRLockSysPhysAddressesOSMem(PMR_IMPL_PRIVDATA pvPriv,
                             // struct _PMR_OSPAGEARRAY_DATA_ *psOSPageArrayData,
                             IMG_UINT32 uiLog2DevPageSize)
{
    PVRSRV_ERROR eError;
    struct _PMR_OSPAGEARRAY_DATA_ *psOSPageArrayData;

    psOSPageArrayData = pvPriv;

    if (psOSPageArrayData->bOnDemand)
    {
		/* Allocate Memory for deferred allocation */
    	eError = _AllocOSPages(&psOSPageArrayData);
    	if (eError != PVRSRV_OK)
    	{
    		return eError;
    	}
    }

    /* Physical page addresses are already locked down in this
       implementation, so there is no need to acquire physical
       addresses.  We do need to verify that the physical contiguity
       requested by the caller (i.e. page size of the device they
       intend to map this memory into) is compatible with (i.e. not of
       coarser granularity than) our already known physicial
       contiguity of the pages */
    if (uiLog2DevPageSize > psOSPageArrayData->uiLog2PageSize)
    {
        /* or NOT_MAPPABLE_TO_THIS_PAGE_SIZE ? */
        eError = PVRSRV_ERROR_PMR_INCOMPATIBLE_CONTIGUITY;
        return eError;
    }

    eError = PVRSRV_OK;
    return eError;

}

static PVRSRV_ERROR
PMRUnlockSysPhysAddressesOSMem(PMR_IMPL_PRIVDATA pvPriv
                               //struct _PMR_OSPAGEARRAY_DATA_ *psOSPageArrayData
                               )
{
    /* Just drops the refcount. */

    PVRSRV_ERROR eError = PVRSRV_OK;
    struct _PMR_OSPAGEARRAY_DATA_ *psOSPageArrayData;

    psOSPageArrayData = pvPriv;
    if (psOSPageArrayData->bOnDemand)
    {
		/* Free Memory for deferred allocation */
    	eError = _FreeOSPages(psOSPageArrayData);
    	if (eError != PVRSRV_OK)
    	{
    		return eError;
    	}
    }

    PVR_ASSERT (eError == PVRSRV_OK);
    return eError;
}

/* N.B.  It is assumed that PMRLockSysPhysAddressesOSMem() is called _before_ this function! */
static PVRSRV_ERROR
PMRSysPhysAddrOSMem(PMR_IMPL_PRIVDATA pvPriv,
                    //const struct _PMR_OSPAGEARRAY_DATA_ *psOSPageArrayData,
                    IMG_DEVMEM_OFFSET_T uiOffset,
                    IMG_DEV_PHYADDR *psDevPAddr
                    )
{
    PVRSRV_ERROR eError;
    IMG_UINT32 uiPageSize;
    IMG_UINT32 uiNumPages;
    IMG_UINT32 uiPageIndex;
    IMG_UINT32 uiInPageOffset;
    struct page **ppsPageArray;
    const struct _PMR_OSPAGEARRAY_DATA_ *psOSPageArrayData;

    psOSPageArrayData = pvPriv;
    ppsPageArray = psOSPageArrayData->pagearray;

    uiNumPages = psOSPageArrayData->uiNumPages;
    uiPageSize = 1U << psOSPageArrayData->uiLog2PageSize;

    uiPageIndex = uiOffset >> psOSPageArrayData->uiLog2PageSize;
    uiInPageOffset = uiOffset - ((IMG_DEVMEM_OFFSET_T)uiPageIndex << psOSPageArrayData->uiLog2PageSize);
    PVR_ASSERT(uiPageIndex < uiNumPages);
    PVR_ASSERT(uiInPageOffset < uiPageSize);

    psDevPAddr->uiAddr = page_to_phys(ppsPageArray[uiPageIndex]) + uiInPageOffset;

    eError = PVRSRV_OK;

    return eError;
}

static PVRSRV_ERROR
PMRAcquireKernelMappingDataOSMem(PMR_IMPL_PRIVDATA pvPriv,
                                 IMG_SIZE_T uiOffset,
                                 IMG_SIZE_T uiSize,
                                 IMG_VOID **ppvKernelAddressOut,
                                 IMG_HANDLE *phHandleOut,
                                 PMR_FLAGS_T ulFlags)
{
    PVRSRV_ERROR eError;
    struct _PMR_OSPAGEARRAY_DATA_ *psOSPageArrayData;
    IMG_VOID *pvAddress;
    pgprot_t prot = PAGE_KERNEL;
    IMG_UINT32 ui32CPUCacheFlags;

#if defined(CONFIG_GENERIC_ALLOCATOR) && defined(CONFIG_X86)
	if (!pvrsrv_pool_writecombine)
		init_pvr_pool();
#endif	/* defined(CONFIG_GENERIC_ALLOCATOR) && defined(CONFIG_X86) */

    psOSPageArrayData = pvPriv;
	ui32CPUCacheFlags = DevmemCPUCacheMode(ulFlags);

    if (psOSPageArrayData->uiLog2PageSize != PAGE_SHIFT)
    {
        /* we only know how to use vmap on allocations comprising
           individual pages.  Higher-order "pages" are not supported
           with this. */
        eError = PVRSRV_ERROR_PMR_INCOMPATIBLE_CONTIGUITY;
        goto e0;
    }

	switch (ui32CPUCacheFlags)
	{
		case PVRSRV_MEMALLOCFLAG_CPU_UNCACHED:
				prot = pgprot_noncached(prot);
				break;

		case PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE:
				prot = pgprot_writecombine(prot);
				break;

		case PVRSRV_MEMALLOCFLAG_CPU_CACHED:
				break;

		default:
				eError = PVRSRV_ERROR_INVALID_PARAMS;
				goto e0;
	}

#if defined(CONFIG_GENERIC_ALLOCATOR) && defined(CONFIG_X86)
	if (psOSPageArrayData->uiNumPages > 1) {
		pvAddress = vm_map_ram(psOSPageArrayData->pagearray,
				psOSPageArrayData->uiNumPages,
				-1,
				prot);

		if (((IMG_VOID *)pvAddress) == IMG_NULL)
		{
			return PVRSRV_ERROR_FAILED_TO_MAP_KERNELVIRTUAL;
		}
	} else {
		int ret = 0;
		unsigned long size, addr;
		struct vm_struct tmp_area;
		struct page **page_array_ptr;

		page_array_ptr = psOSPageArrayData->pagearray;
		size = psOSPageArrayData->uiNumPages * PAGE_SIZE;

		addr = gen_pool_alloc(pvrsrv_pool_writecombine, size);
		pvAddress = (IMG_VOID *)addr;

		if (pvAddress) {
			tmp_area.addr = pvAddress;
			tmp_area.size = size + PAGE_SIZE;
			ret = map_vm_area(&tmp_area, prot, &page_array_ptr);
		} else {
			pvAddress = vm_map_ram(psOSPageArrayData->pagearray,
                                psOSPageArrayData->uiNumPages,
                                -1,
                                prot);

			if (pvAddress == IMG_NULL) {
				PVR_DPF((PVR_DBG_ERROR,
					 "%s: Cannot map pages linearly to kernel virtual address",
					 __func__));
				return PVRSRV_ERROR_FAILED_TO_MAP_KERNELVIRTUAL;
			}
		}

		if (ret) {
			gen_pool_free(pvrsrv_pool_writecombine,
						  (unsigned long)pvAddress,
						  size);
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: Cannot map page to pool",
					 __func__));
			return PVRSRV_ERROR_FAILED_TO_MAP_KERNELVIRTUAL;
		}
	}
#else
	pvAddress = vm_map_ram(psOSPageArrayData->pagearray,
						   psOSPageArrayData->uiNumPages,
						   -1,
						   prot);

	if (((IMG_VOID *)pvAddress) == IMG_NULL)
	{
		return PVRSRV_ERROR_FAILED_TO_MAP_KERNELVIRTUAL;
	}

#endif	/* defined(CONFIG_GENERIC_ALLOCATOR) && defined(CONFIG_X86) */

    *ppvKernelAddressOut = pvAddress + uiOffset;
    *phHandleOut = pvAddress;

    return PVRSRV_OK;

    /*
      error exit paths follow
    */

 e0:
    PVR_ASSERT(eError != PVRSRV_OK);
    return eError;
}

static IMG_VOID PMRReleaseKernelMappingDataOSMem(PMR_IMPL_PRIVDATA pvPriv,
                                                 IMG_HANDLE hHandle)
{
    struct _PMR_OSPAGEARRAY_DATA_ *psOSPageArrayData;

    psOSPageArrayData = pvPriv;
#if defined(CONFIG_GENERIC_ALLOCATOR) && defined(CONFIG_X86)
	if (vmap_from_pool(hHandle)) {
		unsigned long addr;
		unsigned long size = psOSPageArrayData->uiNumPages * PAGE_SIZE;
		unsigned long start = (unsigned long)hHandle;
		unsigned long end = start + size;

		/* Flush the data cache */
		flush_cache_vunmap(start, end);
		/* Unmap the page */
		unmap_kernel_range_noflush(start, size);
		/* Flush the TLB */
		for (addr = start; addr < end; addr += PAGE_SIZE)
			__flush_tlb_single(addr);
		/* Free the page back to the pool */
		gen_pool_free(pvrsrv_pool_writecombine, start, size);
	}
	else
#endif	/* defined(CONFIG_GENERIC_ALLOCATOR) && defined(CONFIG_X86) */
	{
		vm_unmap_ram(hHandle, psOSPageArrayData->uiNumPages);
	}
}

static PMR_IMPL_FUNCTAB _sPMROSPFuncTab = {
    .pfnLockPhysAddresses = &PMRLockSysPhysAddressesOSMem,
    .pfnUnlockPhysAddresses = &PMRUnlockSysPhysAddressesOSMem,
    .pfnDevPhysAddr = &PMRSysPhysAddrOSMem,
    .pfnAcquireKernelMappingData = &PMRAcquireKernelMappingDataOSMem,
    .pfnReleaseKernelMappingData = &PMRReleaseKernelMappingDataOSMem,
    .pfnReadBytes = IMG_NULL,
    .pfnWriteBytes = IMG_NULL,
    .pfnFinalize = &PMRFinalizeOSMem
};

static PVRSRV_ERROR
_NewOSAllocPagesPMR(PVRSRV_DEVICE_NODE *psDevNode,
                    IMG_DEVMEM_SIZE_T uiSize,
					IMG_DEVMEM_SIZE_T uiChunkSize,
					IMG_UINT32 ui32NumPhysChunks,
					IMG_UINT32 ui32NumVirtChunks,
					IMG_BOOL *pabMappingTable,
                    IMG_UINT32 uiLog2PageSize,
                    PVRSRV_MEMALLOCFLAGS_T uiFlags,
                    PMR **ppsPMRPtr)
{
    PVRSRV_ERROR eError;
    PVRSRV_ERROR eError2;
    PMR *psPMR;
    struct _PMR_OSPAGEARRAY_DATA_ *psPrivData;
    IMG_HANDLE hPDumpAllocInfo = IMG_NULL;
    PMR_FLAGS_T uiPMRFlags;
    IMG_BOOL bZero;
    IMG_BOOL bPoisonOnAlloc;
    IMG_BOOL bPoisonOnFree;
    IMG_BOOL bOnDemand = ((uiFlags & PVRSRV_MEMALLOCFLAG_NO_OSPAGES_ON_ALLOC) > 0);
	IMG_BOOL bCpuLocal = ((uiFlags & PVRSRV_MEMALLOCFLAG_CPU_LOCAL) > 0);
	IMG_UINT32 ui32CPUCacheFlags = (IMG_UINT32) DevmemCPUCacheMode(uiFlags);


    if (uiFlags & PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC)
    {
        bZero = IMG_TRUE;
    }
    else
    {
        bZero = IMG_FALSE;
    }

    if (uiFlags & PVRSRV_MEMALLOCFLAG_POISON_ON_ALLOC)
    {
        bPoisonOnAlloc = IMG_TRUE;
    }
    else
    {
        bPoisonOnAlloc = IMG_FALSE;
    }

    if (uiFlags & PVRSRV_MEMALLOCFLAG_POISON_ON_FREE)
    {
        bPoisonOnFree = IMG_TRUE;
    }
    else
    {
        bPoisonOnFree = IMG_FALSE;
    }

    if ((uiFlags & PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC) &&
        (uiFlags & PVRSRV_MEMALLOCFLAG_POISON_ON_ALLOC))
    {
        /* Zero on Alloc and Poison on Alloc are mutually exclusive */
        eError = PVRSRV_ERROR_INVALID_PARAMS;
        goto errorOnParam;
    }

	/* Silently round up alignment/pagesize if request was less that
	   PAGE_SHIFT, because it would never be harmful for memory to be
	   _more_ contiguous that was desired */
	uiLog2PageSize = PAGE_SHIFT > uiLog2PageSize
		? PAGE_SHIFT
		: uiLog2PageSize;

	/* Create Array structure that hold the physical pages */
	eError = _AllocOSPageArray(uiChunkSize * ui32NumPhysChunks,
						   uiLog2PageSize,
						   bZero,
						   bPoisonOnAlloc,
						   bPoisonOnFree,
						   bOnDemand,
						   ui32CPUCacheFlags,
						   &psPrivData);
	if (eError != PVRSRV_OK)
	{
		goto errorOnAllocPageArray;
	}

	if (!bOnDemand)
	{
		/* Allocate the physical pages */
		eError = _AllocOSPages(&psPrivData);
		if (eError != PVRSRV_OK)
		{
			goto errorOnAllocPages;
		}
	}

    /* In this instance, we simply pass flags straight through.

       Generically, uiFlags can include things that control the PMR
       factory, but we don't need any such thing (at the time of
       writing!), and our caller specifies all PMR flags so we don't
       need to meddle with what was given to us.
    */
    uiPMRFlags = (PMR_FLAGS_T)(uiFlags & PVRSRV_MEMALLOCFLAGS_PMRFLAGSMASK);
    /* check no significant bits were lost in cast due to different
       bit widths for flags */
    PVR_ASSERT(uiPMRFlags == (uiFlags & PVRSRV_MEMALLOCFLAGS_PMRFLAGSMASK));

    if (bOnDemand)
    {
    	PDUMPCOMMENT("Deferred Allocation PMR (UMA)");
    }
    if (bCpuLocal)
    {
    	PDUMPCOMMENT("CPU_LOCAL allocation requested");
    }
    eError = PMRCreatePMR(psDevNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL],
                          uiSize,
                          uiChunkSize,
                          ui32NumPhysChunks,
                          ui32NumVirtChunks,
                          pabMappingTable,
                          uiLog2PageSize,
                          uiPMRFlags,
                          "PMROSAP",
                          &_sPMROSPFuncTab,
                          psPrivData,
                          &psPMR,
                          &hPDumpAllocInfo,
    					  IMG_FALSE);
    if (eError != PVRSRV_OK)
    {
        goto errorOnCreate;
    }

	psPrivData->hPDumpAllocInfo = hPDumpAllocInfo;
	psPrivData->bPDumpMalloced = IMG_TRUE;

    *ppsPMRPtr = psPMR;
    return PVRSRV_OK;

errorOnCreate:
	if (!bOnDemand)
	{
		eError2 = _FreeOSPages(psPrivData);
		PVR_ASSERT(eError2 == PVRSRV_OK);
	}

errorOnAllocPages:
	eError2 = _FreeOSPagesArray(psPrivData);
	PVR_ASSERT(eError2 == PVRSRV_OK);

errorOnAllocPageArray:
errorOnParam:
    PVR_ASSERT(eError != PVRSRV_OK);
    return eError;
}

PVRSRV_ERROR
PhysmemNewOSRamBackedPMR(PVRSRV_DEVICE_NODE *psDevNode,
                         IMG_DEVMEM_SIZE_T uiSize,
						 IMG_DEVMEM_SIZE_T uiChunkSize,
						 IMG_UINT32 ui32NumPhysChunks,
						 IMG_UINT32 ui32NumVirtChunks,
						 IMG_BOOL *pabMappingTable,
                         IMG_UINT32 uiLog2PageSize,
                         PVRSRV_MEMALLOCFLAGS_T uiFlags,
                         PMR **ppsPMRPtr)
{
    return _NewOSAllocPagesPMR(psDevNode,
                               uiSize,
                               uiChunkSize,
                               ui32NumPhysChunks,
                               ui32NumVirtChunks,
                               pabMappingTable,
                               uiLog2PageSize,
                               uiFlags,
                               ppsPMRPtr);
}
