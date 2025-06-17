#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/version.h>
#include <media/v4l2-image-sizes.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>
#include <media/v4l2-event.h>

#include "device.h"
#include "videobuf.h"

extern const char *euvc_dev_name;

struct __attribute__((__packed__)) rgb_struct {
    unsigned char r, g, b;
};

static const struct euvc_device_format euvc_supported_fmts[] = {
    {
        .name = "RGB24 (LE)",
        .fourcc = V4L2_PIX_FMT_RGB24,
        .bit_depth = 24,
    },
    {
        .name = "GREY (8-bit)",
        .fourcc = V4L2_PIX_FMT_GREY,
        .bit_depth = 8,
    },
};

static const struct v4l2_file_operations euvc_fops = {
    .owner = THIS_MODULE,
    .open = v4l2_fh_open,
    .release = vb2_fop_release,
    .read = vb2_fop_read,
    .poll = vb2_fop_poll,
    .unlocked_ioctl = video_ioctl2,
    .mmap = vb2_fop_mmap,
};

static const struct v4l2_frmsize_discrete euvc_sizes[] = {
    {480, 360},
    {VGA_WIDTH, VGA_HEIGHT},
    {HD_720_WIDTH, HD_720_HEIGHT},
};

static int euvc_querycap(struct file *file,
                         void *priv,
                         struct v4l2_capability *cap)
{
    strcpy(cap->driver, euvc_dev_name);
    strcpy(cap->card, euvc_dev_name);
    strcpy(cap->bus_info, "platform: virtual");
    cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
                        V4L2_CAP_READWRITE | V4L2_CAP_DEVICE_CAPS;

    return 0;
}

static int euvc_enum_input(struct file *file,
                           void *priv,
                           struct v4l2_input *inp)
{
    if (inp->index >= 1)
        return -EINVAL;

    inp->type = V4L2_INPUT_TYPE_CAMERA;
    inp->capabilities = 0;
    sprintf(inp->name, "euvc_in %u", inp->index);
    return 0;
}

static int euvc_g_input(struct file *file, void *priv, unsigned int *i)
{
    *i = 0;
    return 0;
}

static int euvc_s_input(struct file *file, void *priv, unsigned int i)
{
    return (i >= 1) ? -EINVAL : 0;
}

static int euvc_enum_fmt_vid_cap(struct file *file,
                                 void *priv,
                                 struct v4l2_fmtdesc *f)
{
    struct euvc_device_format *fmt;
    struct euvc_device *dev = (struct euvc_device *) video_drvdata(file);
    int idx = f->index;

    if (idx >= dev->nr_fmts)
        return -EINVAL;

    fmt = &dev->out_fmts[idx];
    strcpy(f->description, fmt->name);
    f->pixelformat = fmt->fourcc;
    return 0;
}

static int euvc_g_fmt_vid_cap(struct file *file,
                              void *priv,
                              struct v4l2_format *f)
{
    struct euvc_device *dev = (struct euvc_device *) video_drvdata(file);
    memcpy(&f->fmt.pix, &dev->output_format, sizeof(struct v4l2_pix_format));
    return 0;
}

static bool check_supported_pixfmt(struct euvc_device *dev, unsigned int fourcc)
{
    int i;
    for (i = 0; i < dev->nr_fmts; i++) {
        if (dev->out_fmts[i].fourcc == fourcc)
            break;
    }

    return (i == dev->nr_fmts) ? false : true;
}

void set_crop_resolution(__u32 *width,
                                __u32 *height,
                                struct crop_ratio cropratio)
{
    struct v4l2_rect crop = {0, 0, 0, 0};
    struct v4l2_rect r = {0, 0, *width, *height};
    struct v4l2_rect min_r = {
        0, 0, r.width * cropratio.numerator / cropratio.denominator,
        r.height * cropratio.numerator / cropratio.denominator};
    struct v4l2_rect max_r = {0, 0, r.width, r.height};
    v4l2_rect_set_min_size(&crop, &min_r);
    v4l2_rect_set_max_size(&crop, &max_r);

    *width = crop.width;
    *height = crop.height;
}

static int euvc_try_fmt_vid_cap(struct file *file,
                                void *priv,
                                struct v4l2_format *f)
{
    struct euvc_device *dev = (struct euvc_device *)video_drvdata(file);

