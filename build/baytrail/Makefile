DRIVER_NAME := VXD392

MEDIADIR=../..
BAYTRAILDIR=$(MEDIADIR)/common/baytrail
VIDEOCOMDIR=$(MEDIADIR)/video/common
VIDEODECDIR=$(MEDIADIR)/video/decode

MEDIAINC=../modules/intel_media
BAYTRAILINC=$(MEDIAINC)/common/baytrail
VIDEOCOMINC=$(MEDIAINC)/video/common
VIDEODECINC=$(MEDIAINC)/video/decode

include_dirs := \
	-Iinclude/drm \
	-Iinclude/drm/ttm \
	-Idrivers/gpu/drm/i915 \
	-I$(BAYTRAILINC) \
	-I$(VIDEOCOMINC) \
	-I$(VIDEODECINC)

ccflags-y += $(include_dirs)

ccflags-y += -DPSB_MSVDX_FW_LOADED_BY_HOST

$(DRIVER_NAME)-objs += \
	$(BAYTRAILDIR)/vxd_drv.o \
	$(VIDEOCOMDIR)/psb_ttm_glue.o \
	$(VIDEOCOMDIR)/psb_cmdbuf.o \
	$(VIDEOCOMDIR)/psb_buffer.o \
	$(VIDEOCOMDIR)/psb_fence.o \
	$(VIDEOCOMDIR)/psb_mmu.o \
	$(VIDEOCOMDIR)/psb_ttm_fence.o \
	$(VIDEOCOMDIR)/psb_ttm_fence_user.o \
	$(VIDEOCOMDIR)/psb_ttm_placement_user.o \
	$(VIDEODECDIR)/psb_msvdx.o \
	$(VIDEODECDIR)/psb_msvdxinit.o \
	$(VIDEODECDIR)/psb_msvdx_fw.o

obj-y += $(DRIVER_NAME).o
