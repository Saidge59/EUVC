#ifndef EUVC_CONTROL_H
#define EUVC_CONTROL_H

/**
 * @file control.h
 * @brief Header file defining control functions for UVC devices.
 * This file provides declarations for creating, destroying, and managing control devices
 * for USB Video Class (UVC) operations, including device requests.
 */

#include "euvc.h"

/**
 * @brief Creates a control device with the specified name.
 * @param[in] dev_name Pointer to a string containing the device name (e.g., "/dev/euvcctl").
 * @return int Returns 0 on success, negative error code on failure.
 * @details This function initializes and registers a control device for managing UVC devices.
 * The device name is used to create a character device node in the filesystem.
 */
int create_control_device(const char *dev_name);

/**
 * @brief Destroys the control device.
 * @return void No return value.
 * @details This function unregisters and cleans up the control device, freeing associated resources.
 * It should be called when the control device is no longer needed to avoid resource leaks.
 */
void destroy_control_device(void);

/**
 * @brief Requests a UVC device based on the provided specification.
 * @param[in,out] dev_spec Pointer to a euvc_device_spec structure containing device configuration.
 * @return int Returns 0 on success, negative error code on failure.
 * @details This function sends a request to the kernel to create or retrieve a UVC device
 * based on the parameters in the dev_spec structure. The structure may be updated with
 * device-specific information upon success.
 */
int request_euvc_device(struct euvc_device_spec *dev_spec);

#endif