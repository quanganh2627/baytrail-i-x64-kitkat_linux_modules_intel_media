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
 *     Dale B. Stimson <dale.b.stimson@intel.com>
 *     Javier Torres Castillo <javier.torres.castillo@intel.com>
 *
 * df_rgx.c - devfreq driver for IMG rgx graphics in Tangier.
 * Description:
 *  Early devfreq driver for rgx.  Utilization measures and on-demand
 *  frequency control will be added later.  For now, only thermal
 *  conditions and sysfs file inputs are taken into account.
 *
 *  This driver currently only allows frequencies between 200MHz and
 *  533 MHz.
 *
 *  This driver observes the limits set by the values in:
 *
 *      sysfs file                           initial value (KHz)
 *      ---------------------------------    -------------------
 *      /sys/class/devfreq/dfrgx/min_freq    200000
 *      /sys/class/devfreq/dfrgx/max_freq    320000, 533000 on B0
 *  and provides current frequency from:
 *      /sys/class/devfreq/dfrgx/cur_freq
 *
 *  With current development silicon, instability is a real possibility
 *  at 400 MHz and higher.
 *
 *  While the driver is informed that a thermal condition exists, it
 *  reduces the gpu frequency to 200 MHz.
 *
 *  Temporary:
 *      No use of performance counters.
 *      No utilization computation.
 *      Uses governor "devfreq_powersave", although with throttling if hot.
 *
 *  It would be nice to have more sysfs or debugfs files for testing purposes.
 *
 *  All DEBUG printk messages start with "dfrgx:" for easy searching of
 *  dmesg output.
 *
 *  To test with the module: insmod /lib/modules/dfrgx.ko
 *  To unload module: rmmod dfrgx
 *
 *  See files under /sys/class/devfreq/dfrgx .
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/suspend.h>

#include <linux/thermal.h>
#include <asm/errno.h>

#include <linux/devfreq.h>

#include <governor.h>

#include <ospm/gfx_freq.h>
#include "dev_freq_debug.h"
#include "dev_freq_graphics_pm.h"
#include "df_rgx_defs.h"
#include "df_rgx_burst.h"
#define DFRGX_GLOBAL_ENABLE_DEFAULT 1

#define DF_RGX_NAME_DEV    "dfrgx"
#define DF_RGX_NAME_DRIVER "dfrgxdrv"

#define DFRGX_HEADING DF_RGX_NAME_DEV ": "

/* DF_RGX_POLLING_INTERVAL_MS - Polling interval in milliseconds.
 * FIXME - Need to have this be 5 ms, but have to workaround HZ tick usage.
 */
#define DF_RGX_POLLING_INTERVAL_MS 50

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0))
/**
 * Potential governors:
 *     #define GOVERNOR_TO_USE "performance"
 *     #define GOVERNOR_TO_USE "simple_ondemand"
 *     #define GOVERNOR_TO_USE "userspace"
 *     #define GOVERNOR_TO_USE "powersave"
 */
#define GOVERNOR_TO_USE "performance"
#else
/**
 * Potential governors:
 *     #define GOVERNOR_TO_USE devfreq_simple_ondemand
 *     #define GOVERNOR_TO_USE devfreq_performance
 *     #define GOVERNOR_TO_USE devfreq_powersave
 */
#define GOVERNOR_TO_USE devfreq_performance
#endif


/*is tng B0 hw*/
extern int is_tng_b0;

/* df_rgx_created_dev - Pointer to created device, if any. */
static struct platform_device *df_rgx_created_dev;

/*Need to check if this is the 1st request*/
static int firstRequest = 1;

/**
 * Module parameters:
 *
 * - can be updated (if permission allows) via writing:
 *     /sys/module/dfrgx/parameters/<name>
 * - can be set at module load time:
 *     insmod /lib/modules/dfrgx.ko enable=0
 * - For built-in drivers, can be on kernel command line:
 *     dfrgx.enable=0
 */

/**
 * module parameter "enable" is not writable in sysfs as there is presently
 * no code to detect the transition between 0 and 1.
 */
