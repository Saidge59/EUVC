#ifndef EUVC_VIDEOBUF_H
#define EUVC_VIDEOBUF_H

/**
 * @file videobuf.h
 * @brief Header file defining video buffer management functions for UVC devices.
 * This file provides declarations for setting up video buffers for USB Video Class (UVC) devices.
 */

#include "device.h"

/**
 * @brief Sets up the video buffer queue for a UVC device.
 * @param[in] dev Pointer to the euvc_device structure containing device-specific data.
 * @return int Returns 0 on success, negative error code on failure.
 * @details This function initializes the video buffer queue (vb2_queue) for the specified UVC device.
 * It configures the buffer allocation and setup for video streaming, ensuring proper memory management
 * and compatibility with the V4L2 framework.
 */
int euvc_out_videobuf2_setup(struct euvc_device *dev);

#endif