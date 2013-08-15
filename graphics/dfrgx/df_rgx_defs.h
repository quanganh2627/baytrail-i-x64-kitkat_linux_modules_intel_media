/**************************************************************************
 * Copyright (c) 2012, Intel Corporation.
 * All Rights Reserved.

 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Javier Torres Castillo <javier.torres.castillo@intel.com>
 */

#if !defined DF_RGX_DEFS_H
#define DF_RGX_DEFS_H

#include <linux/device.h>
#include <linux/notifier.h>

/**
 * THERMAL_COOLING_DEVICE_MAX_STATE - The maximum cooling state that this
 * driver (as a thermal cooling device by reducing frequency) supports.
 */
#define THERMAL_COOLING_DEVICE_MAX_STATE	4
#define NUMBER_OF_LEVELS_B0 			8
#define NUMBER_OF_LEVELS			4

typedef enum _DFRGX_FREQ_
{
	DFRGX_FREQ_200_MHZ = 200000,
	DFRGX_FREQ_213_MHZ = 213000,
	DFRGX_FREQ_266_MHZ = 266000,
	DFRGX_FREQ_320_MHZ = 320000,
	DFRGX_FREQ_355_MHZ = 355000,
	DFRGX_FREQ_400_MHZ = 400000,
	DFRGX_FREQ_457_MHZ = 457000,
	DFRGX_FREQ_533_MHZ = 533000,
} DFRGX_FREQ;

/*Available states - freq mapping table*/
const static unsigned long int aAvailableStateFreq[] = {
									DFRGX_FREQ_200_MHZ,
									DFRGX_FREQ_213_MHZ,
									DFRGX_FREQ_266_MHZ,
									DFRGX_FREQ_320_MHZ,
									DFRGX_FREQ_355_MHZ,
									DFRGX_FREQ_400_MHZ,
									DFRGX_FREQ_457_MHZ,
									DFRGX_FREQ_533_MHZ
									};

struct gpu_data {
	unsigned long int     freq_limit;
};

struct busfreq_data {
	struct device        *dev;
	struct devfreq       *devfreq;
	struct notifier_block pm_notifier;
	struct mutex          lock;
	bool                  disabled;
	unsigned long int     bf_freq_mhz_rlzd;

	struct thermal_cooling_device *gbp_cooldv_hdl;
	int                   gbp_cooldv_state_cur;
	int                   gbp_cooldv_state_prev;
	int                   gbp_cooldv_state_highest;
	int                   gbp_cooldv_state_override;
	struct gpu_data	      gpudata[THERMAL_COOLING_DEVICE_MAX_STATE];
};

unsigned int df_rgx_is_valid_freq(unsigned long int freq);

#endif /*DF_RGX_DEFS_H*/
