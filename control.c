#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#define DEBUG

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "control.h"
#include "device.h"

extern unsigned short devices_max;

struct control_device {
    int major;
    dev_t dev_number;
    struct class *dev_class;
    struct device *device;
    struct cdev cdev;
    struct uvc_device **uvc_devices;
    size_t uvc_device_count;
    spinlock_t uvc_devices_lock;
};

static struct control_device *ctldev = NULL;

static int control_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int control_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t control_read(struct file *file,
                            char __user *buffer,
                            size_t length,
                            loff_t *offset)
{
    int len;
    static const char *str = "Virtual V4L2 compatible camera device\n";
    pr_debug("read %p %dB\n", buffer, (int) length);
    len = strlen(str);
    if (len < length)
        len = length;
    if (copy_to_user(buffer, str, len) != 0)
        pr_warn("Failed to copy_to_user!");
    return len;
}

static ssize_t control_write(struct file *file,
                             const char __user *buffer,
                             size_t length,
                             loff_t *offset)
{
    pr_debug("write %p %dB\n", buffer, (int) length);
    return length;
}

static int control_iocontrol_get_device(struct uvc_device_spec *dev_spec)
{
    struct uvc_device *dev;

    if (ctldev->uvc_device_count <= dev_spec->idx)
        return -EINVAL;

    dev = ctldev->uvc_devices[dev_spec->idx];
    dev_spec->width = dev->output_format.width;
    dev_spec->height = dev->output_format.height;
    dev_spec->cropratio = dev->fb_spec.cropratio;

    snprintf((char *)&dev_spec->video_node, sizeof(dev_spec->video_node),
             "/dev/video%d", dev->vdev.num);

    dev_spec->fps = dev->output_fps.denominator / dev->output_fps.numerator;
    dev_spec->exposure = dev->fb_spec.exposure;
    dev_spec->gain = dev->fb_spec.gain;
    dev_spec->bits_per_pixel = dev->fb_spec.bits_per_pixel;
    dev_spec->color_scheme = dev->fb_spec.color_scheme;

    return 0;
}

static int control_iocontrol_destroy_device(struct uvc_device_spec *dev_spec)
{
    struct uvc_device *dev;
    unsigned long flags = 0;
    int i;

    if (ctldev->uvc_device_count <= dev_spec->idx)
        return -EINVAL;

    dev = ctldev->uvc_devices[dev_spec->idx];

    spin_lock_irqsave(&dev->in_fh_slock, flags);
    if (vb2_is_busy(&dev->vb_out_vidq)) {
        spin_unlock_irqrestore(&dev->in_fh_slock, flags);
        return -EBUSY;
    }
    spin_unlock_irqrestore(&dev->in_fh_slock, flags);

    spin_lock_irqsave(&ctldev->uvc_devices_lock, flags);
    for (i = dev_spec->idx; i < (ctldev->uvc_device_count); i++)
        ctldev->uvc_devices[i] = ctldev->uvc_devices[i + 1];
    ctldev->uvc_devices[--ctldev->uvc_device_count] = NULL;
    spin_unlock_irqrestore(&ctldev->uvc_devices_lock, flags);

    destroy_uvc_device(dev);

    return 0;
}

