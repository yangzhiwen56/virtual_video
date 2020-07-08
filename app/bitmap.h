#ifndef _BMP_H_
#define _BMP_H_

/*
BMP文件由文件头、位图信息头、颜色信息和图形数据四部分组成
BMP文件头数据结构含有BMP文件的类型、文件大小和位图起始位置等信息
BMP位图信息头数据用于说明位图的尺寸等信息
*/

typedef unsigned char  __u8;
typedef unsigned short __u16;
typedef unsigned int   __u32;

//文件头结构体
typedef struct  /* bmfh 14byte */ 
{
    __u8 bfType[2];    /*说明文件的类型，该值必需是0x4D42，也就是字符'BM'*/
    __u32 bfSize;      /*说明该位图文件的大小，用字节为单位*/
    __u16 bfReserved1;
    __u16 bfReserved2;
    __u32 bfOffBits;   /*说明从文件头开始到实际的图象数据之间的字节的偏移量
                       位图信息头和调色板的长度会根据不同情况而变化，所以用这个偏移值迅速的从文件中读取到位数据*/
} __attribute__((packed)) BitMapFileHeader;


//信息头结构体
typedef struct 
{
    __u32 biSize;          /*说明BITMAPINFOHEADER结构所需要的字数*/
    __u32 biWidth;         /*说明图象的宽度，以象素为单位*/
    __u32 biHeight;        /*说明图象的高度，以象素为单位，正位正向，反之为倒图 */
    __u16 biPlanes;        /*为目标设备说明位面数，其值将总是被设为1*/
    __u16 biBitCount;      /*说明比特数/象素，其值为1、4、8、16、24、或32*/
    __u32 biCompression;   /*说明图象数据压缩的类型*/
    __u32 biSizeImage;     /*说明图象的大小，以字节为单位*/
    __u32 biXPelsPerMeter; /*说明水平分辨率，用象素/米表示*/
    __u32 biYPelsPerMeter; /*说明垂直分辨率，用象素/米表示*/
    __u32 biClrUsed;       /*说明位图实际使用的彩色表中的颜色索引数（设为0的话，则说明使用所有调色板项）*/
    __u32 biClrImportant;  /*说明对图象显示有重要影响的颜色索引的数目，如果是0，表示都重要*/
} __attribute__((packed)) BitMapInfoHeader; 

//像素点结构体
typedef struct 
{
    __u8 Blue;       /*蓝色的亮度(值范围为0-255)*/
    __u8 Green;      /*绿色的亮度(值范围为0-255)*/
    __u8 Red;        /*红色的亮度(值范围为0-255)*/
    __u8 Reserved;   /*保留，必须为0*/
} __attribute__((packed)) RgbQuad;


int GenBmpFile(__u8 *pData, __u8 bitCountPerPix, __u32 width, __u32 height, const char *filename);
__u8* GetBmpData(__u8 *bitCountPerPix, __u32 *width, __u32 *height, const char* filename);

#endif    /* _BMP_H_ */
