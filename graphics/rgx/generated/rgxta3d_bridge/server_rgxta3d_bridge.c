/*************************************************************************/ /*!
@File
@Title          Server bridge for rgxta3d
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for rgxta3d
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

#include <stddef.h>
#include <asm/uaccess.h>

#include "img_defs.h"

#include "rgxta3d.h"


#include "common_rgxta3d_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#include "rgx_bridge.h"
#include "srvcore.h"
#include "handle.h"

#if defined (SUPPORT_AUTH)
#include "osauth.h"
#endif

#include <linux/slab.h>

/* ***************************************************************************
 * Bridge proxy functions
 */

static PVRSRV_ERROR
RGXDestroyHWRTDataResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}

static PVRSRV_ERROR
RGXDestroyRenderTargetResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}

static PVRSRV_ERROR
RGXDestroyZSBufferResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}

static PVRSRV_ERROR
RGXUnpopulateZSBufferResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}

static PVRSRV_ERROR
RGXDestroyFreeListResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}

static PVRSRV_ERROR
RGXDestroyRenderContextResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}



/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeRGXCreateHWRTData(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXCREATEHWRTDATA *psRGXCreateHWRTDataIN,
					 PVRSRV_BRIDGE_OUT_RGXCREATEHWRTDATA *psRGXCreateHWRTDataOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	RGX_FREELIST * *psapsFreeListsInt = IMG_NULL;
	IMG_HANDLE *hapsFreeListsInt2 = IMG_NULL;
	RGX_RTDATA_CLEANUP_DATA * psCleanupCookieInt = IMG_NULL;
	IMG_HANDLE hCleanupCookieInt2 = IMG_NULL;
	DEVMEM_MEMDESC * psRTACtlMemDescInt = IMG_NULL;
	DEVMEM_MEMDESC * pssHWRTDataMemDescInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXTA3D_RGXCREATEHWRTDATA);



	psRGXCreateHWRTDataOUT->hCleanupCookie = IMG_NULL;

	psapsFreeListsInt = OSAllocMem(RGXFW_MAX_FREELISTS * sizeof(RGX_FREELIST *));
	if (!psapsFreeListsInt)
	{
		psRGXCreateHWRTDataOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;

		goto RGXCreateHWRTData_exit;
	}

	hapsFreeListsInt2 = OSAllocMem(RGXFW_MAX_FREELISTS * sizeof(IMG_HANDLE));
	if (!hapsFreeListsInt2)
	{
		psRGXCreateHWRTDataOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;

		goto RGXCreateHWRTData_exit;
	}

	if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXCreateHWRTDataIN->phapsFreeLists, RGXFW_MAX_FREELISTS * sizeof(IMG_HANDLE)) 
		|| (OSCopyFromUser(NULL, hapsFreeListsInt2, psRGXCreateHWRTDataIN->phapsFreeLists,
		RGXFW_MAX_FREELISTS * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
	{
		psRGXCreateHWRTDataOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

		goto RGXCreateHWRTData_exit;
	}

		/* Look up the address from the handle */
		psRGXCreateHWRTDataOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hDevNodeInt,
							   psRGXCreateHWRTDataIN->hDevNode,
							   PVRSRV_HANDLE_TYPE_DEV_NODE);
		if(psRGXCreateHWRTDataOUT->eError != PVRSRV_OK)
		{
			goto RGXCreateHWRTData_exit;
		}
	{
	IMG_UINT32 i;

	for (i=0;i<RGXFW_MAX_FREELISTS;i++)
	{
		/* Look up the address from the handle */
		psRGXCreateHWRTDataOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hapsFreeListsInt2[i],
							   hapsFreeListsInt2[i],
							   PVRSRV_HANDLE_TYPE_RGX_FREELIST);
		if(psRGXCreateHWRTDataOUT->eError != PVRSRV_OK)
		{
			goto RGXCreateHWRTData_exit;
		}

		/* Look up the data from the resman address */
		psRGXCreateHWRTDataOUT->eError = ResManFindPrivateDataByPtr(hapsFreeListsInt2[i], (IMG_VOID **) &psapsFreeListsInt[i]);
		if(psRGXCreateHWRTDataOUT->eError != PVRSRV_OK)
		{
			goto RGXCreateHWRTData_exit;
		}
	}
	}

	psRGXCreateHWRTDataOUT->eError =
		RGXCreateHWRTData(
					hDevNodeInt,
					psRGXCreateHWRTDataIN->ui32RenderTarget,
					psRGXCreateHWRTDataIN->sPMMlistDevVAddr,
					psRGXCreateHWRTDataIN->sVFPPageTableAddr,
					psapsFreeListsInt,
					&psCleanupCookieInt,
					&psRTACtlMemDescInt,
					psRGXCreateHWRTDataIN->ui32PPPScreen,
					psRGXCreateHWRTDataIN->ui32PPPGridOffset,
					psRGXCreateHWRTDataIN->ui64PPPMultiSampleCtl,
					psRGXCreateHWRTDataIN->ui32TPCStride,
					psRGXCreateHWRTDataIN->sTailPtrsDevVAddr,
					psRGXCreateHWRTDataIN->ui32TPCSize,
					psRGXCreateHWRTDataIN->ui32TEScreen,
					psRGXCreateHWRTDataIN->ui32TEAA,
					psRGXCreateHWRTDataIN->ui32TEMTILE1,
					psRGXCreateHWRTDataIN->ui32TEMTILE2,
					psRGXCreateHWRTDataIN->ui32MTileStride,
					psRGXCreateHWRTDataIN->ui16MaxRTs,
					&pssHWRTDataMemDescInt,
					&psRGXCreateHWRTDataOUT->ui32FWHWRTData);
	/* Exit early if bridged call fails */
	if(psRGXCreateHWRTDataOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateHWRTData_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hCleanupCookieInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_RGX_FWIF_HWRTDATA,
												psCleanupCookieInt,
												/* FIXME: how can we avoid this cast? */
												(RESMAN_FREE_FN)&RGXDestroyHWRTData);
	if (hCleanupCookieInt2 == IMG_NULL)
	{
		psRGXCreateHWRTDataOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto RGXCreateHWRTData_exit;
	}
	psRGXCreateHWRTDataOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psRGXCreateHWRTDataOUT->hCleanupCookie,
							(IMG_HANDLE) hCleanupCookieInt2,
							PVRSRV_HANDLE_TYPE_RGX_RTDATA_CLEANUP,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psRGXCreateHWRTDataOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateHWRTData_exit;
	}
	psRGXCreateHWRTDataOUT->eError = PVRSRVAllocSubHandle(psConnection->psHandleBase,
							&psRGXCreateHWRTDataOUT->hRTACtlMemDesc,
							(IMG_HANDLE) psRTACtlMemDescInt,
							PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,psRGXCreateHWRTDataOUT->hCleanupCookie);
	if (psRGXCreateHWRTDataOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateHWRTData_exit;
	}
	psRGXCreateHWRTDataOUT->eError = PVRSRVAllocSubHandle(psConnection->psHandleBase,
							&psRGXCreateHWRTDataOUT->hsHWRTDataMemDesc,
							(IMG_HANDLE) pssHWRTDataMemDescInt,
							PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,psRGXCreateHWRTDataOUT->hCleanupCookie);
	if (psRGXCreateHWRTDataOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateHWRTData_exit;
	}