static unsigned int mprm_enable = DFRGX_GLOBAL_ENABLE_DEFAULT;
module_param_named(enable, mprm_enable, uint, S_IRUGO);

static unsigned int mprm_verbosity = 2;
module_param_named(verbosity, mprm_verbosity, uint, S_IRUGO|S_IWUSR);


#define DRIVER_AUTHOR "Intel Corporation"
#define DRIVER_DESC "devfreq driver for rgx graphics"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/**
 *  MODULE_VERSION - Allows specification of a module version.
 *  Version of form [<epoch>:]<version>[-<extra-version>].
 *  Or for CVS/RCS ID version, everything but the number is stripped.
 * <epoch>: A (small) unsigned integer which allows you to start versions
 *          anew. If not mentioned, it's zero.  eg. "2:1.0" is after
 *          "1:2.0".
 * <version>: The <version> may contain only alphanumerics and the
 *          character `.'.  Ordered by numeric sort for numeric parts,
 *          ascii sort for ascii parts (as per RPM or DEB algorithm).
 * <extraversion>: Like <version>, but inserted for local
 *          customizations, eg "rh3" or "rusty1".

 * Using this automatically adds a checksum of the .c files and the
 * local headers in "srcversion".
 *
 * Also, if the module is under drivers/staging, this causes a warning to
 * be issued:
 *     <mname>: module is from the staging directory, the quality is unknown,
 *     you have been warned.
 *
 * Example invocation:
 *     MODULE_VERSION("0.1");

/**
 * df_rgx_bus_target - Request setting of a new frequency.
 * @*p_freq: Input: desired frequency in KHz, output: realized freq in KHz.
 * @flags: DEVFREQ_FLAG_* - not used by this implementation.
 */
static int df_rgx_bus_target(struct device *dev, unsigned long *p_freq,
			      u32 flags)
{
	struct platform_device	*pdev;
	struct busfreq_data	*bfdata;
	struct df_rgx_data_s 	*pdfrgx_data;
	struct devfreq       	*df;
	unsigned long desired_freq = *p_freq;
	int ret = 0;
	(void) flags;

	pdev = container_of(dev, struct platform_device, dev);
	bfdata = platform_get_drvdata(pdev);

	DFRGX_DPF(DFRGX_DEBUG_HIGH, "%s:TARGET ***********!\n",
				__func__);
	/*Update max and min freqs in burst table*/
	if(bfdata)
	{
		DFRGX_DPF(DFRGX_DEBUG_HIGH, "%s bfdata Valid!\n",
				__func__);

		df = bfdata->devfreq;
		pdfrgx_data = &bfdata->g_dfrgx_data;

		if(df)
		{
			int new_index = -1;

			DFRGX_DPF(DFRGX_DEBUG_HIGH, "%s:Devfreq Valid!\n",
				__func__);

			/* Dynamic burst needs to be stopped*/
			dfrgx_burst_set_enable(&bfdata->g_dfrgx_data, 0);

			if(df->min_freq != pdfrgx_data->g_freq_mhz_min){
				DFRGX_DPF(DFRGX_DEBUG_HIGH, "%s:Min freq changed!\n",
				__func__);
				new_index = df_rgx_get_util_record_index_by_freq(df->min_freq);
				if(new_index > -1){
					pdfrgx_data->g_freq_mhz_min = df->min_freq;
					pdfrgx_data->g_min_freq_index = new_index;
					if (pdfrgx_data->g_min_freq_index < NUMBER_OF_LEVELS_B0) {
					desired_freq = 	aAvailableStateFreq[pdfrgx_data->g_min_freq_index].freq;
					}
				}
			}

			if(df->max_freq != pdfrgx_data->g_freq_mhz_max){
				DFRGX_DPF(DFRGX_DEBUG_HIGH, "%s:Max freq changed!\n",
				__func__);
				new_index = df_rgx_get_util_record_index_by_freq(df->max_freq);
				if(new_index > -1){
					pdfrgx_data->g_freq_mhz_max = df->max_freq;
					pdfrgx_data->g_max_freq_index = new_index;
					desired_freq = 	aAvailableStateFreq[pdfrgx_data->g_max_freq_index].freq;
				}
			}

			/* Resuming dynamic burst*/
			dfrgx_burst_set_enable(&bfdata->g_dfrgx_data, 1);
		}
	}

	/*FIXME: Need to rethink about this scenario*/
	if(firstRequest){
		*p_freq = DF_RGX_INITIAL_FREQ_KHZ;
		firstRequest = 0;
		goto out;
	}

	if(!df_rgx_is_active()){
		return -EBUSY;
	}

    	if (!bfdata) {
		DFRGX_DPF(DFRGX_DEBUG_HIGH, "%s: dfdata is NULL\n", __func__);
		goto out;

        }
	
	ret = set_desired_frequency_khz(bfdata, desired_freq);
	if (ret <= 0)
		return ret;

	*p_freq = ret;

out:
	return 0;
}


/**
 * df_rgx_bus_get_dev_status() - Update current status, including:
 * - stat->current_frequency - Frequency in KHz.
 * - stat->total_time
 * - stat->busy_time
 * Note: total_time and busy_time have arbitrary units, as they are
 * used only as ratios.
 * Utilization is busy_time / total_time .
 */
static int df_rgx_bus_get_dev_status(struct device *dev,
				      struct devfreq_dev_status *stat)
{
	struct busfreq_data *bfdata = dev_get_drvdata(dev);

	DFRGX_DPF(DFRGX_DEBUG_LOW, "%s: entry\n", __func__);

	stat->current_frequency = bfdata->bf_freq_mhz_rlzd * 1000;

	/* FIXME - Compute real utilization statistics. */
	stat->total_time = 100;
	stat->busy_time = 50;

	return 0;
}


/**
 * tcd_get_max_state() - thermal cooling device callback get_max_state.
 * @tcd: Thermal cooling device structure.
 * @pms: Pointer to integer through which output value is stored.
 *
 * Invoked via interrupt/callback.
 * Function return value: 0 if success, otherwise -error.
 * Execution context: non-atomic
 */
static int tcd_get_max_state(struct thermal_cooling_device *tcd,
	unsigned long *pms)
{
	*pms = THERMAL_COOLING_DEVICE_MAX_STATE - 1;

	return 0;
}


/**
 * tcd_get_cur_state() - thermal cooling device callback get_cur_state.
 * @tcd: Thermal cooling device structure.
 * @pcs: Pointer to integer through which output value is stored.
 *
 * Invoked via interrupt/callback.
 * Function return value: 0 if success, otherwise -error.
 * Execution context: non-atomic
 */
static int tcd_get_cur_state(struct thermal_cooling_device *tcd,
	unsigned long *pcs)
{
	struct busfreq_data *bfdata = (struct busfreq_data *) tcd->devdata;
	*pcs = bfdata->gbp_cooldv_state_cur;

	return 0;
}


/**
 * tcd_set_cur_state() - thermal cooling device callback set_cur_state.
 * @tcd: Thermal cooling device structure.
 * @cs: Input state.
 *
 * Invoked via interrupt/callback.
 * Function return value: 0 if success, otherwise -error.
 * Execution context: non-atomic
 */
static int tcd_set_cur_state(struct thermal_cooling_device *tcd,
	unsigned long cs)
{
	struct devfreq *df;
	struct busfreq_data *bfdata;
	int ret = 0;

	bfdata = (struct busfreq_data *) tcd->devdata;

	if (cs >= THERMAL_COOLING_DEVICE_MAX_STATE)
		cs = THERMAL_COOLING_DEVICE_MAX_STATE - 1;

	/*If different state*/
	if(bfdata->gbp_cooldv_state_cur != cs)
	{

		int new_index = -1;

		mutex_lock(&bfdata->lock);

		/* Dynamic turbo is not enabled so try to change the state */
		if(!bfdata->g_dfrgx_data.g_enable)
		{

			if(!df_rgx_is_active())
				return -EBUSY;

			/* If thermal state is specified explicitely then suspend burst/unburst thread
			* because the user needs the GPU to run at specific frequency/thermal state level
			*/

			ret = set_desired_frequency_khz(bfdata, bfdata->gpudata[cs].freq_limit);
			if (ret <= 0)
				return ret;
		}
		/* In this case we want to limit the max_freq to the thermal state limit*/
		else{
			df = bfdata->devfreq;
			dfrgx_burst_set_enable(&bfdata->g_dfrgx_data, 0);
			df->max_freq = bfdata->gpudata[cs].freq_limit;

			new_index = df_rgx_get_util_record_index_by_freq(df->max_freq);

			if(new_index > -1){
				bfdata->g_dfrgx_data.g_freq_mhz_max = df->max_freq;
				bfdata->g_dfrgx_data.g_max_freq_index = new_index;
			}

			dfrgx_burst_set_enable(&bfdata->g_dfrgx_data, 1);
		}

		bfdata->gbp_cooldv_state_prev = bfdata->gbp_cooldv_state_cur;
		bfdata->gbp_cooldv_state_cur = cs;
		mutex_unlock(&bfdata->lock);

		DFRGX_DPF(DFRGX_DEBUG_HIGH, "Thermal state changed from %d to %d\n",
				bfdata->gbp_cooldv_state_prev,
				bfdata->gbp_cooldv_state_cur);

	}

	return 0;
}

#if defined(THERMAL_DEBUG)
/**
 * tcd_get_available_states() - thermal cooling device callback get_available_states.
 * @tcd: Thermal cooling device structure.
 * @pcs: Pointer to char through which output values are stored.
 *
 * Invoked via interrupt/callback.
 * Function return value: 0 if success, otherwise -error.
 * Execution context: non-atomic
 */
static int tcd_get_available_states(struct thermal_cooling_device *tcd,
	char *buf)
{
	struct busfreq_data *bfdata = (struct busfreq_data *) tcd->devdata;
	int ret = 0;

	if(is_tng_b0){
	ret = sprintf(buf, "%d %d %d %d %d %d %d %d\n", aAvailableStateFreq[0].freq,
			 aAvailableStateFreq[1].freq,
			 aAvailableStateFreq[2].freq,
			 aAvailableStateFreq[3].freq,
			 aAvailableStateFreq[4].freq,
			 aAvailableStateFreq[5].freq,
			 aAvailableStateFreq[6].freq,
			 aAvailableStateFreq[7].freq);
	}
	else{
	ret = sprintf(buf, "%d %d %d %d\n", aAvailableStateFreq[0].freq,
			 aAvailableStateFreq[1].freq,
			 aAvailableStateFreq[2].freq,
			 aAvailableStateFreq[3].freq);
	}

	return ret;
}

/**
 * tcd_get_force_state_override() - thermal cooling device callback get_force_state_override.
 * @tcd: Thermal cooling device structure.
 * @pcs: Pointer to char through which output values are stored.
 *
 * Invoked via interrupt/callback.
 * Function return value: 0 if success, otherwise -error.
 * Execution context: non-atomic
 */
static int tcd_get_force_state_override(struct thermal_cooling_device *tcd,
	char *buf)
{
	struct busfreq_data *bfdata = (struct busfreq_data *) tcd->devdata;

	return sprintf(buf, "%d %d %d %d\n", bfdata->gpudata[0].freq_limit,
			 bfdata->gpudata[1].freq_limit,
			 bfdata->gpudata[2].freq_limit,
			 bfdata->gpudata[3].freq_limit);
}

/**
 * tcd_set_force_state_override() - thermal cooling device callback set_force_state_override.
 * @tcd: Thermal cooling device structure.
 * @pcs: Pointer to char containing the input values.
 *
 * Invoked via interrupt/callback.
 * Function return value: 0 if success, otherwise -error.
 * Execution context: non-atomic
 */
static int tcd_set_force_state_override(struct thermal_cooling_device *tcd,
	char *buf)
{
	struct busfreq_data *bfdata = (struct busfreq_data *) tcd->devdata;
	unsigned long int freqs[THERMAL_COOLING_DEVICE_MAX_STATE];
	unsigned long int prev_freq = DFRGX_FREQ_320_MHZ;
	int i = 0;

	if(is_tng_b0)
		prev_freq = DFRGX_FREQ_533_MHZ;

	sscanf(buf, "%u %u %u %u\n", &freqs[0],
			 &freqs[1],
			 &freqs[2],
			 &freqs[3]);

	DFRGX_DPF(DFRGX_DEBUG_HIGH, "%s values: %u %u %u %u\n", __func__,
			freqs[0],
			freqs[1],
			freqs[2],
			freqs[3]);

	for(i = 0; (i < THERMAL_COOLING_DEVICE_MAX_STATE) &&
				df_rgx_is_valid_freq(freqs[i]) &&
				prev_freq >= freqs[i]; i++)
	{
		prev_freq = freqs[i];
	}

	if(i < THERMAL_COOLING_DEVICE_MAX_STATE)
	{
		return -EINVAL;
	}

	for(i = 0; i < THERMAL_COOLING_DEVICE_MAX_STATE; i++)
	{
		bfdata->gpudata[i].freq_limit = freqs[i];
	}

	return 0;
}

#endif /*THERMAL_DEBUG*/

/**
 * df_rgx_bus_exit() - An optional callback that is called when devfreq is
 * removing the devfreq object due to error or from devfreq_remove_device()
 * call. If the user has registered devfreq->nb at a notifier-head, this is
 * the time to unregister it.
 */
static void df_rgx_bus_exit(struct device *dev)
{
	struct busfreq_data *bfdata = dev_get_drvdata(dev);
	(void) bfdata;

	DFRGX_DPF(DFRGX_DEBUG_LOW, "%s: entry\n", __func__);

	/*  devfreq_unregister_opp_notifier(dev, bfdata->devfreq); */
}


static struct devfreq_dev_profile df_rgx_devfreq_profile = {
	.initial_freq	= DF_RGX_INITIAL_FREQ_KHZ,
	.polling_ms	= DF_RGX_POLLING_INTERVAL_MS,
	.target		= df_rgx_bus_target,
	.get_dev_status	= df_rgx_bus_get_dev_status,
	.exit		= df_rgx_bus_exit,
};


/**
 * busfreq_mon_reset() - Initialize or reset monitoring
 * hardware state as desired.
 */
static void busfreq_mon_reset(struct busfreq_data *bfdata)
{
	/*  FIXME - reset monitoring? */
}


static int df_rgx_busfreq_pm_notifier_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	struct busfreq_data *bfdata = container_of(this, struct busfreq_data,
						 pm_notifier);
	DFRGX_DPF(DFRGX_DEBUG_LOW, "%s: entry\n", __func__);

	switch (event) {
	case PM_SUSPEND_PREPARE:
		/* Set Fastest and Deactivate DVFS */
		mutex_lock(&bfdata->lock);
		bfdata->disabled = true;
		mutex_unlock(&bfdata->lock);
		return NOTIFY_OK;
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		/* Reactivate */
		mutex_lock(&bfdata->lock);
		bfdata->disabled = false;
		mutex_unlock(&bfdata->lock);
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

static int df_rgx_busfreq_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct busfreq_data *bfdata;
	struct devfreq *df;
	int error = 0;
	int sts = 0;
	int i = 0, j = 0, start = 0;

	DFRGX_DPF(DFRGX_DEBUG_LOW, "%s: entry\n", __func__);

	/* dev_err(dev, "example.\n"); */

	bfdata = kzalloc(sizeof(struct busfreq_data), GFP_KERNEL);
	if (bfdata == NULL) {
		dev_err(dev, "Cannot allocate memory.\n");
		return -ENOMEM;
	}

	bfdata->pm_notifier.notifier_call = df_rgx_busfreq_pm_notifier_event;
	bfdata->dev = dev;
	mutex_init(&bfdata->lock);

	platform_set_drvdata(pdev, bfdata);

	busfreq_mon_reset(bfdata);

	df = devfreq_add_device(dev, &df_rgx_devfreq_profile,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0))
					   GOVERNOR_TO_USE, NULL);
#else
					   &GOVERNOR_TO_USE, NULL);
#endif

	if (IS_ERR(df)) {
		sts = PTR_ERR(bfdata->devfreq);
		goto err_000;
	}

	bfdata->devfreq = df;

	df->min_freq = DF_RGX_FREQ_KHZ_MIN_INITIAL;

	if(is_tng_b0){
		df->max_freq = DF_RGX_FREQ_KHZ_MAX;
		start = sizeof(aAvailableStateFreq)/sizeof(aAvailableStateFreq[0]);
	}
	else{
		df->max_freq = DF_RGX_FREQ_KHZ_MAX_INITIAL;
		start = THERMAL_COOLING_DEVICE_MAX_STATE;
	}
	bfdata->gbp_cooldv_state_override = -1;

	j = 0;
	/*Initial states*/
	for ( i = start - 1; j < THERMAL_COOLING_DEVICE_MAX_STATE; i-- )
	{
		bfdata->gpudata[j].freq_limit = aAvailableStateFreq[i].freq;
		j++;
	}

	{
		static const char *tcd_type = "gpu_burst";
		static const struct thermal_cooling_device_ops tcd_ops = {
			.get_max_state = tcd_get_max_state,
			.get_cur_state = tcd_get_cur_state,
			.set_cur_state = tcd_set_cur_state,
#if defined(THERMAL_DEBUG)
			.get_force_state_override = tcd_get_force_state_override,
			.set_force_state_override = tcd_set_force_state_override,
			.get_available_states = tcd_get_available_states,
#endif
		};
		struct thermal_cooling_device *tcdhdl;

		/**
		  * Example: Thermal zone "type"s and temps milli-deg-C.
		  * These are just examples and are not specific to our usage.
		  *   type              temp
		  *   --------          -------
		  *   skin0             15944
		  *   skin1             22407
		  *   msicdie           37672
		  *
		  * See /sys/class/thermal/thermal_zone<i>
		  * See /sys/class/thermal/cooling_device<i>
		  */

		tcdhdl = thermal_cooling_device_register(
			(char *) tcd_type, bfdata, &tcd_ops);
		if (IS_ERR(tcdhdl)) {
			DFRGX_DPF(DFRGX_DEBUG_HIGH, "Cooling device registration failed: %ld\n",
				-PTR_ERR(tcdhdl));
			sts = PTR_ERR(tcdhdl);
			goto err_001;
		}
		bfdata->gbp_cooldv_hdl = tcdhdl;
	}

	sts = register_pm_notifier(&bfdata->pm_notifier);
	if (sts) {
		dev_err(dev, "Failed to setup pm notifier\n");
		goto err_002;
	}

	bfdata->g_dfrgx_data.bus_freq_data = bfdata;
	bfdata->g_dfrgx_data.g_enable = mprm_enable;
	bfdata->g_dfrgx_data.gpu_utilization_record_index = 3; /*Index for 320 MHZ, initial freq*/
	bfdata->g_dfrgx_data.g_min_freq_index = df_rgx_get_util_record_index_by_freq(df->min_freq);
	bfdata->g_dfrgx_data.g_freq_mhz_min = df->min_freq;
	bfdata->g_dfrgx_data.g_max_freq_index = df_rgx_get_util_record_index_by_freq(df->max_freq);
	bfdata->g_dfrgx_data.g_freq_mhz_max = df->max_freq;

	error = dfrgx_burst_init(&bfdata->g_dfrgx_data);

	if(error){
		DFRGX_DPF(DFRGX_DEBUG_HIGH, "%s: dfrgx_burst_init failed!, no utilization data\n", __func__);
		sts = -1;
		goto err_002;
	}

	DFRGX_DPF(DFRGX_DEBUG_HIGH, "%s: success\n", __func__);

	return 0;

err_002:
	thermal_cooling_device_unregister(bfdata->gbp_cooldv_hdl);
	bfdata->gbp_cooldv_hdl = NULL;
err_001:
	devfreq_remove_device(bfdata->devfreq);
err_000:
	platform_set_drvdata(pdev, NULL);
	mutex_destroy(&bfdata->lock);
	kfree(bfdata);
	return sts;
}

static int df_rgx_busfreq_remove(struct platform_device *pdev)
{
	struct busfreq_data *bfdata = platform_get_drvdata(pdev);

	dfrgx_burst_deinit(&bfdata->g_dfrgx_data);

	unregister_pm_notifier(&bfdata->pm_notifier);
	devfreq_remove_device(bfdata->devfreq);
	mutex_destroy(&bfdata->lock);
	kfree(bfdata);

	return 0;
}

static int df_rgx_busfreq_resume(struct device *dev)
{
	struct busfreq_data *bfdata = dev_get_drvdata(dev);

	DFRGX_DPF(DFRGX_DEBUG_LOW, "%s: entry\n", __func__);

	busfreq_mon_reset(bfdata);
	return 0;
}


static const struct dev_pm_ops df_rgx_busfreq_pm = {
	.resume	= df_rgx_busfreq_resume,
};

static const struct platform_device_id df_rgx_busfreq_id[] = {
	{ DF_RGX_NAME_DEV, 0 },
	{ "", 0 },
};


static struct platform_driver df_rgx_busfreq_driver = {
	.probe	= df_rgx_busfreq_probe,
	.remove	= df_rgx_busfreq_remove,
	.id_table = df_rgx_busfreq_id,
	.driver = {
		.name	= DF_RGX_NAME_DRIVER,
		.owner	= THIS_MODULE,
		.pm	= &df_rgx_busfreq_pm,
	},
};


static struct platform_device * __init df_rgx_busfreq_device_create(void)
{
	struct platform_device *pdev;
	int ret;

	pdev = platform_device_alloc(DF_RGX_NAME_DEV, -1);
	if (!pdev) {
		pr_err("%s: platform_device_alloc failed\n",
			DF_RGX_NAME_DEV);
		return NULL;
	}

	ret = platform_device_add(pdev);
	if (ret < 0) {
		pr_err("%s: platform_device_add failed\n",
			DF_RGX_NAME_DEV);
		platform_device_put(pdev);
		return ERR_PTR(ret);
	}

	return pdev;
}

static int __init df_rgx_busfreq_init(void)
{
	struct platform_device *pdev;
	int ret;

	if (!mprm_enable) {
		DFRGX_DPF(DFRGX_DEBUG_HIGH, "%s: %s: disabled\n",
			DF_RGX_NAME_DRIVER, __func__);
		return -ENODEV;
	}

	gpu_freq_set_suspend_func(&df_rgx_suspend);

	gpu_freq_set_resume_func(&df_rgx_resume);

	DFRGX_DPF(DFRGX_DEBUG_HIGH,"%s: %s: starting\n", DF_RGX_NAME_DRIVER, __func__);

	pdev = df_rgx_busfreq_device_create();
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);
	if (!pdev)
		return -ENOMEM;

	df_rgx_created_dev = pdev;

	ret = platform_driver_register(&df_rgx_busfreq_driver);

	DFRGX_DPF(DFRGX_DEBUG_HIGH, "%s: %s: success\n", DF_RGX_NAME_DRIVER, __func__);

	return ret;
}
late_initcall(df_rgx_busfreq_init);

static void __exit df_rgx_busfreq_exit(void)
{
	struct platform_device *pdev = df_rgx_created_dev;
	struct busfreq_data *bfdata = platform_get_drvdata(pdev);

	DFRGX_DPF(DFRGX_DEBUG_LOW, "%s:\n", __func__);

	if (bfdata && bfdata->gbp_cooldv_hdl) {
		thermal_cooling_device_unregister(bfdata->gbp_cooldv_hdl);
		bfdata->gbp_cooldv_hdl = NULL;
	}

	platform_driver_unregister(&df_rgx_busfreq_driver);

	/* Most state reset is done by function df_rgx_busfreq_remove,
	 * including invocation of:
	 * - unregister_pm_notifier
	 * - devfreq_remove_device
	 * - mutex_destroy(&bfdata->lock);
	 * - kfree(bfdata);
	*/

	if (pdev)
		platform_device_unregister(pdev);
}
module_exit(df_rgx_busfreq_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RGX busfreq driver with devfreq framework");
MODULE_AUTHOR("Dale B Stimson <dale.b.stimson@intel.com>");
