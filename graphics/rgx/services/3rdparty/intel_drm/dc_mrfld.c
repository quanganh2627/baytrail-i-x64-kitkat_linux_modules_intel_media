/**********************************************************************
 *
 * Copyright(c) 2008 Imagination Technologies Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful but, except
 * as otherwise stated in writing, without any warranty; without even the
 * implied warranty of merchantability or fitness for a particular purpose.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Imagination Technologies Ltd. <gpl-support@imgtec.com>
 * Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK
 *
 ******************************************************************************/
#include "img_defs.h"
#include "servicesext.h"
#include "allocmem.h"
#include "kerneldisplay.h"
#include "mm.h"
#include "pvrsrv_error.h"
#include "display_callbacks.h"
#include "dc_server.h"
#include "dc_mrfld.h"
#include "pwr_mgmt.h"

#if !defined(SUPPORT_DRM)
#error "SUPPORT_DRM must be set"
#endif

#define KEEP_UNUSED_CODE 0

static DC_MRFLD_DEVICE *gpsDevice;

#define	 DRVNAME "Merrifield-DRM"

/* GPU asks for 32 pixels of width alignment */
#define DC_MRFLD_WIDTH_ALIGN 32
#define DC_MRFLD_WIDTH_ALIGN_MASK (DC_MRFLD_WIDTH_ALIGN - 1)

/*DC plane asks for 64 bytes alignment*/
#define DC_MRFLD_STRIDE_ALIGN 64
#define DC_MRFLD_STRIDE_ALIGN_MASK (DC_MRFLD_STRIDE_ALIGN - 1)

static IMG_PIXFMT DC_MRFLD_Supported_PixelFormats[] = {
	/*supported RGB formats*/
	IMG_PIXFMT_B8G8R8A8_UNORM,
	IMG_PIXFMT_B5G6R5_UNORM,

	/*supported YUV formats*/
	IMG_PIXFMT_YUV420_2PLANE,
};

static uint32_t DC_MRFLD_PixelFormat_Mapping[] = {
		[IMG_PIXFMT_B5G6R5_UNORM] = (0x5 << 26),
		[IMG_PIXFMT_B8G8R8A8_UNORM] = (0x6 << 26),
};

static inline IMG_UINT32 _Align_To(IMG_UINT32 ulValue,
				IMG_UINT32 ulAlignment)
{
	return (ulValue + ulAlignment - 1) & ~(ulAlignment - 1);
}

static IMG_BOOL _Is_Valid_PixelFormat(IMG_PIXFMT ePixelFormat)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(DC_MRFLD_Supported_PixelFormats); i++) {
		if (ePixelFormat == DC_MRFLD_Supported_PixelFormats[i])
			return IMG_TRUE;
	}

	return IMG_FALSE;
}

#if KEEP_UNUSED_CODE
/*
 * NOTE: only use the 1st plane now.
 */
static IMG_BOOL _Is_Valid_DC_Buffer(DC_BUFFER_IMPORT_INFO *psBufferInfo)
{
	if (!psBufferInfo)
		return IMG_FALSE;

	/*common check*/
	if (!psBufferInfo->ui32BPP || !psBufferInfo->ui32ByteStride[0] ||
		!psBufferInfo->ui32Height[0] || !psBufferInfo->ui32Width[0])
		return IMG_FALSE;

	/*check format*/
	if (!_Is_Valid_PixelFormat(psBufferInfo->ePixFormat))
		return IMG_FALSE;

	/*check stride*/
	if (psBufferInfo->ui32ByteStride[0] & DC_MRFLD_STRIDE_ALIGN_MASK)
		return IMG_FALSE;

	return IMG_TRUE;
}
#endif /* if KEEP_UNUSED_CODE */

static void _Flip_To_Surface(DC_MRFLD_DEVICE *psDevice,
				IMG_UINT32 ulSurfAddr,
				IMG_PIXFMT eFormat,
				IMG_UINT32 ulStride,
				IMG_INT iPipe)
{
	struct drm_device *psDrmDev = psDevice->psDrmDevice;
	uint32_t format = DC_MRFLD_PixelFormat_Mapping[eFormat];
	DCCBFlipToSurface(psDrmDev, ulSurfAddr, format, ulStride, iPipe);
}

static void _Flip_Overlay(DC_MRFLD_DEVICE *psDevice,
			DC_MRFLD_OVERLAY_CONTEXT *psContext,
			IMG_INT iPipe)
{
	if ((iPipe && psContext->pipe) || (!iPipe && !psContext->pipe))
		DCCBFlipOverlay(psDevice->psDrmDevice, psContext);
}

static void _Flip_Sprite(DC_MRFLD_DEVICE *psDevice,
			DC_MRFLD_SPRITE_CONTEXT *psContext,
			IMG_INT iPipe)
{
	int index = psContext->index;

	/* don't flip if plane is inactive */
	if (!((1 << index) & psDevice->ui32ActiveSprites))
		return;

	if ((iPipe && psContext->pipe) || (!iPipe && !psContext->pipe))
		DCCBFlipSprite(psDevice->psDrmDevice, psContext);
}

static void _Flip_Primary(DC_MRFLD_DEVICE *psDevice,
			DC_MRFLD_PRIMARY_CONTEXT *psContext,
			IMG_INT iPipe)
{
	if ((iPipe && psContext->pipe) || (!iPipe && !psContext->pipe))
		DCCBFlipPrimary(psDevice->psDrmDevice, psContext);
}

