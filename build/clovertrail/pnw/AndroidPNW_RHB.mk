# This makefile is included from vendor/intel/*/AndroidBoard.mk.
$(eval $(call build_kernel_module,$(call my-dir),pnwdisp,\
	CONFIG_SUPPORT_MIPI_H8C7_CMD_DISPLAY=y))