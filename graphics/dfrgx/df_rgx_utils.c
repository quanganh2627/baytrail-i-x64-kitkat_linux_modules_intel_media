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
#include "df_rgx_defs.h"
#include "dev_freq_debug.h"

extern int is_tng_b0;

unsigned int df_rgx_is_valid_freq(unsigned long int freq)
{
	unsigned int valid = 0;
	int i;
	int aSize = NUMBER_OF_LEVELS;
	
	if(is_tng_b0)
		aSize = NUMBER_OF_LEVELS_B0;
	

	DFRGX_DPF(DFRGX_DEBUG_HIGH, "%s freq: %d\n", __func__, 
			freq);

	for(i = 0; i < aSize; i++)
	{
		if(freq == aAvailableStateFreq[i])
		{
			valid = 1;
			break;
		}
	}
	
	
	DFRGX_DPF(DFRGX_DEBUG_HIGH, "%s valid: %d\n", __func__, 
			valid);

	return valid;
}