RGXCreateHWRTData_exit:
	if (psRGXCreateHWRTDataOUT->eError != PVRSRV_OK)
	{
		if (psRGXCreateHWRTDataOUT->hCleanupCookie)
		{
			PVRSRVReleaseHandle(psConnection->psHandleBase,
						(IMG_HANDLE) psRGXCreateHWRTDataOUT->hCleanupCookie,
						PVRSRV_HANDLE_TYPE_RGX_RTDATA_CLEANUP);
		}

		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hCleanupCookieInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hCleanupCookieInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psCleanupCookieInt)
		{
			RGXDestroyHWRTData(psCleanupCookieInt);
		}
	}

	if (psapsFreeListsInt)
		OSFreeMem(psapsFreeListsInt);
	if (hapsFreeListsInt2)
		OSFreeMem(hapsFreeListsInt2);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXDestroyHWRTData(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXDESTROYHWRTDATA *psRGXDestroyHWRTDataIN,
					 PVRSRV_BRIDGE_OUT_RGXDESTROYHWRTDATA *psRGXDestroyHWRTDataOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hCleanupCookieInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYHWRTDATA);




		/* Look up the address from the handle */
		psRGXDestroyHWRTDataOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hCleanupCookieInt2,
							   psRGXDestroyHWRTDataIN->hCleanupCookie,
							   PVRSRV_HANDLE_TYPE_RGX_RTDATA_CLEANUP);
		if(psRGXDestroyHWRTDataOUT->eError != PVRSRV_OK)
		{
			goto RGXDestroyHWRTData_exit;
		}

	psRGXDestroyHWRTDataOUT->eError = RGXDestroyHWRTDataResManProxy(hCleanupCookieInt2);
	/* Exit early if bridged call fails */
	if(psRGXDestroyHWRTDataOUT->eError != PVRSRV_OK)
	{
		goto RGXDestroyHWRTData_exit;
	}

	psRGXDestroyHWRTDataOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyHWRTDataIN->hCleanupCookie,
					PVRSRV_HANDLE_TYPE_RGX_RTDATA_CLEANUP);


RGXDestroyHWRTData_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXCreateRenderTarget(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXCREATERENDERTARGET *psRGXCreateRenderTargetIN,
					 PVRSRV_BRIDGE_OUT_RGXCREATERENDERTARGET *psRGXCreateRenderTargetOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	RGX_RT_CLEANUP_DATA * pssRenderTargetMemDescInt = IMG_NULL;
	IMG_HANDLE hsRenderTargetMemDescInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXTA3D_RGXCREATERENDERTARGET);




		/* Look up the address from the handle */
		psRGXCreateRenderTargetOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hDevNodeInt,
							   psRGXCreateRenderTargetIN->hDevNode,
							   PVRSRV_HANDLE_TYPE_DEV_NODE);
		if(psRGXCreateRenderTargetOUT->eError != PVRSRV_OK)
		{
			goto RGXCreateRenderTarget_exit;
		}

	psRGXCreateRenderTargetOUT->eError =
		RGXCreateRenderTarget(
					hDevNodeInt,
					psRGXCreateRenderTargetIN->spsVHeapTableDevVAddr,
					&pssRenderTargetMemDescInt,
					&psRGXCreateRenderTargetOUT->ui32sRenderTargetFWDevVAddr);
	/* Exit early if bridged call fails */
	if(psRGXCreateRenderTargetOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateRenderTarget_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hsRenderTargetMemDescInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_RGX_FWIF_RENDERTARGET,
												pssRenderTargetMemDescInt,
												/* FIXME: how can we avoid this cast? */
												(RESMAN_FREE_FN)&RGXDestroyRenderTarget);
	if (hsRenderTargetMemDescInt2 == IMG_NULL)
	{
		psRGXCreateRenderTargetOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto RGXCreateRenderTarget_exit;
	}
	psRGXCreateRenderTargetOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psRGXCreateRenderTargetOUT->hsRenderTargetMemDesc,
							(IMG_HANDLE) hsRenderTargetMemDescInt2,
							PVRSRV_HANDLE_TYPE_RGX_FWIF_RENDERTARGET,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psRGXCreateRenderTargetOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateRenderTarget_exit;
	}


