#ifndef UVC_H
#define UVC_H

#include <asm/types.h>

#define UVC_IOC_MAGIC  'v'

#define UVC_IOCTL_CREATE_DEVICE  _IOW(UVC_IOC_MAGIC, 0x01, struct uvc_device_spec)
#define UVC_IOCTL_DESTROY_DEVICE _IOW(UVC_IOC_MAGIC, 0x02, struct uvc_device_spec)
#define UVC_IOCTL_GET_DEVICE     _IOR(UVC_IOC_MAGIC, 0x03, struct uvc_device_spec)
#define UVC_IOCTL_ENUM_DEVICES   _IOR(UVC_IOC_MAGIC, 0x04, struct uvc_device_spec)
#define UVC_IOCTL_MODIFY_SETTING _IOW(UVC_IOC_MAGIC, 0x05, struct uvc_device_spec)

struct crop_ratio {
    __u32 numerator;
    __u32 denominator;
};

struct uvc_device_spec {
    unsigned int idx;
    __u32 width, height;
    struct crop_ratio cropratio;
    char video_node[64];
    char fb_node[64];
    int fps;
    int exposure;
    int gain;
    int bits_per_pixel;
    enum { UVC_COLOR_RGB = 0, UVC_COLOR_YUV = 1 } color_scheme;
    char frames_dir[256];
    int frame_count;
};

#endif