static int control_iocontrol_modify_device(struct uvc_device_spec *dev_spec)
{
    struct uvc_device *uvc;
    unsigned long flags = 0;

    if (ctldev->uvc_device_count <= dev_spec->idx) {
        pr_err("Device index %d out of range (max %zu)\n", dev_spec->idx, ctldev->uvc_device_count - 1);
        return -EINVAL;
    }

    uvc = ctldev->uvc_devices[dev_spec->idx];
    if (!uvc) {
        pr_err("Device with index %d not found\n", dev_spec->idx);
        return -ENODEV;
    }

    spin_lock_irqsave(&ctldev->uvc_devices_lock, flags);

    if (dev_spec->width && dev_spec->height) {
        uvc->fb_spec.width = dev_spec->width;
        uvc->fb_spec.height = dev_spec->height;
        uvc->output_format.width = dev_spec->width;
        uvc->output_format.height = dev_spec->height;
        uvc->output_format.bytesperline = uvc->output_format.width * (uvc->fb_spec.bits_per_pixel / 8);
        uvc->output_format.sizeimage = uvc->output_format.bytesperline * uvc->output_format.height;
        pr_debug("Modified resolution to %dx%d, bytesperline=%d, sizeimage=%d\n",
                 uvc->output_format.width, uvc->output_format.height,
                 uvc->output_format.bytesperline, uvc->output_format.sizeimage);
    }

    if (dev_spec->fps > 0) {
        uvc->output_fps.numerator = 1001;
        uvc->output_fps.denominator = 1001 * dev_spec->fps;
    }
    if (dev_spec->exposure >= 0) uvc->fb_spec.exposure = dev_spec->exposure;
    if (dev_spec->gain >= 0) uvc->fb_spec.gain = dev_spec->gain;
    if (dev_spec->bits_per_pixel > 0) {
        uvc->fb_spec.bits_per_pixel = dev_spec->bits_per_pixel;
        if (dev_spec->bits_per_pixel == 24) {
            uvc->fb_spec.color_scheme = UVC_COLOR_RGB;
        } else if (dev_spec->bits_per_pixel == 16) {
            uvc->fb_spec.color_scheme = UVC_COLOR_YUV;
        }
        fill_v4l2pixfmt(&uvc->output_format, &uvc->fb_spec);
    }
    if (dev_spec->color_scheme != -1) {
        uvc->fb_spec.color_scheme = dev_spec->color_scheme;
        if (dev_spec->color_scheme == UVC_COLOR_RGB) {
            uvc->fb_spec.bits_per_pixel = 24;
        } else if (dev_spec->color_scheme == UVC_COLOR_YUV) {
            uvc->fb_spec.bits_per_pixel = 16;
        }
        fill_v4l2pixfmt(&uvc->output_format, &uvc->fb_spec);
    }
    if (dev_spec->frames_dir[0]) {
        strncpy(uvc->frames_dir, dev_spec->frames_dir, sizeof(uvc->frames_dir) - 1);
        uvc->frame_count = dev_spec->frame_count;
    }

    spin_unlock_irqrestore(&ctldev->uvc_devices_lock, flags);
    return 0;
}

static long control_ioctl(struct file *file,
                          unsigned int iocontrol_cmd,
                          unsigned long iocontrol_param)
{
    struct uvc_device_spec dev_spec;
    long ret = copy_from_user(&dev_spec, (void __user *) iocontrol_param,
                              sizeof(struct uvc_device_spec));
    if (ret != 0) {
        pr_warn("Failed to copy_from_user!");
        return -1;
    }
    switch (iocontrol_cmd) {
    case UVC_IOCTL_CREATE_DEVICE:
        pr_debug("Requesting new device\n");
        ret = request_uvc_device(&dev_spec);
        break;
    case UVC_IOCTL_DESTROY_DEVICE:
        pr_debug("Requesting removal of device\n");
        ret = control_iocontrol_destroy_device(&dev_spec);
        break;
    case UVC_IOCTL_GET_DEVICE:
        pr_debug("Get device(%d)\n", dev_spec.idx);
        ret = control_iocontrol_get_device(&dev_spec);
        if (!ret) {
            if (copy_to_user((void *__user *) iocontrol_param, &dev_spec,
                             sizeof(struct uvc_device_spec)) != 0) {
                pr_warn("Failed to copy_to_user!");
                ret = -1;
            }
        }
        break;
    case UVC_IOCTL_MODIFY_SETTING:
        pr_debug("Requesting modification of device settings\n");
        ret = control_iocontrol_modify_device(&dev_spec);
        break;
    default:
        ret = -EINVAL;
    }
    return ret;
}