RGXCreateRenderTarget_exit:
	if (psRGXCreateRenderTargetOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hsRenderTargetMemDescInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hsRenderTargetMemDescInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (pssRenderTargetMemDescInt)
		{
			RGXDestroyRenderTarget(pssRenderTargetMemDescInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeRGXDestroyRenderTarget(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXDESTROYRENDERTARGET *psRGXDestroyRenderTargetIN,
					 PVRSRV_BRIDGE_OUT_RGXDESTROYRENDERTARGET *psRGXDestroyRenderTargetOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hsRenderTargetMemDescInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYRENDERTARGET);




		/* Look up the address from the handle */
		psRGXDestroyRenderTargetOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hsRenderTargetMemDescInt2,
							   psRGXDestroyRenderTargetIN->hsRenderTargetMemDesc,
							   PVRSRV_HANDLE_TYPE_RGX_FWIF_RENDERTARGET);
		if(psRGXDestroyRenderTargetOUT->eError != PVRSRV_OK)
		{
			goto RGXDestroyRenderTarget_exit;
		}

	psRGXDestroyRenderTargetOUT->eError = RGXDestroyRenderTargetResManProxy(hsRenderTargetMemDescInt2);
	/* Exit early if bridged call fails */
	if(psRGXDestroyRenderTargetOUT->eError != PVRSRV_OK)
	{
		goto RGXDestroyRenderTarget_exit;
	}

	psRGXDestroyRenderTargetOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyRenderTargetIN->hsRenderTargetMemDesc,
					PVRSRV_HANDLE_TYPE_RGX_FWIF_RENDERTARGET);


RGXDestroyRenderTarget_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXCreateZSBuffer(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXCREATEZSBUFFER *psRGXCreateZSBufferIN,
					 PVRSRV_BRIDGE_OUT_RGXCREATEZSBUFFER *psRGXCreateZSBufferOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	DEVMEMINT_RESERVATION * psReservationInt = IMG_NULL;
	IMG_HANDLE hReservationInt2 = IMG_NULL;
	PMR * psPMRInt = IMG_NULL;
	IMG_HANDLE hPMRInt2 = IMG_NULL;
	RGX_ZSBUFFER_DATA * pssZSBufferKMInt = IMG_NULL;
	IMG_HANDLE hsZSBufferKMInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXTA3D_RGXCREATEZSBUFFER);




		/* Look up the address from the handle */
		psRGXCreateZSBufferOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hDevNodeInt,
							   psRGXCreateZSBufferIN->hDevNode,
							   PVRSRV_HANDLE_TYPE_DEV_NODE);
		if(psRGXCreateZSBufferOUT->eError != PVRSRV_OK)
		{
			goto RGXCreateZSBuffer_exit;
		}
		/* Look up the address from the handle */
		psRGXCreateZSBufferOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hReservationInt2,
							   psRGXCreateZSBufferIN->hReservation,
							   PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION);
		if(psRGXCreateZSBufferOUT->eError != PVRSRV_OK)
		{
			goto RGXCreateZSBuffer_exit;
		}

		/* Look up the data from the resman address */
		psRGXCreateZSBufferOUT->eError = ResManFindPrivateDataByPtr(hReservationInt2, (IMG_VOID **) &psReservationInt);
		if(psRGXCreateZSBufferOUT->eError != PVRSRV_OK)
		{
			goto RGXCreateZSBuffer_exit;
		}
		/* Look up the address from the handle */
		psRGXCreateZSBufferOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hPMRInt2,
							   psRGXCreateZSBufferIN->hPMR,
							   PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
		if(psRGXCreateZSBufferOUT->eError != PVRSRV_OK)
		{
			goto RGXCreateZSBuffer_exit;
		}

		/* Look up the data from the resman address */
		psRGXCreateZSBufferOUT->eError = ResManFindPrivateDataByPtr(hPMRInt2, (IMG_VOID **) &psPMRInt);
		if(psRGXCreateZSBufferOUT->eError != PVRSRV_OK)
		{
			goto RGXCreateZSBuffer_exit;
		}

	psRGXCreateZSBufferOUT->eError =
		RGXCreateZSBufferKM(
					hDevNodeInt,
					psReservationInt,
					psPMRInt,
					psRGXCreateZSBufferIN->uiMapFlags,
					&pssZSBufferKMInt,
					&psRGXCreateZSBufferOUT->ui32sZSBufferFWDevVAddr);
	/* Exit early if bridged call fails */
	if(psRGXCreateZSBufferOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateZSBuffer_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hsZSBufferKMInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_RGX_FWIF_ZSBUFFER,
												pssZSBufferKMInt,
												/* FIXME: how can we avoid this cast? */
												(RESMAN_FREE_FN)&RGXDestroyZSBufferKM);
	if (hsZSBufferKMInt2 == IMG_NULL)
	{
		psRGXCreateZSBufferOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto RGXCreateZSBuffer_exit;
	}
	psRGXCreateZSBufferOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psRGXCreateZSBufferOUT->hsZSBufferKM,
							(IMG_HANDLE) hsZSBufferKMInt2,
							PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psRGXCreateZSBufferOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateZSBuffer_exit;
	}


