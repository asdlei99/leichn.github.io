/*******************************************************************************
 * ffplayer.c
 *
 * history:
 *   2018-11-27 - [lei]     Create file: a simplest ffmpeg player
 *   2018-12-01 - [lei]     Playing audio
 *
 * details:
 *   A simple ffmpeg player.
 *
 * refrence:
 *   1. https://blog.csdn.net/leixiaohua1020/article/details/38868499
 *   2. http://dranger.com/ffmpeg/ffmpegtutorial_all.html#tutorial01.html
 *   3. http://dranger.com/ffmpeg/ffmpegtutorial_all.html#tutorial02.html
 *   4. http://dranger.com/ffmpeg/ffmpegtutorial_all.html#tutorial03.html
 *******************************************************************************/

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <sys/time.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <SDL2/SDL.h>

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

static bool s_input_finished = false;   // 文件读取完毕
static bool s_decode_finished = false;  // 解码完毕

typedef struct packet_queue_t
{
    AVPacketList *first_pkt;
    AVPacketList *last_pkt;
    int nb_packets;   // 队列中AVPacket的个数
    int size;         // 队列中AVPacket总的大小(字节数)
    SDL_mutex *mutex;
    SDL_cond *cond;
} packet_queue_t;

packet_queue_t s_audio_pkt_queue;

void packet_queue_init(packet_queue_t *q)
{
    memset(q, 0, sizeof(packet_queue_t));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}

// 写队列尾部。pkt是一包还未解码的音频数据
int packet_queue_push(packet_queue_t *q, AVPacket *pkt)
{
    AVPacketList *pkt_list;
    
    if (av_packet_make_refcounted(pkt) < 0)
    {
        printf("[pkt] is not refrence counted\n");
        return -1;
    }
    pkt_list = av_malloc(sizeof(AVPacketList));
    if (!pkt_list)
    {
        return -1;
    }
    
    pkt_list->pkt = *pkt;
    pkt_list->next = NULL;

    SDL_LockMutex(q->mutex);

    if (!q->last_pkt)   // 队列为空
    {
        q->first_pkt = pkt_list;
    }
    else
    {
        q->last_pkt->next = pkt_list;
    }
    q->last_pkt = pkt_list;
    q->nb_packets++;
    q->size += pkt_list->pkt.size;
    // 发个条件变量的信号：重启等待q->cond条件变量的一个线程
    SDL_CondSignal(q->cond);

    SDL_UnlockMutex(q->mutex);
    return 0;
}

