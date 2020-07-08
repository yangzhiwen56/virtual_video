#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "bitmap.h"

//显示位图文件头信息
void showBitMapFileHead(BitMapFileHeader *pBmpHead){
    printf("BitMapFileHeader:\n");
    printf("  signature:%c%c\n", pBmpHead->bfType[0], pBmpHead->bfType[1]);
    printf("  file size:%d\n",   pBmpHead->bfSize);
    printf("  reserved1:0x%x\n", pBmpHead->bfReserved1);
    printf("  reserved2:0x%x\n", pBmpHead->bfReserved2);
    printf("  data offset:%d\n", pBmpHead->bfOffBits);
}

void showBmpInforHead(BitMapInfoHeader *pBmpInforHead){
    printf("BitMapInfoHeader:\n");
    printf("  info_size:%d\n",        pBmpInforHead->biSize);
    printf("  width:%d\n",            pBmpInforHead->biWidth);
    printf("  height:%d\n",           pBmpInforHead->biHeight);
    printf("  planes:%d\n",           pBmpInforHead->biPlanes);
    printf("  bit_count:%d\n",        pBmpInforHead->biBitCount);
    printf("  compression:%d\n",      pBmpInforHead->biCompression);
    printf("  image_size:%d\n",       pBmpInforHead->biSizeImage);
    printf("  x_pixels_per_m:%d\n",   pBmpInforHead->biXPelsPerMeter);
    printf("  y_pixels_per_m:%d\n",   pBmpInforHead->biYPelsPerMeter);
    printf("  colors_used:%d\n",      pBmpInforHead->biClrUsed);
    printf("  colors_important:%d\n", pBmpInforHead->biClrImportant);
}


int GenBmpFile(__u8 *pData, __u8 bitCountPerPix, __u32 width, __u32 height, const char *filename)
{
    int retval;
    BitMapFileHeader *mFileHead;
    BitMapInfoHeader *mInfoHead;
    FILE *pf;
    __u32 bmp_byte_per_line;
    __u32 buf_byte_per_line;
    __u8 byte_per_pix;
    __u8 *bmp_data;
    __u8 *pbmp, *pbuf;
    __u32 head_szie,file_size;
    __u32 x,y;

    printf("sizeof(mFileHead)=%d\n",sizeof(BitMapFileHeader));
    printf("sizeof(mInfoHead)=%d\n",sizeof(BitMapInfoHeader));

    pf = fopen(filename, "w");  
    if(NULL == pf){
        printf("fopen \'%s\' failed : %s\n", filename, strerror(errno));
        return -1;  
    }

    retval = fseek(pf, 0, SEEK_SET);
    if(retval == (-1)){
        printf("fseek fail : %s\n", strerror(errno));
        fclose(pf);
        return -1;
    }

    bmp_byte_per_line = ((width * bitCountPerPix + 31) >> 5) << 2;
    buf_byte_per_line = width * bitCountPerPix >> 3;
    byte_per_pix = bitCountPerPix >> 3;

    head_szie = sizeof(BitMapFileHeader) + sizeof(BitMapInfoHeader);
    file_size = head_szie + bmp_byte_per_line * height;   
    
    printf("bmp_byte_per_line=%d\n", bmp_byte_per_line);
    printf("buf_byte_per_line=%d\n", buf_byte_per_line);
    printf("head_szie=%d\n", head_szie);
    printf("file_size=%d\n", file_size);


    bmp_data = (__u8*)malloc(file_size);
    if(!bmp_data){
        printf("Unable to malloc buff:%s", strerror(errno));
    }
    memset(bmp_data, 0, file_size);

    mFileHead = (BitMapFileHeader *)bmp_data;
    mInfoHead = (BitMapInfoHeader *)(bmp_data + sizeof(BitMapFileHeader));

    mFileHead->bfType[0] = 'B';
    mFileHead->bfType[1] = 'M';
    mFileHead->bfSize = file_size;
    mFileHead->bfOffBits = head_szie;

    mInfoHead->biSize = 40;
    mInfoHead->biWidth = width;
    mInfoHead->biHeight = height;
    mInfoHead->biPlanes = 1;
    mInfoHead->biBitCount = bitCountPerPix;
    mInfoHead->biCompression = 0;
    mInfoHead->biSizeImage = 0;
    mInfoHead->biXPelsPerMeter = 3780;
    mInfoHead->biYPelsPerMeter = 3780;
    mInfoHead->biClrUsed = 0;
    mInfoHead->biClrImportant = 0;

    pbmp = &bmp_data[head_szie];
    pbuf = &pData[(height-1)*buf_byte_per_line];

    for(y=0; y<height; y++){
        //memcpy(pbmp, pbuf, buf_byte_per_line);
        for(x=0; x<width; x++){
            pbmp[x*byte_per_pix+3] = pbuf[x*byte_per_pix+0];
            pbmp[x*byte_per_pix+2] = pbuf[x*byte_per_pix+1];
            pbmp[x*byte_per_pix+1] = pbuf[x*byte_per_pix+2];
            pbmp[x*byte_per_pix+0] = pbuf[x*byte_per_pix+3];
        }
        
        pbuf -= buf_byte_per_line;
        pbmp += bmp_byte_per_line;
    }

    retval = fwrite(bmp_data,file_size,1,pf);
    free(bmp_data);
    fclose(pf);

    return 0;
}