RGXCreateZSBuffer_exit:
	if (psRGXCreateZSBufferOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hsZSBufferKMInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hsZSBufferKMInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (pssZSBufferKMInt)
		{
			RGXDestroyZSBufferKM(pssZSBufferKMInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeRGXDestroyZSBuffer(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXDESTROYZSBUFFER *psRGXDestroyZSBufferIN,
					 PVRSRV_BRIDGE_OUT_RGXDESTROYZSBUFFER *psRGXDestroyZSBufferOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hsZSBufferMemDescInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYZSBUFFER);




		/* Look up the address from the handle */
		psRGXDestroyZSBufferOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hsZSBufferMemDescInt2,
							   psRGXDestroyZSBufferIN->hsZSBufferMemDesc,
							   PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER);
		if(psRGXDestroyZSBufferOUT->eError != PVRSRV_OK)
		{
			goto RGXDestroyZSBuffer_exit;
		}

	psRGXDestroyZSBufferOUT->eError = RGXDestroyZSBufferResManProxy(hsZSBufferMemDescInt2);
	/* Exit early if bridged call fails */
	if(psRGXDestroyZSBufferOUT->eError != PVRSRV_OK)
	{
		goto RGXDestroyZSBuffer_exit;
	}

	psRGXDestroyZSBufferOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyZSBufferIN->hsZSBufferMemDesc,
					PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER);


RGXDestroyZSBuffer_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXPopulateZSBuffer(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXPOPULATEZSBUFFER *psRGXPopulateZSBufferIN,
					 PVRSRV_BRIDGE_OUT_RGXPOPULATEZSBUFFER *psRGXPopulateZSBufferOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_ZSBUFFER_DATA * pssZSBufferKMInt = IMG_NULL;
	IMG_HANDLE hsZSBufferKMInt2 = IMG_NULL;
	RGX_POPULATION * pssPopulationInt = IMG_NULL;
	IMG_HANDLE hsPopulationInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXTA3D_RGXPOPULATEZSBUFFER);




		/* Look up the address from the handle */
		psRGXPopulateZSBufferOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hsZSBufferKMInt2,
							   psRGXPopulateZSBufferIN->hsZSBufferKM,
							   PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER);
		if(psRGXPopulateZSBufferOUT->eError != PVRSRV_OK)
		{
			goto RGXPopulateZSBuffer_exit;
		}

		/* Look up the data from the resman address */
		psRGXPopulateZSBufferOUT->eError = ResManFindPrivateDataByPtr(hsZSBufferKMInt2, (IMG_VOID **) &pssZSBufferKMInt);
		if(psRGXPopulateZSBufferOUT->eError != PVRSRV_OK)
		{
			goto RGXPopulateZSBuffer_exit;
		}

	psRGXPopulateZSBufferOUT->eError =
		RGXPopulateZSBufferKM(
					pssZSBufferKMInt,
					&pssPopulationInt);
	/* Exit early if bridged call fails */
	if(psRGXPopulateZSBufferOUT->eError != PVRSRV_OK)
	{
		goto RGXPopulateZSBuffer_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hsPopulationInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_RGX_POPULATION,
												pssPopulationInt,
												/* FIXME: how can we avoid this cast? */
												(RESMAN_FREE_FN)&RGXUnpopulateZSBufferKM);
	if (hsPopulationInt2 == IMG_NULL)
	{
		psRGXPopulateZSBufferOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto RGXPopulateZSBuffer_exit;
	}
	psRGXPopulateZSBufferOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psRGXPopulateZSBufferOUT->hsPopulation,
							(IMG_HANDLE) hsPopulationInt2,
							PVRSRV_HANDLE_TYPE_RGX_POPULATION,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psRGXPopulateZSBufferOUT->eError != PVRSRV_OK)
	{
		goto RGXPopulateZSBuffer_exit;
	}


