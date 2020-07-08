#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>
#include <media/videobuf-vmalloc.h>

/* Limits minimum and default number of buffers */
#define ELMO_VIDEO_MIN_BUF 4
#define ELMO_VIDEO_DEF_BUF 8

#define DBG_ERR  (0x1<<0)
#define DBG_WARN (0x1<<1)
#define DBG_INFO (0x1<<2)

static int debug=0;
module_param(debug, int, 0644);

#define debug_printk(level, fmt, arg...)    \
    do {                                    \
        if (debug & level)                  \
            printk(fmt , ## arg);           \
    }while(0)

static unsigned int vid_limit = 16; /* Video memory limit, in Mb */

struct virtual_video_fmt {
    char *name;
    u32 fourcc; /* v4l2 format id */
    int depth;
};

struct virtual_video{
    struct v4l2_device v4l2_dev;
    struct video_device video_dev;
    u32 io_usrs;

    struct mutex lock;
    spinlock_t slock;
    struct videobuf_queue vb_vidq;

    unsigned int fourcc;
    unsigned int width, height;
    struct virtual_video_fmt *fmt;

    struct timer_list tick_timer;
    struct list_head queued;
};

/* buffer for one video frame */
struct virtual_video_buffer {
    /* common v4l buffer stuff -- must be first */
    struct videobuf_buffer vb;
    //struct virtual_video_fmt *fmt;
};

struct virtual_video_fh {
    struct v4l2_fh fh;
    struct virtual_video *dev;
};

struct virtual_video *virtual_dev;

static struct virtual_video_fmt format[] = {
    {
        .name     = "ARGB8888, 32 bpp",
        .fourcc   = V4L2_PIX_FMT_RGB32,  //byte0:a byte1:r byte2:g byte3:b
        .depth      = 32,
    }, {
        .name     = "32 bpp RGB, be",
        .fourcc   = V4L2_PIX_FMT_BGR32,  //byte0:b byte1:g byte2:r byte3:a
        .depth    = 32,
    },/* {
        .name     = "4:2:2, packed, YVY2",
        .fourcc   = V4L2_PIX_FMT_YUYV,
        .depth    = 16,
    }, {
        .name     = "4:2:2, packed, UYVY",
        .fourcc   = V4L2_PIX_FMT_UYVY,
        .depth    = 16,
    }*/
};
static struct virtual_video_fmt *format_by_fourcc(unsigned int fourcc)
{
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE(format); i++){
        if (format[i].fourcc == fourcc){
            return format+i;
        }
    }
    return NULL;
}