static void _Do_Flip(DC_MRFLD_FLIP *psFlip, int iPipe)
{
	DC_MRFLD_SURF_CUSTOM *psSurfCustom;
	DC_MRFLD_BUFFER *pasBuffers;
	IMG_UINT32 uiNumBuffers;
	IMG_UINT32 ulAddr;
	IMG_PIXFMT eFormat;
	IMG_UINT32 ulStride;
	int i, j;

	if (!gpsDevice || !psFlip) {
		DRM_ERROR("%s: Invalid Flip\n", __func__);
		return;
	}

	if (iPipe != DC_PIPE_A && iPipe != DC_PIPE_B) {
		DRM_ERROR("%s: Invalid pipe %d\n", __func__, iPipe);
		return;
	}

	/* skip it if updated*/
	if (psFlip->eFlipStates[iPipe] == DC_MRFLD_FLIP_DC_UPDATED)
		return;

	pasBuffers = psFlip->asBuffers;
	uiNumBuffers = psFlip->uiNumBuffers;

	if (!pasBuffers || !uiNumBuffers) {
		DRM_ERROR("%s: Invalid buffer list\n", __func__);
		return;
	}

	/*turn on required power islands*/
	if (!power_island_get(psFlip->uiPowerIslands))
		return;

	for (i = 0; i < uiNumBuffers; i++) {
		if (pasBuffers[i].eFlipOp == DC_MRFLD_FLIP_SURFACE) {
			/*No context attach just flip the primary surface*/
			ulAddr = pasBuffers[i].sDevVAddr.uiAddr;
			eFormat = pasBuffers[i].ePixFormat;
			ulStride = pasBuffers[i].ui32ByteStride;
			_Flip_To_Surface(gpsDevice, ulAddr, eFormat, ulStride,
					iPipe);
			continue;
		}

		if (pasBuffers[i].eFlipOp != DC_MRFLD_FLIP_CONTEXT) {
			DRM_ERROR("%s: bad flip operation %d\n", __func__,
					pasBuffers[i].eFlipOp);
			continue;
		}

		for (j = 0; j < pasBuffers[i].ui32ContextCount; j++) {
			psSurfCustom = &pasBuffers[i].sContext[j];
			switch (psSurfCustom->type) {
			case DC_SPRITE_PLANE:
				/*Flip sprite context*/
				_Flip_Sprite(gpsDevice,
						&psSurfCustom->ctx.sp_ctx,
						iPipe);
				break;
			case DC_PRIMARY_PLANE:
				/*need fix up the surface address*/
				if (!psSurfCustom->ctx.prim_ctx.surf)
					psSurfCustom->ctx.prim_ctx.surf =
						pasBuffers[i].sDevVAddr.uiAddr;

				/*Flip primary context*/
				_Flip_Primary(gpsDevice,
						&psSurfCustom->ctx.prim_ctx,
						iPipe);
				break;
			case DC_OVERLAY_PLANE:
				/*Flip overlay context*/
				_Flip_Overlay(gpsDevice,
						&psSurfCustom->ctx.ov_ctx,
						iPipe);
				break;
			default:
				DRM_ERROR("Unknown plane type %d\n",
						psSurfCustom->type);
			}
		}
	}

	if (!iPipe)
		DCCBUpdateDbiPanel(gpsDevice->psDrmDevice);

	/*change the flip state to DC_UPDATED*/
	psFlip->eFlipStates[iPipe] = DC_MRFLD_FLIP_DC_UPDATED;

	gpsDevice->psLastFlip = psFlip;

	power_island_put(psFlip->uiPowerIslands);
}

static DC_MRFLD_FLIP *_Next_Queued_Flip(int iPipe)
{
	struct list_head *psFlipQueue;
	DC_MRFLD_FLIP *psFlip, *psTmp;
	DC_MRFLD_FLIP *psQueuedFlip = 0;

	if (iPipe != DC_PIPE_A && iPipe != DC_PIPE_B) {
		DRM_ERROR("%s: Invalid pipe %d\n", __func__, iPipe);
		return IMG_NULL;
	}

	psFlipQueue = &gpsDevice->sFlipQueues[iPipe];

	list_for_each_entry_safe(psFlip, psTmp, psFlipQueue, sFlips[iPipe])
	{
		if (psFlip->eFlipStates[iPipe] == DC_MRFLD_FLIP_QUEUED) {
			psQueuedFlip = psFlip;
			break;
		}
	}

	return psQueuedFlip;
}

static void _Dispatch_Flip(DC_MRFLD_FLIP *psFlip)
{
	DC_MRFLD_SURF_CUSTOM *psSurfCustom;
	DC_MRFLD_BUFFER *pasBuffers;
	IMG_UINT32 uiNumBuffers;
	IMG_BOOL bActivePipes[MAX_PIPE_NUM];
	int i, j;

	if (!gpsDevice || !psFlip) {
		DRM_ERROR("%s: Invalid Flip\n", __func__);
		return;
	}

	pasBuffers = psFlip->asBuffers;
	uiNumBuffers = psFlip->uiNumBuffers;

	if (!pasBuffers || !uiNumBuffers) {
		DRM_ERROR("%s: Invalid buffer list\n", __func__);
		return;
	}

	for (i = 0; i < MAX_PIPE_NUM; i++)
		bActivePipes[i] = IMG_FALSE;

	for (i = 0; i < uiNumBuffers; i++) {
		if (pasBuffers[i].eFlipOp == DC_MRFLD_FLIP_SURFACE) {
			/* assign to pipe 0 by default*/
			bActivePipes[DC_PIPE_A] = IMG_TRUE;
			continue;
		}

		if (pasBuffers[i].eFlipOp != DC_MRFLD_FLIP_CONTEXT) {
			DRM_ERROR("%s: bad flip operation %d\n", __func__,
					pasBuffers[i].eFlipOp);
			continue;
		}

		for (j = 0; j < pasBuffers[i].ui32ContextCount; j++) {
			psSurfCustom = &pasBuffers[i].sContext[j];
			switch (psSurfCustom->type) {
			case DC_SPRITE_PLANE:
				/*Flip sprite context*/
				if (psSurfCustom->ctx.sp_ctx.pipe)
					bActivePipes[DC_PIPE_B] = IMG_TRUE;
				else
					bActivePipes[DC_PIPE_A] = IMG_TRUE;

				/*need turn on pipe C*/
				psFlip->uiPowerIslands |= OSPM_DISPLAY_C;
				break;
			case DC_PRIMARY_PLANE:
				if (psSurfCustom->ctx.prim_ctx.pipe)
					bActivePipes[DC_PIPE_B] = IMG_TRUE;
				else
					bActivePipes[DC_PIPE_A] = IMG_TRUE;
				break;
			case DC_OVERLAY_PLANE:
				if (psSurfCustom->ctx.ov_ctx.pipe)
					bActivePipes[DC_PIPE_B] = IMG_TRUE;
				else
					bActivePipes[DC_PIPE_A] = IMG_TRUE;
				/*if overlay C need turn on pipe C*/
				psFlip->uiPowerIslands |= OSPM_DISPLAY_C;
				break;
			default:
				DRM_ERROR("Unknown plane type %d\n",
						psSurfCustom->type);
			}
		}
	}

	mutex_lock(&gpsDevice->sFlipQueueLock);
	/* dispatch this flip*/
	for (i = 0; i < MAX_PIPE_NUM; i++) {
		if (bActivePipes[i]) {
			/* if pipe is not active */
			if (!DCCBIsPipeActive(gpsDevice->psDrmDevice, i))
				continue;
			/*turn on pipe power island*/
			if (i == 0)
				psFlip->uiPowerIslands |=  OSPM_DISPLAY_A;
			else
				psFlip->uiPowerIslands |=  OSPM_DISPLAY_B;

			/* if there's no pending queued flip, flip it*/
			if (!_Next_Queued_Flip(i))
				_Do_Flip(psFlip, i);

			INIT_LIST_HEAD(&psFlip->sFlips[i]);
			list_add_tail(&psFlip->sFlips[i],
					&gpsDevice->sFlipQueues[i]);
			/*increase refCount*/
			psFlip->uiRefCount++;
		}
	}

	/* if failed to dispatch, skip this flip*/
	if (!psFlip->uiRefCount) {
		DCDisplayConfigurationRetired(psFlip->hConfigData);
		/* free it */
		kfree(psFlip);
	}

	mutex_unlock(&gpsDevice->sFlipQueueLock);
}