    if (dev->fb_spec.color_scheme == EUVC_COLOR_GREY && dev->fb_spec.bits_per_pixel == 8) {
        f->fmt.pix.pixelformat = V4L2_PIX_FMT_GREY;
        f->fmt.pix.bytesperline = f->fmt.pix.width;
        f->fmt.pix.sizeimage = f->fmt.pix.width * f->fmt.pix.height;
        f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
    } else if (!check_supported_pixfmt(dev, f->fmt.pix.pixelformat)) {
        f->fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
        f->fmt.pix.bytesperline = f->fmt.pix.width * 3;
        f->fmt.pix.sizeimage = f->fmt.pix.width * f->fmt.pix.height * 3;
        f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
    }

    f->fmt.pix.width = dev->output_format.width;
    f->fmt.pix.height = dev->output_format.height;

    f->fmt.pix.field = V4L2_FIELD_NONE;
    if (f->fmt.pix.pixelformat == V4L2_PIX_FMT_GREY) {
        f->fmt.pix.bytesperline = f->fmt.pix.width;
        f->fmt.pix.sizeimage = f->fmt.pix.width * f->fmt.pix.height;
    } else {
        f->fmt.pix.bytesperline = f->fmt.pix.width * 3;
        f->fmt.pix.sizeimage = f->fmt.pix.width * f->fmt.pix.height * 3;
    }

    return 0;
}

static int euvc_s_fmt_vid_cap(struct file *file,
                              void *priv,
                              struct v4l2_format *f)
{
    int ret;
    struct euvc_device *dev = (struct euvc_device *)video_drvdata(file);

    ret = euvc_try_fmt_vid_cap(file, priv, f);
    if (ret < 0)
        return ret;

    dev->output_format = f->fmt.pix;
    dev->fb_spec.width = f->fmt.pix.width;
    dev->fb_spec.height = f->fmt.pix.height;
    if (f->fmt.pix.pixelformat == V4L2_PIX_FMT_GREY) {
        dev->fb_spec.bits_per_pixel = 8;
        dev->fb_spec.color_scheme = EUVC_COLOR_GREY;
    } else {
        dev->fb_spec.bits_per_pixel = 24;
        dev->fb_spec.color_scheme = EUVC_COLOR_RGB;
    }
    fill_v4l2pixfmt(&dev->output_format, &dev->fb_spec);

    pr_debug("Resolution set to %dx%d, format set to %c%c%c%c\n",
             dev->output_format.width, dev->output_format.height,
             (f->fmt.pix.pixelformat & 0xff),
             (f->fmt.pix.pixelformat >> 8 & 0xff),
             (f->fmt.pix.pixelformat >> 16 & 0xff),
             (f->fmt.pix.pixelformat >> 24 & 0xff));

    vb2_queue_release(&dev->vb_out_vidq);
    euvc_out_videobuf2_setup(dev);

    return 0;
}

static int euvc_enum_frameintervals(struct file *file,
                                    void *priv,
                                    struct v4l2_frmivalenum *fival)
{
    struct v4l2_frmival_stepwise *frm_step;
    struct euvc_device *dev = (struct euvc_device *) video_drvdata(file);

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