/*calculates the size of the video buffers and avoid they to waste more than some maximum limit of RAM;*/
static int buffer_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
    struct virtual_video *dev = vq->priv_data;

    debug_printk(DBG_INFO, "%s:count=%d\n", __FUNCTION__, *count);
    debug_printk(DBG_INFO, "%s:depth=%d, width=%d, height=%d\n", __FUNCTION__, dev->fmt->depth, dev->width, dev->height);

    *size = dev->fmt->depth * dev->width * dev->height >> 3;

    if (0 == *count)
        *count = ELMO_VIDEO_DEF_BUF;

    if (*count < ELMO_VIDEO_MIN_BUF)
        *count = ELMO_VIDEO_MIN_BUF;

    while (*size * *count > vid_limit * 1024 * 1024){
        (*count)--;
    }

    debug_printk(DBG_INFO, "%s done:count=%d, size=%d\n", __FUNCTION__, *count, *size);
    return 0;
}
/*fills the video buffer structs and calls videobuf_iolock() to alloc and prepare mmaped memory;*/
static int buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb, enum v4l2_field field)
{
    struct virtual_video *dev = vq->priv_data;
    struct virtual_video_buffer *buf = container_of(vb, struct virtual_video_buffer, vb);
    int retval = 0;

    debug_printk(DBG_INFO, "%s:vb.state=%d\n", __FUNCTION__, buf->vb.state);

    /* FIXME: It assumes depth=2 */
    /* The only currently supported format is 16 bits/pixel */
    buf->vb.size = dev->fmt->depth * dev->width * dev->height >> 3;
    if (buf->vb.baddr != 0 && buf->vb.bsize < buf->vb.size) {
        debug_printk(DBG_ERR, "invalid buffer prepare\n");
        return -EINVAL;
    }

    //buf->fmt       = dev->fmt;
    buf->vb.width  = dev->width;
    buf->vb.height = dev->height;
    buf->vb.field  = field;

    if (buf->vb.state == VIDEOBUF_NEEDS_INIT) {
        debug_printk(DBG_INFO, "videobuf_iolock\n");
        retval = videobuf_iolock(vq, &buf->vb, NULL);
        if (retval < 0){
            debug_printk(DBG_ERR, "videobuf_iolock error:%d\n", retval);
            goto fail;
        }
    }

    buf->vb.state = VIDEOBUF_PREPARED;
    return 0;
fail:
    debug_printk(DBG_ERR, "buffer_prepare fail:%d\n",retval);
    videobuf_vmalloc_free(&buf->vb);
    buf->vb.state = VIDEOBUF_NEEDS_INIT;
    return retval;
}
/*advices the driver that another buffer were requested (by read() or by QBUF);*/
static void buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
    struct virtual_video_buffer *buf = container_of(vb, struct virtual_video_buffer, vb);
    struct virtual_video *dev = vq->priv_data;

    debug_printk(DBG_INFO, "%s\n", __FUNCTION__);
    buf->vb.state = VIDEOBUF_QUEUED;
    list_add_tail(&buf->vb.queue, &dev->queued);
}
/*frees any buffer that were allocated.*/
static void buffer_release(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
    struct virtual_video_buffer *buf = container_of(vb, struct virtual_video_buffer, vb);
    debug_printk(DBG_INFO, "%s\n", __FUNCTION__);

    videobuf_vmalloc_free(&buf->vb);
    buf->vb.state = VIDEOBUF_NEEDS_INIT;
}
static struct videobuf_queue_ops virtual_video_qops = {
    .buf_setup      = buffer_setup,
    .buf_prepare    = buffer_prepare,
    .buf_queue      = buffer_queue,
    .buf_release    = buffer_release,
};

static int virtual_video_fops_open(struct file *file)
{
    struct video_device *vdev = video_devdata(file);
    struct virtual_video *dev = video_get_drvdata(vdev);
    struct virtual_video_fh *fh;

    debug_printk(DBG_INFO, "%s:dev=%s minor=%d users=%d\n", __FUNCTION__, video_device_node_name(vdev), vdev->minor, dev->io_usrs);

    if (dev->io_usrs != 0) {
        debug_printk(DBG_ERR, "io_users error\n");
        return -EBUSY;
    }
    dev->io_usrs++;

    fh = kzalloc(sizeof(struct virtual_video_fh), GFP_KERNEL);
    if (NULL == fh) {
        debug_printk(DBG_ERR, "kzalloc virtual_video_fh error\n");
        dev->io_usrs--;
        return -ENOMEM;
    }

    v4l2_fh_init(&fh->fh, vdev);
    file->private_data = fh;
    fh->dev = dev;

    dev->width = 800;
    dev->height = 480;
    dev->fourcc = format[0].fourcc;
    dev->fmt = format_by_fourcc(dev->fourcc);

    videobuf_queue_vmalloc_init(&dev->vb_vidq, &virtual_video_qops,
                NULL, &dev->slock,
                V4L2_BUF_TYPE_VIDEO_CAPTURE,
                V4L2_FIELD_INTERLACED,
                sizeof(struct virtual_video_buffer), dev, &dev->lock);

    v4l2_fh_add(&fh->fh);

    dev->tick_timer.expires = jiffies + HZ;
    add_timer(&dev->tick_timer);

    return 0;
}

static int virtual_video_fops_release(struct file *file)
{
    //struct video_device *vdev = video_devdata(file);
    struct virtual_video_fh *fh = (struct virtual_video_fh *)file->private_data;
    struct virtual_video *dev = fh->dev;


    debug_printk(DBG_INFO, "%s\n", __FUNCTION__);
    dev->io_usrs--;
    videobuf_mmap_free(&dev->vb_vidq);
    del_timer(&dev->tick_timer);
    v4l2_fh_del(&fh->fh);
    v4l2_fh_exit(&fh->fh);

    return 0;
}