RGXPopulateZSBuffer_exit:
	if (psRGXPopulateZSBufferOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hsPopulationInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hsPopulationInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (pssPopulationInt)
		{
			RGXUnpopulateZSBufferKM(pssPopulationInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeRGXUnpopulateZSBuffer(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXUNPOPULATEZSBUFFER *psRGXUnpopulateZSBufferIN,
					 PVRSRV_BRIDGE_OUT_RGXUNPOPULATEZSBUFFER *psRGXUnpopulateZSBufferOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hsPopulationInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXTA3D_RGXUNPOPULATEZSBUFFER);




		/* Look up the address from the handle */
		psRGXUnpopulateZSBufferOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hsPopulationInt2,
							   psRGXUnpopulateZSBufferIN->hsPopulation,
							   PVRSRV_HANDLE_TYPE_RGX_POPULATION);
		if(psRGXUnpopulateZSBufferOUT->eError != PVRSRV_OK)
		{
			goto RGXUnpopulateZSBuffer_exit;
		}

	psRGXUnpopulateZSBufferOUT->eError = RGXUnpopulateZSBufferResManProxy(hsPopulationInt2);
	/* Exit early if bridged call fails */
	if(psRGXUnpopulateZSBufferOUT->eError != PVRSRV_OK)
	{
		goto RGXUnpopulateZSBuffer_exit;
	}

	psRGXUnpopulateZSBufferOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXUnpopulateZSBufferIN->hsPopulation,
					PVRSRV_HANDLE_TYPE_RGX_POPULATION);


RGXUnpopulateZSBuffer_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXCreateFreeList(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXCREATEFREELIST *psRGXCreateFreeListIN,
					 PVRSRV_BRIDGE_OUT_RGXCREATEFREELIST *psRGXCreateFreeListOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	PMR * pssFreeListPMRInt = IMG_NULL;
	IMG_HANDLE hsFreeListPMRInt2 = IMG_NULL;
	RGX_FREELIST * psCleanupCookieInt = IMG_NULL;
	IMG_HANDLE hCleanupCookieInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXTA3D_RGXCREATEFREELIST);




		/* Look up the address from the handle */
		psRGXCreateFreeListOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hDevNodeInt,
							   psRGXCreateFreeListIN->hDevNode,
							   PVRSRV_HANDLE_TYPE_DEV_NODE);
		if(psRGXCreateFreeListOUT->eError != PVRSRV_OK)
		{
			goto RGXCreateFreeList_exit;
		}
		/* Look up the address from the handle */
		psRGXCreateFreeListOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hsFreeListPMRInt2,
							   psRGXCreateFreeListIN->hsFreeListPMR,
							   PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
		if(psRGXCreateFreeListOUT->eError != PVRSRV_OK)
		{
			goto RGXCreateFreeList_exit;
		}

		/* Look up the data from the resman address */
		psRGXCreateFreeListOUT->eError = ResManFindPrivateDataByPtr(hsFreeListPMRInt2, (IMG_VOID **) &pssFreeListPMRInt);
		if(psRGXCreateFreeListOUT->eError != PVRSRV_OK)
		{
			goto RGXCreateFreeList_exit;
		}

	psRGXCreateFreeListOUT->eError =
		RGXCreateFreeList(
					hDevNodeInt,
					psRGXCreateFreeListIN->ui32ui32MaxFLPages,
					psRGXCreateFreeListIN->ui32ui32InitFLPages,
					psRGXCreateFreeListIN->ui32ui32GrowFLPages,
					psRGXCreateFreeListIN->bbFreeListCheck,
					psRGXCreateFreeListIN->spsFreeListDevVAddr,
					pssFreeListPMRInt,
					psRGXCreateFreeListIN->uiPMROffset,
					&psCleanupCookieInt);
	/* Exit early if bridged call fails */
	if(psRGXCreateFreeListOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateFreeList_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hCleanupCookieInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_RGX_FWIF_FREELIST,
												psCleanupCookieInt,
												/* FIXME: how can we avoid this cast? */
												(RESMAN_FREE_FN)&RGXDestroyFreeList);
	if (hCleanupCookieInt2 == IMG_NULL)
	{
		psRGXCreateFreeListOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto RGXCreateFreeList_exit;
	}
	psRGXCreateFreeListOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psRGXCreateFreeListOUT->hCleanupCookie,
							(IMG_HANDLE) hCleanupCookieInt2,
							PVRSRV_HANDLE_TYPE_RGX_FREELIST,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psRGXCreateFreeListOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateFreeList_exit;
	}


RGXCreateFreeList_exit:
	if (psRGXCreateFreeListOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hCleanupCookieInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hCleanupCookieInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psCleanupCookieInt)
		{
			RGXDestroyFreeList(psCleanupCookieInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeRGXDestroyFreeList(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXDESTROYFREELIST *psRGXDestroyFreeListIN,
					 PVRSRV_BRIDGE_OUT_RGXDESTROYFREELIST *psRGXDestroyFreeListOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hCleanupCookieInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYFREELIST);




		/* Look up the address from the handle */
		psRGXDestroyFreeListOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hCleanupCookieInt2,
							   psRGXDestroyFreeListIN->hCleanupCookie,
							   PVRSRV_HANDLE_TYPE_RGX_FREELIST);
		if(psRGXDestroyFreeListOUT->eError != PVRSRV_OK)
		{
			goto RGXDestroyFreeList_exit;
		}

	psRGXDestroyFreeListOUT->eError = RGXDestroyFreeListResManProxy(hCleanupCookieInt2);
	/* Exit early if bridged call fails */
	if(psRGXDestroyFreeListOUT->eError != PVRSRV_OK)
	{
		goto RGXDestroyFreeList_exit;
	}

	psRGXDestroyFreeListOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyFreeListIN->hCleanupCookie,
					PVRSRV_HANDLE_TYPE_RGX_FREELIST);


RGXDestroyFreeList_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXAddBlockToFreeList(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXADDBLOCKTOFREELIST *psRGXAddBlockToFreeListIN,
					 PVRSRV_BRIDGE_OUT_RGXADDBLOCKTOFREELIST *psRGXAddBlockToFreeListOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_FREELIST * pssFreeListInt = IMG_NULL;
	IMG_HANDLE hsFreeListInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXTA3D_RGXADDBLOCKTOFREELIST);




		/* Look up the address from the handle */
		psRGXAddBlockToFreeListOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hsFreeListInt2,
							   psRGXAddBlockToFreeListIN->hsFreeList,
							   PVRSRV_HANDLE_TYPE_RGX_FREELIST);
		if(psRGXAddBlockToFreeListOUT->eError != PVRSRV_OK)
		{
			goto RGXAddBlockToFreeList_exit;
		}

		/* Look up the data from the resman address */
		psRGXAddBlockToFreeListOUT->eError = ResManFindPrivateDataByPtr(hsFreeListInt2, (IMG_VOID **) &pssFreeListInt);
		if(psRGXAddBlockToFreeListOUT->eError != PVRSRV_OK)
		{
			goto RGXAddBlockToFreeList_exit;
		}

	psRGXAddBlockToFreeListOUT->eError =
		RGXAddBlockToFreeListKM(
					pssFreeListInt,
					psRGXAddBlockToFreeListIN->ui3232NumPages);