static void _Queue_Flip(IMG_HANDLE hConfigData, IMG_HANDLE *ahBuffers,
			IMG_UINT32 uiNumBuffers, IMG_UINT32 ui32DisplayPeriod)
{
	IMG_UINT32 uiFlipSize;
	DC_MRFLD_BUFFER *psBuffer;
	DC_MRFLD_FLIP *psFlip;
	int i;

	uiFlipSize = sizeof(DC_MRFLD_FLIP);
	uiFlipSize += uiNumBuffers * sizeof(DC_MRFLD_BUFFER);

	psFlip = kzalloc(uiFlipSize , GFP_KERNEL);
	if (!gpsDevice || !psFlip) {
		DRM_ERROR("Failed to allocate a flip\n");
		/*force it to complete*/
		DCDisplayConfigurationRetired(hConfigData);
		return;
	}

	/*set flip state as queued*/
	for (i = 0; i < MAX_PIPE_NUM; i++)
		psFlip->eFlipStates[i] = DC_MRFLD_FLIP_QUEUED;

	/*update buffer number*/
	psFlip->uiNumBuffers = uiNumBuffers;

	/*initialize buffers*/
	for (i = 0; i < uiNumBuffers; i++) {
		psBuffer = ahBuffers[i];
		if (!psBuffer) {
			DRM_DEBUG("%s: buffer %d is empty!\n", __func__, i);
			continue;
		}

		memcpy(&psFlip->asBuffers[i], psBuffer, sizeof(*psBuffer));
	}

	for (i = 0; i < MAX_PIPE_NUM; i++)
		psFlip->asPipeInfo[i].uiSwapInterval = ui32DisplayPeriod;

	psFlip->hConfigData = hConfigData;

	/*queue it to flip queue*/
	_Dispatch_Flip(psFlip);
}

static int _Vsync_ISR(struct drm_device *psDrmDev, int iPipe)
{
	struct list_head *psFlipQueue;
	DC_MRFLD_FLIP *psFlip, *psTmp;
	DC_MRFLD_FLIP *psNextFlip;

	if (!gpsDevice)
		return IMG_TRUE;

	if (iPipe != DC_PIPE_A && iPipe != DC_PIPE_B)
		return IMG_FALSE;

	/* acquire flip queue mutex */
	mutex_lock(&gpsDevice->sFlipQueueLock);

	psFlipQueue = &gpsDevice->sFlipQueues[iPipe];

	/* complete the flips which has been DC_UPDATED */
	list_for_each_entry_safe(psFlip, psTmp, psFlipQueue, sFlips[iPipe])
	{
		if (psFlip->eFlipStates[iPipe] == DC_MRFLD_FLIP_DC_UPDATED) {
			/*remove this entry from flip queue, decrease refCount*/
			list_del(&psFlip->sFlips[iPipe]);

			if (!(--psFlip->uiRefCount)) {
				/*retire all buffers possessed by this flip*/
				DCDisplayConfigurationRetired(
						psFlip->hConfigData);
				/* free it */
				kfree(psFlip);
			}
		}
	}

	/*if flip queue isn't empty, flip the first flip*/
	if (list_empty_careful(psFlipQueue)) {
		INIT_LIST_HEAD(&gpsDevice->sFlipQueues[iPipe]);
		/*TODO: disable vblank interrupt on this pipe*/
		goto vsync_done;
	}

	/* get next queued flip */
	psNextFlip = _Next_Queued_Flip(iPipe);
	if (psNextFlip)
		_Do_Flip(psNextFlip, iPipe);
vsync_done:
	mutex_unlock(&gpsDevice->sFlipQueueLock);
	return IMG_TRUE;
}

/*----------------------------------------------------------------------------*/

static IMG_VOID DC_MRFLD_GetInfo(IMG_HANDLE hDeviceData,
				DC_DISPLAY_INFO *psDisplayInfo)
{
	DC_MRFLD_DEVICE *psDevice = (DC_MRFLD_DEVICE *)hDeviceData;

	DRM_DEBUG("%s\n", __func__);

	if (psDevice && psDisplayInfo)
		*psDisplayInfo = psDevice->sDisplayInfo;
}

static PVRSRV_ERROR DC_MRFLD_PanelQueryCount(IMG_HANDLE hDeviceData,
						IMG_UINT32 *ppui32NumPanels)
{
	DC_MRFLD_DEVICE *psDevice = (DC_MRFLD_DEVICE *)hDeviceData;
	if (!psDevice || !ppui32NumPanels)
		return PVRSRV_ERROR_INVALID_PARAMS;

	DRM_DEBUG("%s\n", __func__);

	*ppui32NumPanels = 1;
	return PVRSRV_OK;
}