static long virtual_video_fops_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int retval=0;

    //debug_printk(DBG_INFO, "%s:cmd=0x%x,arg=%lu\n", __FUNCTION__, cmd, arg);
    retval = video_ioctl2(file, cmd, arg);
    //if(retval != 0){
    //    debug_printk(DBG_INFO, "video_ioctl2:cmd=%d,retval=%d\n", _IOC_NR(cmd), retval);
    //}
    return retval;
}

static int virtual_video_fops_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct virtual_video_fh *fh = (struct virtual_video_fh *)file->private_data;
    struct virtual_video *dev = fh->dev;
    int retval;

    debug_printk(DBG_INFO, "%s\n", __FUNCTION__);
    retval = videobuf_mmap_mapper(&dev->vb_vidq, vma);
    return retval;
}

static unsigned int virtual_video_fops_poll(struct file *file, struct poll_table_struct *wait)
{
    struct virtual_video_fh *fh = file->private_data;
    struct virtual_video *dev = fh->dev;
    unsigned long req_events = poll_requested_events(wait);
    int res = 0;

    debug_printk(DBG_INFO, "%s\n", __FUNCTION__);

    if (v4l2_event_pending(&fh->fh))
        res = POLLPRI;
    else if (req_events & POLLPRI){
        poll_wait(file, &fh->fh.wait, wait);
    }

    if (req_events & (POLLIN | POLLRDNORM)) {
        return res | videobuf_poll_stream(file, &dev->vb_vidq, wait); /* read() capture */
    }

    debug_printk(DBG_INFO, "%s done:%d\n", __FUNCTION__,res);

    return res;
}

static const struct v4l2_file_operations virtual_video_fops = {
    .owner          = THIS_MODULE,
    .open           = virtual_video_fops_open,
    .release        = virtual_video_fops_release,
    .unlocked_ioctl = virtual_video_fops_unlocked_ioctl,  //video_ioctl2,
    .mmap           = virtual_video_fops_mmap,
    .poll           = virtual_video_fops_poll             //vb2_fop_poll
};


// 查询是否是一个 摄像头设备
static int virtual_video_iops_querycap(struct file *file, void  *priv, struct v4l2_capability *cap)
{
    debug_printk(DBG_INFO, "%s\n", __FUNCTION__);

    cap->version = 0x0001;
    strlcpy(cap->driver,   "virtual_video", sizeof(cap->driver));
    strlcpy(cap->card,     "virtual_video", sizeof(cap->card));
    strlcpy(cap->bus_info, "virtual_video", sizeof(cap->bus_info));

    cap->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE;
    cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

    return 0;
}

