#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/version.h>
#include <media/v4l2-image-sizes.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>
#include <linux/fs.h>
#include <linux/slab.h>

#include "device.h"
#include "videobuf.h"

extern const char *uvc_dev_name;

struct __attribute__((__packed__)) rgb_struct {
    unsigned char r, g, b;
};

static const struct uvc_device_format uvc_supported_fmts[] = {
    {
        .name = "RGB24 (LE)",
        .fourcc = V4L2_PIX_FMT_RGB24,
        .bit_depth = 24,
    },
    {
        .name = "YUV 4:2:2 (YUYV)",
        .fourcc = V4L2_PIX_FMT_YUYV,
        .bit_depth = 16,
    },
};

static const struct v4l2_file_operations uvc_fops = {
    .owner = THIS_MODULE,
    .open = v4l2_fh_open,
    .release = vb2_fop_release,
    .read = vb2_fop_read,
    .poll = vb2_fop_poll,
    .unlocked_ioctl = video_ioctl2,
    .mmap = vb2_fop_mmap,
};

static const struct v4l2_frmsize_discrete uvc_sizes[] = {
    {480, 360},
    {VGA_WIDTH, VGA_HEIGHT},
    {HD_720_WIDTH, HD_720_HEIGHT},
};

static int load_raw_frame(struct uvc_device *dev, void *vbuf_ptr, int frame_idx)
{
    char filename[256];
    struct file *filp = NULL;
    loff_t pos = 0;
    ssize_t read_bytes;
    int cyclic_idx = frame_idx % dev->frame_count;

    snprintf(filename, sizeof(filename), "%s/output_%04d.raw", dev->frames_dir, cyclic_idx + 1);
    pr_info("Attempting to load frame from %s\n", filename);

    filp = filp_open(filename, O_RDONLY, 0);
    if (IS_ERR(filp)) {
        pr_err("Failed to open file %s\n", filename);
        return -ENOENT;
    }

    read_bytes = kernel_read(filp, vbuf_ptr, dev->output_format.sizeimage, &pos);
    filp_close(filp, NULL);

    if (read_bytes != dev->output_format.sizeimage) {
        pr_err("Failed to read full frame from %s, expected %u, got %zd\n",
               filename, dev->output_format.sizeimage, read_bytes);
        return -EIO;
    }

    uint8_t *data = (uint8_t *)vbuf_ptr;
    for (size_t i = 0; i < dev->output_format.sizeimage; i += 3) {
        uint8_t temp = data[i];
        data[i] = data[i + 2];
        data[i + 2] = temp;
    }

    pr_info("Successfully loaded frame %s\n", filename);
    return 0;
}

static void fill_with_color(struct uvc_device *dev, void *vbuf_ptr)
{
    uint8_t *data = (uint8_t *)vbuf_ptr;
    size_t bytesperline = dev->output_format.bytesperline;
    size_t width = dev->output_format.width;
    size_t height = dev->output_format.height;

    if (dev->fb_spec.color_scheme == UVC_COLOR_RGB && dev->fb_spec.bits_per_pixel == 24) {
        for (size_t i = 0; i < height; i++) {
            uint8_t *line_ptr = data + i * bytesperline;
            for (size_t j = 0; j < width * 3; j += 3) {
                line_ptr[j] = 0;
                line_ptr[j + 1] = 0;
                line_ptr[j + 2] = 255;
            }
        }
    } else if (dev->fb_spec.color_scheme == UVC_COLOR_YUV && dev->fb_spec.bits_per_pixel == 16) {
        uint8_t Y = 29;
        uint8_t U = 240;
        uint8_t V = 107;
        for (size_t i = 0; i < height; i++) {
            uint8_t *line_ptr = data + i * bytesperline;
            for (size_t j = 0; j < width * 2; j += 4) {
                line_ptr[j] = Y;
                line_ptr[j + 1] = U;
                line_ptr[j + 2] = Y;
                line_ptr[j + 3] = V;
            }
        }
    }
}

