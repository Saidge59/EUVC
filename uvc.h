#ifndef UVC_H
#define UVC_H

/**
 * @file uvc.h
 * @brief Header file defining UVC (USB Video Class) device control and specification structures.
 */

/**
 * @def UVC_IOC_MAGIC
 * @brief Magic number for UVC ioctl commands.
 * This value is used to identify UVC-specific ioctl operations.
 */
#define UVC_IOC_MAGIC  'v'

/**
 * @def UVC_IOCTL_CREATE_DEVICE
 * @brief Ioctl command to create a new UVC device.
 * @param _IOW(UVC_IOC_MAGIC, 0x01, struct uvc_device_spec)
 * Writes a uvc_device_spec structure to the kernel to initialize a new device.
 */
#define UVC_IOCTL_CREATE_DEVICE  _IOW(UVC_IOC_MAGIC, 0x01, struct uvc_device_spec)

/**
 * @def UVC_IOCTL_DESTROY_DEVICE
 * @brief Ioctl command to destroy an existing UVC device.
 * @param _IOW(UVC_IOC_MAGIC, 0x02, struct uvc_device_spec)
 * Writes a uvc_device_spec structure to the kernel to remove the specified device.
 */
#define UVC_IOCTL_DESTROY_DEVICE _IOW(UVC_IOC_MAGIC, 0x02, struct uvc_device_spec)

/**
 * @def UVC_IOCTL_GET_DEVICE
 * @brief Ioctl command to retrieve information about a UVC device.
 * @param _IOR(UVC_IOC_MAGIC, 0x03, struct uvc_device_spec)
 * Reads a uvc_device_spec structure from the kernel with device details.
 */
#define UVC_IOCTL_GET_DEVICE     _IOR(UVC_IOC_MAGIC, 0x03, struct uvc_device_spec)

/**
 * @def UVC_IOCTL_ENUM_DEVICES
 * @brief Ioctl command to enumerate all available UVC devices.
 * @param _IOR(UVC_IOC_MAGIC, 0x04, struct uvc_device_spec)
 * Reads a uvc_device_spec structure containing a list of available devices.
 */
#define UVC_IOCTL_ENUM_DEVICES   _IOR(UVC_IOC_MAGIC, 0x04, struct uvc_device_spec)

/**
 * @def UVC_IOCTL_MODIFY_SETTING
 * @brief Ioctl command to modify settings of an existing UVC device.
 * @param _IOW(UVC_IOC_MAGIC, 0x05, struct uvc_device_spec)
 * Writes a uvc_device_spec structure to the kernel to update device settings.
 */
#define UVC_IOCTL_MODIFY_SETTING _IOW(UVC_IOC_MAGIC, 0x05, struct uvc_device_spec)

/**
 * @struct crop_ratio
 * @brief Structure representing a crop ratio with numerator and denominator.
 */
struct crop_ratio {
    unsigned int numerator;   /**< Numerator of the crop ratio. */
    unsigned int denominator; /**< Denominator of the crop ratio. */
};

/**
 * @struct uvc_device_spec
 * @brief Structure defining the specification of a UVC device.
 * This structure is used to pass configuration and status information between user space and kernel.
 */
struct uvc_device_spec {
    unsigned int idx;           /**< Index of the device (0-based). */
    unsigned int orig_width;    /**< Original width of the frame (e.g., 800). */
    unsigned int orig_height;   /**< Original height of the frame (e.g., 700). */
    unsigned int width;         /**< Current width of the frame (after cropping or scaling). */
    unsigned int height;        /**< Current height of the frame (after cropping or scaling). */
    struct crop_ratio cropratio;/**< Crop ratio for adjusting the frame dimensions. */
    char video_node[64];        /**< Path to the video device node (e.g., "/dev/video0"). */
    int fps;                    /**< Frames per second (negative value indicates default). */
    int exposure;               /**< Exposure setting (negative value indicates default). */
    int gain;                   /**< Gain setting (negative value indicates default). */
    int bits_per_pixel;         /**< Bits per pixel (negative value indicates default, e.g., 8 or 24). */
    enum { 
        UVC_COLOR_EMPTY = -1,   /**< Placeholder for uninitialized color scheme. */
        UVC_COLOR_RGB = 0,      /**< RGB color scheme. */
        UVC_COLOR_GREY = 1      /**< Greyscale color scheme. */
    } color_scheme;             /**< Color scheme of the video frame. */
    char frames_dir[256];       /**< Directory path for frame data files. */
    int frame_idx;              /**< Current frame index (used for looping). */
    int frame_count;            /**< Total number of frames in the directory. */
    bool loop;                  /**< Flag to enable/disable frame looping. */
};

#endif