static int virtual_video_iops_enum_fmt_vid_cap(struct file *file, void *priv, struct v4l2_fmtdesc *f)
{
    debug_printk(DBG_INFO, "%s\n", __FUNCTION__);
    if (f->index >= ARRAY_SIZE(format))
        return -EINVAL;

    strlcpy(f->description, format[f->index].name, sizeof(f->description));
    f->pixelformat = format[f->index].fourcc;

    return 0;
}
static int virtual_video_iops_g_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
    struct virtual_video_fh *fh = (struct virtual_video_fh *)priv;
    struct virtual_video *dev = fh->dev;
    debug_printk(DBG_INFO, "%s\n", __FUNCTION__);

    f->fmt.pix.width        = dev->width;
    f->fmt.pix.height       = dev->height;
    f->fmt.pix.field        = dev->vb_vidq.field;
    f->fmt.pix.pixelformat  = dev->fmt->fourcc;
    f->fmt.pix.colorspace   = V4L2_COLORSPACE_SMPTE170M;
    f->fmt.pix.bytesperline = (f->fmt.pix.width * dev->fmt->depth) >> 3;
    f->fmt.pix.sizeimage    = f->fmt.pix.height * f->fmt.pix.bytesperline;

    return 0;
}
static int virtual_video_iops_try_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
    struct virtual_video_fh *fh = (struct virtual_video_fh *)priv;
    struct virtual_video *dev = fh->dev;
    struct virtual_video_fmt *fmt;

    debug_printk(DBG_INFO, "%s\n", __FUNCTION__);
    
    fmt = format_by_fourcc(f->fmt.pix.pixelformat);
    if (NULL == fmt) {
        debug_printk(DBG_INFO, "Fourcc format (0x%08x) invalid.\n", f->fmt.pix.pixelformat);
        return -EINVAL;
    }

    f->fmt.pix.width  = dev->width;
    f->fmt.pix.height = dev->height;

    f->fmt.pix.width &= ~0x01;

    f->fmt.pix.field = V4L2_FIELD_INTERLACED;

    f->fmt.pix.bytesperline = (f->fmt.pix.width * fmt->depth) >> 3;
    f->fmt.pix.sizeimage    = f->fmt.pix.height * f->fmt.pix.bytesperline;
    f->fmt.pix.colorspace   = V4L2_COLORSPACE_SMPTE170M;

    return 0;
}
static int virtual_video_iops_s_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
    struct virtual_video_fh *fh = (struct virtual_video_fh *)priv;
    struct virtual_video *dev = fh->dev;
    int retval=0;
    
    debug_printk(DBG_INFO, "%s\n", __FUNCTION__);
    debug_printk(DBG_INFO, "%s:width=%d,height=%d\n", __FUNCTION__, f->fmt.pix.width, f->fmt.pix.height);
    debug_printk(DBG_INFO, "%s:field=%d,type=%d\n", __FUNCTION__,f->fmt.pix.field, f->type);

    dev->fmt           = format_by_fourcc(f->fmt.pix.pixelformat);
    dev->width         = f->fmt.pix.width;
    dev->height        = f->fmt.pix.height;
    dev->vb_vidq.field = f->fmt.pix.field;

    dev->fourcc       = f->fmt.pix.pixelformat;

    debug_printk(DBG_INFO, "%s:width=%d,height=%d\n", __FUNCTION__, dev->width, dev->height);
    debug_printk(DBG_INFO, "pixelformat:%c%c%c%c\n",(dev->fourcc >> 0) & 0xFF,
                                                    (dev->fourcc >> 8) & 0xFF,
                                                    (dev->fourcc >> 16) & 0xFF,
                                                    (dev->fourcc >> 24) & 0xFF);
    return retval;
}


static int virtual_video_iops_reqbufs(struct file *file, void *priv, struct v4l2_requestbuffers *p)
{
    int retval = 0;
    struct virtual_video_fh *fh = (struct virtual_video_fh *)priv;
    struct virtual_video *dev = fh->dev;
    debug_printk(DBG_INFO, "%s:count=%d, type=0x%x, memory=0x%x\n", __FUNCTION__, p->count, p->type, p->memory);

    if (V4L2_BUF_TYPE_VIDEO_CAPTURE != p->type) {
        debug_printk(DBG_ERR, "Invalid buffer type\n");
        return -EINVAL;
    }

    retval = videobuf_reqbufs(&dev->vb_vidq, p);
    if(retval != 0){
        debug_printk(DBG_ERR, "%s:videobuf_reqbufs retval=%d\n", __FUNCTION__, retval);
    }
    return retval;
}
static int virtual_video_iops_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
    int retval = 0;
    struct virtual_video_fh *fh = (struct virtual_video_fh *)priv;
    struct virtual_video *dev = fh->dev;
    debug_printk(DBG_INFO, "%s\n", __FUNCTION__);
    retval = videobuf_querybuf(&dev->vb_vidq, p);
    if(retval != 0){
        debug_printk(DBG_ERR, "%s:videobuf_querybuf error,ret=%d\n", __FUNCTION__, retval);
    }
    return retval;
}
static int virtual_video_iops_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
    int retval = 0;
    struct virtual_video_fh *fh = (struct virtual_video_fh *)priv;
    struct virtual_video *dev = fh->dev;

    debug_printk(DBG_INFO, "%s\n", __FUNCTION__);
    retval = videobuf_qbuf(&dev->vb_vidq, p);
    if(retval != 0){
        debug_printk(DBG_ERR, "%s:videobuf_qbuf err,ret=%d\n", __FUNCTION__, retval);
    }
    return retval;
}
static int virtual_video_iops_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
    int retval = 0;
    struct virtual_video_fh *fh = (struct virtual_video_fh *)priv;
    struct virtual_video *dev = fh->dev;
    debug_printk(DBG_INFO, "%s\n", __FUNCTION__);
    retval = videobuf_dqbuf(&dev->vb_vidq, p, file->f_flags & O_NONBLOCK);
    if(retval != 0){
        debug_printk(DBG_ERR, "%s:videobuf_dqbuf err,ret=%d\n", __FUNCTION__, retval);
    }
    return retval;
}