    if ((fival->width % 2) || (fival->height % 2)) {
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

static int euvc_g_parm(struct file *file,
                       void *priv,
                       struct v4l2_streamparm *sp)
{
    struct euvc_device *dev;
    struct v4l2_captureparm *cp;

    if (sp->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    cp = &sp->parm.capture;
    dev = (struct euvc_device *) video_drvdata(file);

    memset(cp, 0x00, sizeof(struct v4l2_captureparm));
    cp->capability = V4L2_CAP_TIMEPERFRAME;
    cp->timeperframe = dev->output_fps;
    cp->extendedmode = 0;
    cp->readbuffers = 1;

    return 0;
}

static int euvc_s_parm(struct file *file,
                       void *priv,
                       struct v4l2_streamparm *sp)
{
    struct euvc_device *dev;
    struct v4l2_captureparm *cp;

    if (sp->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    cp = &sp->parm.capture;
    dev = (struct euvc_device *) video_drvdata(file);

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

static int euvc_enum_framesizes(struct file *file,
                                void *priv,
                                struct v4l2_frmsizeenum *fsize)
{
    struct v4l2_frmsize_discrete *size_discrete;

    struct euvc_device *dev = (struct euvc_device *) video_drvdata(file);
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

static const struct v4l2_ioctl_ops euvc_ioctl_ops = {
    .vidioc_querycap = euvc_querycap,
    .vidioc_enum_input = euvc_enum_input,
    .vidioc_g_input = euvc_g_input,
    .vidioc_s_input = euvc_s_input,
    .vidioc_enum_fmt_vid_cap = euvc_enum_fmt_vid_cap,
    .vidioc_g_fmt_vid_cap = euvc_g_fmt_vid_cap,
    .vidioc_try_fmt_vid_cap = euvc_try_fmt_vid_cap,
    .vidioc_s_fmt_vid_cap = euvc_s_fmt_vid_cap,
    .vidioc_g_parm = euvc_g_parm,
    .vidioc_s_parm = euvc_s_parm,
    .vidioc_enum_frameintervals = euvc_enum_frameintervals,
    .vidioc_enum_framesizes = euvc_enum_framesizes,
    .vidioc_reqbufs = vb2_ioctl_reqbufs,
    .vidioc_create_bufs = vb2_ioctl_create_bufs,
    .vidioc_prepare_buf = vb2_ioctl_prepare_buf,
    .vidioc_querybuf = vb2_ioctl_querybuf,
    .vidioc_qbuf = vb2_ioctl_qbuf,
    .vidioc_dqbuf = vb2_ioctl_dqbuf,
    .vidioc_expbuf = vb2_ioctl_expbuf,
    .vidioc_streamon = vb2_ioctl_streamon,
    .vidioc_streamoff = vb2_ioctl_streamoff};

static const struct video_device euvc_video_device_template = {
    .fops = &euvc_fops,
    .ioctl_ops = &euvc_ioctl_ops,
    .release = video_device_release_empty,
};

void fill_v4l2pixfmt(struct v4l2_pix_format *fmt, struct euvc_device_spec *dev_spec)
{
    if (!fmt || !dev_spec)
        return;

    memset(fmt, 0x00, sizeof(struct v4l2_pix_format));
    fmt->width = dev_spec->width;
    fmt->height = dev_spec->height;
    pr_debug("Filling %dx%d\n", dev_spec->width, dev_spec->height);

    if (dev_spec->bits_per_pixel == 8 && dev_spec->color_scheme == EUVC_COLOR_GREY) {
        fmt->pixelformat = V4L2_PIX_FMT_GREY;
        fmt->bytesperline = fmt->width;
        fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
    } else {
        fmt->pixelformat = V4L2_PIX_FMT_RGB24;
        fmt->bytesperline = fmt->width * 3;
        fmt->colorspace = V4L2_COLORSPACE_SRGB;
    }

    fmt->field = V4L2_FIELD_NONE;
    fmt->sizeimage = fmt->bytesperline * fmt->height;
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

static void fill_with_color(struct euvc_device *dev, void *vbuf_ptr)
{
    uint8_t *data = (uint8_t *)vbuf_ptr;
    size_t bytesperline = dev->output_format.bytesperline;
    size_t width = dev->output_format.width;
    size_t height = dev->output_format.height;

    if (dev->fb_spec.bits_per_pixel == 24) {
        for (size_t i = 0; i < height; i++) {
            uint8_t *line_ptr = data + i * bytesperline;
            for (size_t j = 0; j < width * 3; j += 3) {
                line_ptr[j] = 255;
                line_ptr[j + 1] = 0;
                line_ptr[j + 2] = 0;
            }
        }
    } else if (dev->fb_spec.bits_per_pixel == 8) {
        for (size_t i = 0; i < height; i++) {
            uint8_t *line_ptr = data + i * bytesperline;
            for (size_t j = 0; j < width; j++) {
                line_ptr[j] = 128;
            }
        }
    }
}

static void submit_noinput_buffer(struct euvc_out_buffer *buf, struct euvc_device *dev)
{
    void *vbuf_ptr = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);

    if (!vbuf_ptr) {
        pr_err("NULL buffer pointer\n");
        return;
    }

    if (dev->fb_spec.frames_dir[0]) {
        uint8_t *temp_data = kmalloc(dev->fb_spec.orig_width * dev->fb_spec.orig_height * (dev->fb_spec.bits_per_pixel / 8), GFP_KERNEL);
        if (!temp_data) {
            pr_err("Failed to allocate temp buffer\n");
            fill_with_color(dev, vbuf_ptr);
            goto done;
        }

        if (load_raw_frame(dev, temp_data, dev->fb_spec.frame_idx) == 0) {
            pr_debug("Loaded frame %d with fps=%d, bpp=%d\n",
                    dev->fb_spec.frame_idx + 1, dev->output_fps.denominator / dev->output_fps.numerator,
                    dev->fb_spec.bits_per_pixel);

            int orig_width = dev->fb_spec.orig_width;
            int orig_height = dev->fb_spec.orig_height;
            int target_width = dev->output_format.width;
            int target_height = dev->output_format.height;
            int bpp = dev->fb_spec.bits_per_pixel / 8;
            size_t bytesperline = dev->output_format.bytesperline;
            size_t sizeimage = dev->output_format.sizeimage;

            int start_x = (orig_width - target_width) / 2;
            int start_y = (orig_height - target_height) / 2;
            if (start_x < 0) start_x = 0;
            if (start_y < 0) start_y = 0;

            uint8_t *data = (uint8_t *)vbuf_ptr;

            int max_y = min(target_height, orig_height - start_y);

            for (int y = 0; y < max_y; y++) {
                size_t src_offset = (start_y + y) * orig_width * bpp + start_x * bpp;
                size_t dst_offset = y * bytesperline;
                size_t copy_size = target_width * bpp;
                size_t dst_line_end = dst_offset + copy_size;

                pr_debug("Copying row y=%d, src_offset=%zu, dst_offset=%zu, copy_size=%zu, dst_line_end=%zu\n",
                         y, src_offset, dst_offset, copy_size, dst_line_end);

                if (dst_line_end > sizeimage) {
                    pr_err("Buffer overflow detected at y=%d, dst_line_end=%zu, sizeimage=%zu\n",
                           y, dst_line_end, sizeimage);
                    break;
                }
                if (src_offset + copy_size > orig_width * orig_height * bpp) {
                    pr_err("Source overflow at y=%d, src_offset=%zu, copy_size=%zu\n",
                           y, src_offset, copy_size);
                    break;
                }

                memcpy(data + dst_offset,
                       temp_data + src_offset,
                       copy_size);

                if (copy_size < bytesperline) {
                    memset(data + dst_offset + copy_size, 0, bytesperline - copy_size);
                    pr_debug("Cleared padding at y=%d, offset=%zu, size=%zu\n",
                             y, dst_offset + copy_size, bytesperline - copy_size);
                }
            }

            size_t size = target_width * target_height * bpp;
            int exp_factor = dev->fb_spec.exposure - 100;
            int gain_factor = dev->fb_spec.gain - 50;

            for (size_t i = 0; i < size; i += bpp) {
                for (int ch = 0; ch < bpp; ++ch) {
                    int base = (data[i + ch] * (100 + exp_factor)) / 100;
                    data[i + ch] = clamp(base + (base * gain_factor) / 100, 0, 255);
                }
            }

            if (dev->fb_spec.loop) {
                dev->fb_spec.frame_idx = (dev->fb_spec.frame_idx + 1) % dev->fb_spec.frame_count;
            } else if (dev->fb_spec.frame_idx < dev->fb_spec.frame_count - 1) {
                dev->fb_spec.frame_idx++;
            }
        } else {
            pr_err("Failed to load frame %d, falling back to synthetic fill\n", dev->fb_spec.frame_idx + 1);
            fill_with_color(dev, vbuf_ptr);
        }
        kfree(temp_data);
    } else {
        fill_with_color(dev, vbuf_ptr);
    }

done:
    buf->vb.vb2_buf.timestamp = ktime_get_ns();
    vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
}

int submitter_thread(void *data)
{
    unsigned long flags = 0;
    struct euvc_device *dev = (struct euvc_device *) data;
    struct euvc_out_queue *q = &dev->euvc_out_vidq;

    while (!kthread_should_stop()) {
        struct euvc_out_buffer *buf;
        int timeout_ms, timeout;

        /* Do something and sleep */
        int computation_time_jiff = jiffies;
        spin_lock_irqsave(&dev->out_q_slock, flags);
        if (list_empty(&q->active)) {
            pr_debug("Buffer queue is empty\n");
            spin_unlock_irqrestore(&dev->out_q_slock, flags);
            goto have_a_nap;
        }
        buf = list_entry(q->active.next, struct euvc_out_buffer, list);
        list_del(&buf->list);
        spin_unlock_irqrestore(&dev->out_q_slock, flags);

        submit_noinput_buffer(buf, dev);

    have_a_nap:
        if (dev->output_fps.denominator && dev->output_fps.numerator) {
            int fps = dev->output_fps.denominator / dev->output_fps.numerator;

            timeout_ms = 1000 / fps;
            if (timeout_ms <= 0) {
                timeout_ms = 1;
                pr_warn("FPS too high, using minimum timeout of 1 ms\n");
            }
        } else {
            dev->output_fps.numerator = 1000;
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
            dev->output_fps.denominator = 1000 * new_fps;
        } else if (timeout > computation_time_jiff) {
            if (kthread_should_stop()) {
                pr_info("Thread interrupted, stopping\n");
                break;
            }
            schedule_timeout_interruptible(timeout - computation_time_jiff);
        }
    }

    return 0;
}

struct euvc_device *create_euvc_device(size_t idx,
                                       struct euvc_device_spec *dev_spec)
{
    struct video_device *vdev;
    int i, ret = 0;

    struct euvc_device *euvc =
        (struct euvc_device *) kzalloc(sizeof(struct euvc_device), GFP_KERNEL);
    if (!euvc)
        goto euvc_alloc_failure;

    snprintf(euvc->v4l2_dev.name, sizeof(euvc->v4l2_dev.name), "%s-%d",
             euvc_dev_name, (int) idx);
    ret = v4l2_device_register(NULL, &euvc->v4l2_dev);
    if (ret) {
        pr_err("v4l2 registration failure\n");
        goto v4l2_registration_failure;
    }

    mutex_init(&euvc->euvc_mutex);

    ret = euvc_out_videobuf2_setup(euvc);
    if (ret) {
        pr_err(" failed to initialize output videobuffer\n");
        goto vb2_out_init_failed;
    }

    spin_lock_init(&euvc->out_q_slock);

    INIT_LIST_HEAD(&euvc->euvc_out_vidq.active);

    vdev = &euvc->vdev;
    *vdev = euvc_video_device_template;
    vdev->v4l2_dev = &euvc->v4l2_dev;
    vdev->queue = &euvc->vb_out_vidq;
    vdev->lock = &euvc->euvc_mutex;
    vdev->tvnorms = 0;
    vdev->device_caps =
        V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;

    snprintf(vdev->name, sizeof(vdev->name), "%s-%d", euvc_dev_name, (int) idx);
    video_set_drvdata(vdev, euvc);

    ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);

    if (ret < 0) {
        pr_err("video_register_device failure\n");
        goto video_regdev_failure;
    }

    for (i = 0; i < ARRAY_SIZE(euvc_supported_fmts); i++)
        euvc->out_fmts[i] = euvc_supported_fmts[i];
    euvc->nr_fmts = ARRAY_SIZE(euvc_supported_fmts);

    euvc->fb_spec = *dev_spec;
    strncpy(euvc->fb_spec.frames_dir, dev_spec->frames_dir, sizeof(euvc->fb_spec.frames_dir) - 1);
    euvc->fb_spec.frame_count = dev_spec->frame_count;

    euvc->fb_spec.orig_width = dev_spec->width;
    euvc->fb_spec.orig_height = dev_spec->height;

    fill_v4l2pixfmt(&euvc->output_format, &euvc->fb_spec);
    euvc->output_format.width = dev_spec->width;
    euvc->output_format.height = dev_spec->height;

    euvc->sub_thr_id = NULL;

    euvc->output_fps.numerator = 1000;
    euvc->output_fps.denominator = 30000;

    euvc->disconnect_event.type = EUVC_EVENT_DISCONNECT;
    euvc->disconnect_event.u.data[0] = 0;

    pr_info("euvc: Created virtual device #%zu (%s)\n", idx, euvc->vdev.name);

    return euvc;

video_regdev_failure:
    video_unregister_device(&euvc->vdev);
    video_device_release(&euvc->vdev);
vb2_out_init_failed:
    v4l2_device_unregister(&euvc->v4l2_dev);
v4l2_registration_failure:
    kfree(euvc);
euvc_alloc_failure:
    return NULL;
}

void destroy_euvc_device(struct euvc_device *euvc)
{
    if (!euvc) 
        return;

    pr_info("euvc: Destroying virtual device (%s)\n", euvc->vdev.name);

    if (euvc->sub_thr_id)
        kthread_stop(euvc->sub_thr_id);
    mutex_destroy(&euvc->euvc_mutex);
    video_unregister_device(&euvc->vdev);
    v4l2_device_unregister(&euvc->v4l2_dev);

    kfree(euvc);
    pr_info("euvc: Device destroyed successfully\n");
}
