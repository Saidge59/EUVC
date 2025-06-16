#ifndef UVC_VIDEOBUF_H
#define UVC_VIDEOBUF_H

/**
 * @file uvc_videobuf.h
 * @brief Header file defining video buffer management functions for UVC devices.
 * This file provides declarations for setting up video buffers for USB Video Class (UVC) devices.
 */

#include "device.h"

/**
 * @brief Sets up the video buffer queue for a UVC device.
 * @param[in] dev Pointer to the uvc_device structure containing device-specific data.
 * @return int Returns 0 on success, negative error code on failure.
 * @details This function initializes the video buffer queue (vb2_queue) for the specified UVC device.
 * It configures the buffer allocation and setup for video streaming, ensuring proper memory management
 * and compatibility with the V4L2 framework.
 */
int uvc_out_videobuf2_setup(struct uvc_device *dev);

#endif