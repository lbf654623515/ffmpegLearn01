#include <stdbool.h>
#include <libavcodec/avcodec.h>
#include <libavutil/timestamp.h>
#include <libavutil/avutil.h>
#include <libavformat/avformat.h>

// ffmpeg -re -i tnhaoxc.flv -c copy -f flv rtmp://192.168.0.104/live

// ffmpeg -i rtmp://192.168.0.104/live -c copy tnlinyrx.flv

// 打开本地文件，推流到流媒体服务
// ./streamer tnhaoxc.flv rtmp://192.168.0.104/live
//
// 打开实时流，保存到文件
// ./streamer rtmp://192.168.0.104/live tnhaoxc.flv
int main(int argc, char **argv){
    AVOutputFormat *o_fmt = NULL;
    AVFormatContext *i_fmt_ctx = NULL, *o_fmt_ctx = NULL;

    const char *in_filename, *out_filename;
    int ret, i;
    int stream_index = 0;
    int *stream_mapping = NULL;
    int stream_mapping_size = 0;

    for(int i=0 ; i<argc && argc>0;i++){
    	printf("arg[%d]:%s\n",i,argv[i]);
    }

    if (argc < 3) {
        printf("usage: %s input output\n"
               "API example program to remux a media file with libavformat and libavcodec.\n"
               "The output format is guessed according to the file extension.\n"
               "\n", argv[0]);
        return 1;
    }
    const char * ff_version = av_version_info();// 打印ffmpeg版本号
    printf("av_version_info:%s\n",ff_version);
    unsigned _version=avformat_version();// 打印ffmpeg中格式化模块的版本号
    printf("avformat_version:%d\n",_version);

    in_filename  = argv[1];//输入流媒体地址
    out_filename = argv[2];//输出流媒体文件名

    // 1. 打开输入
    // 1.1 读取文件头，获取封装格式相关信息
    if ((ret = avformat_open_input(&i_fmt_ctx, in_filename, 0, 0)) < 0) {
        printf("Could not open input file '%s'", in_filename);
        goto end;
    }
    
    // 1.2 解码一段数据，获取流相关信息
    if ((ret = avformat_find_stream_info(i_fmt_ctx, 0)) < 0) {
        printf("Failed to retrieve input stream information");
        goto end;
    }

    av_dump_format(i_fmt_ctx, 0, in_filename, 0);

    // 2. 打开输出
    // 2.1 分配输出ctx
    bool is_net_output_push_stream = false;
    char *o_fmt_name = NULL;
    if (strstr(out_filename, "rtmp://") != NULL) {
        is_net_output_push_stream = true;
        o_fmt_name = "flv";
    } else if (strstr(out_filename, "udp://") != NULL) {
        is_net_output_push_stream = true;
        o_fmt_name = "mpegts";
    } else {
        is_net_output_push_stream = false;
        o_fmt_name = NULL;
    }
    avformat_alloc_output_context2(&o_fmt_ctx, NULL, o_fmt_name, out_filename);
    if (!o_fmt_ctx) {
        printf("Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    stream_mapping_size = i_fmt_ctx->nb_streams;
    stream_mapping = av_mallocz_array(stream_mapping_size, sizeof(*stream_mapping));
    if (!stream_mapping) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    o_fmt = o_fmt_ctx->oformat;

    double duration;

    for (i = 0; i < i_fmt_ctx->nb_streams; i++) {
        AVStream *out_stream;
        AVStream *in_stream = i_fmt_ctx->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;

        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            stream_mapping[i] = -1;
            continue;
        }

        if (is_net_output_push_stream && (in_codecpar->codec_type == AVMEDIA_TYPE_VIDEO)) {
            AVRational frame_rate;
            frame_rate = av_guess_frame_rate(i_fmt_ctx, in_stream, NULL);
            duration = (frame_rate.num && frame_rate.den ? av_q2d((AVRational){frame_rate.num, frame_rate.den}) : 0);
        }

        stream_mapping[i] = stream_index++;

        // 2.2 将一个新流(out_stream)添加到输出文件(o_fmt_ctx)
        out_stream = avformat_new_stream(o_fmt_ctx, NULL);
        if (!out_stream) {
            printf("Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        // 2.3 将当前输入流中的参数拷贝到输出流中
        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0) {
            printf("Failed to copy codec parameters\n");
            goto end;
        }
        out_stream->codecpar->codec_tag = 0;
    }
    av_dump_format(o_fmt_ctx, 0, out_filename, 1);

    if (!(o_fmt->flags & AVFMT_NOFILE)) {    // TODO: 如果输出格式是输出到文件
        // 2.4 创建并初始化一个AVIOContext，用以访问URL(out_filename)指定的资源
        ret = avio_open(&o_fmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            printf("Could not open output file '%s'", out_filename);
            goto end;
        }
    }

    // 3. 数据处理
    // 3.1 写输出文件头
    ret = avformat_write_header(o_fmt_ctx, NULL);
    if (ret < 0) {
        printf("Error occurred when opening output file\n");
        goto end;
    }

    while (1) {
        AVStream *in_stream, *out_stream;
        AVPacket pkt;   // 存储一个packet的结构体
        // 3.2 从输出流读取一个packet
        ret = av_read_frame(i_fmt_ctx, &pkt);
        if (ret < 0) {
            break;
        }

        in_stream  = i_fmt_ctx->streams[pkt.stream_index];
        if (pkt.stream_index >= stream_mapping_size ||
            stream_mapping[pkt.stream_index] < 0) { // 如果这个包是一个无效的包，跳过
            av_packet_unref(&pkt);
            continue;
        }

        int codec_type = in_stream->codecpar->codec_type;
        if (is_net_output_push_stream && (codec_type == AVMEDIA_TYPE_VIDEO)) {
            av_usleep((int64_t)(duration*AV_TIME_BASE));
        }

        pkt.stream_index = stream_mapping[pkt.stream_index];
        out_stream = o_fmt_ctx->streams[pkt.stream_index];
        printf("更新之前pts:% 12.ld,dts:% 12.ld\n",pkt.pts,pkt.dts);
        /* copy packet */
        // 3.3 更新packet中的pts和dts
        // 关于AVStream.time_base(容器中的time_base)的说明：
        // 输入：输入流中含有time_base，在avformat_find_stream_info()中可取到每个流中的time_base
        // 输出：avformat_write_header()会根据输出的封装格式确定每个流的time_base并写入文件中
        // AVPacket.pts和AVPacket.dts的单位是AVStream.time_base，不同的封装格式AVStream.time_base不同
        // 所以输出文件中，每个packet需要根据输出封装格式重新计算pts和dts
        av_packet_rescale_ts(&pkt, in_stream->time_base, out_stream->time_base);
        printf("更新之后pts:% 12.ld,dts:% 12.ld\n",pkt.pts,pkt.dts);
        pkt.pos = -1;

        // 3.4 将packet写入输出
        ret = av_interleaved_write_frame(o_fmt_ctx, &pkt);
        if (ret < 0) {
            printf("Error muxing packet\n");
            break;
        }
        av_packet_unref(&pkt);
    }

    // 3.5 写输出文件尾
    av_write_trailer(o_fmt_ctx);
    printf("处理完成！\n");
end:
    avformat_close_input(&i_fmt_ctx);

    /* close output */
    if (o_fmt_ctx && !(o_fmt->flags & AVFMT_NOFILE)) {
        avio_closep(&o_fmt_ctx->pb);
    }
    avformat_free_context(o_fmt_ctx);

    av_freep(&stream_mapping);

    if (ret < 0 && ret != AVERROR_EOF) {
        printf("Error occurred: %s\n", av_err2str(ret));
        return 1;
    }

    return 0;
}
