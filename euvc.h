#ifndef EUVC_H
#define EUVC_H

/**
 * @file euvc.h
 * @brief Header file defining UVC (USB Video Class) device control and specification structures.
 */

/**
 * @def EUVC_IOC_MAGIC
 * @brief Magic number for UVC ioctl commands.
 * This value is used to identify UVC-specific ioctl operations.
 */
#define EUVC_IOC_MAGIC  'v'

/**
 * @def EUVC_IOCTL_CREATE_DEVICE
 * @brief Ioctl command to create a new UVC device.
 * @param _IOW(EUVC_IOC_MAGIC, 0x01, struct euvc_device_spec)
 * Writes a euvc_device_spec structure to the kernel to initialize a new device.
 */
#define EUVC_IOCTL_CREATE_DEVICE  _IOW(EUVC_IOC_MAGIC, 0x01, struct euvc_device_spec)

/**
 * @def EUVC_IOCTL_DESTROY_DEVICE
 * @brief Ioctl command to destroy an existing UVC device.
 * @param _IOW(EUVC_IOC_MAGIC, 0x02, struct euvc_device_spec)
 * Writes a euvc_device_spec structure to the kernel to remove the specified device.
 */
#define EUVC_IOCTL_DESTROY_DEVICE _IOW(EUVC_IOC_MAGIC, 0x02, struct euvc_device_spec)

/**
 * @def EUVC_IOCTL_GET_DEVICE
 * @brief Ioctl command to retrieve information about a UVC device.
 * @param _IOR(EUVC_IOC_MAGIC, 0x03, struct euvc_device_spec)
 * Reads a euvc_device_spec structure from the kernel with device details.
 */
#define EUVC_IOCTL_GET_DEVICE     _IOR(EUVC_IOC_MAGIC, 0x03, struct euvc_device_spec)

/**
 * @def EUVC_IOCTL_ENUM_DEVICES
 * @brief Ioctl command to enumerate all available UVC devices.
 * @param _IOR(EUVC_IOC_MAGIC, 0x04, struct euvc_device_spec)
 * Reads a euvc_device_spec structure containing a list of available devices.
 */
#define EUVC_IOCTL_ENUM_DEVICES   _IOR(EUVC_IOC_MAGIC, 0x04, struct euvc_device_spec)

/**
 * @def EUVC_IOCTL_MODIFY_SETTING
 * @brief Ioctl command to modify settings of an existing UVC device.
 * @param _IOW(EUVC_IOC_MAGIC, 0x05, struct euvc_device_spec)
 * Writes a euvc_device_spec structure to the kernel to update device settings.
 */
#define EUVC_IOCTL_MODIFY_SETTING _IOW(EUVC_IOC_MAGIC, 0x05, struct euvc_device_spec)

/**
 * @struct crop_ratio
 * @brief Structure representing a crop ratio with numerator and denominator.
 */
struct crop_ratio {
    unsigned int numerator;   /**< Numerator of the crop ratio. */
    unsigned int denominator; /**< Denominator of the crop ratio. */
};

/**
 * @struct euvc_device_spec
 * @brief Structure defining the specification of a UVC device.
 * This structure is used to pass configuration and status information between user space and kernel.
 */
struct euvc_device_spec {
    unsigned int idx;           /**< Index of the device (0-based). */
    unsigned int orig_width;    /**< Original width of the frame (e.g., 800). */
    unsigned int orig_height;   /**< Original height of the frame (e.g., 700). */
    unsigned int width;         /**< Current width of the frame (after cropping or scaling). */
    unsigned int height;        /**< Current height of the frame (after cropping or scaling). */
    unsigned int fps;           /**< Frames per second (negative value indicates default). */
    unsigned int exposure;      /**< Exposure setting (negative value indicates default). */
    unsigned int gain;          /**< Gain setting (negative value indicates default). */
    unsigned int bits_per_pixel;/**< Bits per pixel (negative value indicates default, e.g., 8 or 24). */
    unsigned int frame_idx;     /**< Current frame index (used for looping). */
    unsigned int frame_count;   /**< Total number of frames in the directory. */
    unsigned int loop;          /**< Flag to enable/disable frame looping. */
    char video_node[64];        /**< Path to the video device node (e.g., "/dev/video0"). */
    struct crop_ratio cropratio;/**< Crop ratio for adjusting the frame dimensions. */
    enum { 
        EUVC_COLOR_EMPTY = -1,   /**< Placeholder for uninitialized color scheme. */
        EUVC_COLOR_RGB = 0,      /**< RGB color scheme. */
        EUVC_COLOR_GREY = 1      /**< Greyscale color scheme. */
    } color_scheme;             /**< Color scheme of the video frame. */
    char frames_dir[256];       /**< Directory path for frame data files. */
};

#endif