# 一、问题描述
调用ffmpeg的avformat_open_input()及av_read_frame()函数时，由于输入源(文件或TCP/UDP流等)的阻塞性质，导致这两个函数阻塞，线程阻塞后无法响应其他事件。

# 二、解决方法
## 1. 打开输入源时设为非阻塞(不推荐)
```c
AVFormatContext *p_ifmt_ctx = NULL;  
p_ifmt_ctx = avformat_alloc_context();  
p_ifmt_ctx->flags |= AVFMT_FLAG_NONBLOCK;  
avformat_open_input(&p_ifmt_ctx, in_fname, 0, 0);  
......
```

## 2. 若输入源是阻塞型FIFO文件
需要**同时使用**如下两种方法  
**1 避免读空FIFO导致阻塞：**  
使用select/poll等设置超时时间，判断FIFO里有数据后再进行读操作：  
```c
fd = fifo_open_for_read("/tmp/audio_ts.fifo", true);  
avformat_open_input(&p_ifmt_ctx, (char *)in_fname, 0, 0);  
...  
poll_fd.fd = fd;  
poll_fd.events = POLLIN;  
poll_fd.revents = 0;  
ret = poll(&poll_fd, 1, 1000);  
if (ret < 0)  
{  
    printf("decoder poll error\n");  
    continue;  
}  
else if (ret == 0)  
{  
    printf("decoder read timeout\n");  
    continue;  
}  
if ((poll_fd.revents & POLLIN) == 0)  
{  
    printf("decoder read no data\n");  
    continue;  
}  
ret = av_read_frame(p_ifmt_ctx, &pkt);
```
**2 避免TS流中只有表无实际音视频数据导致的阻塞：**  
利用IO中断回调函数，同如下第3步描述

## 3. 若输入源是TCP/UDP网址
利用IO中断回调函数(数据结构AVIOInterruptCB)决定退出时机：  
```c
static int condition;
AVFormatContext *p_ifmt_ctx = NULL;
p_ifmt_ctx = avformat_alloc_context();
p_ifmt_ctx->interrupt_callback.callback = decode_interrupt_cb;
p_ifmt_ctx->interrupt_callback.opaque = (void *)&condition;
avformat_open_input(&p_ifmt_ctx, in_fname, 0, 0);
......
static int decode_interrupt_cb(void *ctx)
{
    int *is = (int *)ctx; // ctx即前述p_ifmt_ctx->interrupt_callback.opaque

    if (*is == ??)  // 判断外部条件，满足特定条件则退出阻塞
    {
        return 1;   // 使av_read_frame()退出阻塞
    }
    else
    {
        return 0;   // 不退出阻塞
    }
}
```
# 三、原理分析

## 1. avformat_alloc_context()解析
```c
main()->
stream_open()->
read_thread()->
avformat_alloc_context()->
avformat_get_context_defaults()->
s->io_open  = io_open_default;
```

## 2. avformat_open_input()解析
```c
main()->
stream_open()->
read_thread()->
avformat_open_input()->
init_input()->
```

2.1 填充p_ifmt_ctx->iformat，从av_register_all()注册的全局链表里查到  
注意，这里同样也填充了p_ifmt_ctx->iformat->read_packet，对于AAC而言，将被赋值为ff_raw_read_partial_packet  
```c
av_probe_input_format2()->
av_probe_input_format3()
```

2.2 探测输入源URL协议，填充url_open，url_read等函数，填充p_ifmt_ctx->pb->read_packet  
```c
io_open()->                 // 回调，io_open即io_open_default，参50行
io_open_default()->
ffio_open_whitelist()->
```

2.2.1 获取URLProtocol全局变量数组，填充url_open，url_read等函数，并调用url_open函数打开输入源
```c
ffurl_open_whitelist()->
```  
2.2.1.1 获取到url_open，url_read等，存在一个临时变量h里  
```c
ffurl_alloc()->
url_find_protocol()->
ffurl_get_protocols()   // 从全局数组里查找当前url协议对应的变量
```
2.2.1.2 打开输入源  
```c
ffurl_connect()->
uc->prot->url_open()    // 调用url_open函数打开输入源，注意在上一步url_open已赋值
```

2.2.2 p_ifmt_ctx->pb->opaque->h赋值为上一步的h变量，h里填充了url_open，url_read等函数  
```c
p_ifmt_ctx->pb->read_packet赋值为io_read_packet
ffio_fdopen()->
*s = avio_alloc_context(buffer, buffer_size, h->flags & AVIO_FLAG_WRITE,
                        internal, io_read_packet, io_write_packet, io_seek);
ffio_init_context()->
s->read_packet = read_packet;   // p_ifmt_ctx->pb->read_packet，等号右边实际是io_read_packet
io_read_packet()
```

## 3. av_read_frame()解析
```c
main()->
stream_open()->
read_thread()->
ic->interrupt_callback.callback = decode_interrupt_cb;
ic->interrupt_callback.opaque = is;
err = avformat_open_input(&ic, is->filename, is->iformat, &format_opts);
av_read_frame()->
read_frame_internal()->
ff_read_packet()->
s->iformat->read_packet()->
ff_raw_read_partial_packet()->  // AAC格式对应此函数p_ifmt_ctx->iformat->read_packet, 重要数据结构AVInputFormat
ffio_read_partial()->
s->read_packet()->              // 即p_ifmt_ctx->pb->read_packet，即
io_read_packet()->
ffurl_read()->
retry_transfer_wrapper(h, buf, size, 1, h->prot->url_read)->
file_read()                    // 函数指针h->prot->url_read就等于file_read()
或tcp_read()
```