RGXAddBlockToFreeList_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXRemoveBlockFromFreeList(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXREMOVEBLOCKFROMFREELIST *psRGXRemoveBlockFromFreeListIN,
					 PVRSRV_BRIDGE_OUT_RGXREMOVEBLOCKFROMFREELIST *psRGXRemoveBlockFromFreeListOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_FREELIST * pssFreeListInt = IMG_NULL;
	IMG_HANDLE hsFreeListInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXTA3D_RGXREMOVEBLOCKFROMFREELIST);




		/* Look up the address from the handle */
		psRGXRemoveBlockFromFreeListOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hsFreeListInt2,
							   psRGXRemoveBlockFromFreeListIN->hsFreeList,
							   PVRSRV_HANDLE_TYPE_RGX_FREELIST);
		if(psRGXRemoveBlockFromFreeListOUT->eError != PVRSRV_OK)
		{
			goto RGXRemoveBlockFromFreeList_exit;
		}

		/* Look up the data from the resman address */
		psRGXRemoveBlockFromFreeListOUT->eError = ResManFindPrivateDataByPtr(hsFreeListInt2, (IMG_VOID **) &pssFreeListInt);
		if(psRGXRemoveBlockFromFreeListOUT->eError != PVRSRV_OK)
		{
			goto RGXRemoveBlockFromFreeList_exit;
		}

	psRGXRemoveBlockFromFreeListOUT->eError =
		RGXRemoveBlockFromFreeListKM(
					pssFreeListInt);



RGXRemoveBlockFromFreeList_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXCreateRenderContext(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXCREATERENDERCONTEXT *psRGXCreateRenderContextIN,
					 PVRSRV_BRIDGE_OUT_RGXCREATERENDERCONTEXT *psRGXCreateRenderContextOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	DEVMEM_MEMDESC * psTACCBMemDescInt = IMG_NULL;
	DEVMEM_MEMDESC * psTACCBCtlMemDescInt = IMG_NULL;
	DEVMEM_MEMDESC * ps3DCCBMemDescInt = IMG_NULL;
	DEVMEM_MEMDESC * ps3DCCBCtlMemDescInt = IMG_NULL;
	RGX_RC_CLEANUP_DATA * psCleanupCookieInt = IMG_NULL;
	IMG_HANDLE hCleanupCookieInt2 = IMG_NULL;
	DEVMEM_MEMDESC * psFWRenderContextInt = IMG_NULL;
	DEVMEM_MEMDESC * psFW3DContextStateInt = IMG_NULL;
	IMG_BYTE *psFrameworkCmdInt = IMG_NULL;
	IMG_HANDLE hPrivDataInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXTA3D_RGXCREATERENDERCONTEXT);



	psRGXCreateRenderContextOUT->hCleanupCookie = IMG_NULL;

	psFrameworkCmdInt = OSAllocMem(RGXFWIF_RF_CMD_SIZE * sizeof(IMG_BYTE));
	if (!psFrameworkCmdInt)
	{
		psRGXCreateRenderContextOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;

		goto RGXCreateRenderContext_exit;
	}


	if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXCreateRenderContextIN->psFrameworkCmd, RGXFWIF_RF_CMD_SIZE * sizeof(IMG_BYTE)) 
		|| (OSCopyFromUser(NULL, psFrameworkCmdInt, psRGXCreateRenderContextIN->psFrameworkCmd,
		RGXFWIF_RF_CMD_SIZE * sizeof(IMG_BYTE)) != PVRSRV_OK) )
	{
		psRGXCreateRenderContextOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

		goto RGXCreateRenderContext_exit;
	}

		/* Look up the address from the handle */
		psRGXCreateRenderContextOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hDevNodeInt,
							   psRGXCreateRenderContextIN->hDevNode,
							   PVRSRV_HANDLE_TYPE_DEV_NODE);
		if(psRGXCreateRenderContextOUT->eError != PVRSRV_OK)
		{
			goto RGXCreateRenderContext_exit;
		}
		/* Look up the address from the handle */
		psRGXCreateRenderContextOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &psTACCBMemDescInt,
							   psRGXCreateRenderContextIN->hTACCBMemDesc,
							   PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC);
		if(psRGXCreateRenderContextOUT->eError != PVRSRV_OK)
		{
			goto RGXCreateRenderContext_exit;
		}
		/* Look up the address from the handle */
		psRGXCreateRenderContextOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &psTACCBCtlMemDescInt,
							   psRGXCreateRenderContextIN->hTACCBCtlMemDesc,
							   PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC);
		if(psRGXCreateRenderContextOUT->eError != PVRSRV_OK)
		{
			goto RGXCreateRenderContext_exit;
		}
		/* Look up the address from the handle */
		psRGXCreateRenderContextOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &ps3DCCBMemDescInt,
							   psRGXCreateRenderContextIN->h3DCCBMemDesc,
							   PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC);
		if(psRGXCreateRenderContextOUT->eError != PVRSRV_OK)
		{
			goto RGXCreateRenderContext_exit;
		}
		/* Look up the address from the handle */
		psRGXCreateRenderContextOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &ps3DCCBCtlMemDescInt,
							   psRGXCreateRenderContextIN->h3DCCBCtlMemDesc,
							   PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC);
		if(psRGXCreateRenderContextOUT->eError != PVRSRV_OK)
		{
			goto RGXCreateRenderContext_exit;
		}
		/* Look up the address from the handle */
		psRGXCreateRenderContextOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hPrivDataInt,
							   psRGXCreateRenderContextIN->hPrivData,
							   PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA);
		if(psRGXCreateRenderContextOUT->eError != PVRSRV_OK)
		{
			goto RGXCreateRenderContext_exit;
		}

	psRGXCreateRenderContextOUT->eError =
		PVRSRVRGXCreateRenderContextKM(
					hDevNodeInt,
					psTACCBMemDescInt,
					psTACCBCtlMemDescInt,
					ps3DCCBMemDescInt,
					ps3DCCBCtlMemDescInt,
					&psCleanupCookieInt,
					&psFWRenderContextInt,
					&psFW3DContextStateInt,
					psRGXCreateRenderContextIN->ui32Priority,
					psRGXCreateRenderContextIN->sMCUFenceAddr,
					psRGXCreateRenderContextIN->sVDMCallStackAddr,
					psRGXCreateRenderContextIN->ui32FrameworkCmdize,
					psFrameworkCmdInt,
					hPrivDataInt);
	/* Exit early if bridged call fails */
	if(psRGXCreateRenderContextOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateRenderContext_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hCleanupCookieInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_RGX_RENDER_CONTEXT,
												psCleanupCookieInt,
												/* FIXME: how can we avoid this cast? */
												(RESMAN_FREE_FN)&PVRSRVRGXDestroyRenderContextKM);
	if (hCleanupCookieInt2 == IMG_NULL)
	{
		psRGXCreateRenderContextOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto RGXCreateRenderContext_exit;
	}
	psRGXCreateRenderContextOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psRGXCreateRenderContextOUT->hCleanupCookie,
							(IMG_HANDLE) hCleanupCookieInt2,
							PVRSRV_HANDLE_TYPE_RGX_RC_CLEANUP,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psRGXCreateRenderContextOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateRenderContext_exit;
	}
	psRGXCreateRenderContextOUT->eError = PVRSRVAllocSubHandle(psConnection->psHandleBase,
							&psRGXCreateRenderContextOUT->hFWRenderContext,
							(IMG_HANDLE) psFWRenderContextInt,
							PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,psRGXCreateRenderContextOUT->hCleanupCookie);
	if (psRGXCreateRenderContextOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateRenderContext_exit;
	}
	psRGXCreateRenderContextOUT->eError = PVRSRVAllocSubHandle(psConnection->psHandleBase,
							&psRGXCreateRenderContextOUT->hFW3DContextState,
							(IMG_HANDLE) psFW3DContextStateInt,
							PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,psRGXCreateRenderContextOUT->hCleanupCookie);
	if (psRGXCreateRenderContextOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateRenderContext_exit;
	}


