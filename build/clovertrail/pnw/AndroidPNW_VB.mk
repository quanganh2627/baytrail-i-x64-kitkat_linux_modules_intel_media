# This makefile is included from vendor/intel/*/AndroidBoard.mk.
$(eval $(call build_kernel_module,$(call my-dir),pnwdisp,\
	CONFIG_VB_LCD_DISPLAY=y))