static int virtual_video_iops_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
    int retval = 0;
    struct virtual_video_fh *fh = (struct virtual_video_fh *)priv;
    struct virtual_video *dev = fh->dev;

    debug_printk(DBG_INFO, "%s\n", __FUNCTION__);

    retval = videobuf_streamon(&dev->vb_vidq);
    if(retval != 0){
        debug_printk(DBG_ERR, "%s:videobuf_streamon err,ret=%d\n", __FUNCTION__, retval);
    }

//    dev->tick_timer.expires = jiffies + HZ/20;
//    add_timer(&dev->tick_timer);

    return retval;
}
static int virtual_video_iops_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
    int retval = 0;
    //struct virtual_video *dev = (struct virtual_video *)fh;

    debug_printk(DBG_INFO, "%s\n", __FUNCTION__);
    //del_timer(&dev->tick_timer);
    
    return retval;
}

static const struct v4l2_ioctl_ops virtual_video_ioctl_ops =
{
    /* 表示它是一个摄像头设备 */
    .vidioc_querycap          = virtual_video_iops_querycap,

    /* 用于 列举/获得/设置/测试 摄像头的数据的格式 */
    .vidioc_enum_fmt_vid_cap  = virtual_video_iops_enum_fmt_vid_cap,
    .vidioc_g_fmt_vid_cap     = virtual_video_iops_g_fmt_vid_cap,
    .vidioc_s_fmt_vid_cap     = virtual_video_iops_s_fmt_vid_cap,
    .vidioc_try_fmt_vid_cap   = virtual_video_iops_try_fmt_vid_cap,

    /* 缓冲区操作: 申请/查询/放入队列/取出 队列 */
    .vidioc_reqbufs       = virtual_video_iops_reqbufs,
    .vidioc_querybuf      = virtual_video_iops_querybuf,
    .vidioc_qbuf          = virtual_video_iops_qbuf,
    .vidioc_dqbuf         = virtual_video_iops_dqbuf,

    // 启动/停止
    .vidioc_streamon      = virtual_video_iops_streamon,
    .vidioc_streamoff     = virtual_video_iops_streamoff,   
};

static void virtual_video_device_release(struct video_device *vdev)
{
    //struct virtual_video *dev = container_of(vdev, struct virtual_video, video_dev);
    debug_printk(DBG_INFO, "%s\n", __FUNCTION__);

    //video_device_release(dev->video_dev);
    //kfree(dev);
}

static void virtual_video_v4l2_device_release(struct v4l2_device *vdev)
{
    //struct virtual_video *dev = container_of(vdev, struct virtual_video, v4l2_dev);
    debug_printk(DBG_INFO, "%s\n", __FUNCTION__);

    //video_device_release(dev->video_dev);
    //kfree(dev);
}