RGXCreateRenderContext_exit:
	if (psRGXCreateRenderContextOUT->eError != PVRSRV_OK)
	{
		if (psRGXCreateRenderContextOUT->hCleanupCookie)
		{
			PVRSRVReleaseHandle(psConnection->psHandleBase,
						(IMG_HANDLE) psRGXCreateRenderContextOUT->hCleanupCookie,
						PVRSRV_HANDLE_TYPE_RGX_RC_CLEANUP);
		}

		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hCleanupCookieInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hCleanupCookieInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psCleanupCookieInt)
		{
			PVRSRVRGXDestroyRenderContextKM(psCleanupCookieInt);
		}
	}

	if (psFrameworkCmdInt)
		OSFreeMem(psFrameworkCmdInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXDestroyRenderContext(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXDESTROYRENDERCONTEXT *psRGXDestroyRenderContextIN,
					 PVRSRV_BRIDGE_OUT_RGXDESTROYRENDERCONTEXT *psRGXDestroyRenderContextOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hCleanupCookieInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYRENDERCONTEXT);




		/* Look up the address from the handle */
		psRGXDestroyRenderContextOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hCleanupCookieInt2,
							   psRGXDestroyRenderContextIN->hCleanupCookie,
							   PVRSRV_HANDLE_TYPE_RGX_RC_CLEANUP);
		if(psRGXDestroyRenderContextOUT->eError != PVRSRV_OK)
		{
			goto RGXDestroyRenderContext_exit;
		}

	psRGXDestroyRenderContextOUT->eError = RGXDestroyRenderContextResManProxy(hCleanupCookieInt2);
	/* Exit early if bridged call fails */
	if(psRGXDestroyRenderContextOUT->eError != PVRSRV_OK)
	{
		goto RGXDestroyRenderContext_exit;
	}

	psRGXDestroyRenderContextOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyRenderContextIN->hCleanupCookie,
					PVRSRV_HANDLE_TYPE_RGX_RC_CLEANUP);


