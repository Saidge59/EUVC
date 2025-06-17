/**
 * @file module.c
 * @brief Main module file for the virtual V4L2 compatible camera device driver.
 * This file implements the initialization and exit functions for the Emulated UVC driver module,
 * including device creation and control device setup.
 *
 * Copyright © 2025 Aplit-Soft LTD.
 * All Rights Reserved.
 *
 * THIS SOFTWARE is proprietary and confidential. Duplication or disclosure
 * without explicit written permission is prohibited.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "control.h"

MODULE_AUTHOR("Aplit-Soft LTD");
MODULE_DESCRIPTION("Virtual V4L2 compatible camera device driver");
MODULE_LICENSE("GPL");

/**
 * @def CONTROL_DEV_NAME
 * @brief Name of the control device.
 * Defines the name of the control device node (e.g., "/dev/euvcctl").
 */
#define CONTROL_DEV_NAME "euvcctl"

/**
 * @def EUVC_DEV_NAME
 * @brief Base name for UVC devices.
 * Defines the base name for video device nodes (e.g., "/dev/videoX").
 */
#define EUVC_DEV_NAME "euvc"

unsigned short devices_max = 8; // Maximum number of devices supported by the module.
unsigned short create_devices = 1; // Number of devices to create during initialization.

/**
 * @var euvc_dev_name
 * @brief Constant string for the UVC device name base.
 * Used as the prefix for naming video device nodes.
 */
const char *euvc_dev_name = EUVC_DEV_NAME;

module_param(devices_max, ushort, 0);
MODULE_PARM_DESC(devices_max, "Maximal number of devices\n");

module_param(create_devices, ushort, 0);
MODULE_PARM_DESC(create_devices,
                 "Number of devices to be created during initialization\n");

/**
 * @brief Module initialization function.
 */
static int __init euvc_init(void)
{
    int i;
    int ret = create_control_device(CONTROL_DEV_NAME);
    if (ret)
        goto failure;

    for (i = 0; i < create_devices; i++)
        request_euvc_device(NULL);

failure:
    return ret;
}

/**
 * @brief Module exit function.
 */
static void __exit euvc_exit(void)
{
    destroy_control_device();
}

module_init(euvc_init);
module_exit(euvc_exit);