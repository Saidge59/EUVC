#ifndef EUVC_DEVICE_H
#define EUVC_DEVICE_H

/**
 * @file device.h
 * @brief Header file defining UVC device structures and function prototypes.
 * This file provides declarations for UVC (USB Video Class) device management, including
 * device structures, buffer handling, and related utility functions.
 */

#include <linux/version.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-rect.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-v4l2.h>

#include "euvc.h"

/**
 * @def PIXFMTS_MAX
 * @brief Maximum number of supported pixel formats.
 * Defines the maximum size of the out_fmts array in the euvc_device structure.
 */
#define PIXFMTS_MAX 4

/**
 * @def FB_NAME_MAXLENGTH
 * @brief Maximum length of the frame buffer name.
 * Defines the maximum length of names stored in the euvc_device_format structure.
 */
#define FB_NAME_MAXLENGTH 16

/**
 * @def EUVC_EVENT_DISCONNECT
 * @brief Custom V4L2 event code for device disconnection.
 * Offset from V4L2_EVENT_PRIVATE_START to indicate a device disconnect event.
 */
#define EUVC_EVENT_DISCONNECT (V4L2_EVENT_PRIVATE_START + 0)

/**
 * @brief Conditional compilation for older kernel versions.
 * Adjusts VFL_TYPE_VIDEO and HD resolution constants for kernels older than 5.7.0.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 7, 0)
#define VFL_TYPE_VIDEO VFL_TYPE_GRABBER
#define HD_720_WIDTH 1280
#define HD_720_HEIGHT 720
#endif

/**
 * @struct euvc_out_buffer
 * @brief Structure representing an output buffer for UVC device.
 */
struct euvc_out_buffer {
    struct vb2_v4l2_buffer vb; /**< V4L2 video buffer structure. */
    struct list_head list;     /**< List head for buffer queue management. */
    size_t filled;             /**< Number of bytes filled in the buffer. */
};

/**
 * @struct euvc_out_queue
 * @brief Structure managing the output buffer queue.
 */
struct euvc_out_queue {
    struct list_head active; /**< List of active output buffers. */
    int frame;               /**< Current frame index in the queue. */
};

/**
 * @struct euvc_device_format
 * @brief Structure defining a supported video format.
 */
struct euvc_device_format {
    char *name;     /**< Name of the format (e.g., "GREY" or "RGB"). */
    int fourcc;     /**< FourCC code identifying the pixel format. */
    int bit_depth;  /**< Bit depth of the format (e.g., 8 or 24). */
};

/**
 * @struct euvc_device
 * @brief Main structure representing a UVC device.
 * Contains device configuration, buffers, and synchronization mechanisms.
 */
struct euvc_device {
    dev_t dev_number;           /**< Device number assigned to the UVC device. */
    struct v4l2_device v4l2_dev;/**< V4L2 device structure for video operations. */
    struct video_device vdev;   /**< Video device structure for V4L2 framework. */

    struct vb2_queue vb_out_vidq;/**< Video buffer queue for output. */
    struct euvc_out_queue euvc_out_vidq; /**< Output buffer queue management. */
    spinlock_t out_q_slock;     /**< Spinlock for synchronizing output queue access. */
    struct v4l2_fract output_fps;/**< Output frame rate (numerator/denominator). */

    struct mutex euvc_mutex;     /**< Mutex for thread-safe device operations. */

    struct task_struct *sub_thr_id; /**< Pointer to the submitter thread. */

    size_t nr_fmts;             /**< Number of supported formats in out_fmts. */
    struct euvc_device_format out_fmts[PIXFMTS_MAX]; /**< Array of supported formats. */

    struct euvc_device_spec fb_spec; /**< Device specification (frame buffer settings). */
    struct v4l2_pix_format output_format; /**< Current output pixel format. */

    struct v4l2_event disconnect_event; /**< Event for device disconnection. */
};

/**
 * @brief Creates a new UVC device instance.
 * @param[in] idx Index of the device to create.
 * @param[in] dev_spec Pointer to a euvc_device_spec structure with device configuration.
 * @return struct euvc_device* Pointer to the created UVC device, NULL on failure.
 * @details Allocates and initializes a new UVC device based on the provided specification.
 * The device is registered with the V4L2 framework.
 */
struct euvc_device *create_euvc_device(size_t idx, struct euvc_device_spec *dev_spec);

/**
 * @brief Destroys a UVC device instance.
 * @param[in] euvc Pointer to the euvc_device structure to destroy.
 * @return void No return value.
 * @details Unregisters and frees the resources associated with the specified UVC device.
 */
void destroy_euvc_device(struct euvc_device *euvc);

/**
 * @brief Thread function for submitting video frames.
 * @param[in] data Pointer to the euvc_device structure for thread context.
 * @return int Returns 0 on success, negative error code on failure.
 * @details Runs as a kernel thread to manage the submission of video frames to the output queue.
 */
int submitter_thread(void *data);

/**
 * @brief Fills a v4l2_pix_format structure based on device specification.
 * @param[out] fmt Pointer to the v4l2_pix_format structure to fill.
 * @param[in] dev_spec Pointer to a euvc_device_spec structure with format details.
 * @return void No return value.
 * @details Configures the pixel format structure with width, height, bytesperline, sizeimage,
 * and other parameters based on the device specification.
 */
void fill_v4l2pixfmt(struct v4l2_pix_format *fmt, struct euvc_device_spec *dev_spec);

/**
 * @brief Sets the resolution based on a crop ratio.
 * @param[in,out] width Pointer to the width value to adjust.
 * @param[in,out] height Pointer to the height value to adjust.
 * @param[in] cropratio Crop ratio structure defining the adjustment.
 * @return void No return value.
 * @details Adjusts the width and height parameters according to the specified crop ratio,
 * ensuring the aspect ratio is maintained.
 */
void set_crop_resolution(__u32 *width, __u32 *height, struct crop_ratio cropratio);

/**
 * @brief Frees the memory allocated for the frames buffer.
 * @param[in] euvc Pointer to the euvc_device structure.
 * @return void No return value.
 * @details Releases any memory resources associated with the frames buffer
 * within the specified UVC device. Ensures that all dynamically allocated
 * frame data is properly deallocated to prevent memory leaks.
 */
void free_frames_buffer(struct euvc_device *euvc);

#endif