#include "stdio.h"
#include "stdlib.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <time.h>
#include "bitmap.h"

#define FILE_VIDEO  "/dev/video0"
#define IMAGE_WIDTH  800
#define IMAGE_HEIGHT 480
#define FRAME_NUM    4


struct buffer
{
    void * start;
    unsigned int length;
    long long int timestamp;
};

int fd;
struct v4l2_buffer buf;
struct buffer *buffers;
int frame_num = 4;

int main(int argc, char argv[])
{
    int retval;
    
    struct v4l2_capability cap;
    
    struct v4l2_fmtdesc fmtdesc;
    struct v4l2_format fmt;
    struct v4l2_streamparm stream_para;

    unsigned int n_buffers;
    struct v4l2_requestbuffers req;

    enum v4l2_buf_type type;

    char name[22];

    printf("Hello Elmo.\n");

    //打开设备
    fd = open(FILE_VIDEO, O_RDWR);
    if(fd == -1){
        printf("Error opening video interface " FILE_VIDEO " : %s\n", strerror(errno));
        return -1;
    }

    //查询设备属性
    memset(&cap, 0, sizeof(cap));
    retval = ioctl(fd, VIDIOC_QUERYCAP, &cap);
    if(retval == -1) {
        printf("unable to query device "FILE_VIDEO" : %s.\n", strerror(errno));
        close(fd);
        return -2;
    } 
    printf("driver:\t\t%s\n", cap.driver);
    printf("card:\t\t%s\n",   cap.card);
    printf("bus_info:\t%s\n", cap.bus_info);
    printf("version:\t%d\n",  cap.version);
    printf("capabilities:\t%x\n", cap.capabilities);
    if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == V4L2_CAP_VIDEO_CAPTURE){
        printf("Device %s: supports capture.\n",FILE_VIDEO);
    }
    if ((cap.capabilities & V4L2_CAP_STREAMING) == V4L2_CAP_STREAMING){
        printf("Device %s: supports streaming.\n",FILE_VIDEO);
    }
    if(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT){
        printf("Device %s: support output\n", FILE_VIDEO);
    }
    if(cap.capabilities & V4L2_CAP_VIDEO_OVERLAY){
        printf("Device %s: support overlay\n", FILE_VIDEO);
    }
    if(cap.capabilities & V4L2_CAP_READWRITE){
        printf("Device %s: support read write\n", FILE_VIDEO);
    }

    //显示所有支持帧格式
    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.index=0;
    fmtdesc.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
    printf("Support format:\n");
    while(ioctl(fd,VIDIOC_ENUM_FMT,&fmtdesc)!=-1)
    {
        printf("\t%d.%s\n", fmtdesc.index+1, fmtdesc.description);
        fmtdesc.index++;
    }

    //测试是否支持某帧格式
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    retval = ioctl(fd,VIDIOC_TRY_FMT,&fmt);
    if(retval == -1){
        printf("Not support format YUYV!\n");      
    } else{
        printf("Support format YUYV\n");
    }

    //设置当前格式
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    //fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB32;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR32;
    fmt.fmt.pix.width  = IMAGE_WIDTH;
    fmt.fmt.pix.height = IMAGE_HEIGHT;
    fmt.fmt.pix.field  = V4L2_FIELD_INTERLACED;
    retval = ioctl(fd, VIDIOC_S_FMT, &fmt);
    if(retval == -1){
        printf("Unable to set format:%s\n", strerror(errno));
        close(fd);
        return -3;
    }

    //读取当前格式
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    retval = ioctl(fd, VIDIOC_G_FMT, &fmt);
    if(retval == -1){
        printf("Unable to get format:%s\n", strerror(errno));
        close(fd);
        return -4;
    }
    printf("fmt.type:\t\t%d\n",fmt.type);
    printf("pix.pixelformat:\t%c%c%c%c\n",(fmt.fmt.pix.pixelformat >> 0) & 0xFF, 
                                          (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
                                          (fmt.fmt.pix.pixelformat >> 16) & 0xFF, 
                                          (fmt.fmt.pix.pixelformat >> 24) & 0xFF);
    printf("pix.height:\t\t%d\n",fmt.fmt.pix.height);
    printf("pix.width:\t\t%d\n",fmt.fmt.pix.width);
    printf("pix.field:\t\t%d\n",fmt.fmt.pix.field);

#if 0    
    //设置帧速率
    memset(&stream_para, 0, sizeof(struct v4l2_streamparm));
    stream_para.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    stream_para.parm.capture.timeperframe.denominator = 30;
    stream_para.parm.capture.timeperframe.numerator   = 1;
    retval = ioctl(fd, VIDIOC_S_PARM, &stream_para);
    if(retval == -1){
        printf("Unable to set frame rate:%s\n", strerror(errno));
        close(fd);
        return -5;
    }
    //查看帧速率
    retval = ioctl(fd, VIDIOC_G_PARM, &stream_para);
    if(retval == -1){
        printf("Unable to get frame rate:%s\n", strerror(errno));
        close(fd);
        return -6;       
    }
    printf("numerator:%d\n", stream_para.parm.capture.timeperframe.numerator);
    printf("denominator:%d\n", stream_para.parm.capture.timeperframe.denominator);
#endif    

    //申请帧缓冲
    memset(&req, 0, sizeof(req));
    req.count  = FRAME_NUM;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    retval = ioctl(fd, VIDIOC_REQBUFS, &req);    /*请求系统分配缓冲区*/
    if(retval == -1){
        printf("request for buffers error:%s\n", strerror(errno));
        close(fd);
        return -7;
    }
    frame_num = req.count;
    printf("req.count=%d\n", req.count);

    // 申请用户空间的地址列
    buffers = malloc(req.count * sizeof (*buffers));
    if (!buffers) {
        printf ("out of memory!\n");
        close(fd);
        return -8;
    }
    // 进行内存映射
    for (n_buffers = 0; n_buffers < frame_num; n_buffers++) {
        printf("query buffer %d\n", n_buffers);
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = n_buffers;
        retval = ioctl(fd, VIDIOC_QUERYBUF, &buf);    /*查询所分配的缓冲区*/
        if (retval == -1){
            printf("query buffer error\n");
            free(buffers);
            close(fd);
            return -9;
        }
        buffers[n_buffers].length = buf.length;
        buffers[n_buffers].start = mmap(NULL,buf.length, PROT_READ|PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (buffers[n_buffers].start == MAP_FAILED){
            printf("buffer map error:%s\n", strerror(errno));
            free(buffers);
            close(fd);
            return -10;
        }
    }

    //入队
    for (n_buffers = 0; n_buffers < frame_num; n_buffers++){
        memset(&buf, 0, sizeof(buf));
        buf.index = n_buffers;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        retval = ioctl(fd, VIDIOC_QBUF, &buf);
        if(retval == -1){
            printf("QBUF error:%s\n", strerror(errno));
            free(buffers);
            close(fd);
            return -10;
        }
    }


    //开启采集
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    retval = ioctl(fd, VIDIOC_STREAMON, &type);
    if(retval==-1){
        printf("STREAMON error:%s\n", strerror(errno));
        free(buffers);
        close(fd);
        return -11;
    }

    for(n_buffers = 0; n_buffers < FRAME_NUM; n_buffers++){
        //出队
        memset(&buf, 0, sizeof(buf));
        buf.index = n_buffers;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        retval = ioctl(fd, VIDIOC_DQBUF, &buf);
        if (retval == -1) {
            printf("DQBUF error:%s\n", strerror(errno));
            free(buffers);
            close(fd);
            return -12;
        }

        memset(name, 0, 22);
        sprintf(name,"./img/image%d.bmp",n_buffers);
        GenBmpFile(buffers[n_buffers].start, 32, IMAGE_WIDTH, IMAGE_HEIGHT, name);

        //入队循环
        retval = ioctl(fd, VIDIOC_QBUF, &buf); 
        if (retval == -1) {
            printf("QBUF error:%s\n", strerror(errno));
            free(buffers);
            close(fd);
            return -13;
        }
    }

    //关闭流
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd, VIDIOC_STREAMOFF, &type);

    //关闭内存映射
    for(n_buffers=0;n_buffers<frame_num;n_buffers++) {
        munmap(buffers[n_buffers].start, buffers[n_buffers].length);
    }
    
    free(buffers);
    close(fd);
    return 0;
}

