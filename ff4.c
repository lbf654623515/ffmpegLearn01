#include "stdio.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"

int main(int argc,char*argv[]){

    int ret =-1;
    const char * input_filePath=argv[1];
    const char * output_filePath=argv[2];
    AVFormatContext*ifmt_ctx=NULL;  //流媒体信息
    AVCodec * videoCodec=NULL;//视频解码器
    AVCodecContext*videoCodec_ctx=NULL;
    AVPacket*pkt=NULL;
    AVFrame*frame=NULL;
    if (argc<3){
        printf("argc = %d\n",argc);
        return -1;
    }
    //打印FFmepg版本
    printf("ffmpeg version:%s\n",av_version_info());

    //打印输入流信息
    printf("avformat_open_input\n");
    if (avformat_open_input(&ifmt_ctx,input_filePath,NULL,NULL)!=0){
        printf("avformat_open_input failed\n");
        return -1;
    }
    //查询流信息
    printf("avformat_find_stream_info\n");
    if (avformat_find_stream_info(ifmt_ctx,NULL)<0){
        printf("avformat_find_stream_info failed\n");
        goto end;
    }
    //寻找视频流索引和解码器
    printf("av_find_best_stream\n");
    int video_index=av_find_best_stream(ifmt_ctx,AVMEDIA_TYPE_VIDEO,-1,-1,&videoCodec,0);
    if (video_index<0){
        printf("av_find_best_stream  failed\n");
        goto end;
    }
    
    //打印输入流信息
    printf("av_dump_format\n");
    av_dump_format(ifmt_ctx,video_index,input_filePath,0);
    
    printf("avcodec_alloc_context3\n");
    videoCodec_ctx = avcodec_alloc_context3(videoCodec);
    printf("\n");
    if (!videoCodec_ctx)    {
        printf("avcodec_alloc_context3 failed\n");
        goto end;
    }
    //将流的解码器参数拷贝到解码器上下文    
    printf("avcodec_parameters_to_context\n");
    if (avcodec_parameters_to_context(videoCodec_ctx,ifmt_ctx->streams[video_index]->codecpar)<0){
        printf("avcodec_parameters_to_context failed\n");
        goto end;
    }
    printf("avcodec_open2\n");
    if (avcodec_open2(videoCodec_ctx,videoCodec,NULL)<0){
        printf("avcodec_open2 failed\n");
        goto end;
    }
     
    printf("av_packet_alloc\n");
    pkt=av_packet_alloc();
    if (!pkt){
        printf("av_packet_alloc failed\n") ;
        goto end;
    }

    printf("av_frame_alloc\n");
    frame = av_frame_alloc();
    if (!frame){
        printf("av_frame_alloc  failed\n");
        goto end;
    }
    //打开输出文件
    printf("fopen\n");
    FILE*foutput = fopen(output_filePath,"wb");
    if (!foutput){
        printf("fopen failed\n");
        goto end;
    }
    //计算一张图片的大小
    printf("av_image_get_buffer_size\n");
    int size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P,videoCodec_ctx->width,videoCodec_ctx->height,1);
    printf("av_malloc\n");
    uint8_t*yuvbuf= (uint8_t*)av_malloc(size);
    if (!yuvbuf){
        printf("av_malloc failed\n");
        goto end;
    }

   
    printf("av_read_frame\n");
    while ((ret=av_read_frame(ifmt_ctx,pkt)) >= 0){ //从输入文件中读取数据包
        if (pkt->stream_index==video_index){//如果数据包为视频流的数据包
            // printf("avcodec_send_packet\n");
            int val = avcodec_send_packet(videoCodec_ctx,pkt);//发送数据包到解码器
            if (val==AVERROR(EAGAIN)){
                av_packet_unref(pkt);
                continue;
            }else if (val >= 0){
                // printf("avcodec_receive_frame\n");
                val=avcodec_receive_frame(videoCodec_ctx,frame);//解码为数据帧
                if (val==AVERROR(EAGAIN)){//分包没有结束
                    // printf("av_packet_unref\n");
                    av_packet_unref(pkt);
                    continue;
                } else if(val == AVERROR_EOF){//流结束了
                    av_packet_unref(pkt);
                    goto end;
                } else if (val<0){ // 解码错误了
                    // printf("avcodec_receive_frame error (errmsg ‘%s’)\n",av_err2str(val));
                    // printf("av_packet_unref\n");
                    av_packet_unref(pkt);
                    goto end;
                } else {
                    //其他情况
                }
                if (videoCodec_ctx->pix_fmt==AV_PIX_FMT_YUV420P){
                    // printf("memcpy\n");
                    int ySize=frame -> linesize[0] * frame -> height;       //1280*720
                    int uSize=frame -> linesize[1] * frame -> height / 2;   //640*720/2
                    int vSize=frame -> linesize[2] * frame -> height / 2;   //640*720/2
                    
                    memcpy(
                        yuvbuf,
                        frame -> data[0],
                        ySize);
                    memcpy(
                        yuvbuf + ySize,
                        frame -> data[1],
                        uSize);
                    memcpy(
                        yuvbuf + ySize + uSize,
                        frame -> data[2],
                        vSize);

                    // printf("fwrite\n");
                    fwrite(yuvbuf,1,size,foutput);
                }
            }else{
                    av_packet_unref(pkt);
                    goto end;
            }
        }
        // printf("av_packet_unref\n");
        av_packet_unref(pkt);
    }
    if (ret<0&&ret!=AVERROR_EOF){
        printf("av_read_frame error (errmsg '%s')\n",av_err2str(ret));
        goto end;
    }

    //冲刷解码器
    printf("avcodec_send_packet\n");
    avcodec_send_packet(videoCodec_ctx,NULL);
    while (1){
        printf("avcodec_receive_frame\n");
        int val = avcodec_receive_frame(videoCodec_ctx,frame);
        if (val == AVERROR_EOF){
            break;
        }else if (val<0){
            printf("avcodec_receive_frame error (errmsg '%s')\n",av_err2str(val));
            goto end;
        }
        if (val>=0){
            if ( videoCodec_ctx -> pix_fmt == AV_PIX_FMT_YUV420P){
                for (int i = 0; i < frame -> height; i++){
                    memcpy(
                        yuvbuf + i * frame -> linesize[0],
                        frame -> data[0] + frame -> linesize[0] * i,
                        frame -> linesize[0]);
                }
                for (int i = 0; i < frame -> height/2; i++){
                    memcpy(
                        yuvbuf + frame -> width * frame -> height + i * frame -> linesize[i],
                        frame -> data[1] + frame -> linesize[1],
                        frame -> linesize[1]);
                }
                for (int i = 0; i < frame->height/2; i++){
                    memcpy(
                        yuvbuf + frame -> width * frame -> height * 5 / 4 + i * frame -> linesize[2],
                        frame -> data[2] + frame -> linesize[2] * i,
                        frame->linesize[2]);
                }
                printf("fwrite\n");
                fwrite(yuvbuf,1,size,foutput);
            }
        }
    }
    
    end:
    printf("end start\n");
    if (yuvbuf){
        av_free(yuvbuf);
    }
    if (foutput){
        fclose(foutput);
    }
    if (frame){
        av_frame_free(&frame);
    }

    if (videoCodec_ctx){
        avcodec_free_context(&videoCodec_ctx);
    }
    avformat_close_input(&ifmt_ctx);
    return 0;
}