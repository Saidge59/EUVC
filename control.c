#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <media/v4l2-event.h>
#include "control.h"
#include "device.h"
#include "videobuf.h"

extern unsigned short devices_max;

struct control_device {
    int major;
    dev_t dev_number;
    struct class *dev_class;
    struct device *device;
    struct cdev cdev;
    struct euvc_device **euvc_devices;
    size_t euvc_device_count;
    spinlock_t euvc_devices_lock;
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

static int load_raw_frame(struct euvc_device *dev, void *vbuf_ptr, int frame_idx)
{
    char filename[256];
    struct file *filp = NULL;
    loff_t pos = 0;
    ssize_t read_bytes;
    const size_t orig_size = dev->fb_spec.orig_width * dev->fb_spec.orig_height * (dev->fb_spec.bits_per_pixel / 8);

    snprintf(filename, sizeof(filename), "%soutput_%04d.raw", dev->fb_spec.frames_dir, frame_idx + 1);
    pr_debug("Attempting to load frame from %s\n", filename);

    filp = filp_open(filename, O_RDONLY, 0);
    if (IS_ERR(filp)) {
        pr_err("Failed to open file %s\n", filename);
        return -ENOENT;
    }

    read_bytes = kernel_read(filp, vbuf_ptr, orig_size, &pos);
    filp_close(filp, NULL);

    if (read_bytes != orig_size) {
        pr_err("Failed to read full frame from %s, expected %zu, got %zd\n",
               filename, orig_size, read_bytes);
        return -EIO;
    }

    pr_debug("Successfully loaded frame %s, size=%zu\n", filename, orig_size);
    return 0;
}

static int prepare_raw_frames(struct euvc_device *euvc)
{
    int ret;

    if (euvc->fb_spec.frame_count_old) {
        free_frames_buffer(euvc);
    }

    const size_t frame_size = euvc->fb_spec.orig_width * euvc->fb_spec.orig_height * (euvc->fb_spec.bits_per_pixel / 8);
    euvc->fb_spec.buffer_size = euvc->fb_spec.frame_count * frame_size;
    euvc->fb_spec.buffer = vmalloc(euvc->fb_spec.buffer_size);
    if (!euvc->fb_spec.buffer) {
        pr_err("Failed to allocate virtual frame buffer of size %zu\n", euvc->fb_spec.buffer_size);
        return -ENOMEM;
    }

    euvc->fb_spec.frames_buffer = kmalloc_array(euvc->fb_spec.frame_count, sizeof(void *), GFP_KERNEL);
    if (!euvc->fb_spec.frames_buffer) {
        vfree(euvc->fb_spec.buffer);
        euvc->fb_spec.buffer = NULL;
        pr_err("Failed to allocate frame buffer array\n");
        return -ENOMEM;
    }

    for (int i = 0; i < euvc->fb_spec.frame_count; i++) {
        euvc->fb_spec.frames_buffer[i] = euvc->fb_spec.buffer + (i * frame_size);
    }

    for (int i = 0; i < euvc->fb_spec.frame_count; i++) {
        ret = load_raw_frame(euvc, euvc->fb_spec.frames_buffer[i], i);
        if (ret) {
            pr_err("Failed to load frame %d\n", i);
            for (int j = 0; j < i; j++) {
                kfree(euvc->fb_spec.frames_buffer[j]);
            }
            kfree(euvc->fb_spec.frames_buffer);
            vfree(euvc->fb_spec.buffer);
            euvc->fb_spec.frames_buffer = NULL;
            euvc->fb_spec.buffer = NULL;
            return ret;
        }
    }
    euvc->fb_spec.frame_count_old = euvc->fb_spec.frame_count;
    pr_info("Successfully loaded %d frames from %s\n", euvc->fb_spec.frame_count, euvc->fb_spec.frames_dir);
    
    return 0;
}

static int control_iocontrol_get_device(struct euvc_device_spec *dev_spec)
{
    struct euvc_device *dev;

    if (ctldev->euvc_device_count <= dev_spec->idx)
        return -EINVAL;

    dev = ctldev->euvc_devices[dev_spec->idx];
    dev_spec->orig_width = dev->fb_spec.orig_width;
    dev_spec->orig_height = dev->fb_spec.orig_height;
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
    dev_spec->loop = dev->fb_spec.loop;

    return 0;
}

static int control_iocontrol_modify_input_setting(struct euvc_device_spec *dev_spec)
{
    struct euvc_device *euvc;
    unsigned long flags = 0;

    if (ctldev->euvc_device_count <= dev_spec->idx) {
        pr_err("Device index %d out of range (max %zu)\n", dev_spec->idx, ctldev->euvc_device_count - 1);
        return -EINVAL;
    }

    euvc = ctldev->euvc_devices[dev_spec->idx];
    if (!euvc) {
        pr_err("Device with index %d not found\n", dev_spec->idx);
        return -ENODEV;
    }

    spin_lock_irqsave(&ctldev->euvc_devices_lock, flags);

    set_crop_resolution(&dev_spec->width, &dev_spec->height, dev_spec->cropratio);

    euvc->fb_spec.cropratio = dev_spec->cropratio;

    if (dev_spec->width && dev_spec->height) {
        euvc->fb_spec.width = dev_spec->width;
        euvc->fb_spec.height = dev_spec->height;
        euvc->output_format.width = dev_spec->width;
        euvc->output_format.height = dev_spec->height;
        euvc->output_format.bytesperline = euvc->output_format.width * (euvc->fb_spec.bits_per_pixel / 8);
        euvc->output_format.sizeimage = euvc->output_format.bytesperline * euvc->output_format.height;
        pr_info("Modified resolution %dx%d to %dx%dx%d/%d, bytesperline=%d, sizeimage=%d\n",
                euvc->fb_spec.orig_width, euvc->fb_spec.orig_height,
                euvc->output_format.width, euvc->output_format.height,
                dev_spec->cropratio.numerator, dev_spec->cropratio.denominator,
                euvc->output_format.bytesperline, euvc->output_format.sizeimage);
    }

    if (dev_spec->fps > 0) {
        euvc->output_fps.numerator = 1000;
        euvc->output_fps.denominator = 1000 * dev_spec->fps;
    }
    if (dev_spec->exposure >= 0) euvc->fb_spec.exposure = dev_spec->exposure;
    if (dev_spec->gain >= 0) euvc->fb_spec.gain = dev_spec->gain;
    if (dev_spec->bits_per_pixel > 0) {
        euvc->fb_spec.bits_per_pixel = dev_spec->bits_per_pixel;
        if (dev_spec->bits_per_pixel == 24) {
            euvc->fb_spec.color_scheme = EUVC_COLOR_RGB;
        } else if (dev_spec->bits_per_pixel == 8) {
            euvc->fb_spec.color_scheme = EUVC_COLOR_GREY;
        }
        fill_v4l2pixfmt(&euvc->output_format, &euvc->fb_spec);
    }
    if (dev_spec->color_scheme != -1) {
        euvc->fb_spec.color_scheme = dev_spec->color_scheme;
        if (dev_spec->color_scheme == EUVC_COLOR_RGB) {
            euvc->fb_spec.bits_per_pixel = 24;
        } else if (dev_spec->color_scheme == EUVC_COLOR_GREY) {
            euvc->fb_spec.bits_per_pixel = 8;
        }
        fill_v4l2pixfmt(&euvc->output_format, &euvc->fb_spec);
    }
    
    euvc->fb_spec.loop = dev_spec->loop; 

    euvc->output_format.pixelformat = (euvc->fb_spec.color_scheme == EUVC_COLOR_GREY) ? V4L2_PIX_FMT_GREY : V4L2_PIX_FMT_RGB24;
    euvc->output_format.bytesperline = euvc->output_format.width * (euvc->fb_spec.bits_per_pixel / 8);
    euvc->output_format.sizeimage = euvc->output_format.bytesperline * euvc->output_format.height;

    spin_unlock_irqrestore(&ctldev->euvc_devices_lock, flags);

    if (dev_spec->frames_dir[0] && dev_spec->frame_count) {
        strncpy(euvc->fb_spec.frames_dir, dev_spec->frames_dir, sizeof(euvc->fb_spec.frames_dir) - 1);
        euvc->fb_spec.frames_dir[sizeof(euvc->fb_spec.frames_dir) - 1] = '\0';
        euvc->fb_spec.frame_count = dev_spec->frame_count;
        return prepare_raw_frames(euvc);
    }

    return 0;
}

static int control_iocontrol_destroy_device(struct euvc_device_spec *dev_spec)
{
    struct euvc_device *dev;
    unsigned long flags = 0;
    int i;

    if (ctldev->euvc_device_count <= dev_spec->idx)
        return -EINVAL;

    dev = ctldev->euvc_devices[dev_spec->idx];

    pr_info("USB disconnect, device number %d\n", dev_spec->idx + 1);
    v4l2_event_queue(&dev->vdev, &dev->disconnect_event);
    free_frames_buffer(dev);

    spin_lock_irqsave(&ctldev->euvc_devices_lock, flags);
    for (i = dev_spec->idx; i < (ctldev->euvc_device_count); i++)
        ctldev->euvc_devices[i] = ctldev->euvc_devices[i + 1];
    ctldev->euvc_devices[--ctldev->euvc_device_count] = NULL;
    spin_unlock_irqrestore(&ctldev->euvc_devices_lock, flags);

    destroy_euvc_device(dev);

    return 0;
}

static long control_ioctl(struct file *file,
                          unsigned int iocontrol_cmd,
                          unsigned long iocontrol_param)
{
    struct euvc_device_spec dev_spec;
    long ret = copy_from_user(&dev_spec, (void __user *) iocontrol_param,
                              sizeof(struct euvc_device_spec));
    if (ret != 0) {
        pr_warn("Failed to copy_from_user!");
        return -1;
    }
    switch (iocontrol_cmd) {
    case EUVC_IOCTL_CREATE_DEVICE:
        pr_info("Requesting new device\n");
        ret = request_euvc_device(&dev_spec);
        break;
    case EUVC_IOCTL_DESTROY_DEVICE:
        pr_info("Requesting removal of device\n");
        ret = control_iocontrol_destroy_device(&dev_spec);
        break;
    case EUVC_IOCTL_GET_DEVICE:
        pr_debug("Get device(%d)\n", dev_spec.idx);
        ret = control_iocontrol_get_device(&dev_spec);
        if (!ret) {
            if (copy_to_user((void *__user *) iocontrol_param, &dev_spec,
                             sizeof(struct euvc_device_spec)) != 0) {
                pr_warn("Failed to copy_to_user!");
                ret = -1;
            }
        }
        break;
    case EUVC_IOCTL_MODIFY_SETTING:
        ret = control_iocontrol_modify_input_setting(&dev_spec);
        break;
    default:
        ret = -EINVAL;
    }
    return ret;
}

static struct euvc_device_spec default_euvc_spec = {
    .width = 800,
    .height = 700,
    .cropratio = {.numerator = 1, .denominator = 1},
    .fps = 30,
    .exposure = 100,
    .gain = 50,
    .bits_per_pixel = 8,
    .color_scheme = EUVC_COLOR_GREY,
    .frames_dir[0] = '\0',
    .frame_count = 0,
    .frame_count_old = 0,
    .loop = 1
};

int request_euvc_device(struct euvc_device_spec *dev_spec)
{
    struct euvc_device *euvc;
    int idx;
    unsigned long flags = 0;

    if (!ctldev)
        return -ENODEV;

    if (ctldev->euvc_device_count > devices_max)
        return -ENOMEM;

    if (!dev_spec)
        euvc = create_euvc_device(ctldev->euvc_device_count, &default_euvc_spec);
    else {
        euvc = create_euvc_device(ctldev->euvc_device_count, dev_spec);
        if (dev_spec->frames_dir[0] && dev_spec->frame_count) {
            prepare_raw_frames(euvc);
        }
    }

    if (!euvc)
        return -ENODEV;

    spin_lock_irqsave(&ctldev->euvc_devices_lock, flags);
    idx = ctldev->euvc_device_count++;
    ctldev->euvc_devices[idx] = euvc;
    spin_unlock_irqrestore(&ctldev->euvc_devices_lock, flags);
    return 0;
}

static struct control_device *alloc_control_device(void)
{
    struct control_device *res =
        (struct control_device *) kmalloc(sizeof(*res), GFP_KERNEL);
    if (!res)
        goto return_res;

    res->euvc_devices = (struct euvc_device **) kmalloc(
        sizeof(struct euvc_device *) * devices_max, GFP_KERNEL);
    if (!(res->euvc_devices))
        goto euvc_alloc_failure;
    memset(res->euvc_devices, 0x00,
           sizeof(struct euvc_devices *) * devices_max);
    res->euvc_device_count = 0;

    return res;

euvc_alloc_failure:
    kfree(res);
    res = NULL;
return_res:
    return res;
}

static void free_control_device(struct control_device *dev)
{
    size_t i;
    for (i = 0; i < dev->euvc_device_count; i++)
        destroy_euvc_device(dev->euvc_devices[i]);
    kfree(dev->euvc_devices);
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

    spin_lock_init(&ctldev->euvc_devices_lock);

    return 0;
device_create_failure:
    cdev_del(&ctldev->cdev);
registration_failure:
    unregister_chrdev_region(ctldev->dev_number, 1);
    class_destroy(ctldev->dev_class);
alloc_chrdev_error:
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