__u8* GetBmpData(__u8 *bitCountPerPix, __u32 *width, __u32 *height, const char* filename)
{
    int retval;
    FILE *pf;
    BitMapFileHeader mFileHead;
    BitMapInfoHeader mInfoHead;
    //RgbQuad rgb;

    __u32 bmp_byte_per_line;
    __u32 buf_byte_per_line;
    __u8 *pdata, *pbuf;
    __u8 *line_buf;
    __u8 byte_per_pix;
    int x,y;

    pf = fopen(filename, "rb");  
    if(NULL == pf){
        printf("fopen \'%s\' failed : %s\n", filename, strerror(errno));
        return NULL;  
    }
    
    retval = fseek(pf, 0, SEEK_SET);
    if(retval == (-1)){
        printf("fseek fail : %s\n", strerror(errno));
        fclose(pf);
        return NULL;
    }

    retval = fread(&mFileHead, sizeof(BitMapFileHeader), 1, pf);
    if(retval!=1){
        printf("read BitMapFileHeader error:%s\n", strerror(errno));
        fclose(pf);
        return NULL;
    }
    retval = fread(&mInfoHead, sizeof(BitMapInfoHeader), 1, pf);
    if(retval != 1){
        printf("read BitMapFileHeader error:%s\n", strerror(errno));
        fclose(pf);
        return NULL;
    }
    showBitMapFileHead(&mFileHead);
    showBmpInforHead(&mInfoHead);

    if(bitCountPerPix){
        *bitCountPerPix = mInfoHead.biBitCount;
    }
    if(width){
        *width = mInfoHead.biWidth;
    }
    if(height){
        *height = mInfoHead.biHeight;
    }

    retval = fseek(pf, mFileHead.bfOffBits, SEEK_SET);
    if(retval == (-1)){
        printf("fseek to %d fail : %s\n", mFileHead.bfOffBits, strerror(errno));
        fclose(pf);
        return NULL;
    }

    bmp_byte_per_line = ((mInfoHead.biWidth * mInfoHead.biBitCount + 31) >> 5) << 2;
    line_buf = (__u8*)malloc(bmp_byte_per_line);
    printf("bmp_byte_per_line=%d\n", bmp_byte_per_line);

    byte_per_pix = mInfoHead.biBitCount >> 3;
    pdata = (__u8*)malloc(mInfoHead.biWidth * mInfoHead.biHeight * byte_per_pix);
    
    buf_byte_per_line = mInfoHead.biWidth * byte_per_pix;
    printf("buf_byte_per_line=%d\n", buf_byte_per_line);

    if(!pdata || !line_buf){
        if(pdata){
            free(pdata);
        }
        if(line_buf){
            free(line_buf);
        }
        fclose(pf);
        return NULL;
    }

    pbuf = &pdata[(mInfoHead.biHeight-1)*buf_byte_per_line];
    for(y=0; y<mInfoHead.biHeight; y++){
        fread(line_buf, bmp_byte_per_line, 1, pf);
        //memcpy(pbuf,line_buf,buf_byte_per_line);
        for(x=0; x<mInfoHead.biWidth; x++){
            pbuf[x*byte_per_pix+0] = line_buf[x*byte_per_pix+3];
            pbuf[x*byte_per_pix+1] = line_buf[x*byte_per_pix+2];
            pbuf[x*byte_per_pix+2] = line_buf[x*byte_per_pix+1];
            pbuf[x*byte_per_pix+3] = line_buf[x*byte_per_pix+0];
        }
        pbuf -= buf_byte_per_line;
    }
    free(line_buf);
    fclose(pf);
    return pdata;
}
