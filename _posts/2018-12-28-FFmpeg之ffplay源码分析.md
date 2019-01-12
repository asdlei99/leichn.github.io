ffplay是FFmpeg工程自带的简单播放器，使用FFmpeg提供的解码器和SDL库进行视频播放。  
ffplay虽是一个简单的播放器，但代码涉及很多概念和细节，分析起来比较烦琐。  
个人感觉分析过程还是有些困难的，业余时间也不多，断断续续持续了挺长一段时间。  

ffplay源码路径为“ffmpeg-4.1/fftools/ffplay.c”，“ffmpeg-4.1”为FFmpeg工程顶层目录，版本为4.1。  
本文主要从如下几个方面进行分析：  
[1]. 各工作线程的功能及数据流向  
[2]. 音视频同步的实现方式，ffplay播放器中最核心的内容  
[3]. 播放/暂停的实现方式  
[5]. 播放速度控制  
[6]. 指定播放点播放(seek，拖动进度条，快进/快退)的实现方式  

在尝试分析源码前，应先阅读如下参考文章：  
[1]. [雷霄骅，视音频编解码技术零基础学习方法](https://blog.csdn.net/leixiaohua1020/article/details/18893769)”  
[2]. [视频编解码基础概念]()  
[3]. [FFmpeg基础概念]()  

## 1. 基本原理  
### 1.1 播放器基本原理  
下图引用自“[雷霄骅，视音频编解码技术零基础学习方法](https://blog.csdn.net/leixiaohua1020/article/details/18893769)”，因原图太小，看不太清楚，故重新制作了一张图片。  
![播放器基本原理示意图](https://leihl.github.io/img/avideo_basics/player_flow.jpg "播放器基本原理示意图")  
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

### 1.2 FFmpeg转码流程  
<pre>
 _______              ______________
|       |            |              |
| input |  demuxer   | encoded data |   decoder
| file  | ---------> | packets      | -----+
|_______|            |______________|      |
                                           v
                                       _________
                                      |         |
                                      | decoded |
                                      | frames  |
                                      |_________|
 ________             ______________       |
|        |           |              |      |
| output | <-------- | encoded data | <----+
| file   |   muxer   | packets      |   encoder
|________|           |______________|

</pre>
`ffmpeg`调用libavformat库(包含解复用器demuxer)，从输入文件中读取到包含编码数据的包(packet)。如果有多个输入文件，`ffmpeg`尝试追踪多个有效输入流的最小时间戳(timestamp)，用这种方式实现多个输入文件的同步。

然后编码包被传递到解码器(decoder)，解码器解码后生成原始帧(frame)，原始帧可以被滤镜(filter)处理(图中未画滤镜)，经滤镜处理后的帧送给编码器，编码器将之编码后输出编码包。最终，由复用器(muxex)将编码码写入特定封装格式的输出文件。

ffplay不需要编码过程，是将上图中的解码后帧送往屏幕显示。

## 2. 代码框架与处理流程  
本节简单梳理ffplay.c代码框架。一些关键问题及细节问题在后续章节探讨。  

### 2.1 流程图  
![ffplay流程图](https://leihl.github.io/img/ffplay_analysis/ffplay_flow.jpg "ffplay流程图")  

### 2.2 关键数据结构  
#### 2.2.1 struct VideoState  
```c  
typedef struct VideoState {
    SDL_Thread *read_tid;           // demux解复用线程
    AVInputFormat *iformat;
    int abort_request;
    int force_refresh;
    int paused;
    int last_paused;
    int queue_attachments_req;
    int seek_req;
    int seek_flags;
    int64_t seek_pos;
    int64_t seek_rel;
    int read_pause_return;
    AVFormatContext *ic;
    int realtime;

    Clock audclk;                   // 音频时钟
    Clock vidclk;                   // 视频时钟
    Clock extclk;                   // 外部时钟

    FrameQueue pictq;               // 视频frame队列
    FrameQueue subpq;               // 字幕frame队列
    FrameQueue sampq;               // 音频frame队列

    Decoder auddec;                 // 音频解码器
    Decoder viddec;                 // 视频解码器
    Decoder subdec;                 // 字幕解码器

    int audio_stream;               // 音频流索引

    int av_sync_type;

    double audio_clock;             // 每个音频帧更新一下此值，以pts形式表示
    int audio_clock_serial;         // 播放序列，seek可改变此值
    double audio_diff_cum; /* used for AV difference average computation */
    double audio_diff_avg_coef;
    double audio_diff_threshold;
    int audio_diff_avg_count;
    AVStream *audio_st;             // 音频流
    PacketQueue audioq;             // 音频packet队列
    int audio_hw_buf_size;          // SDL音频缓冲区大小(单位字节)
    uint8_t *audio_buf;             // 指向待播放的一帧音频数据，指向的数据区将被拷入SDL音频缓冲区。若经过重采样则指向audio_buf1，否则指向frame中的音频
    uint8_t *audio_buf1;            // 音频重采样的输出缓冲区
    unsigned int audio_buf_size; /* in bytes */ // 待播放的一帧音频数据(audio_buf指向)的大小
    unsigned int audio_buf1_size;   // 申请到的音频缓冲区audio_buf1的实际尺寸
    int audio_buf_index; /* in bytes */ // 当前音频帧中已拷入SDL音频缓冲区的位置索引(指向第一个待拷贝字节)
    int audio_write_buf_size;       // 当前音频帧中尚未拷入SDL音频缓冲区的数据量，audio_buf_size = audio_buf_index + audio_write_buf_size
    int audio_volume;               // 音量
    int muted;                      // 静音状态
    struct AudioParams audio_src;   // 音频frame的参数
#if CONFIG_AVFILTER
    struct AudioParams audio_filter_src;
#endif
    struct AudioParams audio_tgt;   // SDL支持的音频参数，重采样转换：audio_src->audio_tgt
    struct SwrContext *swr_ctx;     // 音频重采样context
    int frame_drops_early;          // 丢弃视频packet计数
    int frame_drops_late;           // 丢弃视频frame计数

    enum ShowMode {
        SHOW_MODE_NONE = -1, SHOW_MODE_VIDEO = 0, SHOW_MODE_WAVES, SHOW_MODE_RDFT, SHOW_MODE_NB
    } show_mode;
    int16_t sample_array[SAMPLE_ARRAY_SIZE];
    int sample_array_index;
    int last_i_start;
    RDFTContext *rdft;
    int rdft_bits;
    FFTSample *rdft_data;
    int xpos;
    double last_vis_time;
    SDL_Texture *vis_texture;
    SDL_Texture *sub_texture;
    SDL_Texture *vid_texture;

    int subtitle_stream;                // 字幕流索引
    AVStream *subtitle_st;              // 字幕流
    PacketQueue subtitleq;              // 字幕packet队列

    double frame_timer;                 // 记录最后一帧播放的时刻
    double frame_last_returned_time;
    double frame_last_filter_delay;
    int video_stream;
    AVStream *video_st;                 // 视频流
    PacketQueue videoq;                 // 视频队列
    double max_frame_duration;      // maximum duration of a frame - above this, we consider the jump a timestamp discontinuity
    struct SwsContext *img_convert_ctx;
    struct SwsContext *sub_convert_ctx;
    int eof;

    char *filename;
    int width, height, xleft, ytop;
    int step;

#if CONFIG_AVFILTER
    int vfilter_idx;
    AVFilterContext *in_video_filter;   // the first filter in the video chain
    AVFilterContext *out_video_filter;  // the last filter in the video chain
    AVFilterContext *in_audio_filter;   // the first filter in the audio chain
    AVFilterContext *out_audio_filter;  // the last filter in the audio chain
    AVFilterGraph *agraph;              // audio filter graph
#endif

    int last_video_stream, last_audio_stream, last_subtitle_stream;

    SDL_cond *continue_read_thread;
} VideoState;
```

#### 2.2.2 struct Clock  
```c  
typedef struct Clock {
    // 当前帧(待播放)显示时间戳，播放后，当前帧变成上一帧
    double pts;           /* clock base */
    // 当前帧显示时间戳与当前系统时钟时间的差值
    double pts_drift;     /* clock base minus time at which we updated the clock */
    // 当前时钟(如视频时钟)最后一次更新时间，也可称当前时钟时间
    double last_updated;
    // 时钟速度控制，用于控制播放速度
    double speed;
    // 播放序列，所谓播放序列就是一段连续的播放动作，一个seek操作会启动一段新的播放序列
    int serial;           /* clock is based on a packet with this serial */
    // 暂停标志
    int paused;
    // 指向packet_serial
    int *queue_serial;    /* pointer to the current packet queue serial, used for obsolete clock detection */
} Clock;
```

#### 2.2.3 struct PacketQueue  
```c  
typedef struct PacketQueue {
    MyAVPacketList *first_pkt, *last_pkt;
    int nb_packets;                 // 队列中packet的数量
    int size;                       // 队列所占内存空间大小
    int64_t duration;               // 队列中所有packet总的播放时长
    int abort_request;
    int serial;                     // 播放序列，所谓播放序列就是一段连续的播放动作，一个seek操作会启动一段新的播放序列
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;
```

#### 2.2.4 struct FrameQueue  
```c  
typedef struct FrameQueue {
    Frame queue[FRAME_QUEUE_SIZE];
    int rindex;                     // 读索引。待播放时读取此帧进行播放，播放后此帧成为上一帧
    int windex;                     // 写索引。
    int size;                       // 总帧数
    int max_size;                   // 队列可存储最大帧数
    int keep_last;
    int rindex_shown;               // 当前是否有帧在显示
    SDL_mutex *mutex;
    SDL_cond *cond;
    PacketQueue *pktq;              // 指向对应的packet_queue
} FrameQueue;
```

### 2.3 主线程  
**主线程主要实现三项功能：视频播放(音视频同步)、字幕播放、SDL消息处理。**  

主线程在进行一些必要的初始化工作、创建解复用线程后，即进入event_loop()主循环，处理视频播放和SDL消息事件：  
```c
main() -->
static void event_loop(VideoState *cur_stream)
{
    SDL_Event event;
    ......

    for (;;) {
        // SDL event队列为空，则在while循环中播放视频帧。否则从队列头部取一个event，退出当前函数，在上级函数中处理event
        refresh_loop_wait_event(cur_stream, &event);
        // SDL事件处理
        switch (event.type) {
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
            case SDLK_f:            // f键：强制刷新
                ......
                break;
            case SDLK_p:            // p键
            case SDLK_SPACE:        // 空格键：暂停
                ......
            case SDLK_s:            // s键：逐帧播放
                ......
                break;
            ......
        ......
        }
    }
}
```

#### 2.3.1 视频播放  
##### 2.3.1.1 refresh_loop_wait_event()  
```c
static void refresh_loop_wait_event(VideoState *is, SDL_Event *event) {
    double remaining_time = 0.0;
    SDL_PumpEvents();
    // SDL event队列为空，则在while循环中播放视频帧。否则从队列头部取一个event，退出当前函数，在上级函数中处理event
    while (!SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT)) {
        if (!cursor_hidden && av_gettime_relative() - cursor_last_shown > CURSOR_HIDE_DELAY) {
            SDL_ShowCursor(0);
            cursor_hidden = 1;
        }
        if (remaining_time > 0.0)
            av_usleep((int64_t)(remaining_time * 1000000.0));
        remaining_time = REFRESH_RATE;
        if (is->show_mode != SHOW_MODE_NONE && (!is->paused || is->force_refresh))
            // 立即显示当前帧，或延时remaining_time后再显示
            video_refresh(is, &remaining_time);
        SDL_PumpEvents();
    }
}
```

##### 2.3.1.2 video_refresh()  
主要是从视频frame队列中取出frame进行显示。此函数中进行了音视频的同步。此函数为ffplay.c中最核心函数之一，在第3.3节分析。  

#### 2.3.2 SDL消息处理  
处理各种SDL消息，比如暂停、强制刷新等按键事件。比较简单。  
```c
main() -->
static void event_loop(VideoState *cur_stream)
{
    SDL_Event event;
    ......

    for (;;) {
        // SDL event队列为空，则在while循环中播放视频帧。否则从队列头部取一个event，退出当前函数，在上级函数中处理event
        refresh_loop_wait_event(cur_stream, &event);
        // SDL事件处理
        switch (event.type) {
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
            case SDLK_f:            // f键：强制刷新
                ......
                break;
            case SDLK_p:            // p键
            case SDLK_SPACE:        // 空格键：暂停
                ......
                break;
            ......
        ......
        }
    }
}
```

### 2.4 解复用线程  
**解复用线程读取视频文件，将取到的packet根据类型(音频、视频、字幕)存入不同是packet队列中。**  
为节省篇幅，如下源码中非关键内容的源码使用“......”替代。代码流程参考注释。  
```c
/* this thread gets the stream from the disk or the network */
static int read_thread(void *arg)
{
    VideoState *is = arg;
    AVFormatContext *ic = NULL;
    int st_index[AVMEDIA_TYPE_NB];
    ......

    ......
    
    // 中断回调机制。为底层I/O层提供一个处理接口，比如中止IO操作。
    ic->interrupt_callback.callback = decode_interrupt_cb;
    ic->interrupt_callback.opaque = is;
    if (!av_dict_get(format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
        av_dict_set(&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        scan_all_pmts_set = 1;
    }
    // 1. 构建AVFormatContext
    // 1.1 打开视频文件：读取文件头，将文件格式信息存储在"fmt context"中
    err = avformat_open_input(&ic, is->filename, is->iformat, &format_opts);
    
    ......

    if (find_stream_info) {
        ......
        // 1.2 搜索流信息：读取一段视频文件数据，尝试解码，将取到的流信息填入ic->streams
        //     ic->streams是一个指针数组，数组大小是ic->nb_streams
        err = avformat_find_stream_info(ic, opts);
        ......
    }

    ......

    // 2. 查找用于解码处理的流
    // 2.1 将对应的stream_index存入st_index[]数组
    if (!video_disable)
        st_index[AVMEDIA_TYPE_VIDEO] =          // 视频流
            av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO,
                                st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);
    if (!audio_disable)
        st_index[AVMEDIA_TYPE_AUDIO] =          // 音频流
            av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO,
                                st_index[AVMEDIA_TYPE_AUDIO],
                                st_index[AVMEDIA_TYPE_VIDEO],
                                NULL, 0);
    if (!video_disable && !subtitle_disable)
        st_index[AVMEDIA_TYPE_SUBTITLE] =       // 字幕流
            av_find_best_stream(ic, AVMEDIA_TYPE_SUBTITLE,
                                st_index[AVMEDIA_TYPE_SUBTITLE],
                                (st_index[AVMEDIA_TYPE_AUDIO] >= 0 ?
                                 st_index[AVMEDIA_TYPE_AUDIO] :
                                 st_index[AVMEDIA_TYPE_VIDEO]),
                                NULL, 0);

    is->show_mode = show_mode;
    // 2.2 从待处理流中获取相关参数，设置显示窗口的宽度、高度及宽高比
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        AVStream *st = ic->streams[st_index[AVMEDIA_TYPE_VIDEO]];
        AVCodecParameters *codecpar = st->codecpar;
        // 根据流和帧宽高比猜测帧的样本宽高比。
        // 由于帧宽高比由解码器设置，但流宽高比由解复用器设置，因此这两者可能不相等。此函数会尝试返回待显示帧应当使用的宽高比值。
        // 基本逻辑是优先使用流宽高比(前提是值是合理的)，其次使用帧宽高比。这样，流宽高比(容器设置，易于修改)可以覆盖帧宽高比。
        AVRational sar = av_guess_sample_aspect_ratio(ic, st, NULL);
        if (codecpar->width)
            // 设置显示窗口的大小和宽高比
            set_default_window_size(codecpar->width, codecpar->height, sar);
    }

    // 3. 创建对应流的解码线程
    /* open the streams */
    if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
        // 3.1 创建音频解码线程
        stream_component_open(is, st_index[AVMEDIA_TYPE_AUDIO]);
    }

    ret = -1;
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        // 3.2 创建视频解码线程
        ret = stream_component_open(is, st_index[AVMEDIA_TYPE_VIDEO]);
    }
    if (is->show_mode == SHOW_MODE_NONE)
        is->show_mode = ret >= 0 ? SHOW_MODE_VIDEO : SHOW_MODE_RDFT;

    if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0) {
        // 3.3 创建字幕解码线程
        stream_component_open(is, st_index[AVMEDIA_TYPE_SUBTITLE]);
    }

    ......

    // 4. 解复用处理
    for (;;) {
        // 停止
        ......
        
        // 暂停/继续
        ......
        
        // seek操作
        ......

        ......
        
        // 4.1 从输入文件中读取一个packet
        ret = av_read_frame(ic, pkt);
        if (ret < 0) {
            if ((ret == AVERROR_EOF || avio_feof(ic->pb)) && !is->eof) {
                // 输入文件已读完，则往packet队列中发送NULL packet，以冲洗(flush)解码器，否则解码器中缓存的帧取不出来
                if (is->video_stream >= 0)
                    packet_queue_put_nullpacket(&is->videoq, is->video_stream);
                if (is->audio_stream >= 0)
                    packet_queue_put_nullpacket(&is->audioq, is->audio_stream);
                if (is->subtitle_stream >= 0)
                    packet_queue_put_nullpacket(&is->subtitleq, is->subtitle_stream);
                is->eof = 1;
            }
            if (ic->pb && ic->pb->error)    // 出错则退出当前线程
                break;
            SDL_LockMutex(wait_mutex);
            SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
            SDL_UnlockMutex(wait_mutex);
            continue;
        } else {
            is->eof = 0;
        }
        // 4.2 判断当前packet是否在播放范围内，是则入列，否则丢弃
        /* check if packet is in play range specified by user, then queue, otherwise discard */
        stream_start_time = ic->streams[pkt->stream_index]->start_time; // 第一个显示帧的pts
        pkt_ts = pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
        // 简化一下"||"后那个长长的表达式：
        // [pkt_pts]  - [stream_start_time] - [start_time]                       <= [duration]
        // [当前帧pts] - [第一帧pts]         - [当前播放序列第一帧(seek起始点)pts] <= [duration]
        pkt_in_play_range = duration == AV_NOPTS_VALUE ||
                (pkt_ts - (stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0)) *
                av_q2d(ic->streams[pkt->stream_index]->time_base) -
                (double)(start_time != AV_NOPTS_VALUE ? start_time : 0) / 1000000
                <= ((double)duration / 1000000);
        // 4.3 根据当前packet类型(音频、视频、字幕)，将其存入对应的packet队列
        if (pkt->stream_index == is->audio_stream && pkt_in_play_range) {
            packet_queue_put(&is->audioq, pkt);
        } else if (pkt->stream_index == is->video_stream && pkt_in_play_range
                   && !(is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            packet_queue_put(&is->videoq, pkt);
        } else if (pkt->stream_index == is->subtitle_stream && pkt_in_play_range) {
            packet_queue_put(&is->subtitleq, pkt);
        } else {
            av_packet_unref(pkt);
        }
    }

    ret = 0;
 fail:
    ......
    return 0;
}
```
解复用线程实现如下功能：  
[1]. 创建音频、视频、字幕解码线程  
[2]. 从输入文件读取packet，根据packet类型(音频、视频、字幕)将这放入不同packet队列  

### 2.5 视频解码线程  
**视频解码线程从视频packet队列中取数据，解码后存入视频frame队列。**  

#### 2.5.1 video_thread()  
视频解码线程将解码后的帧放入frame队列中。为节省篇幅，如下源码中删除了滤镜filter相关代码。  
```c
// 视频解码线程：从视频packet_queue中取数据，解码后放入视频frame_queue
static int video_thread(void *arg)
{
    VideoState *is = arg;
    AVFrame *frame = av_frame_alloc();
    double pts;
    double duration;
    int ret;
    AVRational tb = is->video_st->time_base;
    AVRational frame_rate = av_guess_frame_rate(is->ic, is->video_st, NULL);

    if (!frame) {
        return AVERROR(ENOMEM);
    }

    for (;;) {
        ret = get_video_frame(is, frame);
        if (ret < 0)
            goto the_end;
        if (!ret)
            continue;
        
        // 当前帧播放时长
        duration = (frame_rate.num && frame_rate.den ? av_q2d((AVRational){frame_rate.den, frame_rate.num}) : 0);
        // 当前帧显示时间戳
        pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
        // 将当前帧压入frame_queue
        ret = queue_picture(is, frame, pts, duration, frame->pkt_pos, is->viddec.pkt_serial);
        av_frame_unref(frame);

        if (ret < 0)
            goto the_end;
    }
the_end:
    av_frame_free(&frame);
    return 0;
}
```

#### 2.5.2 get_video_frame()  
从packet队列中取一个packet解码得到一个frame，并判断是否要根据framedrop机制丢弃失去同步的视频帧。参考源码中注释：  
```c
static int get_video_frame(VideoState *is, AVFrame *frame)
{
    int got_picture;

    if ((got_picture = decoder_decode_frame(&is->viddec, frame, NULL)) < 0)
        return -1;

    if (got_picture) {
        double dpts = NAN;

        if (frame->pts != AV_NOPTS_VALUE)
            dpts = av_q2d(is->video_st->time_base) * frame->pts;

        frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(is->ic, is->video_st, frame);

        // ffplay文档中对"-framedrop"选项的说明: 
        //   Drop video frames if video is out of sync.Enabled by default if the master clock is not set to video.
        //   Use this option to enable frame dropping for all master clock sources, use - noframedrop to disable it.
        // "-framedrop"选项用于设置当视频帧失去同步时，是否丢弃视频帧。"-framedrop"选项以bool方式改变变量framedrop值。
        // 音视频同步方式有三种：A同步到视频，B同步到音频，C同步到外部时钟。
        // 1) 当命令行不带"-framedrop"选项或"-noframedrop"时，framedrop值为默认值-1，若同步方式是"同步到视频"
        //    则不丢弃失去同步的视频帧，否则将丢弃失去同步的视频帧。
        // 2) 当命令行带"-framedrop"选项时，framedrop值为1，无论何种同步方式，均丢弃失去同步的视频帧。
        // 3) 当命令行带"-noframedrop"选项时，framedrop值为0，无论何种同步方式，均不丢弃失去同步的视频帧。
        if (framedrop>0 || (framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) {
            if (frame->pts != AV_NOPTS_VALUE) {
                double diff = dpts - get_master_clock(is);
                if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
                    diff - is->frame_last_filter_delay < 0 &&
                    is->viddec.pkt_serial == is->vidclk.serial &&
                    is->videoq.nb_packets) {
                    is->frame_drops_early++;
                    av_frame_unref(frame);  // 视频帧失去同步则直接扔掉
                    got_picture = 0;
                }
            }
        }
    }

    return got_picture;
}
```
ffplay中framedrop处理有两种，一处是此处解码后得到的frame尚未存入frame队列前，以is->frame_drops_early++为标记；另一处是frame队列中读取frame进行显示的时候，以is->frame_drops_late++为标记。  
本处framedrop操作涉及的变量is->frame_last_filter_delay属于滤镜filter操作相关，ffplay中默认是关闭滤镜的，本文不考虑滤镜相关操作。  

#### 2.5.3 decoder_decode_frame()  
这个函数是很核心的一个函数，可以解码视频帧和音频帧。视频解码线程中，视频帧实际的解码操作就在此函数中进行。分析过程参考3.2节。  

### 2.6 音频解码线程  
**音频解码线程从音频packet队列中取数据，解码后存入音频frame队列**  

#### 2.6.1 打开音频设备  
音频设备的打开实际是在解复用线程中实现的。解复用线程中先打开音频设备(设定音频回调函数供SDL音频播放线程回调)，然后再创建音频解码线程。调用链如下：  
```c
main() -->
stream_open() -->
read_thread() -->
stream_component_open() -->
    audio_open(is, channel_layout, nb_channels, sample_rate, &is->audio_tgt);
    decoder_start(&is->auddec, audio_thread, is);
```
audio_open()函数填入期望的音频参数，打开音频设备后，将实际的音频参数存入输出参数is->audio_tgt中，后面音频播放线程用会用到此参数，使用此参数将原始音频数据重采样，转换为音频设备支持的格式。  
```c
static int audio_open(void *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params)
{
    SDL_AudioSpec wanted_spec, spec;
    const char *env;
    static const int next_nb_channels[] = {0, 0, 1, 6, 2, 6, 4, 6};
    static const int next_sample_rates[] = {0, 44100, 48000, 96000, 192000};
    int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;

    env = SDL_getenv("SDL_AUDIO_CHANNELS");
    if (env) {  // 若环境变量有设置，优先从环境变量取得声道数和声道布局
        wanted_nb_channels = atoi(env);
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
    }
    if (!wanted_channel_layout || wanted_nb_channels != av_get_channel_layout_nb_channels(wanted_channel_layout)) {
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
        wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
    }
    // 根据channel_layout获取nb_channels，当传入参数wanted_nb_channels不匹配时，此处会作修正
    wanted_nb_channels = av_get_channel_layout_nb_channels(wanted_channel_layout);
    wanted_spec.channels = wanted_nb_channels;  // 声道数
    wanted_spec.freq = wanted_sample_rate;      // 采样率
    if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
        av_log(NULL, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
        return -1;
    }
    while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
        next_sample_rate_idx--;     // 从采样率数组中找到第一个不大于传入参数wanted_sample_rate的值
    // 音频采样格式有两大类型：planar和packed，假设一个双声道音频文件，一个左声道采样点记作L，一个右声道采样点记作R，则：
    // planar存储格式：(plane1)LLLLLLLL...LLLL (plane2)RRRRRRRR...RRRR
    // packed存储格式：(plane1)LRLRLRLR...........................LRLR
    // 在这两种采样类型下，又细分多种采样格式，如AV_SAMPLE_FMT_S16、AV_SAMPLE_FMT_S16P等，注意SDL2.0目前不支持planar格式
    // channel_layout是int64_t类型，表示音频声道布局，每bit代表一个特定的声道，参考channel_layout.h中的定义，一目了然
    // 数据量(bits/秒) = 采样率(Hz) * 采样深度(bit) * 声道数
    wanted_spec.format = AUDIO_S16SYS;          // 采样格式：S表带符号，16是采样深度(位深)，SYS表采用系统字节序，这个宏在SDL中定义
    wanted_spec.silence = 0;                    // 静音值
    wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));   // SDL声音缓冲区尺寸，单位是单声道采样点尺寸x声道数
    wanted_spec.callback = sdl_audio_callback;  // 回调函数，若为NULL，则应使用SDL_QueueAudio()机制
    wanted_spec.userdata = opaque;              // 提供给回调函数的参数
    // 打开音频设备并创建音频处理线程。期望的参数是wanted_spec，实际得到的硬件参数是spec
    // 1) SDL提供两种使音频设备取得音频数据方法：
    //    a. push，SDL以特定的频率调用回调函数，在回调函数中取得音频数据
    //    b. pull，用户程序以特定的频率调用SDL_QueueAudio()，向音频设备提供数据。此种情况wanted_spec.callback=NULL
    // 2) 音频设备打开后播放静音，不启动回调，调用SDL_PauseAudio(0)后启动回调，开始正常播放音频
    // SDL_OpenAudioDevice()第一个参数为NULL时，等价于SDL_OpenAudio()
    while (!(audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE))) {
        av_log(NULL, AV_LOG_WARNING, "SDL_OpenAudio (%d channels, %d Hz): %s\n",
               wanted_spec.channels, wanted_spec.freq, SDL_GetError());
        // 如果打开音频设备失败，则尝试用不同的声道数或采样率再试打开音频设备，这里有些奇怪，暂不深究
        wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
        if (!wanted_spec.channels) {
            wanted_spec.freq = next_sample_rates[next_sample_rate_idx--];
            wanted_spec.channels = wanted_nb_channels;
            if (!wanted_spec.freq) {
                av_log(NULL, AV_LOG_ERROR,
                       "No more combinations to try, audio open failed\n");
                return -1;
            }
        }
        wanted_channel_layout = av_get_default_channel_layout(wanted_spec.channels);
    }
    // 检查打开音频设备的实际参数：采样格式
    if (spec.format != AUDIO_S16SYS) {
        av_log(NULL, AV_LOG_ERROR,
               "SDL advised audio format %d is not supported!\n", spec.format);
        return -1;
    }
    // 检查打开音频设备的实际参数：声道数
    if (spec.channels != wanted_spec.channels) {
        wanted_channel_layout = av_get_default_channel_layout(spec.channels);
        if (!wanted_channel_layout) {
            av_log(NULL, AV_LOG_ERROR,
                   "SDL advised channel count %d is not supported!\n", spec.channels);
            return -1;
        }
    }

    // wanted_spec是期望的参数，spec是实际的参数，wanted_spec和spec都是SDL中的结构。
    // 此处audio_hw_params是FFmpeg中的参数，输出参数供上级函数使用
    audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
    audio_hw_params->freq = spec.freq;
    audio_hw_params->channel_layout = wanted_channel_layout;
    audio_hw_params->channels =  spec.channels;
    audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->channels, 1, audio_hw_params->fmt, 1);
    audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
    if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
        av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
        return -1;
    }
    return spec.size;
}
```
打开音频设备，涉及到FFmpeg中音频存储的基础概念，为稍显清晰，将相关注释摘抄如下：  
**[1]. 音频格式相关**  
     **planar&packed**  
     音频采样格式有两大类型：planar和packed，假设一个双声道音频文件，一个左声道采样点记作L，一个右声道采样点记作R，则：  
     planar存储格式：(plane1)LLLLLLLL...LLLL (plane2)RRRRRRRR...RRRR  
     packed存储格式：(plane1)LRLRLRLR...........................LRLR  
     在这两种采样类型下，又细分多种采样格式，如AV_SAMPLE_FMT_S16、AV_SAMPLE_FMT_S16P等，注意SDL2.0目前不支持planar格式  

     SDL中定义音频参数数据结构定义如下：  
```c  
/**
 *  The calculated values in this structure are calculated by SDL_OpenAudio().
 *
 *  For multi-channel audio, the default SDL channel mapping is:
 *  2:  FL FR                       (stereo)
 *  3:  FL FR LFE                   (2.1 surround)
 *  4:  FL FR BL BR                 (quad)
 *  5:  FL FR FC BL BR              (quad + center)
 *  6:  FL FR FC LFE SL SR          (5.1 surround - last two can also be BL BR)
 *  7:  FL FR FC LFE BC SL SR       (6.1 surround)
 *  8:  FL FR FC LFE BL BR SL SR    (7.1 surround)
 */
typedef struct SDL_AudioSpec
{
    int freq;                   /**< DSP frequency -- samples per second */
    SDL_AudioFormat format;     /**< Audio data format */
    Uint8 channels;             /**< Number of channels: 1 mono, 2 stereo */
    Uint8 silence;              /**< Audio buffer silence value (calculated) */
    Uint16 samples;             /**< Audio buffer size in sample FRAMES (total samples divided by channel count) */
    Uint16 padding;             /**< Necessary for some compile environments */
    Uint32 size;                /**< Audio buffer size in bytes (calculated) */
    SDL_AudioCallback callback; /**< Callback that feeds the audio device (NULL to use SDL_QueueAudio()). */
    void *userdata;             /**< Userdata passed to callback (ignored for NULL callbacks). */
} SDL_AudioSpec;
```

    SDL音频格式定义如下：  
```c  
/**
 *  \brief Audio format flags.
 *
 *  These are what the 16 bits in SDL_AudioFormat currently mean...
 *  (Unspecified bits are always zero).
 *
 *  \verbatim
    ++-----------------------sample is signed if set
    ||
    ||       ++-----------sample is bigendian if set
    ||       ||
    ||       ||          ++---sample is float if set
    ||       ||          ||
    ||       ||          || +---sample bit size---+
    ||       ||          || |                     |
    15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00
    \endverbatim
 *
 *  There are macros in SDL 2.0 and later to query these bits.
 */
typedef Uint16 SDL_AudioFormat;

/**
 *  \name Audio format flags
 *
 *  Defaults to LSB byte order.
 */
/* @{ */
#define AUDIO_U8        0x0008  /**< Unsigned 8-bit samples */
#define AUDIO_S8        0x8008  /**< Signed 8-bit samples */
#define AUDIO_U16LSB    0x0010  /**< Unsigned 16-bit samples */
#define AUDIO_S16LSB    0x8010  /**< Signed 16-bit samples */
#define AUDIO_U16MSB    0x1010  /**< As above, but big-endian byte order */
#define AUDIO_S16MSB    0x9010  /**< As above, but big-endian byte order */
#define AUDIO_U16       AUDIO_U16LSB
#define AUDIO_S16       AUDIO_S16LSB
/* @} */
```

     FFmpeg中定义音频参数的相关数据结构为：  
```c  
// 这个结构是在ffplay.c中定义的：
typedef struct AudioParams {
    int freq;
    int channels;
    int64_t channel_layout;
    enum AVSampleFormat fmt;
    int frame_size;
    int bytes_per_sec;
} AudioParams;

/**
 * Audio sample formats
 *
 * - The data described by the sample format is always in native-endian order.
 *   Sample values can be expressed by native C types, hence the lack of a signed
 *   24-bit sample format even though it is a common raw audio data format.
 *
 * - The floating-point formats are based on full volume being in the range
 *   [-1.0, 1.0]. Any values outside this range are beyond full volume level.
 *
 * - The data layout as used in av_samples_fill_arrays() and elsewhere in FFmpeg
 *   (such as AVFrame in libavcodec) is as follows:
 *
 * @par
 * For planar sample formats, each audio channel is in a separate data plane,
 * and linesize is the buffer size, in bytes, for a single plane. All data
 * planes must be the same size. For packed sample formats, only the first data
 * plane is used, and samples for each channel are interleaved. In this case,
 * linesize is the buffer size, in bytes, for the 1 plane.
 *
 */
enum AVSampleFormat {
    AV_SAMPLE_FMT_NONE = -1,
    AV_SAMPLE_FMT_U8,          ///< unsigned 8 bits
    AV_SAMPLE_FMT_S16,         ///< signed 16 bits
    AV_SAMPLE_FMT_S32,         ///< signed 32 bits
    AV_SAMPLE_FMT_FLT,         ///< float
    AV_SAMPLE_FMT_DBL,         ///< double

    AV_SAMPLE_FMT_U8P,         ///< unsigned 8 bits, planar
    AV_SAMPLE_FMT_S16P,        ///< signed 16 bits, planar
    AV_SAMPLE_FMT_S32P,        ///< signed 32 bits, planar
    AV_SAMPLE_FMT_FLTP,        ///< float, planar
    AV_SAMPLE_FMT_DBLP,        ///< double, planar
    AV_SAMPLE_FMT_S64,         ///< signed 64 bits
    AV_SAMPLE_FMT_S64P,        ///< signed 64 bits, planar

    AV_SAMPLE_FMT_NB           ///< Number of sample formats. DO NOT USE if linking dynamically
};
```

     **channel_layout**  
     channel_layout是int64_t类型，表示音频声道布局，每bit代表一个特定的声道，参考channel_layout.h中的定义：
```c  
/**
 * @defgroup channel_masks Audio channel masks
 *
 * A channel layout is a 64-bits integer with a bit set for every channel.
 * The number of bits set must be equal to the number of channels.
 * The value 0 means that the channel layout is not known.
 * @note this data structure is not powerful enough to handle channels
 * combinations that have the same channel multiple times, such as
 * dual-mono.
 *
 * @{
 */
#define AV_CH_FRONT_LEFT             0x00000001
#define AV_CH_FRONT_RIGHT            0x00000002
#define AV_CH_FRONT_CENTER           0x00000004
#define AV_CH_LOW_FREQUENCY          0x00000008
#define AV_CH_BACK_LEFT              0x00000010
#define AV_CH_BACK_RIGHT             0x00000020
#define AV_CH_FRONT_LEFT_OF_CENTER   0x00000040
#define AV_CH_FRONT_RIGHT_OF_CENTER  0x00000080
#define AV_CH_BACK_CENTER            0x00000100
#define AV_CH_SIDE_LEFT              0x00000200
#define AV_CH_SIDE_RIGHT             0x00000400
#define AV_CH_TOP_CENTER             0x00000800
#define AV_CH_TOP_FRONT_LEFT         0x00001000
#define AV_CH_TOP_FRONT_CENTER       0x00002000
#define AV_CH_TOP_FRONT_RIGHT        0x00004000
#define AV_CH_TOP_BACK_LEFT          0x00008000
#define AV_CH_TOP_BACK_CENTER        0x00010000
#define AV_CH_TOP_BACK_RIGHT         0x00020000
#define AV_CH_STEREO_LEFT            0x20000000  ///< Stereo downmix.
#define AV_CH_STEREO_RIGHT           0x40000000  ///< See AV_CH_STEREO_LEFT.
#define AV_CH_WIDE_LEFT              0x0000000080000000ULL
#define AV_CH_WIDE_RIGHT             0x0000000100000000ULL
#define AV_CH_SURROUND_DIRECT_LEFT   0x0000000200000000ULL
#define AV_CH_SURROUND_DIRECT_RIGHT  0x0000000400000000ULL
#define AV_CH_LOW_FREQUENCY_2        0x0000000800000000ULL

/** Channel mask value used for AVCodecContext.request_channel_layout
    to indicate that the user requests the channel order of the decoder output
    to be the native codec channel order. */
#define AV_CH_LAYOUT_NATIVE          0x8000000000000000ULL

/**
 * @}
 * @defgroup channel_mask_c Audio channel layouts
 * @{
 * */
#define AV_CH_LAYOUT_MONO              (AV_CH_FRONT_CENTER)
#define AV_CH_LAYOUT_STEREO            (AV_CH_FRONT_LEFT|AV_CH_FRONT_RIGHT)
#define AV_CH_LAYOUT_2POINT1           (AV_CH_LAYOUT_STEREO|AV_CH_LOW_FREQUENCY)
#define AV_CH_LAYOUT_2_1               (AV_CH_LAYOUT_STEREO|AV_CH_BACK_CENTER)
#define AV_CH_LAYOUT_SURROUND          (AV_CH_LAYOUT_STEREO|AV_CH_FRONT_CENTER)
#define AV_CH_LAYOUT_3POINT1           (AV_CH_LAYOUT_SURROUND|AV_CH_LOW_FREQUENCY)
#define AV_CH_LAYOUT_4POINT0           (AV_CH_LAYOUT_SURROUND|AV_CH_BACK_CENTER)
#define AV_CH_LAYOUT_4POINT1           (AV_CH_LAYOUT_4POINT0|AV_CH_LOW_FREQUENCY)
#define AV_CH_LAYOUT_2_2               (AV_CH_LAYOUT_STEREO|AV_CH_SIDE_LEFT|AV_CH_SIDE_RIGHT)
#define AV_CH_LAYOUT_QUAD              (AV_CH_LAYOUT_STEREO|AV_CH_BACK_LEFT|AV_CH_BACK_RIGHT)
#define AV_CH_LAYOUT_5POINT0           (AV_CH_LAYOUT_SURROUND|AV_CH_SIDE_LEFT|AV_CH_SIDE_RIGHT)
#define AV_CH_LAYOUT_5POINT1           (AV_CH_LAYOUT_5POINT0|AV_CH_LOW_FREQUENCY)
#define AV_CH_LAYOUT_5POINT0_BACK      (AV_CH_LAYOUT_SURROUND|AV_CH_BACK_LEFT|AV_CH_BACK_RIGHT)
#define AV_CH_LAYOUT_5POINT1_BACK      (AV_CH_LAYOUT_5POINT0_BACK|AV_CH_LOW_FREQUENCY)
#define AV_CH_LAYOUT_6POINT0           (AV_CH_LAYOUT_5POINT0|AV_CH_BACK_CENTER)
#define AV_CH_LAYOUT_6POINT0_FRONT     (AV_CH_LAYOUT_2_2|AV_CH_FRONT_LEFT_OF_CENTER|AV_CH_FRONT_RIGHT_OF_CENTER)
#define AV_CH_LAYOUT_HEXAGONAL         (AV_CH_LAYOUT_5POINT0_BACK|AV_CH_BACK_CENTER)
#define AV_CH_LAYOUT_6POINT1           (AV_CH_LAYOUT_5POINT1|AV_CH_BACK_CENTER)
#define AV_CH_LAYOUT_6POINT1_BACK      (AV_CH_LAYOUT_5POINT1_BACK|AV_CH_BACK_CENTER)
#define AV_CH_LAYOUT_6POINT1_FRONT     (AV_CH_LAYOUT_6POINT0_FRONT|AV_CH_LOW_FREQUENCY)
#define AV_CH_LAYOUT_7POINT0           (AV_CH_LAYOUT_5POINT0|AV_CH_BACK_LEFT|AV_CH_BACK_RIGHT)
#define AV_CH_LAYOUT_7POINT0_FRONT     (AV_CH_LAYOUT_5POINT0|AV_CH_FRONT_LEFT_OF_CENTER|AV_CH_FRONT_RIGHT_OF_CENTER)
#define AV_CH_LAYOUT_7POINT1           (AV_CH_LAYOUT_5POINT1|AV_CH_BACK_LEFT|AV_CH_BACK_RIGHT)
#define AV_CH_LAYOUT_7POINT1_WIDE      (AV_CH_LAYOUT_5POINT1|AV_CH_FRONT_LEFT_OF_CENTER|AV_CH_FRONT_RIGHT_OF_CENTER)
#define AV_CH_LAYOUT_7POINT1_WIDE_BACK (AV_CH_LAYOUT_5POINT1_BACK|AV_CH_FRONT_LEFT_OF_CENTER|AV_CH_FRONT_RIGHT_OF_CENTER)
#define AV_CH_LAYOUT_OCTAGONAL         (AV_CH_LAYOUT_5POINT0|AV_CH_BACK_LEFT|AV_CH_BACK_CENTER|AV_CH_BACK_RIGHT)
#define AV_CH_LAYOUT_HEXADECAGONAL     (AV_CH_LAYOUT_OCTAGONAL|AV_CH_WIDE_LEFT|AV_CH_WIDE_RIGHT|AV_CH_TOP_BACK_LEFT|AV_CH_TOP_BACK_RIGHT|AV_CH_TOP_BACK_CENTER|AV_CH_TOP_FRONT_CENTER|AV_CH_TOP_FRONT_LEFT|AV_CH_TOP_FRONT_RIGHT)
#define AV_CH_LAYOUT_STEREO_DOWNMIX    (AV_CH_STEREO_LEFT|AV_CH_STEREO_RIGHT)
```

**[2]. 打开音频设备**  
     打开音频设备并创建音频处理线程，通过调用SDL_OpenAudio()或SDL_OpenAudioDevice()实现。输入参数是预期的参数，输出参数是实际参数  
     1) SDL提供两种使音频设备取得音频数据方法：  
        a. push，SDL以特定的频率调用回调函数，在回调函数中取得音频数据  
        b. pull，用户程序以特定的频率调用SDL_QueueAudio()，向音频设备提供数据。此种情况wanted_spec.callback=NULL  
     2) 音频设备打开后播放静音，不启动回调，调用SDL_PauseAudio(0)后启动回调，开始正常播放音频  
        SDL_OpenAudioDevice()第一个参数为NULL时，等价于SDL_OpenAudio()  

#### 2.6.2 audio_thread()  
从音频packet_queue中取数据，解码后放入音频frame_queue：  
```c
// 音频解码线程：从音频packet_queue中取数据，解码后放入音频frame_queue
static int audio_thread(void *arg)
{
    VideoState *is = arg;
    AVFrame *frame = av_frame_alloc();
    Frame *af;
    int got_frame = 0;
    AVRational tb;
    int ret = 0;

    if (!frame)
        return AVERROR(ENOMEM);

    do {
        if ((got_frame = decoder_decode_frame(&is->auddec, frame, NULL)) < 0)
            goto the_end;

        if (got_frame) {
                tb = (AVRational){1, frame->sample_rate};

                if (!(af = frame_queue_peek_writable(&is->sampq)))
                    goto the_end;

                af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
                af->pos = frame->pkt_pos;
                af->serial = is->auddec.pkt_serial;
                // 当前帧包含的(单个声道)采样数/采样率就是当前帧的播放时长
                af->duration = av_q2d((AVRational){frame->nb_samples, frame->sample_rate});

                // 将frame数据拷入af->frame，af->frame指向音频frame队列尾部
                av_frame_move_ref(af->frame, frame);
                // 更新音频frame队列大小及写指针
                frame_queue_push(&is->sampq);
        }
    } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
 the_end:
    av_frame_free(&frame);
    return ret;
}
```

#### 2.6.3 decoder_decode_frame()  
此函数既可以解码音频帧，也可以解码视频帧，函数分析参考3.2节。  

### 2.7 字幕解码线程  
实现细节略。以后有机会研究字幕时，再作补充。  

### 2.8 音频播放线程  
音频播放线程是SDL内建的线程，通过回调的方式调用用户提供的回调函数。  
回调函数在SDL_OpenAudio()时指定。  
暂停/继续回调过程由SDL_PauseAudio()控制。  

#### 2.8.1 sdl_audio_callback()  
音频回调函数如下：  
```c  
// 音频处理回调函数。读队列获取音频包，解码，播放
// 此函数被SDL按需调用，此函数不在用户主线程中，因此数据需要保护
// \param[in]  opaque 用户在注册回调函数时指定的参数
// \param[out] stream 音频数据缓冲区地址，将解码后的音频数据填入此缓冲区
// \param[out] len    音频数据缓冲区大小，单位字节
// 回调函数返回后，stream指向的音频缓冲区将变为无效
// 双声道采样点的顺序为LRLRLR
/* prepare a new audio buffer */
static void sdl_audio_callback(void *opaque, Uint8 *stream, int len)
{
    VideoState *is = opaque;
    int audio_size, len1;

    audio_callback_time = av_gettime_relative();

    while (len > 0) {   // 输入参数len等于is->audio_hw_buf_size，是audio_open()中申请到的SDL音频缓冲区大小
        if (is->audio_buf_index >= is->audio_buf_size) {
           // 1. 从音频frame队列中取出一个frame，转换为音频设备支持的格式，返回值是重采样音频帧的大小
           audio_size = audio_decode_frame(is);
           if (audio_size < 0) {
                /* if error, just output silence */
               is->audio_buf = NULL;
               is->audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE / is->audio_tgt.frame_size * is->audio_tgt.frame_size;
           } else {
               if (is->show_mode != SHOW_MODE_VIDEO)
                   update_sample_display(is, (int16_t *)is->audio_buf, audio_size);
               is->audio_buf_size = audio_size;
           }
           is->audio_buf_index = 0;
        }
        // 引入is->audio_buf_index的作用：防止一帧音频数据大小超过SDL音频缓冲区大小，这样一帧数据需要经过多次拷贝
        // 用is->audio_buf_index标识重采样帧中已拷入SDL音频缓冲区的数据位置索引，len1表示本次拷贝的数据量
        len1 = is->audio_buf_size - is->audio_buf_index;
        if (len1 > len)
            len1 = len;
        // 2. 将转换后的音频数据拷贝到音频缓冲区stream中，之后的播放就是音频设备驱动程序的工作了
        if (!is->muted && is->audio_buf && is->audio_volume == SDL_MIX_MAXVOLUME)
            memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
        else {
            memset(stream, 0, len1);
            if (!is->muted && is->audio_buf)
                SDL_MixAudioFormat(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, AUDIO_S16SYS, len1, is->audio_volume);
        }
        len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
    }
    // is->audio_write_buf_size是本帧中尚未拷入SDL音频缓冲区的数据量
    is->audio_write_buf_size = is->audio_buf_size - is->audio_buf_index;
    /* Let's assume the audio driver that is used by SDL has two periods. */
    // 3. 更新时钟
    if (!isnan(is->audio_clock)) {
        // 更新音频时钟，更新时刻：每次往声卡缓冲区拷入数据后
        // 前面audio_decode_frame中更新的is->audio_clock是以音频帧为单位，所以此处第二个参数要减去未拷贝数据量占用的时间
        set_clock_at(&is->audclk, is->audio_clock - (double)(2 * is->audio_hw_buf_size + is->audio_write_buf_size) / is->audio_tgt.bytes_per_sec, is->audio_clock_serial, audio_callback_time / 1000000.0);
        // 使用音频时钟更新外部时钟
        sync_clock_to_slave(&is->extclk, &is->audclk);
    }
}
```

#### 2.8.2 audio_decode_frame()  
从音频frame队列中取出一个frame，此frame的格式是输入文件中的音频格式，音频设备不一定支持这些参数，所以要将frame转换为音频设备支持的格式。  
```c  
/**
 * Decode one audio frame and return its uncompressed size.
 *
 * The processed audio frame is decoded, converted if required, and
 * stored in is->audio_buf, with size in bytes given by the return
 * value.
 */
static int audio_decode_frame(VideoState *is)
{
    int data_size, resampled_data_size;
    int64_t dec_channel_layout;
    av_unused double audio_clock0;
    int wanted_nb_samples;
    Frame *af;

    if (is->paused)
        return -1;

    do {
#if defined(_WIN32)
        while (frame_queue_nb_remaining(&is->sampq) == 0) {
            if ((av_gettime_relative() - audio_callback_time) > 1000000LL * is->audio_hw_buf_size / is->audio_tgt.bytes_per_sec / 2)
                return -1;
            av_usleep (1000);
        }
#endif
        // 若队列头部可读，则由af指向可读帧
        if (!(af = frame_queue_peek_readable(&is->sampq)))
            return -1;
        frame_queue_next(&is->sampq);
    } while (af->serial != is->audioq.serial);

    // 根据frame中指定的音频参数获取缓冲区的大小
    data_size = av_samples_get_buffer_size(NULL, af->frame->channels,   // 本行两参数：linesize，声道数
                                           af->frame->nb_samples,       // 本行一参数：本帧中包含的单个声道中的样本数
                                           af->frame->format, 1);       // 本行两参数：采样格式，不对齐

    // 获取声道布局
    dec_channel_layout =
        (af->frame->channel_layout && af->frame->channels == av_get_channel_layout_nb_channels(af->frame->channel_layout)) ?
        af->frame->channel_layout : av_get_default_channel_layout(af->frame->channels);
    // 获取样本数校正值：若同步时钟是音频，则不调整样本数；否则根据同步需要调整样本数
    wanted_nb_samples = synchronize_audio(is, af->frame->nb_samples);

    // is->audio_tgt是SDL可接受的音频帧数，是audio_open()中取得的参数
    // 在audio_open()函数中又有“is->audio_src = is->audio_tgt”
    // 此处表示：如果frame中的音频参数 == is->audio_src == is->audio_tgt，那音频重采样的过程就免了(因此时is->swr_ctr是NULL)
    // 　　　　　否则使用frame(源)和is->audio_tgt(目标)中的音频参数来设置is->swr_ctx，并使用frame中的音频参数来赋值is->audio_src
    if (af->frame->format        != is->audio_src.fmt            ||
        dec_channel_layout       != is->audio_src.channel_layout ||
        af->frame->sample_rate   != is->audio_src.freq           ||
        (wanted_nb_samples       != af->frame->nb_samples && !is->swr_ctx)) {
        swr_free(&is->swr_ctx);
        // 使用frame(源)和is->audio_tgt(目标)中的音频参数来设置is->swr_ctx
        is->swr_ctx = swr_alloc_set_opts(NULL,
                                         is->audio_tgt.channel_layout, is->audio_tgt.fmt, is->audio_tgt.freq,
                                         dec_channel_layout,           af->frame->format, af->frame->sample_rate,
                                         0, NULL);
        if (!is->swr_ctx || swr_init(is->swr_ctx) < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                    af->frame->sample_rate, av_get_sample_fmt_name(af->frame->format), af->frame->channels,
                    is->audio_tgt.freq, av_get_sample_fmt_name(is->audio_tgt.fmt), is->audio_tgt.channels);
            swr_free(&is->swr_ctx);
            return -1;
        }
        // 使用frame中的参数更新is->audio_src，第一次更新后后面基本不用执行此if分支了，因为一个音频流中各frame通用参数一样
        is->audio_src.channel_layout = dec_channel_layout;
        is->audio_src.channels       = af->frame->channels;
        is->audio_src.freq = af->frame->sample_rate;
        is->audio_src.fmt = af->frame->format;
    }

    if (is->swr_ctx) {
        // 重采样输入参数1：输入音频样本数是af->frame->nb_samples
        // 重采样输入参数2：输入音频缓冲区
        const uint8_t **in = (const uint8_t **)af->frame->extended_data;
        // 重采样输出参数1：输出音频缓冲区尺寸
        // 重采样输出参数2：输出音频缓冲区
        uint8_t **out = &is->audio_buf1;
        // 重采样输出参数：输出音频样本数(多加了256个样本)
        int out_count = (int64_t)wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate + 256;
        // 重采样输出参数：输出音频缓冲区尺寸(以字节为单位)
        int out_size  = av_samples_get_buffer_size(NULL, is->audio_tgt.channels, out_count, is->audio_tgt.fmt, 0);
        int len2;
        if (out_size < 0) {
            av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
            return -1;
        }
        // 如果frame中的样本数经过校正，则条件成立
        if (wanted_nb_samples != af->frame->nb_samples) {
            // 重采样补偿：不清楚参数怎么算的
            if (swr_set_compensation(is->swr_ctx, (wanted_nb_samples - af->frame->nb_samples) * is->audio_tgt.freq / af->frame->sample_rate, 
                                     wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate) < 0) {
                av_log(NULL, AV_LOG_ERROR, "swr_set_compensation() failed\n");
                return -1;
            }
        }
        av_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, out_size);
        if (!is->audio_buf1)
            return AVERROR(ENOMEM);
        // 音频重采样：返回值是重采样后得到的音频数据中单个声道的样本数
        len2 = swr_convert(is->swr_ctx, out, out_count, in, af->frame->nb_samples);
        if (len2 < 0) {
            av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
            return -1;
        }
        if (len2 == out_count) {
            av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
            if (swr_init(is->swr_ctx) < 0)
                swr_free(&is->swr_ctx);
        }
        is->audio_buf = is->audio_buf1;
        // 重采样返回的一帧音频数据大小(以字节为单位)
        resampled_data_size = len2 * is->audio_tgt.channels * av_get_bytes_per_sample(is->audio_tgt.fmt);
    } else {
        // 未经重采样，则将指针指向frame中的音频数据
        is->audio_buf = af->frame->data[0];
        resampled_data_size = data_size;
    }

    audio_clock0 = is->audio_clock;
    /* update the audio clock with the pts */
    if (!isnan(af->pts))
        is->audio_clock = af->pts + (double) af->frame->nb_samples / af->frame->sample_rate;
    else
        is->audio_clock = NAN;
    is->audio_clock_serial = af->serial;
#ifdef DEBUG
    {
        static double last_clock;
        printf("audio: delay=%0.3f clock=%0.3f clock0=%0.3f\n",
               is->audio_clock - last_clock,
               is->audio_clock, audio_clock0);
        last_clock = is->audio_clock;
    }
#endif
    return resampled_data_size;
}
```

## 3. 音视频同步  
音视频同步的目的是为了使播放的声音和显示的画面保持一致。视频按帧播放，图像显示设备每次显示一帧画面，视频播放速度由帧率确定，帧率指示每秒显示多少帧；音频按采样点播放，声音播放设备每次播放一个采样点，声音播放速度由采样率确定，采样率指示每秒播放多少个采样点。如果仅仅是视频按帧率播放，音频按采样率播放，二者没有同步机制，即使最初音视频是基本同步的，随着时间的流逝，音视频会逐渐失去同步，并且不同步的现象会越来越严重。这是因为：一、播放时间难以精确控制，二、异常及误差会随时间累积。所以，必须要采用一定的同步策略，不断对音视频的时间差作校正，使图像显示与声音播放总体保持一致。  

我们以一个44.1KHz的AAC音频流和25FPS的H264视频流为例，来看一下理想情况下音视频的同步过程：  
一个AAC音频frame每个声道包含1024个采样点(也可能是2048，参[13][14])，则一个frame的播放时长(duration)为：(1024/44100)×1000ms = 23.22ms  
一个H264视频frame播放时长(duration)为：1000ms/25 = 40ms  
声卡虽然是以音频采样点为播放单位，但通常我们每次往声卡缓冲区送一个音频frame，每送一个音频frame更新一下音频的播放时刻，即每隔一个音频frame时长更新一下音频时钟，实际上ffplay就是这么做的。  
我们暂且把一个音频时钟更新点记作其播放点，理想情况下，音视频完全同步，音视频播放过程如下图所示：  

![音视频同步理想情况](https://leihl.github.io/img/ffplay_analysis/ffplay_avsync_ideal.jpg "音视频同步理想情况")  

音视频同步的方式基本是确定一个时钟(音频时钟、视频时钟、外部时钟)作为主时钟，非主时钟的音频或视频时钟为从时钟。在播放过程中，主时钟作为同步基准，不断判断从时钟与主时钟的差异，调节从时钟，使从时钟追赶(落后时)或等待(超前时)主时钟。按照主时钟的不同种类，可以将音视频同步模式分为如下三种：  
**音频同步到视频**，视频时钟作为主时钟。  
**视频同步到音频**，音频时钟作为主时钟。  
**音视频同步到外部时钟**，外部时钟作为主时钟。  
ffplay中同步模式的定义如下：  
```c  
enum {
    AV_SYNC_AUDIO_MASTER, /* default choice */
    AV_SYNC_VIDEO_MASTER,
    AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};
```

### 3.1 time_base  
time_base是PTS和DTS的时间单位，也称时间基。  
不同的封装格式time_base不一样，转码过程中的不同阶段time_base也不一样。  
以mpegts封装格式为例，假设视频帧率25FPS为。编码数据包packet(数据结构AVPacket)对应的time_base为AVRational{1,90000}。原始数据帧frame(数据结构AVFrame)对应的time_base为AVRational{1,25}。在解码或播放过程中，我们关注的是frame的time_base，定义在AVStream结构体中，其表示形式AVRational{1,25}是一个分数，值为1/25，单位是秒。在旧的FFmpeg版本中，AVStream中的time_base成员有如下注释：
>For fixed-fps content, time base should be 1/framerate and timestamp increments should be 1.  

当前新版本中已无此条注释。  
```c  
typedef struct AVStream {
    ......
    
    /**
     * This is the fundamental unit of time (in seconds) in terms
     * of which frame timestamps are represented.
     *
     * decoding: set by libavformat
     * encoding: May be set by the caller before avformat_write_header() to
     *           provide a hint to the muxer about the desired timebase. In
     *           avformat_write_header(), the muxer will overwrite this field
     *           with the timebase that will actually be used for the timestamps
     *           written into the file (which may or may not be related to the
     *           user-provided one, depending on the format).
     */
    AVRational time_base;
    
    ......
}

/**
 * Rational number (pair of numerator and denominator).
 */
typedef struct AVRational{
    int num; ///< Numerator
    int den; ///< Denominator
} AVRational;
```
time_base是一个分数，av_q2d(time_base)则可将分数转换为对应的double类型数。因此有如下计算：  
```c  
AVStream *st;
double duration_of_stream = st->duration * av_q2d(st->time_base);   // 视频流播放时长
double pts_of_frame = frame->pts * av_q2d(st->time_base);           // 视频帧显示时间戳
```

### 3.2 PTS&DTS&解码过程  
DTS(Decoding Time Stamp, 解码时间戳)，表示packet的解码时间。  
PTS(Presentation Time Stamp, 显示时间戳)，表示packet解码后数据的显示时间。  
音频中DTS和PTS是相同的。视频中由于B帧需要双向预测，B帧依赖于其前和其后的帧，因此含B帧的视频解码顺序与显示顺序不同，即DTS与PTS不同。当然，不含B帧的视频，其DTS和PTS是相同的。  

下图以一个开放式GOP示意图为例，说明视频流的解码顺序和显示顺序  

![解码和显示顺序](https://leihl.github.io/img/avideo_basics/decode_order.jpg "解码和显示顺序")  

**采集顺序**指图像传感器采集原始信号得到图像帧的顺序。  
**编码顺序**指编码器编码后图像帧的顺序。存储到磁盘的本地视频文件中图像帧的顺序与编码顺序相同。  
**传输顺序**指编码后的流在网络中传输过程中图像帧的顺序。  
**解码顺序**指解码器解码图像帧的顺序。  
**显示顺序**指图像帧在显示器上显示的顺序。  
**采集顺序与显示顺序相同。编码顺序、传输顺序和解码顺序相同。**  
图中“B[1]”帧依赖于“I[0]”帧和“P[3]”帧，因此“P[3]”帧必须比“B[1]”帧先解码。这就导致了解码顺序和显示顺序的不一致，后显示的帧需要先解码。  

理解了含B帧视频流解码顺序与显示顺序的不同，才容易理解解码函数decoder_decode_frame()中对视频解码的处理：  
**avcodec_send_packet()按解码顺序发送packet。**  
**avcodec_receive_frame()按显示顺序输出frame。**  
这个过程由解码器处理，不需要用户程序费心。  
decoder_decode_frame()是非常核心的一个函数，代码本身并不难理解。decoder_decode_frame()是一个通用函数，可以解码音频帧、视频帧和字幕帧，本节着重关注视频帧解码过程。音频帧解码过程在注释中。  
```c
// 从packet_queue中取一个packet，解码生成frame
static int decoder_decode_frame(Decoder *d, AVFrame *frame, AVSubtitle *sub) {
    int ret = AVERROR(EAGAIN);

    for (;;) {
        AVPacket pkt;

        // 本函数被各解码线程(音频、视频、字幕)首次调用时，d->pkt_serial等于-1，d->queue->serial等于1
        if (d->queue->serial == d->pkt_serial) {
            do {
                if (d->queue->abort_request)
                    return -1;

                // 3. 从解码器接收frame
                switch (d->avctx->codec_type) {
                    case AVMEDIA_TYPE_VIDEO:
                        // 3.1 一个视频packet含一个视频frame
                        //     解码器缓存一定数量的packet后，才有解码后的frame输出
                        //     frame输出顺序是按pts的顺序，如IBBPBBP
                        //     frame->pkt_pos变量是此frame对应的packet在视频文件中的偏移地址，值同pkt.pos
                        ret = avcodec_receive_frame(d->avctx, frame);
                        if (ret >= 0) {
                            if (decoder_reorder_pts == -1) {
                                frame->pts = frame->best_effort_timestamp;
                            } else if (!decoder_reorder_pts) {
                                frame->pts = frame->pkt_dts;
                            }
                        }
                        break;
                    case AVMEDIA_TYPE_AUDIO:
                        // 3.2 一个音频packet含多个音频frame，每次avcodec_receive_frame()返回一个frame，此函数返回。
                        // 下次进来此函数，继续获取一个frame，直到avcodec_receive_frame()返回AVERROR(EAGAIN)，
                        // 表示解码器需要填入新的音频packet
                        ret = avcodec_receive_frame(d->avctx, frame);
                        if (ret >= 0) {
                            AVRational tb = (AVRational){1, frame->sample_rate};
                            if (frame->pts != AV_NOPTS_VALUE)
                                frame->pts = av_rescale_q(frame->pts, d->avctx->pkt_timebase, tb);
                            else if (d->next_pts != AV_NOPTS_VALUE)
                                frame->pts = av_rescale_q(d->next_pts, d->next_pts_tb, tb);
                            if (frame->pts != AV_NOPTS_VALUE) {
                                d->next_pts = frame->pts + frame->nb_samples;
                                d->next_pts_tb = tb;
                            }
                        }
                        break;
                }
                if (ret == AVERROR_EOF) {
                    d->finished = d->pkt_serial;
                    avcodec_flush_buffers(d->avctx);
                    return 0;
                }
                if (ret >= 0)
                    return 1;   // 成功解码得到一个视频帧或一个音频帧，则返回
            } while (ret != AVERROR(EAGAIN));
        }

        do {
            if (d->queue->nb_packets == 0)  // packet_queue为空则等待
                SDL_CondSignal(d->empty_queue_cond);
            if (d->packet_pending) {        // 有未处理的packet则先处理
                av_packet_move_ref(&pkt, &d->pkt);
                d->packet_pending = 0;
            } else {
                // 1. 取出一个packet。使用pkt对应的serial赋值给d->pkt_serial
                if (packet_queue_get(d->queue, &pkt, 1, &d->pkt_serial) < 0)
                    return -1;
            }
        } while (d->queue->serial != d->pkt_serial);

        // packet_queue中第一个总是flush_pkt。每次seek操作会插入flush_pkt，更新serial，开启新的播放序列
        if (pkt.data == flush_pkt.data) {
            // 复位解码器内部状态/刷新内部缓冲区。当seek操作或切换流时应调用此函数。
            avcodec_flush_buffers(d->avctx);
            d->finished = 0;
            d->next_pts = d->start_pts;
            d->next_pts_tb = d->start_pts_tb;
        } else {
            if (d->avctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                int got_frame = 0;
                ret = avcodec_decode_subtitle2(d->avctx, sub, &got_frame, &pkt);
                if (ret < 0) {
                    ret = AVERROR(EAGAIN);
                } else {
                    if (got_frame && !pkt.data) {
                       d->packet_pending = 1;
                       av_packet_move_ref(&d->pkt, &pkt);
                    }
                    ret = got_frame ? 0 : (pkt.data ? AVERROR(EAGAIN) : AVERROR_EOF);
                }
            } else {
                // 2. 将packet发送给解码器
                //    发送packet的顺序是按dts递增的顺序，如IPBBPBB
                //    pkt.pos变量可以标识当前packet在视频文件中的地址偏移
                if (avcodec_send_packet(d->avctx, &pkt) == AVERROR(EAGAIN)) {
                    av_log(d->avctx, AV_LOG_ERROR, "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
                    d->packet_pending = 1;
                    av_packet_move_ref(&d->pkt, &pkt);
                }
            }
            av_packet_unref(&pkt);
        }
    }
}
```
本函数实现如下功能：  
[1]. 从视频packet队列中取一个packet  
[2]. 将取得的packet发送给解码器  
[3]. 从解码器接收解码后的frame，此frame作为函数的输出参数供上级函数处理  

注意如下几点：  
[1]. 含B帧的视频文件，其视频帧存储顺序与显示顺序不同  
[2]. 解码器的输入是packet队列，视频帧解码顺序与存储顺序相同，是按dts递增的顺序。dts是解码时间戳，因此存储顺序解码顺序都是dts递增的顺序。avcodec_send_packet()就是将视频文件中的packet序列依次发送给解码器。发送packet的顺序如IPBBPBB。  
[3]. 解码器的输出是frame队列，frame输出顺序是按pts递增的顺序。pts是解码时间戳。pts与dts不一致的问题由解码器进行了处理，用户程序不必关心。从解码器接收frame的顺序如IBBPBBP。  
[4]. 解码器中会缓存一定数量的帧，一个新的解码动作启动后，向解码器送入好几个packet解码器才会输出第一个packet，这比较容易理解，因为解码时帧之间有信赖关系，例如IPB三个帧被送入解码器后，B帧解码需要依赖I帧和P帧，所在在B帧输出前，I帧和P帧必须存在于解码器中而不能删除。理解了这一点，后面视频frame队列中对视频帧的显示和删除机制才容易理解。  
[5]. 解码器中缓存的帧可以通过冲洗(flush)解码器取出。冲洗(flush)解码器的方法就是调用avcodec_send_packet(..., NULL)，然后多次调用avcodec_receive_frame()将缓存帧取尽。缓存帧取完后，avcodec_receive_frame()返回AVERROR_EOF。ffplay中，是通过向解码器发送flush_pkt(实际为NULL)，每次seek操作都会向解码器发送flush_pkt。

如何确定解码器的输出frame与输入packet的对应关系呢？可以对比frame->pkt_pos和pkt.pos的值，这两个值表示packet在视频文件中的偏移地址，如果这两个变量值相等，表示此frame来自此packet。调试跟踪这两个变量值，即能发现解码器输入帧与输出帧的关系。为简便，就不贴图了。  

### 3.3 视频同步到音频  
视频同步到音频是ffplay的默认同步方式。在视频播放线程中实现。  
视频播放函数video_refresh()实现了视频显示(包含同步控制)，是非常核心的一个函数，理解起来也有些难度。  
这个函数的调用过程如下：  
```c  
main() -->
event_loop() -->
refresh_loop_wait_event() -->
video_refresh()
```
函数实现如下：  
```c  
/* called to display each frame */
static void video_refresh(void *opaque, double *remaining_time)
{
    VideoState *is = opaque;
    double time;

    Frame *sp, *sp2;

    if (!is->paused && get_master_sync_type(is) == AV_SYNC_EXTERNAL_CLOCK && is->realtime)
        check_external_clock_speed(is);

    // 音频波形图显示
    if (!display_disable && is->show_mode != SHOW_MODE_VIDEO && is->audio_st) {
        time = av_gettime_relative() / 1000000.0;
        if (is->force_refresh || is->last_vis_time + rdftspeed < time) {
            video_display(is);
            is->last_vis_time = time;
        }
        *remaining_time = FFMIN(*remaining_time, is->last_vis_time + rdftspeed - time);
    }

    // 视频播放
    if (is->video_st) {
retry:
        if (frame_queue_nb_remaining(&is->pictq) == 0) {    // 所有帧已显示
            // nothing to do, no picture to display in the queue
        } else {                                            // 有未显示帧
            double last_duration, duration, delay;
            Frame *vp, *lastvp;

            /* dequeue the picture */
            lastvp = frame_queue_peek_last(&is->pictq);     // 上一帧：上次已显示的帧
            vp = frame_queue_peek(&is->pictq);              // 当前帧：当前待显示的帧

            if (vp->serial != is->videoq.serial) {
                frame_queue_next(&is->pictq);
                goto retry;
            }

            // lastvp和vp不是同一播放序列(一个seek会开始一个新播放序列)，将frame_timer更新为当前时间
            if (lastvp->serial != vp->serial)
                is->frame_timer = av_gettime_relative() / 1000000.0;

            // 暂停处理：不停播放上一帧图像
            if (is->paused)
                goto display;

            /* compute nominal last_duration */
            last_duration = vp_duration(is, lastvp, vp);        // 上一帧播放时长：vp->pts - lastvp->pts
            delay = compute_target_delay(last_duration, is);    // 根据视频时钟和同步时钟的差值，计算delay值

            time= av_gettime_relative()/1000000.0;
            // 当前帧播放时刻(is->frame_timer+delay)大于当前时刻(time)，表示播放时刻未到
            if (time < is->frame_timer + delay) {
                // 播放时刻未到，则更新刷新时间remaining_time为当前时刻到下一播放时刻的时间差
                *remaining_time = FFMIN(is->frame_timer + delay - time, *remaining_time);
                // 播放时刻未到，则不更新rindex，把上一帧再lastvp再播放一遍
                goto display;
            }

            // 更新frame_timer值
            is->frame_timer += delay;
            // 校正frame_timer值：若frame_timer落后于当前系统时间太久(超过最大同步域值)，则更新为当前系统时间
            if (delay > 0 && time - is->frame_timer > AV_SYNC_THRESHOLD_MAX)
                is->frame_timer = time;

            SDL_LockMutex(is->pictq.mutex);
            if (!isnan(vp->pts))
                update_video_pts(is, vp->pts, vp->pos, vp->serial); // 更新视频时钟：时间戳、时钟时间
            SDL_UnlockMutex(is->pictq.mutex);

            // 是否要丢弃未能及时播放的视频帧
            if (frame_queue_nb_remaining(&is->pictq) > 1) {         // 队列中未显示帧数>1(只有一帧则不考虑丢帧)
                Frame *nextvp = frame_queue_peek_next(&is->pictq);  // 下一帧：下一待显示的帧
                duration = vp_duration(is, vp, nextvp);             // 当前帧vp播放时长 = nextvp->pts - vp->pts
                // 1. 非步进模式；2. 丢帧策略生效；3. 当前帧vp未能及时播放，即下一帧播放时刻(is->frame_timer+duration)小于当前系统时刻(time)
                if(!is->step && (framedrop>0 || (framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) && time > is->frame_timer + duration){
                    is->frame_drops_late++;         // framedrop丢帧处理有两处：1) packet入队列前，2) frame未及时显示(此处)
                    frame_queue_next(&is->pictq);   // 删除上一帧已显示帧，即删除lastvp，读指针加1(从lastvp更新到vp)
                    goto retry;
                }
            }

            // 字幕播放
            if (is->subtitle_st) {
                    while (frame_queue_nb_remaining(&is->subpq) > 0) {
                        sp = frame_queue_peek(&is->subpq);

                        if (frame_queue_nb_remaining(&is->subpq) > 1)
                            sp2 = frame_queue_peek_next(&is->subpq);
                        else
                            sp2 = NULL;

                        if (sp->serial != is->subtitleq.serial
                                || (is->vidclk.pts > (sp->pts + ((float) sp->sub.end_display_time / 1000)))
                                || (sp2 && is->vidclk.pts > (sp2->pts + ((float) sp2->sub.start_display_time / 1000))))
                        {
                            if (sp->uploaded) {
                                int i;
                                for (i = 0; i < sp->sub.num_rects; i++) {
                                    AVSubtitleRect *sub_rect = sp->sub.rects[i];
                                    uint8_t *pixels;
                                    int pitch, j;

                                    if (!SDL_LockTexture(is->sub_texture, (SDL_Rect *)sub_rect, (void **)&pixels, &pitch)) {
                                        for (j = 0; j < sub_rect->h; j++, pixels += pitch)
                                            memset(pixels, 0, sub_rect->w << 2);
                                        SDL_UnlockTexture(is->sub_texture);
                                    }
                                }
                            }
                            frame_queue_next(&is->subpq);
                        } else {
                            break;
                        }
                    }
            }

            // 删除当前读指针元素，读指针+1。若未丢帧，读指针从lastvp更新到vp；若有丢帧，读指针从vp更新到nextvp
            frame_queue_next(&is->pictq);
            is->force_refresh = 1;

            if (is->step && !is->paused)
                stream_toggle_pause(is);
        }
display:
        /* display picture */
        if (!display_disable && is->force_refresh && is->show_mode == SHOW_MODE_VIDEO && is->pictq.rindex_shown)
            video_display(is);                      // 取出当前帧vp(若有丢帧是nextvp)进行播放
    }
    is->force_refresh = 0;
    if (show_status) {                              // 更新显示播放状态
        ......
    }
}
```
视频同步到音频的基本方法是：如果视频超前音频，则重复播放上一帧以等待音频；如果视频落后音频，则丢弃当前帧直接播放下一帧来追赶音频。此函数执行流程参考如下流程图：  

![video_refresh()流程图](https://leihl.github.io/img/ffplay_analysis/video_refresh_flow.jpg "video_refresh()流程图")  

步骤如下：  
[1] 根据上一帧lastvp的播放时长duration，校正等到delay值，duration是上一帧理想播放时长，delay是上一帧实际播放时长，根据delay值可以计算得到当前帧的播放时刻  
[2] 如果当前帧vp播放时刻未到，则继续显示上一帧lastvp，并将延时值remaining_time作为输出参数供上级调用函数处理  
[3] 如果当前帧vp播放时刻已到，则立即显示当前帧，并更新读指针  

在video_refresh()函数中，调用了compute_target_delay()来根据视频时钟与主时钟的差异来调节delay值，从而调节视频帧播放的时刻。  
```c
// 根据视频时钟与同步时钟(如音频时钟)的差值，校正delay值，使视频时钟追赶或等待同步时钟
// 输入参数delay是上一帧播放时长，即上一帧播放后应延时多长时间后再播放当前帧，通过调节此值来调节当前帧播放快慢
// 返回值delay是将输入参数delay经校正后得到的值
static double compute_target_delay(double delay, VideoState *is)
{
    double sync_threshold, diff = 0;

    /* update delay to follow master synchronisation source */
    if (get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER) {
        /* if video is slave, we try to correct big delays by
           duplicating or deleting a frame */
        // 视频时钟与同步时钟(如音频时钟)的差异，时钟值是上一帧pts值(实为：上一帧pts + 上一帧至今流逝的时间差)
        diff = get_clock(&is->vidclk) - get_master_clock(is);
        // delay是上一帧播放时长：当前帧(待播放的帧)播放时间与上一帧播放时间差理论值
        // diff是视频时钟与同步时钟的差值

        /* skip or repeat frame. We take into account the
           delay to compute the threshold. I still don't know
           if it is the best guess */
        // 若delay < AV_SYNC_THRESHOLD_MIN，则同步域值为AV_SYNC_THRESHOLD_MIN
        // 若delay > AV_SYNC_THRESHOLD_MAX，则同步域值为AV_SYNC_THRESHOLD_MAX
        // 若AV_SYNC_THRESHOLD_MIN < delay < AV_SYNC_THRESHOLD_MAX，则同步域值为delay
        sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
        if (!isnan(diff) && fabs(diff) < is->max_frame_duration) {
            if (diff <= -sync_threshold)        // 视频时钟落后于同步时钟，且超过同步域值
                delay = FFMAX(0, delay + diff); // 当前帧播放时刻落后于同步时钟(delay+diff<0)则delay=0(视频追赶，立即播放)，否则delay=delay+diff
            else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)  // 视频时钟超前于同步时钟，且超过同步域值，但上一帧播放时长超长
                delay = delay + diff;           // 仅仅校正为delay=delay+diff，主要是AV_SYNC_FRAMEDUP_THRESHOLD参数的作用，不作同步补偿
            else if (diff >= sync_threshold)    // 视频时钟超前于同步时钟，且超过同步域值
                delay = 2 * delay;              // 视频播放要放慢脚步，delay扩大至2倍
        }
    }

    av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n",
            delay, -diff);

    return delay;
}
```
compute_target_delay()的输入参数delay是上一帧理想播放时长duration，返回值delay是经校正后的上一帧实际播放时长。为方便描述，下面我们将输入参数记作duration(对应函数的输入参数delay)，返回值记作delay(对应函数返回值delay)。  
本函数实现功能如下：  
[1] 计算视频时钟与音频时钟(主时钟)的偏差diff，实际就是视频上一帧pts减去音频上一帧pts。所谓上一帧，就是已经播放的最后一帧，上一帧的pts可以标识视频流/音频流的播放时刻(进度)。  
[2] 计算同步域值sync_threshold，同步域值的作用是：若视频时钟与音频时钟差异值小于同步域值，则认为音视频是同步的，不校正delay；若差异值大于同步域值，则认为音视频不同步，需要校正delay值。  
同步域值的计算方法如下：  
若duration < AV_SYNC_THRESHOLD_MIN，则同步域值为AV_SYNC_THRESHOLD_MIN  
若duration > AV_SYNC_THRESHOLD_MAX，则同步域值为AV_SYNC_THRESHOLD_MAX  
若AV_SYNC_THRESHOLD_MIN < duration < AV_SYNC_THRESHOLD_MAX，则同步域值为duration  
[3] delay校正策略如下：  
a) 视频时钟落后于同步时钟且落后值超过同步域值：  
   a1) 若当前帧播放时刻落后于同步时钟(delay+diff<0)则delay=0(视频追赶，立即播放)；  
   a2) 否则delay=duration+diff  
b) 视频时钟超前于同步时钟且超过同步域值：  
   b1) 上一帧播放时长过长(超过最大值)，仅校正为delay=duration+diff；  
   b2) 否则delay=duration×2，视频播放放慢脚步，等待音频  