static PVRSRV_ERROR DC_MRFLD_PanelQuery(IMG_HANDLE hDeviceData,
				IMG_UINT32 ui32PanelsArraySize,
				IMG_UINT32 *pui32NumPanels,
				PVRSRV_PANEL_INFO *pasPanelInfo)
{
	DC_MRFLD_DEVICE *psDevice = (DC_MRFLD_DEVICE *)hDeviceData;

	if (!psDevice || !pui32NumPanels || !pasPanelInfo)
		return PVRSRV_ERROR_INVALID_PARAMS;

	DRM_DEBUG("%s\n", __func__);

	*pui32NumPanels = 1;

	pasPanelInfo[0].sSurfaceInfo = psDevice->sPrimInfo;
	/*TODO: export real panel info*/
	/*pasPanelInfo[0].ui32RefreshRate = 60;*/
	/*pasPanelInfo[0].ui32PhysicalWidthmm = 0;*/
	/*pasPanelInfo[0].ui32PhysicalHeightmm = 0;*/

	return PVRSRV_OK;
}

static PVRSRV_ERROR DC_MRFLD_FormatQuery(IMG_HANDLE hDeviceData,
					IMG_UINT32 ui32NumFormats,
					PVRSRV_SURFACE_FORMAT *pasFormat,
					IMG_UINT32 *pui32Supported)
{
	DC_MRFLD_DEVICE *psDevice = (DC_MRFLD_DEVICE *)hDeviceData;
	int i;

	if (!psDevice || !pasFormat || !pui32Supported)
		return PVRSRV_ERROR_INVALID_PARAMS;

	DRM_DEBUG("%s\n", __func__);

	for (i = 0; i < ui32NumFormats; i++)
		pui32Supported[i] =
			_Is_Valid_PixelFormat(pasFormat[i].ePixFormat) ? 1 : 0;

	return PVRSRV_OK;
}