static void submit_noinput_buffer(struct uvc_out_buffer *buf, struct uvc_device *dev)
{
    void *vbuf_ptr = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);
    static int frame_idx = 0;

    if (!vbuf_ptr) {
        pr_err("NULL buffer pointer\n");
        return;
    }

    if (dev->frames_dir[0]) {
        if (load_raw_frame(dev, vbuf_ptr, frame_idx) == 0) {
            pr_debug("Loaded frame %d from %s with exp=%d, gain=%d, bpp=%d\n",
                     frame_idx + 1, dev->frames_dir,
                     dev->fb_spec.exposure, dev->fb_spec.gain, dev->fb_spec.bits_per_pixel);

            uint8_t *data = (uint8_t *)vbuf_ptr;
            size_t size = dev->output_format.sizeimage;
            int exp_factor = dev->fb_spec.exposure - 100;
            int gain_factor = dev->fb_spec.gain - 50;
            size_t step = dev->fb_spec.bits_per_pixel / 8;

            for (size_t i = 0; i < size; i += step) {
                if (dev->fb_spec.color_scheme == UVC_COLOR_RGB) {
                    if (dev->fb_spec.bits_per_pixel == 24) { // RGB24
                        for (int ch = 0; ch < 3; ++ch) {
                            int base = (data[i + ch] * (100 + exp_factor)) / 100;
                            data[i + ch] = clamp(base + (base * gain_factor) / 100, 0, 255);
                        }
                    }
                } else { // YUV (YUYV)
                    if (dev->fb_spec.bits_per_pixel == 16) {
                        int base = (data[i] * (100 + exp_factor)) / 100;
                        data[i] = clamp(base + (base * gain_factor) / 100, 0, 255);
                        base = (data[i + 2] * (100 + exp_factor)) / 100;
                        data[i + 2] = clamp(base + (base * gain_factor) / 100, 0, 255);
                        data[i + 1] = clamp((data[i + 1] * (100 + gain_factor)) / 100, 0, 255);
                        data[i + 3] = clamp((data[i + 3] * (100 + gain_factor)) / 100, 0, 255);
                    }
                }
            }

            frame_idx = (frame_idx + 1) % dev->frame_count;
        } else {
            pr_err("Failed to load frame %d, falling back to synthetic fill\n", frame_idx + 1);
            fill_with_color(dev, vbuf_ptr);
        }
    } else {
        fill_with_color(dev, vbuf_ptr);
    }

    buf->vb.vb2_buf.timestamp = ktime_get_ns();
    vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
}

static int uvc_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
    strcpy(cap->driver, uvc_dev_name);
    strcpy(cap->card, uvc_dev_name);
    strcpy(cap->bus_info, "platform: virtual");
    cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
                        V4L2_CAP_READWRITE | V4L2_CAP_DEVICE_CAPS;
    return 0;
}

static int uvc_enum_input(struct file *file, void *priv, struct v4l2_input *inp)
{
    if (inp->index >= 1)
        return -EINVAL;
    inp->type = V4L2_INPUT_TYPE_CAMERA;
    inp->capabilities = 0;
    sprintf(inp->name, "uvc_in %u", inp->index);
    return 0;
}

static int uvc_g_input(struct file *file, void *priv, unsigned int *i)
{
    *i = 0;
    return 0;
}

static int uvc_s_input(struct file *file, void *priv, unsigned int i)
{
    return (i >= 1) ? -EINVAL : 0;
}

static int uvc_enum_fmt_vid_cap(struct file *file, void *priv, struct v4l2_fmtdesc *f)
{
    struct uvc_device *dev = (struct uvc_device *)video_drvdata(file);
    if (f->index >= dev->nr_fmts)
        return -EINVAL;
    struct uvc_device_format *fmt = &dev->out_fmts[f->index];
    strcpy(f->description, fmt->name);
    f->pixelformat = fmt->fourcc;
    return 0;
}

static int uvc_g_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
    struct uvc_device *dev = (struct uvc_device *)video_drvdata(file);
    memcpy(&f->fmt.pix, &dev->output_format, sizeof(struct v4l2_pix_format));
    return 0;
}

static bool check_supported_pixfmt(struct uvc_device *dev, unsigned int fourcc)
{
    int i;
    for (i = 0; i < dev->nr_fmts; i++) {
        if (dev->out_fmts[i].fourcc == fourcc)
            break;
    }
    return (i == dev->nr_fmts) ? false : true;
}

static int uvc_try_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
    struct uvc_device *dev = (struct uvc_device *)video_drvdata(file);

    if (!check_supported_pixfmt(dev, f->fmt.pix.pixelformat)) {
        f->fmt.pix.pixelformat = dev->output_format.pixelformat;
        pr_debug("Unsupported\n");
    }

    f->fmt.pix.width = dev->output_format.width;
    f->fmt.pix.height = dev->output_format.height;
    f->fmt.pix.field = V4L2_FIELD_NONE;
    if (f->fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV) {
        f->fmt.pix.bytesperline = f->fmt.pix.width << 1;
        f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
    } else {
        f->fmt.pix.bytesperline = f->fmt.pix.width * 3;
        f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
    }
    f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * f->fmt.pix.height;

    return 0;
}