static void tick_timer_function(struct timer_list *t)
{
    struct virtual_video *dev = from_timer(dev, t, tick_timer);
    struct videobuf_buffer *vb;
    char *vbuf;
    int size;
    int i,step;

    if (list_empty(&dev->queued)) {
        mod_timer(&dev->tick_timer, jiffies + HZ/30);
        //debug_printk(DBG_INFO, "err%d\n",__LINE__);
        return;
    }

    vb = list_entry(dev->queued.next, struct videobuf_buffer, queue);

    if (!waitqueue_active(&vb->done)){
        mod_timer(&dev->tick_timer, jiffies + HZ/30);
        //debug_printk(DBG_INFO, "err%d\n",__LINE__);
        return;
    }

    vbuf = (char*)videobuf_to_vmalloc(vb);
    size = dev->fmt->depth * dev->width * dev->height >> 3;
    step = size/3;
    if(dev->fmt->fourcc == V4L2_PIX_FMT_RGB32){
        for(i=0;i<step;i+=4){
            vbuf[i]   = 0x00;  //a
            vbuf[i+1] = 0x00;  //r
            vbuf[i+2] = 0x00;  //g
            vbuf[i+3] = 0xff;  //b
        }
        for(;i<step*2;i+=4){
            vbuf[i]   = 0x00;
            vbuf[i+1] = 0x00;
            vbuf[i+2] = 0xff;
            vbuf[i+3] = 0x00;
        }
        for(;i<size;i+=4){
            vbuf[i]   = 0x00;
            vbuf[i+1] = 0xff;
            vbuf[i+2] = 0x00;
            vbuf[i+3] = 0x00;
        }
    } else if(dev->fmt->fourcc == V4L2_PIX_FMT_BGR32){
        for(i=0;i<step;i+=4){
            vbuf[i]   = 0xff;  //b
            vbuf[i+1] = 0x00;  //g
            vbuf[i+2] = 0x00;  //r
            vbuf[i+3] = 0xff;  //a
        }
        for(;i<step*2;i+=4){
            vbuf[i]   = 0x00;
            vbuf[i+1] = 0xff;
            vbuf[i+2] = 0x00;
            vbuf[i+3] = 0xff;
        }
        for(;i<size;i+=4){
            vbuf[i]   = 0x00;
            vbuf[i+1] = 0x00;
            vbuf[i+2] = 0xff;
            vbuf[i+3] = 0xff;
        }
    }


    vb->field_count++;
    v4l2_get_timestamp(&vb->ts);
    vb->state = VIDEOBUF_DONE;

    list_del(&vb->queue);
    wake_up(&vb->done);

    mod_timer(&dev->tick_timer, jiffies + HZ/30);
}

static int virtual_video_init(void)
{
    int retval = 0;
    struct virtual_video *dev;

    debug_printk(DBG_INFO, "virtual_video module init.\n");
    
    dev = kzalloc(sizeof(struct virtual_video), GFP_KERNEL);
    if (!dev){
        debug_printk(DBG_ERR, "Unable to alloc virtual_video device\n");
        return -ENOMEM;
    }
    virtual_dev = dev;

    dev->io_usrs = 0;
    spin_lock_init(&dev->slock);
    mutex_init(&dev->lock);
    INIT_LIST_HEAD(&dev->queued);

    dev->v4l2_dev.release = virtual_video_v4l2_device_release;
    strncpy(dev->v4l2_dev.name, "virtual_video", sizeof(dev->v4l2_dev.name));
    retval = v4l2_device_register(NULL, &dev->v4l2_dev);//dev->dev
    if (retval < 0) {
        debug_printk(DBG_ERR, "v4l2_device_register failed: %d\n", retval);
        goto v4l2_device_register_err;
    }

    dev->video_dev.release   = virtual_video_device_release;
    dev->video_dev.fops      = &virtual_video_fops;
    dev->video_dev.ioctl_ops = &virtual_video_ioctl_ops;
    dev->video_dev.v4l2_dev  = &dev->v4l2_dev;
    strncpy(dev->video_dev.name, "virtual_video", sizeof(dev->video_dev.name));
    retval = video_register_device(&dev->video_dev, VFL_TYPE_GRABBER, -1);
    if (retval < 0) {
        debug_printk(DBG_ERR, "video_register_device failed: %d\n", retval);
        goto video_register_device_err;
    }
 
    video_set_drvdata(&dev->video_dev, dev);

    timer_setup(&dev->tick_timer, tick_timer_function, 0);

   debug_printk(DBG_INFO, "virtual_video module init ok,ret=%d\n",retval);
    return retval;

video_register_device_err:
    v4l2_device_unregister(&dev->v4l2_dev);
v4l2_device_register_err:
    kfree(dev);
    return retval;
}

static void virtual_video_exit(void)
{
    video_unregister_device(&virtual_dev->video_dev);
    v4l2_device_unregister(&virtual_dev->v4l2_dev);
    debug_printk(DBG_INFO, "virtual_video module exit\n");
}

module_init(virtual_video_init);
module_exit(virtual_video_exit);

MODULE_AUTHOR("Elmo.Yang");
MODULE_DESCRIPTION("Elmo Virtual Video Test");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