static PVRSRV_ERROR DC_MRFLD_DimQuery(IMG_HANDLE hDeviceData,
					IMG_UINT32 ui32NumDims,
					PVRSRV_SURFACE_DIMS *psDim,
					IMG_UINT32 *pui32Supported)
{
	DC_MRFLD_DEVICE *psDevice = (DC_MRFLD_DEVICE *)hDeviceData;
	int i;

	if (!psDevice || !psDim || !pui32Supported)
		return PVRSRV_ERROR_INVALID_PARAMS;

	DRM_DEBUG("%s\n", __func__);

	for (i = 0; i < ui32NumDims; i++) {
		pui32Supported[i] = 0;

		if (psDim[i].ui32Width != psDevice->sPrimInfo.sDims.ui32Width)
			continue;
		if (psDim[i].ui32Height != psDevice->sPrimInfo.sDims.ui32Height)
			continue;

		pui32Supported[i] = 1;
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR DC_MRFLD_BufferSystemAcquire(IMG_HANDLE hDeviceData,
					IMG_DEVMEM_LOG2ALIGN_T *puiLog2PageSize,
					IMG_UINT32 *pui32PageCount,
					IMG_UINT32 *pui32PhysHeapID,
					IMG_UINT32 *pui32ByteStride,
					IMG_HANDLE *phSystemBuffer)
{
	DC_MRFLD_DEVICE *psDevice = (DC_MRFLD_DEVICE *)hDeviceData;
	IMG_UINT32 ulPagesNumber, ulBufferSize;

	if (!psDevice || !puiLog2PageSize || !pui32PageCount ||
		!pui32PhysHeapID || !pui32ByteStride || !phSystemBuffer)
		return PVRSRV_ERROR_INVALID_PARAMS;

	DRM_DEBUG("%s\n", __func__);

	ulBufferSize = psDevice->psSystemBuffer->ui32BufferSize;
	ulPagesNumber = (ulBufferSize + (PAGE_SIZE - 1))  >> PAGE_SHIFT;

	*puiLog2PageSize = PAGE_SHIFT;
	*pui32PageCount = ulPagesNumber;
	*pui32PhysHeapID = 0;
	*pui32ByteStride = psDevice->psSystemBuffer->ui32ByteStride;
	*phSystemBuffer = (IMG_HANDLE)psDevice->psSystemBuffer;

	return PVRSRV_OK;
}

static IMG_VOID DC_MRFLD_BufferSystemRelease(IMG_HANDLE hSystemBuffer)
{
	/*TODO: do something here*/
}

static PVRSRV_ERROR DC_MRFLD_ContextCreate(IMG_HANDLE hDeviceData,
					IMG_HANDLE *hDisplayContext)
{
	DC_MRFLD_DISPLAY_CONTEXT *psDisplayContext = NULL;
	DC_MRFLD_DEVICE *psDevice;
	PVRSRV_ERROR eRes = PVRSRV_OK;

	if (!hDisplayContext)
		return PVRSRV_ERROR_INVALID_PARAMS;

	DRM_DEBUG("%s\n", __func__);

	psDevice = (DC_MRFLD_DEVICE *)hDeviceData;

	/*Allocate a new context*/
	psDisplayContext =
		kzalloc(sizeof(DC_MRFLD_DISPLAY_CONTEXT), GFP_KERNEL);
	if (!psDisplayContext) {
		DRM_ERROR("Failed to create display context\n");
		eRes = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto create_error;
	}

	psDisplayContext->psDevice = psDevice;
	*hDisplayContext = (IMG_HANDLE)psDisplayContext;
create_error:
	return eRes;
}

static PVRSRV_ERROR DC_MRFLD_ContextConfigureCheck(
				IMG_HANDLE hDisplayContext,
				IMG_UINT32 ui32PipeCount,
				PVRSRV_SURFACE_CONFIG_INFO *pasSurfAttrib,
				IMG_HANDLE *ahBuffers)
{
	DC_MRFLD_DISPLAY_CONTEXT *psDisplayContext =
		(DC_MRFLD_DISPLAY_CONTEXT *)hDisplayContext;
	DC_MRFLD_DEVICE *psDevice;
	DC_MRFLD_SURF_CUSTOM *psSurfCustom;
	DC_MRFLD_BUFFER *psBuffer;
	int err;
	int i;

	if (!psDisplayContext || !pasSurfAttrib || !ahBuffers)
		return PVRSRV_ERROR_INVALID_PARAMS;

	DRM_DEBUG("%s\n", __func__);

	psDevice = psDisplayContext->psDevice;

	/*TODO: handle ui32PipeCount = 0*/

	/* reset buffer context count*/
	for (i = 0; i < ui32PipeCount; i++) {
		psBuffer = (DC_MRFLD_BUFFER *) ahBuffers[i];
		if (psBuffer)
			psBuffer->ui32ContextCount = 0;
	}

	for (i = 0; i < ui32PipeCount; i++) {
		psBuffer = (DC_MRFLD_BUFFER *)ahBuffers[i];
		if (!psBuffer) {
			DRM_ERROR("%s: no buffer for layer %d\n", __func__, i);
			continue;
		}

		/*post flip*/
		if (!pasSurfAttrib[i].ui32Custom) {
			psBuffer->eFlipOp = DC_MRFLD_FLIP_SURFACE;
			continue;
		}

		/*check context count*/
		if (psBuffer->ui32ContextCount >= MAX_CONTEXT_COUNT) {
			DRM_ERROR("%s: plane context overflow\n", __func__);
			continue;
		}

		psSurfCustom =
			&psBuffer->sContext[psBuffer->ui32ContextCount++];

		/*copy the context from userspace*/
		err = copy_from_user(psSurfCustom,
				(void *)pasSurfAttrib[i].ui32Custom,
				sizeof(DC_MRFLD_SURF_CUSTOM));
		if (err) {
			DRM_ERROR("Failed to copy plane context\n");
			continue;
		}

		psBuffer->eFlipOp = DC_MRFLD_FLIP_CONTEXT;
	}

	return PVRSRV_OK;
}

static IMG_VOID DC_MRFLD_ContextConfigure(IMG_HANDLE hDisplayContext,
				IMG_UINT32 ui32PipeCount,
				PVRSRV_SURFACE_CONFIG_INFO *pasSurfAttrib,
				IMG_HANDLE *ahBuffers,
				IMG_UINT32 ui32DisplayPeriod,
				IMG_HANDLE hConfigData)
{
	DRM_DEBUG("%s\n", __func__);

	if (!ui32PipeCount) {
		/* Called from DCDisplayContextDestroy()
		 * Retire the current config  */
		DCDisplayConfigurationRetired(hConfigData);
		return;
	}

	/*queue this configure update*/
	_Queue_Flip(hConfigData, ahBuffers, ui32PipeCount, ui32DisplayPeriod);
}

static IMG_VOID DC_MRFLD_ContextDestroy(IMG_HANDLE hDisplayContext)
{
	DC_MRFLD_DISPLAY_CONTEXT *psDisplayContext =
		(DC_MRFLD_DISPLAY_CONTEXT *)hDisplayContext;
	kfree(psDisplayContext);
}

/**
 *
 */
static PVRSRV_ERROR DC_MRFLD_BufferAlloc(IMG_HANDLE hDisplayContext,
					DC_BUFFER_CREATE_INFO *psCreateInfo,
					IMG_DEVMEM_LOG2ALIGN_T *puiLog2PageSize,
					IMG_UINT32 *pui32PageCount,
					IMG_UINT32 *pui32PhysHeapID,
					IMG_UINT32 *pui32ByteStride,
					IMG_HANDLE *phBuffer)
{
	PVRSRV_ERROR eRes = PVRSRV_OK;
	DC_MRFLD_BUFFER *psBuffer = NULL;
	PVRSRV_SURFACE_INFO *psSurfInfo;
	IMG_UINT32 ulPagesNumber;
	IMG_UINT32 i, j;
	DC_MRFLD_DEVICE *psDevice;
	struct drm_device *psDrmDev;
	DC_MRFLD_DISPLAY_CONTEXT *psDisplayContext =
		(DC_MRFLD_DISPLAY_CONTEXT *)hDisplayContext;

	if (!psDisplayContext || !psCreateInfo || !puiLog2PageSize ||
		!pui32PageCount || !pui32PhysHeapID || !pui32ByteStride ||
		!phBuffer)
		return PVRSRV_ERROR_INVALID_PARAMS;

	DRM_DEBUG("%s\n", __func__);

	psBuffer = kzalloc(sizeof(DC_MRFLD_BUFFER), GFP_KERNEL);
	if (!psBuffer) {
		DRM_ERROR("Failed to create buffer\n");
		eRes = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto create_error;
	}

	psSurfInfo = &psCreateInfo->sSurface;

	/*
	 * As we're been asked to allocate this buffer we decide what it's
	 * stride should be.
	 */
	psBuffer->eSource = DCMrfldEX_BUFFER_SOURCE_ALLOC;
	psBuffer->hDisplayContext = hDisplayContext;

	/* Align 32 pixels of width alignment */
	psBuffer->ui32Width =
		_Align_To(psSurfInfo->sDims.ui32Width, DC_MRFLD_WIDTH_ALIGN_MASK);

	psBuffer->ui32ByteStride =
		psBuffer->ui32Width * psCreateInfo->ui32BPP;
	/*align stride*/
	psBuffer->ui32ByteStride =
		_Align_To(psBuffer->ui32ByteStride, DC_MRFLD_STRIDE_ALIGN);

	psBuffer->ui32Height = psSurfInfo->sDims.ui32Height;
	psBuffer->ui32BufferSize =
		psBuffer->ui32Height * psBuffer->ui32ByteStride;
	psBuffer->ePixFormat = psSurfInfo->sFormat.ePixFormat;

	/*
	 * Allocate display adressable memory. We only need physcial addresses
	 * at this stage.
	 * Note: This could be defered till the 1st map or acquire call.
	 * IMG uses pgprot_noncached(PAGE_KERNEL)
	 */
	psBuffer->sCPUVAddr = __vmalloc(psBuffer->ui32BufferSize,
			GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO,
			 __pgprot((pgprot_val(PAGE_KERNEL) & ~_PAGE_CACHE_MASK)
			| _PAGE_CACHE_WC));
	if (psBuffer->sCPUVAddr == NULL) {
		DRM_ERROR("Failed to allocate buffer\n");
		eRes = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto alloc_error;
	}

	ulPagesNumber =
		(psBuffer->ui32BufferSize + (PAGE_SIZE - 1)) >> PAGE_SHIFT;

	psBuffer->psSysAddr =
		kzalloc(ulPagesNumber * sizeof(IMG_SYS_PHYADDR), GFP_KERNEL);
	if (!psBuffer->psSysAddr) {
		DRM_ERROR("Failed to allocate phy array\n");
		eRes = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto phy_error;
	}

	i = 0; j = 0;
	for (i = 0; i < psBuffer->ui32BufferSize; i += PAGE_SIZE) {
#if defined(UNDER_WDDM)
		psBuffer->psSysAddr[j++].uiAddr =
			(IMG_UINTPTR_T)vmalloc_to_pfn(psBuffer->sCPUVAddr + i) << PAGE_SHIFT;
#else
		psBuffer->psSysAddr[j++].uiAddr =
			(IMG_UINT64)vmalloc_to_pfn(psBuffer->sCPUVAddr + i) << PAGE_SHIFT;
#endif
	}

	psBuffer->bIsAllocated = IMG_TRUE;
	psBuffer->bIsContiguous = IMG_FALSE;
	psBuffer->ui32OwnerTaskID = task_tgid_nr(current);

	psDevice = psDisplayContext->psDevice;
	psDrmDev = psDevice->psDrmDevice;

	ulPagesNumber =
		(psBuffer->ui32BufferSize + (PAGE_SIZE - 1)) >> PAGE_SHIFT;

	/*map this buffer to gtt*/
	DCCBgttMapMemory(psDrmDev,
			(unsigned int)psBuffer,
			psBuffer->ui32OwnerTaskID,
			psBuffer->psSysAddr,
			ulPagesNumber,
			(unsigned int *)&psBuffer->sDevVAddr.uiAddr);

	psBuffer->sDevVAddr.uiAddr <<= PAGE_SHIFT;

	/*setup output params*/
	*pui32ByteStride = psBuffer->ui32ByteStride;
	*puiLog2PageSize = PAGE_SHIFT;
	*pui32PageCount = ulPagesNumber;
	*pui32PhysHeapID = 0;
	*phBuffer = psBuffer;

	DRM_DEBUG("%s: allocated buffer: %dx%d\n", __func__,
		psBuffer->ui32Width, psBuffer->ui32Height);

	return PVRSRV_OK;
phy_error:
	vfree(psBuffer->sCPUVAddr);
alloc_error:
	kfree(psBuffer);
create_error:
	return eRes;
}

static PVRSRV_ERROR DC_MRFLD_BufferImport(IMG_HANDLE hDisplayContext,
					IMG_UINT32 ui32NumPlanes,
					IMG_HANDLE **paphImport,
					DC_BUFFER_IMPORT_INFO *psSurfAttrib,
					IMG_HANDLE *phBuffer)
{
	DC_MRFLD_BUFFER *psBuffer;
	DC_MRFLD_DISPLAY_CONTEXT *psDisplayContext =
		(DC_MRFLD_DISPLAY_CONTEXT *)hDisplayContext;
	DC_MRFLD_DEVICE *psDevice;

	if (!psDisplayContext || !ui32NumPlanes || !paphImport ||
		!psSurfAttrib || !phBuffer)
		return PVRSRV_ERROR_INVALID_PARAMS;

	DRM_DEBUG("%s\n", __func__);

	psDevice = psDisplayContext->psDevice;

	/*NOTE: we are only using the first plane(buffer)*/
	DRM_DEBUG("%s: import surf format 0x%x, w %d, h %d, bpp %d," \
		"stride %d\n",
		__func__,
		psSurfAttrib->ePixFormat,
		psSurfAttrib->ui32Width[0],
		psSurfAttrib->ui32Height[0],
		psSurfAttrib->ui32BPP,
		psSurfAttrib->ui32ByteStride[0]);

	psBuffer = kzalloc(sizeof(DC_MRFLD_BUFFER), GFP_KERNEL);
	if (!psBuffer) {
		DRM_ERROR("Failed to create DC buffer\n");
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/*initialize this buffer*/
	psBuffer->eSource = DCMrfldEX_BUFFER_SOURCE_IMPORT;
	psBuffer->hDisplayContext = hDisplayContext;
	psBuffer->ui32Width = psSurfAttrib->ui32Width[0];
	psBuffer->ePixFormat = psSurfAttrib->ePixFormat;
	psBuffer->ui32ByteStride = psSurfAttrib->ui32ByteStride[0];
	psBuffer->ui32Width = psSurfAttrib->ui32Width[0];
	psBuffer->ui32Height = psSurfAttrib->ui32Height[0];
	psBuffer->bIsAllocated = IMG_FALSE;
	psBuffer->bIsContiguous = IMG_FALSE;
	psBuffer->ui32OwnerTaskID = task_tgid_nr(current);

	psBuffer->hImport = paphImport[0];
	/*setup output param*/
	*phBuffer = psBuffer;

	return PVRSRV_OK;
}

static PVRSRV_ERROR DC_MRFLD_BufferAcquire(IMG_HANDLE hBuffer,
					IMG_DEV_PHYADDR *pasDevPAddr,
					IMG_PVOID *ppvLinAddr)
{
	DC_MRFLD_BUFFER *psBuffer = (DC_MRFLD_BUFFER *)hBuffer;
	IMG_UINT32 ulPages;
	int i;

	if (!psBuffer || !pasDevPAddr || !ppvLinAddr)
		return PVRSRV_ERROR_INVALID_PARAMS;

	DRM_DEBUG("%s\n", __func__);

	ulPages = (psBuffer->ui32BufferSize + (PAGE_SIZE - 1)) >> PAGE_SHIFT;

	/*allocate new buffer for import buffer*/
	if (psBuffer->eSource == DCMrfldEX_BUFFER_SOURCE_ALLOC) {
		for (i = 0; i < ulPages; i++)
			pasDevPAddr[i].uiAddr = psBuffer->psSysAddr[i].uiAddr;
		*ppvLinAddr = psBuffer->sCPUVAddr;
	}
	return PVRSRV_OK;
}

static IMG_VOID DC_MRFLD_BufferRelease(IMG_HANDLE hBuffer)
{

}

static IMG_VOID DC_MRFLD_BufferFree(IMG_HANDLE hBuffer)
{
	DC_MRFLD_DISPLAY_CONTEXT *psDisplayContext;
	DC_MRFLD_BUFFER *psBuffer = (DC_MRFLD_BUFFER *)hBuffer;
	DC_MRFLD_DEVICE *psDevice;
	struct drm_device *psDrmDev;

	if (!psBuffer)
		return;

	DRM_DEBUG("%s\n", __func__);

	psDisplayContext =
		(DC_MRFLD_DISPLAY_CONTEXT *)psBuffer->hDisplayContext;
	if (!psDisplayContext)
		return;

	psDevice = psDisplayContext->psDevice;
	psDrmDev = psDevice->psDrmDevice;

	if (psBuffer->eSource == DCMrfldEX_BUFFER_SOURCE_SYSTEM)
		return;

	/*
	 * if it's buffer allocated by display device contineu to free
	 * the buffer pages
	 */
	if (psBuffer->eSource == DCMrfldEX_BUFFER_SOURCE_ALLOC) {
		/*make sure unmap this buffer from gtt*/
		DCCBgttUnmapMemory(psDrmDev, (unsigned int)psBuffer,
				psBuffer->ui32OwnerTaskID);
		kfree(psBuffer->psSysAddr);
		vfree(psBuffer->sCPUVAddr);
	}

	kfree(psBuffer);
}

static PVRSRV_ERROR DC_MRFLD_BufferMap(IMG_HANDLE hBuffer)
{
	return PVRSRV_OK;
}

static IMG_VOID DC_MRFLD_BufferUnmap(IMG_HANDLE hBuffer)
{

}

static DC_DEVICE_FUNCTIONS sDCFunctions = {
	.pfnGetInfo			= DC_MRFLD_GetInfo,
	.pfnPanelQueryCount		= DC_MRFLD_PanelQueryCount,
	.pfnPanelQuery			= DC_MRFLD_PanelQuery,
	.pfnFormatQuery			= DC_MRFLD_FormatQuery,
	.pfnDimQuery			= DC_MRFLD_DimQuery,
	.pfnSetBlank			= IMG_NULL,
	.pfnSetVSyncReporting		= IMG_NULL,
	.pfnLastVSyncQuery		= IMG_NULL,
	.pfnContextCreate		= DC_MRFLD_ContextCreate,
	.pfnContextDestroy		= DC_MRFLD_ContextDestroy,
	.pfnContextConfigure		= DC_MRFLD_ContextConfigure,
	.pfnContextConfigureCheck	= DC_MRFLD_ContextConfigureCheck,
	.pfnBufferAlloc			= DC_MRFLD_BufferAlloc,
	.pfnBufferAcquire		= DC_MRFLD_BufferAcquire,
	.pfnBufferRelease		= DC_MRFLD_BufferRelease,
	.pfnBufferFree			= DC_MRFLD_BufferFree,
	.pfnBufferImport		= DC_MRFLD_BufferImport,
	.pfnBufferMap			= DC_MRFLD_BufferMap,
	.pfnBufferUnmap			= DC_MRFLD_BufferUnmap,
	.pfnBufferSystemAcquire         = DC_MRFLD_BufferSystemAcquire,
	.pfnBufferSystemRelease         = DC_MRFLD_BufferSystemRelease,

};

static PVRSRV_ERROR _SystemBuffer_Init(DC_MRFLD_DEVICE *psDevice)
{
	struct drm_device *psDrmDev;
	struct psb_framebuffer *psPSBFb;
	IMG_UINT32 ulPagesNumber;
	int i;

	/*get fbDev*/
	psDrmDev = psDevice->psDrmDevice;
	DCCBGetFramebuffer(psDrmDev, &psPSBFb);
	if (!psPSBFb)
		return PVRSRV_ERROR_INVALID_PARAMS;

	/*allocate system buffer*/
	psDevice->psSystemBuffer =
		kzalloc(sizeof(DC_MRFLD_BUFFER), GFP_KERNEL);
	if (!psDevice->psSystemBuffer) {
		DRM_ERROR("Failed to allocate system buffer\n");
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/*initilize system buffer*/
	psDevice->psSystemBuffer->bIsAllocated = IMG_FALSE;
	psDevice->psSystemBuffer->bIsContiguous = IMG_FALSE;
	psDevice->psSystemBuffer->eSource = DCMrfldEX_BUFFER_SOURCE_SYSTEM;
	psDevice->psSystemBuffer->hDisplayContext = 0;
	psDevice->psSystemBuffer->hImport = 0;
	psDevice->psSystemBuffer->sCPUVAddr = psPSBFb->vram_addr;
	psDevice->psSystemBuffer->sDevVAddr.uiAddr = 0;
	psDevice->psSystemBuffer->ui32BufferSize = psPSBFb->size;
	psDevice->psSystemBuffer->ui32ByteStride = psPSBFb->base.pitches[0];
	psDevice->psSystemBuffer->ui32Height = psPSBFb->base.height;
	psDevice->psSystemBuffer->ui32Width = psPSBFb->base.width;
	psDevice->psSystemBuffer->ui32OwnerTaskID = -1;
	psDevice->psSystemBuffer->ui32RefCount = 0;

	switch (psPSBFb->depth) {
	case 32:
	case 24:
		psDevice->psSystemBuffer->ePixFormat =
			IMG_PIXFMT_B8G8R8A8_UNORM;
		break;
	case 16:
		psDevice->psSystemBuffer->ePixFormat =
			IMG_PIXFMT_B5G6R5_UNORM;
		break;
	default:
		DRM_ERROR("Unsupported system buffer format\n");
	}

	ulPagesNumber = (psPSBFb->size + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
	psDevice->psSystemBuffer->psSysAddr =
		kzalloc(ulPagesNumber * sizeof(IMG_SYS_PHYADDR), GFP_KERNEL);
	if (!psDevice->psSystemBuffer->psSysAddr) {
		kfree(psDevice->psSystemBuffer);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	for (i = 0; i < ulPagesNumber; i++) {
		psDevice->psSystemBuffer->psSysAddr[i].uiAddr =
			psPSBFb->stolen_base + i * PAGE_SIZE;
	}

	DRM_DEBUG("%s: allocated system buffer %dx%d, format %d\n",
			__func__,
			psDevice->psSystemBuffer->ui32Width,
			psDevice->psSystemBuffer->ui32Height,
			psDevice->psSystemBuffer->ePixFormat);

	return PVRSRV_OK;
}

static IMG_VOID _SystemBuffer_Deinit(DC_MRFLD_DEVICE *psDevice)
{
	if (psDevice->psSystemBuffer) {
		kfree(psDevice->psSystemBuffer->psSysAddr);
		kfree(psDevice->psSystemBuffer);
	}
}

static PVRSRV_ERROR DC_MRFLD_init(struct drm_device *psDrmDev)
{
	PVRSRV_ERROR eRes = PVRSRV_OK;
	DC_MRFLD_DEVICE *psDevice;
	int i;

	if (!psDrmDev)
		return PVRSRV_ERROR_INVALID_PARAMS;

	/*create new display device*/
	psDevice = kzalloc(sizeof(DC_MRFLD_DEVICE), GFP_KERNEL);
	if (!psDevice) {
		DRM_ERROR("Failed to create display device\n");
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/*init display device*/
	psDevice->psDrmDevice = psDrmDev;
	/*init system frame buffer*/
	eRes = _SystemBuffer_Init(psDevice);
	if (eRes != PVRSRV_OK)
		goto init_error;

	/*init primary surface info*/
	psDevice->sPrimInfo.sDims.ui32Width =
		psDevice->psSystemBuffer->ui32Width;
	psDevice->sPrimInfo.sDims.ui32Height =
		psDevice->psSystemBuffer->ui32Height;
	psDevice->sPrimInfo.sFormat.ePixFormat =
		psDevice->psSystemBuffer->ePixFormat;

	/*init display info*/
	strncpy(psDevice->sDisplayInfo.szDisplayName, DRVNAME, DC_NAME_SIZE);
	psDevice->sDisplayInfo.ui32MinDisplayPeriod = 0;
	psDevice->sDisplayInfo.ui32MaxDisplayPeriod = 5;
	psDevice->sDisplayInfo.ui32MaxPipes = DCCBGetPipeCount();

	/*init flip queue lock*/
	mutex_init(&psDevice->sFlipQueueLock);

	/*init flip queues*/
	for (i = 0; i < MAX_PIPE_NUM; i++)
		INIT_LIST_HEAD(&psDevice->sFlipQueues[i]);

	/*unblank fbdev*/
	DCCBUnblankDisplay(psDevice->psDrmDevice);

	/*register display device*/
	eRes = DCRegisterDevice(&sDCFunctions,
				2,
				psDevice,
				&psDevice->hSrvHandle);
	if (eRes != PVRSRV_OK) {
		DRM_ERROR("Failed to register display device\n");
		goto reg_error;
	}

	/*init ISR*/
	DCCBInstallVSyncISR(psDrmDev, _Vsync_ISR);

	gpsDevice = psDevice;

	return PVRSRV_OK;
reg_error:
	_SystemBuffer_Deinit(psDevice);
init_error:
	kfree(psDevice);
	return eRes;
}

static PVRSRV_ERROR DC_MRFLD_exit(void)
{
	if (!gpsDevice)
		return PVRSRV_ERROR_INVALID_PARAMS;

	/*unregister display device*/
	DCUnregisterDevice(gpsDevice->hSrvHandle);

	/*destroy system frame buffer*/
	_SystemBuffer_Deinit(gpsDevice);

	/*free device*/
	kfree(gpsDevice);
	gpsDevice = 0;

	return PVRSRV_OK;
}

void DCUnAttachPipe(uint32_t iPipe)
{
	struct list_head *psFlipQueue;
	DC_MRFLD_FLIP *psFlip, *psTmp;
	int i;

	if (!gpsDevice)
		return;

	DRM_DEBUG("%s: pipe %d\n", __func__, iPipe);

	if (iPipe != DC_PIPE_A && iPipe != DC_PIPE_B)
		return;

	/* acquire flip queue mutex */
	mutex_lock(&gpsDevice->sFlipQueueLock);

	/* flush all pending flips on all pipes */
	for (i = 0; i < MAX_PIPE_NUM; i++) {
		psFlipQueue = &gpsDevice->sFlipQueues[i];
		/* complete the flips*/
		list_for_each_entry_safe(psFlip, psTmp, psFlipQueue,
					sFlips[i]) {
			_Do_Flip(psFlip, i);
			/*remove this entry from flip queue, decrease refCount*/
			list_del(&psFlip->sFlips[i]);

			if (!(--psFlip->uiRefCount)) {
				/*retire all buffers possessed by this flip*/
				DCDisplayConfigurationRetired(
						psFlip->hConfigData);
				/* free it */
				kfree(psFlip);
			}
		}
		if (list_empty_careful(psFlipQueue))
			INIT_LIST_HEAD(&gpsDevice->sFlipQueues[i]);
	}

	mutex_unlock(&gpsDevice->sFlipQueueLock);
}

int DC_MRFLD_Enable_Plane(int type, int index, u32 ctx)
{
	int err = 0;
	IMG_INT32 *ui32ActivePlanes;

	/*acquire lock*/
	mutex_lock(&gpsDevice->sFlipQueueLock);

	switch (type) {
	case DC_SPRITE_PLANE:
		ui32ActivePlanes = &gpsDevice->ui32ActiveSprites;
		break;
	case DC_OVERLAY_PLANE:
		ui32ActivePlanes = &gpsDevice->ui32ActiveOverlays;
		break;
	default:
		mutex_unlock(&gpsDevice->sFlipQueueLock);
		return 0;
	}

	/* add to active planes*/
	*ui32ActivePlanes |= (1 << index);

	mutex_unlock(&gpsDevice->sFlipQueueLock);

	return err;
}

int DC_MRFLD_Disable_Plane(int type, int index, u32 ctx)
{
	int err;
	IMG_INT32 *ui32ActivePlanes;

	/*acquire lock*/
	mutex_lock(&gpsDevice->sFlipQueueLock);

	switch (type) {
	case DC_SPRITE_PLANE:
		err = DCCBSpriteEnable(gpsDevice->psDrmDevice, ctx, index, 0);
		ui32ActivePlanes = &gpsDevice->ui32ActiveSprites;
		break;
	case DC_OVERLAY_PLANE:
		err = DCCBOverlayEnable(gpsDevice->psDrmDevice, ctx, index, 0);
		ui32ActivePlanes = &gpsDevice->ui32ActiveOverlays;
		break;
	default:
		mutex_unlock(&gpsDevice->sFlipQueueLock);
		return 0;
	}

	/* remove from active planes*/
	if (!err)
		*ui32ActivePlanes &= ~(1 << index);

	mutex_unlock(&gpsDevice->sFlipQueueLock);

	return err;
}

/*----------------------------------------------------------------------------*/
PVRSRV_ERROR MerrifieldDCInit(struct drm_device *psDrmDev)
{
	return DC_MRFLD_init(psDrmDev);
}

PVRSRV_ERROR MerrifieldDCDeinit(void)
{
	return DC_MRFLD_exit();
}
