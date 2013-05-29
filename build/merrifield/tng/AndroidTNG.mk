# This makefile is included from vendor/intel/*/AndroidBoard.mk.
$(eval $(call build_kernel_module,$(call my-dir),tngdisp,\
	CONFIG_SUPPORT_HDMI=y \
	CONFIG_DRM_HANDSET_RELEASE=y \
	CONFIG_PDUMP=n \
	CONFIG_BOARD_MRFLD_VP=n))