#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/version.h>
#include <media/v4l2-image-sizes.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>

#include "device.h"
#include "videobuf.h"

extern const char *vcam_dev_name;

struct __attribute__((__packed__)) rgb_struct {
    unsigned char r, g, b;
};

static const struct vcam_device_format vcam_supported_fmts[] = {
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

static const struct v4l2_file_operations vcam_fops = {
    .owner = THIS_MODULE,
    .open = v4l2_fh_open,
    .release = vb2_fop_release,
    .read = vb2_fop_read,
    .poll = vb2_fop_poll,
    .unlocked_ioctl = video_ioctl2,
    .mmap = vb2_fop_mmap,
};

static const struct v4l2_frmsize_discrete vcam_sizes[] = {
    {480, 360},
    {VGA_WIDTH, VGA_HEIGHT},
    {HD_720_WIDTH, HD_720_HEIGHT},
};

static int vcam_querycap(struct file *file,
                         void *priv,
                         struct v4l2_capability *cap)
{
    strcpy(cap->driver, vcam_dev_name);
    strcpy(cap->card, vcam_dev_name);
    strcpy(cap->bus_info, "platform: virtual");
    cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
                        V4L2_CAP_READWRITE | V4L2_CAP_DEVICE_CAPS;

    return 0;
}

static int vcam_enum_input(struct file *file,
                           void *priv,
                           struct v4l2_input *inp)
{
    if (inp->index >= 1)
        return -EINVAL;

    inp->type = V4L2_INPUT_TYPE_CAMERA;
    inp->capabilities = 0;
    sprintf(inp->name, "vcam_in %u", inp->index);
    return 0;
}

static int vcam_g_input(struct file *file, void *priv, unsigned int *i)
{
    *i = 0;
    return 0;
}

static int vcam_s_input(struct file *file, void *priv, unsigned int i)
{
    return (i >= 1) ? -EINVAL : 0;
}

static int vcam_enum_fmt_vid_cap(struct file *file,
                                 void *priv,
                                 struct v4l2_fmtdesc *f)
{
    struct vcam_device *dev = (struct vcam_device *) video_drvdata(file);
    if (f->index >= dev->nr_fmts)
        return -EINVAL;

    struct vcam_device_format *fmt = &dev->out_fmts[f->index];
    strcpy(f->description, fmt->name);
    f->pixelformat = fmt->fourcc;
    return 0;
}

static int vcam_g_fmt_vid_cap(struct file *file,
                              void *priv,
                              struct v4l2_format *f)
{
    struct vcam_device *dev = (struct vcam_device *) video_drvdata(file);
    memcpy(&f->fmt.pix, &dev->output_format, sizeof(struct v4l2_pix_format));
    return 0;
}

static bool check_supported_pixfmt(struct vcam_device *dev, unsigned int fourcc)
{
    int i;
    for (i = 0; i < dev->nr_fmts; i++) {
        if (dev->out_fmts[i].fourcc == fourcc)
            break;
    }

    return (i == dev->nr_fmts) ? false : true;
}

static int vcam_try_fmt_vid_cap(struct file *file,
                                void *priv,
                                struct v4l2_format *f)
{
    struct vcam_device *dev = (struct vcam_device *) video_drvdata(file);

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
    } else { // Assume RGB24
        f->fmt.pix.bytesperline = f->fmt.pix.width * 3;
        f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
    }
    f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * f->fmt.pix.height;

    return 0;
}

static int vcam_s_fmt_vid_cap(struct file *file,
                              void *priv,
                              struct v4l2_format *f)
{
    int ret;
    struct vcam_device *dev = (struct vcam_device *) video_drvdata(file);

    ret = vcam_try_fmt_vid_cap(file, priv, f);
    if (ret < 0)
        return ret;