## 4. 中断回调核心处理函数
retry_transfer_wrapper()函数调用如下代码：
```c
while (len < size_min) {
    if (ff_check_interrupt(&h->interrupt_callback)) // 调用中断回调函数，若返回1则av_read_frame返回
        return AVERROR_EXIT;
    ret = transfer_func(h, buf + len, size - len);  // 调用实际的IO函数，transfer_func实际是tcp_read或file_read等
}
```

## 5. file_read()和tcp_read()处理的不同之处

**5.1 file_read(): 对于输入源是文件(包括FIFO)**

av_read_frame()最终调用的是file_read()函数，file_read()调用read()，只在read之前调用了一下中断回调函数，之后调用read()可能导致线程长时间阻塞  
```c
ret = file_read(h, buf + len, size - len);
```  
其中file_read()函数调用如下：  
```c
ret = read(c->fd, buf, size); // 若是阻塞型FIFO，FIFO为空时会导致线程在此处阻塞
```  

**5.2 tcp_read(): 对于输入源是socket(以TCP为例)**

av_read_frame()最终调用的是tcp_read()函数，每隔0.1秒会调用一下中断回调函数，判断是否需要退出接收过程。(细节：poll阻塞查询是否有数据，若有数据则调用recv接收，若无数据则查询中断回调函数，判断是否要求退出接收)。  
```c
ret = tcp_read(h, buf + len, size - len);
```  
其中tcp_read()函数调用如下：
```c
if (!(h->flags & AVIO_FLAG_NONBLOCK)) {
    ret = ff_network_wait_fd_timeout(s->fd, 0, h->rw_timeout, &h->interrupt_callback); // 每0.1秒判断是否有数据，无则退出
    if (ret)
        return ret;
}
ret = recv(s->fd, buf, size, 0);    // 有数据则接收
```
关键调用链如下：
```c
ff_network_wait_fd_timeout()->
ff_check_interrupt()                // 调用中断回调函数，判断是否退出
ff_network_wait_fd()->
ret = poll(&p, 1, POLLING_TIME);
```

## 6. 重要数据结构
```c
AVInputFormat ff_aac_demuxer = {
    .name         = "aac",
    .long_name    = NULL_IF_CONFIG_SMALL("raw ADTS AAC (Advanced Audio Coding)"),
    .read_probe   = adts_aac_probe,
    .read_header  = adts_aac_read_header,
    .read_packet  = ff_raw_read_partial_packet,
    .flags        = AVFMT_GENERIC_INDEX,
    .extensions   = "aac",
    .mime_type    = "audio/aac,audio/aacp,audio/x-aac",
    .raw_codec_id = AV_CODEC_ID_AAC,
};

const URLProtocol ff_file_protocol = {
    .name                = "file",
    .url_open            = file_open,
    .url_read            = file_read,
    .url_write           = file_write,
    .url_seek            = file_seek,
    .url_close           = file_close,
    .url_get_file_handle = file_get_handle,
    .url_check           = file_check,
    .url_delete          = file_delete,
    .url_move            = file_move,
    .priv_data_size      = sizeof(FileContext),
    .priv_data_class     = &file_class,
    .url_open_dir        = file_open_dir,
    .url_read_dir        = file_read_dir,
    .url_close_dir       = file_close_dir,
    .default_whitelist   = "file,crypto"
};

const URLProtocol ff_tcp_protocol = {
    .name                = "tcp",
    .url_open            = tcp_open,
    .url_accept          = tcp_accept,
    .url_read            = tcp_read,
    .url_write           = tcp_write,
    .url_close           = tcp_close,
    .url_get_file_handle = tcp_get_file_handle,
    .url_get_short_seek  = tcp_get_window_size,
    .url_shutdown        = tcp_shutdown,
    .priv_data_size      = sizeof(TCPContext),
    .flags               = URL_PROTOCOL_FLAG_NETWORK,
    .priv_data_class     = &tcp_class,
};

static const URLProtocol *url_protocols[] = {
    &ff_async_protocol,
    &ff_cache_protocol,
    &ff_concat_protocol,
    &ff_crypto_protocol,
    &ff_data_protocol,
    &ff_ffrtmphttp_protocol,
    &ff_file_protocol,
    &ff_ftp_protocol,
    &ff_gopher_protocol,
    &ff_hls_protocol,
    &ff_http_protocol,
    &ff_httpproxy_protocol,
    &ff_icecast_protocol,
    &ff_mmsh_protocol,
    &ff_mmst_protocol,
    &ff_md5_protocol,
    &ff_pipe_protocol,
    &ff_prompeg_protocol,
    &ff_rtmp_protocol,
    &ff_rtmpt_protocol,
    &ff_rtp_protocol,
    &ff_srtp_protocol,
    &ff_subfile_protocol,
    &ff_tee_protocol,
    &ff_tcp_protocol,
    &ff_udp_protocol,
    &ff_udplite_protocol,
    &ff_unix_protocol,
    NULL };
```

# 四、参考资料
[1] <http://ffmpeg.org/doxygen/1.0/structAVIOInterruptCB.html>  
[2] <https://medium.com/@Masutangu/ffmpeg-%E6%B5%85%E6%9E%90-17985525d9b6>  
[3] <http://blog.csdn.net/finewind/article/details/39502963>  
[4] <http://blog.csdn.net/leixiaohua1020/article/details/44220151>  
[5] <http://blog.csdn.net/zhuweigangzwg/article/details/37929461>  
[6] <http://www.mamicode.com/info-detail-418734.html>  

# 五、修改记录  
2017-09-05　V1.0　初稿。源码分析基于ffmpeg-3.3版本，函数调用关系分析简单罗列，待整理。  
2018-04-25　V1.1　增加只有表、无音视频数据时的阻塞退出方法。未分析FIFO文件回调机制。  
2018-07-28　V1.1　整理文档格式，由txt模式改为md格式。 