#include "stdio.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"

int main(int argc,char*argv[]){

    int ret =-1;
    const char * input_filePath=argv[1];
    const char * output_filePath=argv[2];
    AVFormatContext*ifmt_ctx=NULL;
    AVCodec * codec=NULL;
    AVPacket*pkt=NULL;
    AVFrame*frame=NULL;
    AVCodecContext*codec_ctx=NULL;
    if (argc<3)
    {
        printf("argc = %d\n",argc);
        return -1;
    }
    //打印FFmepg版本
    printf("ffmpeg version:%s\n",av_version_info());
    //打印输入流信息
    if (avformat_open_input(&ifmt_ctx,input_filePath,NULL,NULL)!=0)
    {
        printf("avformat_open_input failed\n");
        return -1;
    }
    //查询流信息
    if (avformat_find_stream_info(ifmt_ctx,NULL)<0)
    {
        printf("avformat_find_stream_info failed\n");
        goto end;
    }
    //寻找视频流索引和解码器
    int video_index=av_find_best_stream(ifmt_ctx,AVMEDIA_TYPE_VIDEO,-1,-1,&codec,0);
    if (video_index<0)
    {
        printf("av_find_best_stream  failed\n");
        goto end;
    }
    
    //打印输入流信息
    printf("输入流信息:\n");
    av_dump_format(ifmt_ctx,video_index,input_filePath,0);
    end:

    return 0;
}