    // Only update if the requested format is supported
    if (check_supported_pixfmt(dev, f->fmt.pix.pixelformat)) {
        dev->output_format = f->fmt.pix;
    } else {
        // Keep the current format if the requested one is unsupported
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

static int vcam_enum_frameintervals(struct file *file,
                                    void *priv,
                                    struct v4l2_frmivalenum *fival)
{
    struct v4l2_frmival_stepwise *frm_step;
    struct vcam_device *dev = (struct vcam_device *) video_drvdata(file);

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
    frm_step->min.numerator = 1001;
    frm_step->min.denominator = 60000;
    frm_step->max.numerator = 1001;
    frm_step->max.denominator = 1001;
    frm_step->step.numerator = 1001;
    frm_step->step.denominator = 60000;

    return 0;
}

static int vcam_g_parm(struct file *file,
                       void *priv,
                       struct v4l2_streamparm *sp)
{
    struct vcam_device *dev;
    struct v4l2_captureparm *cp;

    if (sp->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    cp = &sp->parm.capture;
    dev = (struct vcam_device *) video_drvdata(file);

    memset(cp, 0x00, sizeof(struct v4l2_captureparm));
    cp->capability = V4L2_CAP_TIMEPERFRAME;
    cp->timeperframe = dev->output_fps;
    cp->extendedmode = 0;
    cp->readbuffers = 1;

    return 0;
}

static int vcam_s_parm(struct file *file,
                       void *priv,
                       struct v4l2_streamparm *sp)
{
    struct vcam_device *dev;
    struct v4l2_captureparm *cp;

    if (sp->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    cp = &sp->parm.capture;
    dev = (struct vcam_device *) video_drvdata(file);

    cp->capability = V4L2_CAP_TIMEPERFRAME;
    if (!cp->timeperframe.numerator || !cp->timeperframe.denominator)
        cp->timeperframe = dev->output_fps;
    else
        dev->output_fps = cp->timeperframe;
    cp->extendedmode = 0;
    cp->readbuffers = 1;

    pr_debug("FPS set to %d/%d\n", cp->timeperframe.numerator,
             cp->timeperframe.denominator);
    return 0;
}

static int vcam_enum_framesizes(struct file *file,
                                void *priv,
                                struct v4l2_frmsizeenum *fsize)
{
    struct v4l2_frmsize_discrete *size_discrete;
    struct vcam_device *dev = (struct vcam_device *) video_drvdata(file);

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

static const struct v4l2_ioctl_ops vcam_ioctl_ops = {
    .vidioc_querycap = vcam_querycap,
    .vidioc_enum_input = vcam_enum_input,
    .vidioc_g_input = vcam_g_input,
    .vidioc_s_input = vcam_s_input,
    .vidioc_enum_fmt_vid_cap = vcam_enum_fmt_vid_cap,
    .vidioc_g_fmt_vid_cap = vcam_g_fmt_vid_cap,
    .vidioc_try_fmt_vid_cap = vcam_try_fmt_vid_cap,
    .vidioc_s_fmt_vid_cap = vcam_s_fmt_vid_cap,
    .vidioc_g_parm = vcam_g_parm,
    .vidioc_s_parm = vcam_s_parm,
    .vidioc_enum_frameintervals = vcam_enum_frameintervals,
    .vidioc_enum_framesizes = vcam_enum_framesizes,
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

static const struct video_device vcam_video_device_template = {
    .fops = &vcam_fops,
    .ioctl_ops = &vcam_ioctl_ops,
    .release = video_device_release_empty,
};

static void fill_v4l2pixfmt(struct v4l2_pix_format *fmt,
                            struct vcam_device_spec *dev_spec)
{
    if (!fmt || !dev_spec)
        return;

    memset(fmt, 0x00, sizeof(struct v4l2_pix_format));
    fmt->width = dev_spec->width;
    fmt->height = dev_spec->height;
    pr_debug("Filling %dx%d\n", dev_spec->width, dev_spec->height);

    switch (dev_spec->pix_fmt) {
    case VCAM_PIXFMT_RGB24:
        fmt->pixelformat = V4L2_PIX_FMT_RGB24;
        fmt->bytesperline = fmt->width * 3;
        fmt->colorspace = V4L2_COLORSPACE_SRGB;
        break;
    case VCAM_PIXFMT_YUYV:
        fmt->pixelformat = V4L2_PIX_FMT_YUYV;
        fmt->bytesperline = fmt->width << 1;
        fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
        break;
    default:
        fmt->pixelformat = V4L2_PIX_FMT_RGB24;
        fmt->bytesperline = fmt->width * 3;
        fmt->colorspace = V4L2_COLORSPACE_SRGB;
        break;
    }

    fmt->field = V4L2_FIELD_NONE;
    fmt->sizeimage = fmt->height * fmt->bytesperline;
}

static void submit_noinput_buffer(struct vcam_out_buffer *buf,
                                 struct vcam_device *dev)
{
    void *vbuf_ptr = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);
    size_t size = dev->output_format.sizeimage;
    size_t bytesperline = dev->output_format.bytesperline;
    size_t width = dev->output_format.width;
    size_t height = dev->output_format.height;

    pr_debug("Buffer size=%zu, bytesperline=%zu, width=%zu, height=%zu, format=%c%c%c%c\n",
             size, bytesperline, width, height,
             dev->output_format.pixelformat & 0xFF,
             (dev->output_format.pixelformat >> 8) & 0xFF,
             (dev->output_format.pixelformat >> 16) & 0xFF,
             (dev->output_format.pixelformat >> 24) & 0xFF);

    if (dev->output_format.pixelformat == V4L2_PIX_FMT_YUYV) {
        uint8_t *yuyv_data = (uint8_t *)vbuf_ptr;

        pr_debug("YUYV: filling %zu x %zu, bytesperline=%zu, size=%zu\n", width, height, bytesperline, size);
        for (size_t i = 0; i < height; i++) {
            uint8_t *line_ptr = yuyv_data + i * bytesperline;
            for (size_t j = 0; j < width * 2; j += 4) { // 2 bytes per pixel, 4 bytes per pair
                line_ptr[j] = 50;     // Y0
                line_ptr[j + 1] = 255;  // V
                line_ptr[j + 2] = 50; // Y1
                line_ptr[j + 3] = 0;   // U
            }
        }
    } else if (dev->output_format.pixelformat == V4L2_PIX_FMT_RGB24) {
        uint8_t *rgb_data = (uint8_t *)vbuf_ptr;

        pr_debug("RGB24: filling %zu x %zu, bytesperline=%zu, size=%zu\n", width, height, bytesperline, size);
        for (size_t i = 0; i < height; i++) {
            uint8_t *line_ptr = rgb_data + i * bytesperline;
            for (size_t j = 0; j < width * 3; j += 3) { // 3 bytes per pixel
                line_ptr[j] = 0;   // R
                line_ptr[j + 1] = 255; // G
                line_ptr[j + 2] = 0;   // B
            }
        }
    } else {
        pr_err("Unsupported pixel format\n");
    }

    buf->vb.vb2_buf.timestamp = ktime_get_ns();
    vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
}

int submitter_thread(void *data)
{
    unsigned long flags = 0;
    struct vcam_device *dev = (struct vcam_device *) data;
    struct vcam_out_queue *q = &dev->vcam_out_vidq;

    while (!kthread_should_stop()) {
        struct vcam_out_buffer *buf;
        int timeout_ms, timeout;

        int computation_time_jiff = jiffies;
        spin_lock_irqsave(&dev->out_q_slock, flags);
        if (list_empty(&q->active)) {
            pr_debug("Buffer queue is empty\n");
            spin_unlock_irqrestore(&dev->out_q_slock, flags);
            goto have_a_nap;
        }
        buf = list_entry(q->active.next, struct vcam_out_buffer, list);
        list_del(&buf->list);
        spin_unlock_irqrestore(&dev->out_q_slock, flags);

        submit_noinput_buffer(buf, dev);

    have_a_nap:
        if (!dev->output_fps.denominator) {
            dev->output_fps.numerator = 1001;
            dev->output_fps.denominator = 30000;
        }
        timeout_ms = dev->output_fps.denominator / dev->output_fps.numerator;
        if (!timeout_ms) {
            dev->output_fps.numerator = 1001;
            dev->output_fps.denominator = 60000;
            timeout_ms = dev->output_fps.denominator / dev->output_fps.numerator;
        }

        computation_time_jiff = jiffies - computation_time_jiff;
        timeout = msecs_to_jiffies(timeout_ms);
        if (computation_time_jiff > timeout) {
            int computation_time_ms = jiffies_to_msecs(computation_time_jiff);
            dev->output_fps.numerator = 1001;
            dev->output_fps.denominator = 1000 * computation_time_ms;
        } else if (timeout > computation_time_jiff) {
            schedule_timeout_interruptible(timeout - computation_time_jiff);
        }
    }

    return 0;
}

struct vcam_device *create_vcam_device(size_t idx,
                                       struct vcam_device_spec *dev_spec)
{
    struct video_device *vdev;
    int i, ret = 0;

    struct vcam_device *vcam =
        (struct vcam_device *) kzalloc(sizeof(struct vcam_device), GFP_KERNEL);
    if (!vcam)
        goto vcam_alloc_failure;

    snprintf(vcam->v4l2_dev.name, sizeof(vcam->v4l2_dev.name), "%s-%d",
             vcam_dev_name, (int) idx);
    ret = v4l2_device_register(NULL, &vcam->v4l2_dev);
    if (ret) {
        pr_err("v4l2 registration failure\n");
        goto v4l2_registration_failure;
    }

    mutex_init(&vcam->vcam_mutex);

    ret = vcam_out_videobuf2_setup(vcam);
    if (ret) {
        pr_err("failed to initialize output videobuffer\n");
        goto vb2_out_init_failed;
    }

    spin_lock_init(&vcam->out_q_slock);
    spin_lock_init(&vcam->in_q_slock);
    spin_lock_init(&vcam->in_fh_slock);

    INIT_LIST_HEAD(&vcam->vcam_out_vidq.active);

    vdev = &vcam->vdev;
    *vdev = vcam_video_device_template;
    vdev->v4l2_dev = &vcam->v4l2_dev;
    vdev->queue = &vcam->vb_out_vidq;
    vdev->lock = &vcam->vcam_mutex;
    vdev->tvnorms = 0;
    vdev->device_caps =
        V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;

    snprintf(vdev->name, sizeof(vdev->name), "%s-%d", vcam_dev_name, (int) idx);
    video_set_drvdata(vdev, vcam);

    ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
    if (ret < 0) {
        pr_err("video_register_device failure\n");
        goto video_regdev_failure;
    }

    for (i = 0; i < ARRAY_SIZE(vcam_supported_fmts); i++)
        vcam->out_fmts[i] = vcam_supported_fmts[i];
    vcam->nr_fmts = i;

    dev_spec->width = 640;
    dev_spec->height = 480;
    dev_spec->pix_fmt = VCAM_PIXFMT_RGB24;
    dev_spec->xres_virtual = dev_spec->width;
    dev_spec->yres_virtual = dev_spec->height;
    dev_spec->cropratio.numerator = 1;
    dev_spec->cropratio.denominator = 1;

    vcam->fb_spec = *dev_spec;

    fill_v4l2pixfmt(&vcam->output_format, dev_spec);
    vcam->output_format.pixelformat = V4L2_PIX_FMT_RGB24;
    vcam->output_fps.numerator = 1001;
    vcam->output_fps.denominator = 30000;

    return vcam;

video_regdev_failure:
    video_unregister_device(&vcam->vdev);
    video_device_release(&vcam->vdev);
vb2_out_init_failed:
    v4l2_device_unregister(&vcam->v4l2_dev);
v4l2_registration_failure:
    kfree(vcam);
vcam_alloc_failure:
    return NULL;
}

void destroy_vcam_device(struct vcam_device *vcam)
{
    if (!vcam)
        return;

    if (vcam->sub_thr_id)
        kthread_stop(vcam->sub_thr_id);
    mutex_destroy(&vcam->vcam_mutex);
    video_unregister_device(&vcam->vdev);
    v4l2_device_unregister(&vcam->v4l2_dev);

    kfree(vcam);
}