#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>

#include "videobuf.h"

static int euvc_out_queue_setup(struct vb2_queue *vq,
                                unsigned int *nbuffers,
                                unsigned int *nplanes,
                                unsigned int sizes[],
                                struct device *alloc_ctxs[])
{
    int i;
    struct euvc_device *dev = vb2_get_drv_priv(vq);
    unsigned long size = dev->output_format.sizeimage;

    if (*nbuffers < 2)
        *nbuffers = 2;

    if (*nplanes > 0) {
        if (sizes[0] < size)
            return -EINVAL;
        return 0;
    }

    *nplanes = 1;

    sizes[0] = size;
    for (i = 1; i < VB2_MAX_PLANES; ++i)
        sizes[i] = 0;
    pr_debug("queue_setup completed\n");
    return 0;
}

static int euvc_out_buffer_prepare(struct vb2_buffer *vb)
{
    struct euvc_device *dev = vb2_get_drv_priv(vb->vb2_queue);
    unsigned long size = dev->output_format.sizeimage;
    if (vb2_plane_size(vb, 0) < size) {
        pr_err(KERN_ERR "data will not fit into buffer\n");
        return -EINVAL;
    }

    vb2_set_plane_payload(vb, 0, size);
    return 0;
}

static void euvc_out_buffer_queue(struct vb2_buffer *vb)
{
    unsigned long flags = 0;
    struct euvc_device *dev = vb2_get_drv_priv(vb->vb2_queue);
    struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
    struct euvc_out_buffer *buf =
        container_of(vbuf, struct euvc_out_buffer, vb);
    struct euvc_out_queue *q = &dev->euvc_out_vidq;
    buf->filled = 0;

    spin_lock_irqsave(&dev->out_q_slock, flags);
    list_add_tail(&buf->list, &q->active);
    spin_unlock_irqrestore(&dev->out_q_slock, flags);
}

static int euvc_start_streaming(struct vb2_queue *q, unsigned int count)
{
    struct euvc_device *dev = q->drv_priv;

    /* Try to start kernel thread */
    dev->sub_thr_id = kthread_create(submitter_thread, dev, "euvc_submitter");
    if (!dev->sub_thr_id) {
        pr_err("Failed to create kernel thread\n");
        return -ECANCELED;
    }

    wake_up_process(dev->sub_thr_id);

    return 0;
}

static void euvc_stop_streaming(struct vb2_queue *vb2_q)
{
    struct euvc_device *dev = vb2_q->drv_priv;
    struct euvc_out_queue *q = &dev->euvc_out_vidq;
    unsigned long flags = 0;

    /* Stop running threads */
    if (dev->sub_thr_id)
        kthread_stop(dev->sub_thr_id);

    dev->sub_thr_id = NULL;
    /* Empty buffer queue */
    spin_lock_irqsave(&dev->out_q_slock, flags);
    while (!list_empty(&q->active)) {
        struct euvc_out_buffer *buf =
            list_entry(q->active.next, struct euvc_out_buffer, list);
        list_del(&buf->list);
        vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
        pr_debug("Throwing out buffer\n");
    }
    spin_unlock_irqrestore(&dev->out_q_slock, flags);
}

static void euvc_outbuf_lock(struct vb2_queue *vq)
{
    struct euvc_device *dev = vb2_get_drv_priv(vq);
    mutex_lock(&dev->euvc_mutex);
}

static void euvc_outbuf_unlock(struct vb2_queue *vq)
{
    struct euvc_device *dev = vb2_get_drv_priv(vq);
    mutex_unlock(&dev->euvc_mutex);
}

static const struct vb2_ops euvc_vb2_ops = {
    .queue_setup = euvc_out_queue_setup,
    .buf_prepare = euvc_out_buffer_prepare,
    .buf_queue = euvc_out_buffer_queue,
    .start_streaming = euvc_start_streaming,
    .stop_streaming = euvc_stop_streaming,
    .wait_prepare = euvc_outbuf_unlock,
    .wait_finish = euvc_outbuf_lock,
};

int euvc_out_videobuf2_setup(struct euvc_device *dev)
{
    struct vb2_queue *q = &dev->vb_out_vidq;

    q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
    q->drv_priv = dev;
    q->buf_struct_size = sizeof(struct euvc_out_buffer);
    q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
    q->ops = &euvc_vb2_ops;
    q->mem_ops = &vb2_vmalloc_memops;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
    q->min_queued_buffers = 2;
#else
    q->min_buffers_needed = 2;
#endif
    q->lock = &dev->euvc_mutex;

    return vb2_queue_init(q);
}