RGXDestroyRenderContext_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXKickTA3D(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXKICKTA3D *psRGXKickTA3DIN,
					 PVRSRV_BRIDGE_OUT_RGXKICKTA3D *psRGXKickTA3DOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	DEVMEM_MEMDESC * psFWRenderContextInt = IMG_NULL;
	RGX_RTDATA_CLEANUP_DATA * psRTDataCleanupInt = IMG_NULL;
	IMG_HANDLE hRTDataCleanupInt2 = IMG_NULL;
	RGX_ZSBUFFER_DATA * psZBufferInt = IMG_NULL;
	IMG_HANDLE hZBufferInt2 = IMG_NULL;
	RGX_ZSBUFFER_DATA * psSBufferInt = IMG_NULL;
	IMG_HANDLE hSBufferInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXTA3D_RGXKICKTA3D);




		/* Look up the address from the handle */
		psRGXKickTA3DOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hDevNodeInt,
							   psRGXKickTA3DIN->hDevNode,
							   PVRSRV_HANDLE_TYPE_DEV_NODE);
		if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
		{
			goto RGXKickTA3D_exit;
		}
		/* Look up the address from the handle */
		psRGXKickTA3DOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &psFWRenderContextInt,
							   psRGXKickTA3DIN->hFWRenderContext,
							   PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC);
		if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
		{
			goto RGXKickTA3D_exit;
		}
	if(psRGXKickTA3DIN->hRTDataCleanup)
	{
		/* Look up the address from the handle */
		psRGXKickTA3DOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hRTDataCleanupInt2,
							   psRGXKickTA3DIN->hRTDataCleanup,
							   PVRSRV_HANDLE_TYPE_RGX_RTDATA_CLEANUP);
		if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
		{
			goto RGXKickTA3D_exit;
		}

		/* Look up the data from the resman address */
		psRGXKickTA3DOUT->eError = ResManFindPrivateDataByPtr(hRTDataCleanupInt2, (IMG_VOID **) &psRTDataCleanupInt);
		if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
		{
			goto RGXKickTA3D_exit;
		}
	}
	if(psRGXKickTA3DIN->hZBuffer)
	{
		/* Look up the address from the handle */
		psRGXKickTA3DOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hZBufferInt2,
							   psRGXKickTA3DIN->hZBuffer,
							   PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER);
		if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
		{
			goto RGXKickTA3D_exit;
		}

		/* Look up the data from the resman address */
		psRGXKickTA3DOUT->eError = ResManFindPrivateDataByPtr(hZBufferInt2, (IMG_VOID **) &psZBufferInt);
		if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
		{
			goto RGXKickTA3D_exit;
		}
	}
	if(psRGXKickTA3DIN->hSBuffer)
	{
		/* Look up the address from the handle */
		psRGXKickTA3DOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
							   (IMG_HANDLE *) &hSBufferInt2,
							   psRGXKickTA3DIN->hSBuffer,
							   PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER);
		if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
		{
			goto RGXKickTA3D_exit;
		}

		/* Look up the data from the resman address */
		psRGXKickTA3DOUT->eError = ResManFindPrivateDataByPtr(hSBufferInt2, (IMG_VOID **) &psSBufferInt);
		if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
		{
			goto RGXKickTA3D_exit;
		}
	}

	psRGXKickTA3DOUT->eError =
		PVRSRVRGXKickTA3DKM(
					hDevNodeInt,
					psFWRenderContextInt,
					psRGXKickTA3DIN->bbLastTAInScene,
					psRGXKickTA3DIN->bbKickTA,
					psRGXKickTA3DIN->bbKickPR,
					psRGXKickTA3DIN->bbKick3D,
					psRGXKickTA3DIN->ui32TAcCCBWoffUpdate,
					psRGXKickTA3DIN->ui323DcCCBWoffUpdate,
					psRGXKickTA3DIN->bbPDumpContinuous,
					psRTDataCleanupInt,
					psZBufferInt,
					psSBufferInt);



RGXKickTA3D_exit:

	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */
 
PVRSRV_ERROR RegisterRGXTA3DFunctions(IMG_VOID);
IMG_VOID UnregisterRGXTA3DFunctions(IMG_VOID);

/*
 * Register all RGXTA3D functions with services
 */
PVRSRV_ERROR RegisterRGXTA3DFunctions(IMG_VOID)
{
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D_RGXCREATEHWRTDATA, PVRSRVBridgeRGXCreateHWRTData);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYHWRTDATA, PVRSRVBridgeRGXDestroyHWRTData);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D_RGXCREATERENDERTARGET, PVRSRVBridgeRGXCreateRenderTarget);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYRENDERTARGET, PVRSRVBridgeRGXDestroyRenderTarget);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D_RGXCREATEZSBUFFER, PVRSRVBridgeRGXCreateZSBuffer);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYZSBUFFER, PVRSRVBridgeRGXDestroyZSBuffer);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D_RGXPOPULATEZSBUFFER, PVRSRVBridgeRGXPopulateZSBuffer);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D_RGXUNPOPULATEZSBUFFER, PVRSRVBridgeRGXUnpopulateZSBuffer);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D_RGXCREATEFREELIST, PVRSRVBridgeRGXCreateFreeList);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYFREELIST, PVRSRVBridgeRGXDestroyFreeList);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D_RGXADDBLOCKTOFREELIST, PVRSRVBridgeRGXAddBlockToFreeList);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D_RGXREMOVEBLOCKFROMFREELIST, PVRSRVBridgeRGXRemoveBlockFromFreeList);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D_RGXCREATERENDERCONTEXT, PVRSRVBridgeRGXCreateRenderContext);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYRENDERCONTEXT, PVRSRVBridgeRGXDestroyRenderContext);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D_RGXKICKTA3D, PVRSRVBridgeRGXKickTA3D);

	return PVRSRV_OK;
}

/*
 * Unregister all rgxta3d functions with services
 */
IMG_VOID UnregisterRGXTA3DFunctions(IMG_VOID)
{
}