c) 视频时钟与音频时钟的差异在同步域值内，表明音视频处于同步状态，不校正delay，则delay=duration  

对上述视频同步到音频的过程作一个总结，参考下图：  

![ffplay音视频同步示意图](https://leihl.github.io/img/ffplay_analysis/ffplay_avsync_illustrate.jpg "ffplay音视频同步示意图")  

图中，小黑圆圈是代表帧的实际播放时刻，小红圆圈代表帧的理论播放时刻，小绿方块表示当前系统时间(当前时刻)，小红方块表示位于不同区间的时间点，则当前时刻处于不同区间时，视频同步策略为：  
[1] 当前时刻在T0位置，则重复播放上一帧，延时remaining_time后再播放当前帧  
[2] 当前时刻在T1位置，则立即播放当前帧  
[3] 当前时刻在T2位置，则忽略当前帧，立即显示下一帧，加速视频追赶  
上述内容是为了方便理解进行的简单而形象的描述。实际过程要计算相关值，根据compute_target_delay()和video_refresh()中的策略来控制播放过程。  

### 3.4 音频同步到视频  
音频同步到视频的方式，在音频播放线程中，实现代码在audio_decode_frame()及synchronize_audio()中。  
函数调用关系如下：  
```c  
sdl_audio_callback() -->
audio_decode_frame() -->
synchronize_audio()
```
以后有时间再补充分析过程。  

### 3.5 音视频同步到外部时钟  
略

## 4. 暂停/继续的实现方式  
暂停/继续状态的切换是由用户按空格键实现的，每按一次空格键，暂停/继续的状态翻转一次。  

### 4.1 暂停/继续状态切换  
函数调用关系如下：  
```c  
main() -->
event_loop() -->
toggle_pause() -->
stream_toggle_pause()
```
stream_toggle_pause()实现状态翻转：  
```c  
/* pause or resume the video */
static void stream_toggle_pause(VideoState *is)
{
    if (is->paused) {
        // 这里表示当前是暂停状态，将切换到继续播放状态。在继续播放之前，先将暂停期间流逝的时间加到frame_timer中
        is->frame_timer += av_gettime_relative() / 1000000.0 - is->vidclk.last_updated;
        if (is->read_pause_return != AVERROR(ENOSYS)) {
            is->vidclk.paused = 0;
        }
        set_clock(&is->vidclk, get_clock(&is->vidclk), is->vidclk.serial);
    }
    set_clock(&is->extclk, get_clock(&is->extclk), is->extclk.serial);
    is->paused = is->audclk.paused = is->vidclk.paused = is->extclk.paused = !is->paused;
}
```

### 4.2 暂停状态下的视频播放  
在video_refresh()函数中有如下代码：  
```c  
/* called to display each frame */
static void video_refresh(void *opaque, double *remaining_time)
{
    ......
    
    // 视频播放
    if (is->video_st) {
        ......
        // 暂停处理：不停播放上一帧图像
        if (is->paused)
            goto display;
        
        ......
    }
    
    ......
}
```
在暂停状态下，实际就是不停播放上一帧(最后一帧)图像。画面不更新。  

### 4.3 逐帧播放的实现  
逐帧播放是用户每按一次s键，播放器播放一帧画现。  
逐帧播放实现的方法是：每次按了s键，就将状态切换为播放，播放一帧画面后，将状态切换为暂停。  
函数调用关系如下：  
```c  
main() -->
event_loop() -->
step_to_next_frame() -->
stream_toggle_pause()
```
实现代码比较简单，如下：  
```c  
static void step_to_next_frame(VideoState *is)
{
    /* if the stream is paused unpause it, then step */
    if (is->paused)
        stream_toggle_pause(is);        // 确保切换到播放状态，播放一帧画面
    is->step = 1;
}
```
```c  
/* called to display each frame */
static void video_refresh(void *opaque, double *remaining_time)
{
    ......
    
    // 视频播放
    if (is->video_st) {
        ......
        if (is->step && !is->paused)
            stream_toggle_pause(is);    // 逐帧播放模式下，播放一帧画面后暂停
        ......
    }
    
    ......
}
```
## 5. 播放速度控制  
待补充  

## 6. seek操作的实现方式  
待补充  

## 7. 参考资料  
[1] 雷霄骅，[视音频编解码技术零基础学习方法](https://blog.csdn.net/leixiaohua1020/article/details/18893769)  
[2] [视频编解码基础概念]()<>  
[3] [FFmpeg基础概念]()<>  
[4] [零基础读懂视频播放器控制原理：ffplay播放器源代码分析](https://cloud.tencent.com/developer/article/1004559)<https://cloud.tencent.com/developer/article/1004559>  
[5] [An ffmpeg and SDL Tutorial, Tutorial 05: Synching Video](http://dranger.com/ffmpeg/ffmpegtutorial_all.html#tutorial05.html)  
[6] [视频同步音频](https://zhuanlan.zhihu.com/p/44615401)<https://zhuanlan.zhihu.com/p/44615401>  
[7] [音频同步视频](https://zhuanlan.zhihu.com/p/44680734)<https://zhuanlan.zhihu.com/p/44680734>  
[8] [音视频同步(播放)原理](https://blog.csdn.net/zhuweigangzwg/article/details/25815851)<https://blog.csdn.net/zhuweigangzwg/article/details/25815851>  
[9] [对ffmpeg的时间戳的理解笔记](https://blog.csdn.net/topsluo/article/details/76239136)<https://blog.csdn.net/topsluo/article/details/76239136>  
[10] [ffmpeg音视频同步---视频同步到音频时钟](https://my.oschina.net/u/735973/blog/806117)<https://my.oschina.net/u/735973/blog/806117>  
[11] [FFmpeg音视频同步原理与实现](https://www.jianshu.com/p/3578e794f6b5)<https://www.jianshu.com/p/3578e794f6b5>  
[12] [FFmpeg学习4：音频格式转换](https://www.cnblogs.com/wangguchangqing/p/5851490.html)<https://www.cnblogs.com/wangguchangqing/p/5851490.html>  
[13] [ffmpeg关于音频的总结(一)](https://blog.csdn.net/zhuweigangzwg/article/details/51499123)<https://blog.csdn.net/zhuweigangzwg/article/details/51499123>  
[14] [FFmpeg关于nb_smples,frame_size以及profile的解释](https://blog.csdn.net/zhuweigangzwg/article/details/53335941)<https://blog.csdn.net/zhuweigangzwg/article/details/53335941>

## 8. 修改记录  
2018-12-28  V1.0  初稿  