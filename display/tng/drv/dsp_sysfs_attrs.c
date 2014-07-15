/**************************************************************************
 * Copyright (c) 2014, Intel Corporation.
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
 * Author:
 *   Dale B Stimson <dale.b.stimson@intel.com>
 */

#include <linux/device.h>

#include "psb_drv.h"

#include "dsp_sysfs_attrs.h"

#include <drm/drmP.h>

#include "mdfld_dsi_output.h"

/*
 * Note: As sysfs files are part of the ABI, they should be documented under
 * Documentation/ABI.  See Documentation/ABI/README.
 */

#define DSP_SYSFS_ATTRS_GROUP_NAME "display"


/*
 * _sysfs_support_fbc_show() - Return 1 if FBC/FBDC supported, else 0.
 * @kdev - Pointer to struct device
 * @attr - pointer to struct device_attribute
 * @buf - Pointer to output buffer to receive character string.
 * The buffer length is PAGE_SIZE bytes.
 */
static ssize_t _sysfs_support_fbc_show(struct device *kdev,
	struct device_attribute *attr, char *buf)
{
	const int buflen = PAGE_SIZE;
	int support_fbc;

	/* Supported for Anniedale, but not for ANN A0 and ANN B0. */
	if (!IS_ANN() || IS_ANN_A0() || IS_ANN_B0())
		support_fbc = 0;
	else
		support_fbc = 1;

	return scnprintf(buf, buflen, "%d\n", support_fbc);
}


/*
 * _sysfs_panel_mode_show() - Return panel mode as "visual", "command",
 * or "unknown"
 * @kdev - Pointer to struct device
 * @attr - pointer to struct device_attribute
 * @buf - Pointer to output buffer to receive character string.
 * The buffer length is PAGE_SIZE bytes.
 */
static ssize_t _sysfs_panel_mode_show(struct device *kdev,
	struct device_attribute *attr, char *buf)
{
	struct drm_minor *minor;
	struct drm_device *dev;
	const int buflen = PAGE_SIZE;
	const char *pms;

	minor = container_of(kdev, struct drm_minor, kdev);
	dev = minor->dev;
	if (!dev)
		return -ENODEV;

	pms = panel_mode_string(dev);

	return scnprintf(buf, buflen, "%s\n", pms);
}


static DEVICE_ATTR(panel_mode, S_IRUGO, _sysfs_panel_mode_show, NULL);

static DEVICE_ATTR(support_fbc, S_IRUGO, _sysfs_support_fbc_show, NULL);

static struct attribute *dsp_sysfs_attr_list[] = {
	&dev_attr_panel_mode.attr,
	&dev_attr_support_fbc.attr,
	NULL
};

static struct attribute_group dsp_sysfs_attr_group = {
	.name = DSP_SYSFS_ATTRS_GROUP_NAME,
	.attrs = dsp_sysfs_attr_list
};


int dsp_sysfs_attr_init(struct drm_device *dev)
{
	int ret;

	/* Initialize the sysfs entries*/
	ret = sysfs_create_group(&dev->primary->kdev.kobj,
		&dsp_sysfs_attr_group);
	if (ret) {
		DRM_ERROR("sysfs attribute group creation failed: %s: %d\n",
			DSP_SYSFS_ATTRS_GROUP_NAME, ret);
	}

	return ret;
}


void dsp_sysfs_attr_uninit(struct drm_device *dev)
{
	sysfs_remove_group(&dev->primary->kdev.kobj, &dsp_sysfs_attr_group);
}