int request_uvc_device(struct uvc_device_spec *dev_spec)
{
    struct uvc_device *uvc;
    int idx;
    unsigned long flags = 0;

    if (!ctldev)
        return -ENODEV;

    if (ctldev->uvc_device_count >= devices_max)
        return -ENOMEM;

    uvc = create_uvc_device(ctldev->uvc_device_count, dev_spec);

    if (!uvc)
        return -ENODEV;

    spin_lock_irqsave(&ctldev->uvc_devices_lock, flags);
    idx = ctldev->uvc_device_count++;
    ctldev->uvc_devices[idx] = uvc;
    spin_unlock_irqrestore(&ctldev->uvc_devices_lock, flags);
    return 0;
}

static struct control_device *alloc_control_device(void)
{
    struct control_device *res =
        (struct control_device *) kmalloc(sizeof(*res), GFP_KERNEL);
    if (!res)
        goto return_res;

    res->uvc_devices = (struct uvc_device **) kmalloc(
        sizeof(struct uvc_device *) * devices_max, GFP_KERNEL);
    if (!(res->uvc_devices))
        goto uvc_alloc_failure;
    memset(res->uvc_devices, 0x00,
           sizeof(struct uvc_devices *) * devices_max);
    res->uvc_device_count = 0;

    return res;

uvc_alloc_failure:
    kfree(res);
    res = NULL;
return_res:
    return res;
}

static void free_control_device(struct control_device *dev)
{
    size_t i;
    for (i = 0; i < dev->uvc_device_count; i++)
        destroy_uvc_device(dev->uvc_devices[i]);
    kfree(dev->uvc_devices);
    device_destroy(dev->dev_class, dev->dev_number);
    class_destroy(dev->dev_class);
    cdev_del(&dev->cdev);
    unregister_chrdev_region(dev->dev_number, 1);
    kfree(dev);
}

static struct file_operations control_fops = {
    .owner = THIS_MODULE,
    .read = control_read,
    .write = control_write,
    .open = control_open,
    .release = control_release,
    .unlocked_ioctl = control_ioctl,
};

int __init create_control_device(const char *dev_name)
{
    int ret = 0;

    ctldev = alloc_control_device();
    if (!ctldev) {
        pr_err("kmalloc_failed\n");
        ret = -ENOMEM;
        goto kmalloc_failure;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    ctldev->dev_class = class_create(dev_name);
#else
    ctldev->dev_class = class_create(THIS_MODULE, dev_name);
#endif
    if (!(ctldev->dev_class)) {
        pr_err("Error creating device class for control device\n");
        ret = -ENODEV;
        goto class_create_failure;
    }

    cdev_init(&ctldev->cdev, &control_fops);
    ctldev->cdev.owner = THIS_MODULE;

    ret = alloc_chrdev_region(&ctldev->dev_number, 0, 1, dev_name);
    if (ret) {
        pr_err("Error allocating device number\n");
        goto alloc_chrdev_error;
    }

    ret = cdev_add(&ctldev->cdev, ctldev->dev_number, 1);
    pr_debug("cdev_add returned %d", ret);
    if (ret < 0) {
        pr_err("device registration failure\n");
        goto registration_failure;
    }

    ctldev->device = device_create(ctldev->dev_class, NULL, ctldev->dev_number,
                                   NULL, dev_name, MINOR(ctldev->dev_number));
    if (!ctldev->device) {
        pr_err("device_create failed\n");
        ret = -ENODEV;
        goto device_create_failure;
    }

    spin_lock_init(&ctldev->uvc_devices_lock);

    return 0;
device_create_failure:
    cdev_del(&ctldev->cdev);
registration_failure:
    unregister_chrdev_region(ctldev->dev_number, 1);
alloc_chrdev_error:
    class_destroy(ctldev->dev_class);
class_create_failure:
    free_control_device(ctldev);
    ctldev = NULL;
kmalloc_failure:
    return ret;
}

void __exit destroy_control_device(void)
{
    if (ctldev) {
        free_control_device(ctldev);
        ctldev = NULL;
    }
}