// 读队列头部。
int packet_queue_pop(packet_queue_t *q, AVPacket *pkt, int block)
{
    AVPacketList *p_pkt_node;
    int ret;

    SDL_LockMutex(q->mutex);

    while (1)
    {
        p_pkt_node = q->first_pkt;
        if (p_pkt_node)             // 队列非空，取一个出来
        {
            q->first_pkt = p_pkt_node->next;
            if (!q->first_pkt)
            {
                q->last_pkt = NULL;
            }
            q->nb_packets--;
            q->size -= p_pkt_node->pkt.size;
            *pkt = p_pkt_node->pkt;
            av_free(p_pkt_node);
            ret = 1;
            break;
        }
        else if (s_input_finished)  // 队列已空，文件已处理完
        {
            ret = 0;
            break;
        }
        else if (!block)            // 队列空且阻塞标志无效，则立即退出
        {
            ret = 0;
            break;
        }
        else                        // 队列空且阻塞标志有效，则等待
        {
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}

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


// 音频处理回调函数。读队列获取音频包，解码，播放
// 此函数被SDL按需调用，此函数不在用户主线程中，因此数据需要保护
// \param[in]  userdata用户在注册回调函数时指定的参数
// \param[out] stream 音频数据缓冲区地址，将解码后的音频数据填入此缓冲区
// \param[out] len    音频数据缓冲区大小，单位字节
// 回调函数返回后，stream指向的音频缓冲区将变为无效
// 双声道采样点的顺序为LRLRLR
void audio_callback(void *userdata, uint8_t *stream, int len)
{
    AVCodecContext *p_codec_ctx = (AVCodecContext *)userdata;
    int copy_len;           // 
    int get_size;           // 获取到解码后的音频数据大小

    static uint8_t s_audio_buf[(MAX_AUDIO_FRAME_SIZE*3)/2]; // 1.5倍声音帧的大小
    static uint32_t s_audio_len = 0;    // 新取得的音频数据大小
    static uint32_t s_tx_idx = 0;       // 已发送给设备的数据量


    AVPacket *p_packet;

    int frm_size = 0;
    int ret_size = 0;
    int ret;

    while (len > 0)         // 确保stream缓冲区填满，填满后此函数返回
    {
        if (s_decode_finished)
        {
            return;
        }

        if (s_tx_idx >= s_audio_len)
        {   // audio_buf缓冲区中数据已全部取出，则从队列中获取更多数据

            p_packet = (AVPacket *)av_malloc(sizeof(AVPacket));
            
            // 1. 从队列中读出一包音频数据
            if (packet_queue_pop(&s_audio_pkt_queue, p_packet, 1) <= 0)
            {
                if (s_input_finished)
                {
                    av_packet_unref(p_packet);
                    p_packet = NULL;    // flush decoder
                    printf("Flushing decoder...\n");
                }
                else
                {
                    av_packet_unref(p_packet);
                    return;
                }
            }

            // 2. 解码音频包
            get_size = audio_decode_frame(p_codec_ctx, p_packet, s_audio_buf, sizeof(s_audio_buf));
            if (get_size < 0)
            {
                // 出错输出一段静音
                s_audio_len = 1024; // arbitrary?
                memset(s_audio_buf, 0, s_audio_len);
                av_packet_unref(p_packet);
            }
            else if (get_size == 0) // 解码缓冲区被冲洗，整个解码过程完毕
            {
                s_decode_finished = true;
            }
            else
            {
                s_audio_len = get_size;
                av_packet_unref(p_packet);
            }
            s_tx_idx = 0;

            if (p_packet->data != NULL)
            {
                //av_packet_unref(p_packet);
            }
        }

        copy_len = s_audio_len - s_tx_idx;
        if (copy_len > len)
        {
            copy_len = len;
        }

        // 将解码后的音频帧(s_audio_buf+)写入音频设备缓冲区(stream)，播放
        memcpy(stream, (uint8_t *)s_audio_buf + s_tx_idx, copy_len);
        len -= copy_len;
        stream += copy_len;
        s_tx_idx += copy_len;
    }
}

int main(int argc, char *argv[])
{
    // Initalizing these to NULL prevents segfaults!
    AVFormatContext*    p_fmt_ctx = NULL;
    AVCodecContext*     p_codec_ctx = NULL;
    AVCodecParameters*  p_codec_par = NULL;
    AVCodec*            p_codec = NULL;
    AVPacket*           p_packet = NULL;

    SDL_AudioSpec       wanted_spec;
    SDL_AudioSpec       actual_spec;

    int                 i = 0;
    int                 a_idx = -1;
    int                 ret = 0;
    int                 res = 0;

    if (argc < 2)
    {
        printf("Please provide a movie file\n");
        return -1;
    }

    // 初始化libavformat(所有格式)，注册所有复用器/解复用器
    // av_register_all();   // 已被申明为过时的，直接不再使用即可

    // A1. 构建AVFormatContext
    // A1.1 打开视频文件：读取文件头，将文件格式信息存储在"fmt context"中
    ret = avformat_open_input(&p_fmt_ctx, argv[1], NULL, NULL);
    if (ret != 0)
    {
        printf("avformat_open_input() failed %d\n", ret);
        res = -1;
        goto exit0;
    }

    // A1.2 搜索流信息：读取一段视频文件数据，尝试解码，将取到的流信息填入p_fmt_ctx->streams
    //      p_fmt_ctx->streams是一个指针数组，数组大小是pFormatCtx->nb_streams
    ret = avformat_find_stream_info(p_fmt_ctx, NULL);
    if (ret < 0)
    {
        printf("avformat_find_stream_info() failed %d\n", ret);
        res = -1;
        goto exit1;
    }

    // 将文件相关信息打印在标准错误设备上
    av_dump_format(p_fmt_ctx, 0, argv[1], 0);

    // A2. 查找第一个音频流
    a_idx = -1;
    for (i=0; i<p_fmt_ctx->nb_streams; i++)
    {
        if (p_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            a_idx = i;
            printf("Find a audio stream, index %d\n", a_idx);
            break;
        }
    }
    if (a_idx == -1)
    {
        printf("Cann't find audio stream\n");
        res = -1;
        goto exit1;
    }

    // A3. 为音频流构建解码器AVCodecContext

    // A3.1 获取解码器参数AVCodecParameters
    p_codec_par = p_fmt_ctx->streams[a_idx]->codecpar;

    // A3.2 获取解码器
    p_codec = avcodec_find_decoder(p_codec_par->codec_id);
    if (p_codec == NULL)
    {
        printf("Cann't find codec!\n");
        res = -1;
        goto exit1;
    }

    // A3.3 构建解码器AVCodecContext
    // A3.3.1 p_codec_ctx初始化：分配结构体，使用p_codec初始化相应成员为默认值
    p_codec_ctx = avcodec_alloc_context3(p_codec);
    if (p_codec_ctx == NULL)
    {
        printf("avcodec_alloc_context3() failed %d\n", ret);
        res = -1;
        goto exit1;
    }
    // A3.3.2 p_codec_ctx初始化：p_codec_par ==> p_codec_ctx，初始化相应成员
    ret = avcodec_parameters_to_context(p_codec_ctx, p_codec_par);
    if (ret < 0)
    {
        printf("avcodec_parameters_to_context() failed %d\n", ret);
        res = -1;
        goto exit2;
    }
    // A3.3.3 p_codec_ctx初始化：使用p_codec初始化p_codec_ctx，初始化完成
    ret = avcodec_open2(p_codec_ctx, p_codec, NULL);
    if (ret < 0)
    {
        printf("avcodec_open2() failed %d\n", ret);
        res = -1;
        goto exit2;
    }

    p_packet = (AVPacket *)av_malloc(sizeof(AVPacket));
    if (p_packet == NULL)
    {  
        printf("av_malloc() failed\n");  
        res = -1;
        goto exit2;
    }

    // B1. 初始化SDL子系统：缺省(事件处理、文件IO、线程)、视频、音频、定时器
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {  
        printf("SDL_Init() failed: %s\n", SDL_GetError()); 
        res = -1;
        goto exit3;
    }

    packet_queue_init(&s_audio_pkt_queue);

    // B2. 打开音频设备并创建音频处理线程。期望的参数是wanted_spec，实际得到的硬件参数是actual_spec
    // 1) SDL提供两种使音频设备取得音频数据方法：
    //    a. push，SDL以特定的频率调用回调函数，在回调函数中取得音频数据
    //    b. pull，用户程序以特定的频率调用SDL_QueueAudio()，向音频设备提供数据。此种情况wanted_spec.callback=NULL
    // 2) 音频设备打开后播放静音，不启动回调，调用SDL_PauseAudio(0)后启动回调，开始正常播放音频
    wanted_spec.freq = p_codec_ctx->sample_rate;    // 采样率
    wanted_spec.format = AUDIO_S16SYS;              // S表带符号，16是采样深度，SYS表采用系统字节序
    wanted_spec.channels = p_codec_ctx->channels;   // 声音通道数
    wanted_spec.silence = 0;                        // 静音值
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;    // SDL声音缓冲区尺寸，单位是单声道采样点尺寸x通道数
    wanted_spec.callback = audio_callback;          // 回调函数，若为NULL，则应使用SDL_QueueAudio()机制
    wanted_spec.userdata = p_codec_ctx;             // 提供给回调函数的参数
    if (SDL_OpenAudio(&wanted_spec, &actual_spec) < 0)
    {
        printf("SDL_OpenAudio() failed: %s\n", SDL_GetError());
        goto exit4;
    }
    
    // B3. 暂停/继续音频回调处理。参数1表暂停，0表继续。
    //     打开音频设备后默认未启动回调处理，通过调用SDL_PauseAudio(0)来启动回调处理。
    //     这样就可以在打开音频设备后先为回调函数安全初始化数据，一切就绪后再启动音频回调。
    //     在暂停期间，会将静音值往音频设备写。
    SDL_PauseAudio(0);

    // A4. 从视频文件中读取一个packet，此处仅处理音频packet
    //     对于音频来说，若是帧长固定的格式则一个packet可包含整数个frame，
    //                   若是帧长可变的格式则一个packet只包含一个frame
    while (av_read_frame(p_fmt_ctx, p_packet) == 0)
    {
        if (p_packet->stream_index == a_idx)
        {
            packet_queue_push(&s_audio_pkt_queue, p_packet);
        }
        else
        {
            av_packet_unref(p_packet);
        }
    }
    SDL_Delay(40);
    s_input_finished = true;

    // A5. 等待解码结束
    while (!s_decode_finished)
    {
        SDL_Delay(1000);
    }
    SDL_Delay(1000);

exit4:
    SDL_Quit();
exit3:
    av_packet_unref(p_packet);
exit2:
    avcodec_free_context(p_codec_ctx);
exit1:
    avformat_close_input(&p_fmt_ctx);
exit0:
    return res;
}