static int uvc_s_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
    int ret;
    struct uvc_device *dev = (struct uvc_device *)video_drvdata(file);

    ret = uvc_try_fmt_vid_cap(file, priv, f);
    if (ret < 0)
        return ret;

    if (check_supported_pixfmt(dev, f->fmt.pix.pixelformat)) {
        dev->output_format = f->fmt.pix;
    } else {
        f->fmt.pix = dev->output_format;
    }

    pr_debug("Resolution set to %dx%d, format set to %c%c%c%c\n",
             dev->output_format.width, dev->output_format.height,
             dev->output_format.pixelformat & 0xFF,
             (dev->output_format.pixelformat >> 8) & 0xFF,
             (dev->output_format.pixelformat >> 16) & 0xFF,
             (dev->output_format.pixelformat >> 24) & 0xFF);
    return 0;
}

static int uvc_enum_frameintervals(struct file *file, void *priv, struct v4l2_frmivalenum *fival)
{
    struct uvc_device *dev = (struct uvc_device *)video_drvdata(file);
    struct v4l2_frmival_stepwise *frm_step;

    if (fival->index > 0) {
        pr_debug("Index out of range\n");
        return -EINVAL;
    }

    if (!check_supported_pixfmt(dev, fival->pixel_format)) {
        pr_debug("Unsupported pixfmt\n");
        return -EINVAL;
    }

    if ((fival->width != dev->output_format.width) ||
        (fival->height != dev->output_format.height)) {
        pr_debug("Unsupported resolution\n");
        return -EINVAL;
    }

    fival->type = V4L2_FRMIVAL_TYPE_STEPWISE;
    frm_step = &fival->stepwise;
    frm_step->min.numerator = 1000;
    frm_step->min.denominator = 1000000;
    frm_step->max.numerator = 1000;
    frm_step->max.denominator = 1000;
    frm_step->step.numerator = 1;
    frm_step->step.denominator = 1000;

    return 0;
}

