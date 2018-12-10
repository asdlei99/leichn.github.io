基于FFmpeg和SDL实现的简易视频播放器，主要分为读取视频文件解码和调用SDL显示两大部分。详细流程可参考代码注释。  
本篇实验笔记主要参考如下两篇文章：  
[1]. [最简单的基于FFMPEG+SDL的视频播放器ver2(采用SDL2.0)](https://blog.csdn.net/leixiaohua1020/article/details/38868499)  
[2]. [An ffmpeg and SDL Tutorial](https://blog.csdn.net/leixiaohua1020/article/details/38868499)   

## 1. 视频播放器基本原理  
下图引用自“[雷霄骅，视音频编解码技术零基础学习方法](https://blog.csdn.net/leixiaohua1020/article/details/18893769)”，因原图太小，看不太清楚，故重新制作了一张图片。  
![播放器基本原理示意图](https://leihl.github.io/img/ffmpeg_player/01_player_flow.jpg "播放器基本原理示意图")  
如下内容引用自“[雷霄骅，视音频编解码技术零基础学习方法](https://blog.csdn.net/leixiaohua1020/article/details/18893769)”：  
>**解协议**  
将流媒体协议的数据，解析为标准的相应的封装格式数据。视音频在网络上传播的时候，常常采用各种流媒体协议，例如HTTP，RTMP，或是MMS等等。这些协议在传输视音频数据的同时，也会传输一些信令数据。这些信令数据包括对播放的控制（播放，暂停，停止），或者对网络状态的描述等。解协议的过程中会去除掉信令数据而只保留视音频数据。例如，采用RTMP协议传输的数据，经过解协议操作后，输出FLV格式的数据。
>
>**解封装**  
将输入的封装格式的数据，分离成为音频流压缩编码数据和视频流压缩编码数据。封装格式种类很多，例如MP4，MKV，RMVB，TS，FLV，AVI等等，它的作用就是将已经压缩编码的视频数据和音频数据按照一定的格式放到一起。例如，FLV格式的数据，经过解封装操作后，输出H.264编码的视频码流和AAC编码的音频码流。
>
>**解码**  
将视频/音频压缩编码数据，解码成为非压缩的视频/音频原始数据。音频的压缩编码标准包含AAC，MP3，AC-3等等，视频的压缩编码标准则包含H.264，MPEG2，VC-1等等。解码是整个系统中最重要也是最复杂的一个环节。通过解码，压缩编码的视频数据输出成为非压缩的颜色数据，例如YUV420P，RGB等等；压缩编码的音频数据输出成为非压缩的音频抽样数据，例如PCM数据。
>
>**音视频同步**  
根据解封装模块处理过程中获取到的参数信息，同步解码出来的视频和音频数据，并将视频音频数据送至系统的显卡和声卡播放出来。

## 2. 简易播放器的实现-音频播放  
### 2.1 实验平台
实验平台：openSUSE Leap 42.3  
FFmpeg版本：4.1  
SDL版本：2.0.9  
FFmpeg开发环境搭建可参考“[ffmpeg开发环境构建](https://www.cnblogs.com/leisure_chn/p/10035365.html)”  

### 2.2 源码流程分析  
本实验仅播放视频文件中的声音，而不显示图像。源码流程参考如下：  
![FFmpeg简易播放器-音频播放流程图](https://leihl.github.io/img/ffmpeg_player/02_player_audio_flow.jpg "FFmpeg简易播放器-音频播放流程图")  

### 2.3 关键函数  
几个关键函数的说明直接写在代码注释里：  
#### 2.3.1 开启音频处理子线程  
```c
// 打开音频设备并创建音频处理线程。期望的参数是wanted_spec，实际得到的硬件参数是actual_spec
// 1) SDL提供两种使音频设备取得音频数据方法：
//    a. push，SDL以特定的频率调用回调函数，在回调函数中取得音频数据
//    b. pull，用户程序以特定的频率调用SDL_QueueAudio()，向音频设备提供数据。此种情况wanted_spec.callback=NULL
// 2) 音频设备打开后播放静音，不启动回调，调用SDL_PauseAudio(0)后启动回调，开始正常播放音频
SDL_AudioSpec wanted_spec;
SDL_AudioSpec actual_spec;
wanted_spec.freq = p_codec_ctx->sample_rate;    // 采样率
wanted_spec.format = AUDIO_S16SYS;              // S表带符号，16是采样深度，SYS表采用系统字节序
wanted_spec.channels = p_codec_ctx->channels;   // 声音通道数
wanted_spec.silence = 0;                        // 静音值
wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;    // SDL声音缓冲区尺寸，单位是单声道采样点尺寸x通道数
wanted_spec.callback = audio_callback;          // 回调函数，若为NULL，则应使用SDL_QueueAudio()机制
wanted_spec.userdata = p_codec_ctx;             // 提供给回调函数的参数
SDL_OpenAudio(&wanted_spec, &actual_spec);
```
#### 2.3.2 启动音频回调机制  
```c
// 暂停/继续音频回调处理。参数1表暂停，0表继续。
// 打开音频设备后默认未启动回调处理，通过调用SDL_PauseAudio(0)来启动回调处理。
// 这样就可以在打开音频设备后先为回调函数安全初始化数据，一切就绪后再启动音频回调。
// 在暂停期间，会将静音值往音频设备写。
SDL_PauseAudio(0);
```
#### 2.3.3 音频回调函数  
用户实现的函数，由SDL音频处理子线程回调  
```c
// 音频处理回调函数。读队列获取音频包，解码，播放
// 此函数被SDL按需调用，此函数不在用户主线程中，因此数据需要保护
// \param[in]  userdata用户在注册回调函数时指定的参数
// \param[out] stream 音频数据缓冲区地址，将解码后的音频数据填入此缓冲区
// \param[out] len    音频数据缓冲区大小，单位字节
// 回调函数返回后，stream指向的音频缓冲区将变为无效
// 双声道采样点的顺序为LRLRLR
void audio_callback(void *userdata, uint8_t *stream, int len)
{
    ...
}
```
#### 2.3.4 音频包队列读写函数  
用户实现的函数，主线程向队列尾部写音频包，SDL音频处理子线程(回调函数处理)从队列头部取出音频包
```c
// 写队列尾部
int packet_queue_push(packet_queue_t *q, AVPacket *pkt)
{
    ...
}

// 读队列头部
int packet_queue_pop(packet_queue_t *q, AVPacket *pkt, int block)
{
    ...
}
```
#### 2.3.5 音频解码  
音频解码功能封装为一个函数，将一个音频packet解码后得到的声音数据传递给输出缓冲区。此处的输出缓冲区audio_buf会由上一级调用函数audio_callback()在返回时将缓冲区数据提供给音频设备。  
```c
int audio_decode_frame(AVCodecContext *p_codec_ctx, AVPacket *p_packet, uint8_t *audio_buf, int buf_size)
{
    AVFrame *p_frame = av_frame_alloc();
    
    int frm_size = 0;
    int ret_size = 0;
    int ret;

    // 1 向解码器喂数据，每次喂一个packet
    ret = avcodec_send_packet(p_codec_ctx, p_packet);
    if (ret != 0)
    {
        printf("avcodec_send_packet() failed %d\n", ret);
        av_packet_unref(p_packet);
        return -1;
    }

    ret_size = 0;
    while (1)
    {
        // 2 接收解码器输出的数据，每次接收一个frame
        ret = avcodec_receive_frame(p_codec_ctx, p_frame);
        if (ret != 0)
        {
            if (ret == AVERROR_EOF)
            {
                printf("audio avcodec_receive_frame(): the decoder has been fully flushed\n");
                return 0;
            }
            else if (ret == AVERROR(EAGAIN))
            {
                printf("audio avcodec_receive_frame(): output is not available in this state - "
                       "user must try to send new input\n");
                break;
            }
            else if (ret == AVERROR(EINVAL))
            {
                printf("audio avcodec_receive_frame(): codec not opened, or it is an encoder\n");
            }
            else
            {
                printf("audio avcodec_receive_frame(): legitimate decoding errors\n");
            }
        }

        // 3. 根据相应音频参数，获得所需缓冲区大小
        frm_size = av_samples_get_buffer_size(
                NULL, 
                p_codec_ctx->channels,
                p_frame->nb_samples,
                p_codec_ctx->sample_fmt,
                1);

        printf("frame size %d, buffer size %d\n", frm_size, buf_size);
        assert(frm_size <= buf_size);
        
        // 4. 将音频帧拷贝到函数输出参数audio_buf
        memcpy(audio_buf, p_frame->data[0], frm_size);
        
        if (frm_size > 0)
        {
            ret_size += frm_size;
        }
    }

    av_frame_unref(p_frame);
    
    return ret_size;
}
```
注意：  
[1]. 一个音频packet中含有多个完整的音频帧，因此一次avcodec_send_packet()后，会多次调用avcodec_receive_frame()来将这一个packet解码后的数据接收完。  
[2]. 解码器内部会有缓冲机制，会缓存一定量的音频帧，不冲洗(flush)解码器的话，缓存帧是取不出来的，未冲洗(flush)解码器情况下，avcodec_receive_frame()返回AVERROR(EAGAIN)，表示解码器中改取的帧已取完了(当然缓存帧还是在的)，需要用avcodec_send_packet()向解码器提供新数据。  
[3]. 文件播放完毕时，应冲洗(flush)解码器。冲洗(flush)解码器的方法就是调用avcodec_send_packet(..., NULL)，然后按之前同样的方式多次调用avcodec_receive_frame()将缓存帧取尽。缓存帧取完后，avcodec_receive_frame()返回AVERROR_EOF。  

### 2.4 源码清单  
代码已经变得挺长了，不贴完整源码了，源码参考：  
<https://github.com/leihl/leihl.github.io/blob/master/source/ffmpeg/player_audio/ffplayer.c>  

源码清单中涉及的一些概念简述如下：  
**container:**  
对应数据结构AVFormatContext  
封装器，将流数据封装为指定格式的文件，文件格式如AVI、MP4等。  
FFmpeg可识别五种流类型：视频video(v)、音频audio(a)、attachment(t)、数据data(d)、字幕subtitle。  

**codec:**  
对应数据结构AVCodec  
编解码器。编码器将未压缩的原始图像或音频数据编码为压缩数据。解码器与之相反。  

**codec context**:  
对应数据结构AVCodecContext  
编解码器上下文。此为非常重要的一个数据结构，后文分析。各API大量使用AVCodecContext来引用编解码器。  

**codec par**:  
对应数据结构AVCodecParameters  
编解码器参数。新版本增加的字段。新版本建议使用AVStream->codepar替代AVStream->codec。  

**packet**:  
对应数据结构AVPacket  
经过编码的数据。通过av_read_frame()从媒体文件中获取得到的一个packet可能包含多个(整数个)音频帧或单个
视频帧，或者其他类型的流数据。  

**frame**:  
对应数据结构AVFrame  
解码后的原始数据。解码器将packet解码后生成frame。  

### 2.3 编译
```shell
gcc -o ffplayer ffplayer.c -lavutil -lavformat -lavcodec -lavutil -lswscale -lSDL2
```

### 2.4 测试
选用clock_320.avi测试文件，此文件
```shell
ffprobe clock_320.avi
```
打印视频文件信息如下：  
```shell
[avi @ 0x9286c0] non-interleaved AVI
Input #0, avi, from 'clock_320.avi':
  Duration: 00:00:12.00, start: 0.000000, bitrate: 42 kb/s
    Stream #0:0: Video: msrle ([1][0][0][0] / 0x0001), pal8, 320x320, 1 fps, 1 tbr, 1 tbn, 1 tbc
    Stream #0:1: Audio: truespeech ([34][0][0][0] / 0x0022), 8000 Hz, mono, s16, 8 kb/s
```

运行测试命令：  
```shell
./ffplayer clock_320.avi 
```
可以听到每隔1秒播放一次“嘀”声，播放12次后播放结束。播放过程只有声音，没有图像窗口。播放正常。

## 3. 参考资料  
[1] 雷霄骅，[视音频编解码技术零基础学习方法](https://blog.csdn.net/leixiaohua1020/article/details/18893769)  
[2] 雷霄骅，[最简单的基于FFMPEG+SDL的视频播放器ver2(采用SDL2.0)](https://blog.csdn.net/leixiaohua1020/article/details/38868499)  
[3] SDL WIKI, <https://wiki.libsdl.org/>  
[4] Martin Bohme, [An ffmpeg and SDL Tutorial, Tutorial 03: Playing Sound](http://dranger.com/ffmpeg/ffmpegtutorial_all.html#tutorial03.html)  

## 4. 修改记录  
2018-12-04  V1.0  初稿  