static int uvc_g_parm(struct file *file, void *priv, struct v4l2_streamparm *sp)
{
    struct uvc_device *dev;
    struct v4l2_captureparm *cp;

    if (sp->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    cp = &sp->parm.capture;
    dev = (struct uvc_device *)video_drvdata(file);

    memset(cp, 0x00, sizeof(struct v4l2_captureparm));
    cp->capability = V4L2_CAP_TIMEPERFRAME;
    cp->timeperframe = dev->output_fps;
    cp->extendedmode = 0;
    cp->readbuffers = 1;

    return 0;
}

static int uvc_s_parm(struct file *file, void *priv, struct v4l2_streamparm *sp)
{
    struct uvc_device *dev;
    struct v4l2_captureparm *cp;

    if (sp->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    cp = &sp->parm.capture;
    dev = (struct uvc_device *)video_drvdata(file);

    cp->capability = V4L2_CAP_TIMEPERFRAME;
    if (!cp->timeperframe.numerator || !cp->timeperframe.denominator)
        cp->timeperframe = dev->output_fps;
    else
        dev->output_fps = cp->timeperframe;
    cp->extendedmode = 0;
    cp->readbuffers = 1;

    pr_debug("FPS set to %d/%d\n", cp->timeperframe.numerator, cp->timeperframe.denominator);
    return 0;
}

static int uvc_enum_framesizes(struct file *file, void *priv, struct v4l2_frmsizeenum *fsize)
{
    struct v4l2_frmsize_discrete *size_discrete;
    struct uvc_device *dev = (struct uvc_device *)video_drvdata(file);

    if (!check_supported_pixfmt(dev, fsize->pixel_format))
        return -EINVAL;

    if (fsize->index > 0)
        return -EINVAL;

    fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
    size_discrete = &fsize->discrete;
    size_discrete->width = dev->output_format.width;
    size_discrete->height = dev->output_format.height;

    return 0;
}

static const struct v4l2_ioctl_ops uvc_ioctl_ops = {
    .vidioc_querycap = uvc_querycap,
    .vidioc_enum_input = uvc_enum_input,
    .vidioc_g_input = uvc_g_input,
    .vidioc_s_input = uvc_s_input,
    .vidioc_enum_fmt_vid_cap = uvc_enum_fmt_vid_cap,
    .vidioc_g_fmt_vid_cap = uvc_g_fmt_vid_cap,
    .vidioc_try_fmt_vid_cap = uvc_try_fmt_vid_cap,
    .vidioc_s_fmt_vid_cap = uvc_s_fmt_vid_cap,
    .vidioc_g_parm = uvc_g_parm,
    .vidioc_s_parm = uvc_s_parm,
    .vidioc_enum_frameintervals = uvc_enum_frameintervals,
    .vidioc_enum_framesizes = uvc_enum_framesizes,
    .vidioc_reqbufs = vb2_ioctl_reqbufs,
    .vidioc_create_bufs = vb2_ioctl_create_bufs,
    .vidioc_prepare_buf = vb2_ioctl_prepare_buf,
    .vidioc_querybuf = vb2_ioctl_querybuf,
    .vidioc_qbuf = vb2_ioctl_qbuf,
    .vidioc_dqbuf = vb2_ioctl_dqbuf,
    .vidioc_expbuf = vb2_ioctl_expbuf,
    .vidioc_streamon = vb2_ioctl_streamon,
    .vidioc_streamoff = vb2_ioctl_streamoff,
};

static struct video_device uvc_video_device_template = {
    .fops = &uvc_fops,
    .ioctl_ops = &uvc_ioctl_ops,
    .release = video_device_release,
    .tvnorms = 0,
    .device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE,
    .vfl_type = VFL_TYPE_VIDEO,
};

void fill_v4l2pixfmt(struct v4l2_pix_format *fmt, struct uvc_device_spec *dev_spec)
{
    if (!fmt || !dev_spec)
        return;

    memset(fmt, 0x00, sizeof(struct v4l2_pix_format));
    fmt->width = dev_spec->width;
    fmt->height = dev_spec->height;
    pr_debug("Filling %dx%d\n", dev_spec->width, dev_spec->height);

    if (dev_spec->color_scheme == UVC_COLOR_RGB) {
        fmt->pixelformat = V4L2_PIX_FMT_RGB24;
        fmt->bytesperline = fmt->width * 3;
        fmt->colorspace = V4L2_COLORSPACE_SRGB;
    } else if (dev_spec->color_scheme == UVC_COLOR_YUV) {
        fmt->pixelformat = V4L2_PIX_FMT_YUYV;
        fmt->bytesperline = fmt->width * 2;
        fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
    } else {
        fmt->pixelformat = V4L2_PIX_FMT_RGB24;
        fmt->bytesperline = fmt->width * 3;
        fmt->colorspace = V4L2_COLORSPACE_SRGB;
    }

    fmt->field = V4L2_FIELD_NONE;
    fmt->sizeimage = fmt->height * fmt->bytesperline;
}

int submitter_thread(void *data)
{
    unsigned long flags = 0;
    struct uvc_device *dev = (struct uvc_device *)data;
    struct uvc_out_queue *q = &dev->uvc_out_vidq;

    while (!kthread_should_stop()) {
        struct uvc_out_buffer *buf;
        int timeout_ms, timeout;

        int computation_time_jiff = jiffies;

        spin_lock_irqsave(&dev->out_q_slock, flags);
        if (list_empty(&q->active)) {
            pr_debug("Buffer queue is empty\n");
            spin_unlock_irqrestore(&dev->out_q_slock, flags);
            goto have_a_nap;
        }
        buf = list_entry(q->active.next, struct uvc_out_buffer, list);
        list_del(&buf->list);
        spin_unlock_irqrestore(&dev->out_q_slock, flags);

        spin_lock_irqsave(&dev->out_q_slock, flags);
        submit_noinput_buffer(buf, dev);
        spin_unlock_irqrestore(&dev->out_q_slock, flags);

    have_a_nap:
        if (dev->output_fps.denominator && dev->output_fps.numerator) {
            int fps = dev->output_fps.denominator / dev->output_fps.numerator;
            timeout_ms = 1000 / fps;
            if (timeout_ms <= 0) {
                timeout_ms = 1;
                pr_warn("FPS too high, using minimum timeout of 1 ms\n");
            }
        } else {
            dev->output_fps.numerator = 1001;
            dev->output_fps.denominator = 30000;
            timeout_ms = 1000 / 30;
            pr_warn("FPS not set, using default 30 fps\n");
        }

        computation_time_jiff = jiffies - computation_time_jiff;
        timeout = msecs_to_jiffies(timeout_ms);

        if (computation_time_jiff > timeout) {
            int computation_time_ms = jiffies_to_msecs(computation_time_jiff);
            pr_warn("Computation time (%d ms) exceeds timeout (%d ms), adjusting FPS\n",
                    computation_time_ms, timeout_ms);
            int new_fps = 1000 / computation_time_ms;
            dev->output_fps.denominator = 1001 * new_fps;
        } else if (timeout > computation_time_jiff) {
            if (kthread_should_stop()) {
                pr_info("Thread interrupted, stopping\n");
                break;
            }
            schedule_timeout_interruptible(timeout - computation_time_jiff);
        }
    }

    pr_info("Thread stopped\n");
    return 0;
}

struct uvc_device *create_uvc_device(size_t idx, struct uvc_device_spec *dev_spec)
{
    struct video_device *vdev;
    int i, ret = 0;

    struct uvc_device *uvc = (struct uvc_device *)kzalloc(sizeof(struct uvc_device), GFP_KERNEL);
    if (!uvc)
        goto uvc_alloc_failure;

    snprintf(uvc->v4l2_dev.name, sizeof(uvc->v4l2_dev.name), "%s-%zu", uvc_dev_name, idx);
    ret = v4l2_device_register(NULL, &uvc->v4l2_dev);
    if (ret) {
        pr_err("v4l2 registration failure\n");
        goto v4l2_registration_failure;
    }

    mutex_init(&uvc->uvc_mutex);

    ret = uvc_out_videobuf2_setup(uvc);
    if (ret) {
        pr_err("failed to initialize output videobuffer\n");
        goto vb2_out_init_failed;
    }

    spin_lock_init(&uvc->out_q_slock);
    spin_lock_init(&uvc->in_q_slock);
    spin_lock_init(&uvc->in_fh_slock);

    INIT_LIST_HEAD(&uvc->uvc_out_vidq.active);

    vdev = &uvc->vdev;
    *vdev = uvc_video_device_template;
    vdev->v4l2_dev = &uvc->v4l2_dev;
    vdev->queue = &uvc->vb_out_vidq;
    vdev->lock = &uvc->uvc_mutex;
    vdev->tvnorms = 0;
    vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;

    snprintf(vdev->name, sizeof(vdev->name), "%s-%zu", uvc_dev_name, idx);
    video_set_drvdata(vdev, uvc);

    ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
    if (ret < 0) {
        pr_err("video_register_device failure\n");
        goto video_regdev_failure;
    }

    for (i = 0; i < ARRAY_SIZE(uvc_supported_fmts); i++)
        uvc->out_fmts[i] = uvc_supported_fmts[i];
    uvc->nr_fmts = i;

    if (dev_spec) {
        uvc->fb_spec = *dev_spec;
        strncpy(uvc->frames_dir, dev_spec->frames_dir, sizeof(uvc->frames_dir) - 1);
        uvc->frame_count = dev_spec->frame_count;
    } else {
        uvc->fb_spec.width = 640;
        uvc->fb_spec.height = 480;
        uvc->fb_spec.cropratio.numerator = 1;
        uvc->fb_spec.cropratio.denominator = 1;
        uvc->fb_spec.fps = 30;
        uvc->fb_spec.exposure = 100;
        uvc->fb_spec.gain = 50;
        uvc->fb_spec.bits_per_pixel = 24;
        uvc->fb_spec.color_scheme = UVC_COLOR_RGB;
        uvc->frames_dir[0] = '\0';
        uvc->frame_count = 0;
    }

    fill_v4l2pixfmt(&uvc->output_format, &uvc->fb_spec);
    uvc->output_fps.numerator = 1001;
    uvc->output_fps.denominator = 1001 * uvc->fb_spec.fps;

    uvc->sub_thr_id = kthread_run(submitter_thread, uvc, "uvc-submitter-%zu", idx);

    return uvc;

video_regdev_failure:
    video_unregister_device(&uvc->vdev);
    video_device_release(&uvc->vdev);
vb2_out_init_failed:
    v4l2_device_unregister(&uvc->v4l2_dev);
v4l2_registration_failure:
    kfree(uvc);
uvc_alloc_failure:
    return NULL;
}

void destroy_uvc_device(struct uvc_device *uvc)
{
    if (!uvc)
        return;

    if (uvc->sub_thr_id) {
        kthread_stop(uvc->sub_thr_id);
        uvc->sub_thr_id = NULL;
        pr_info("Thread for device stopped\n");
    }
    mutex_destroy(&uvc->uvc_mutex);
    video_unregister_device(&uvc->vdev);
    v4l2_device_unregister(&uvc->v4l2_dev);
    kfree(uvc);
}