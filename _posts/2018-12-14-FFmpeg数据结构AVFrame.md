## 1. 数据结构定义  
struct AVPacket定义于avcodec.h  
```c  
struct AVPacket packet;
```
AVPacket中存储的是经过编码的压缩数据。在解码中，AVPacket由解复用器输出到解码器。在编码中，AVPacket由编码器输出到复用器。如下图：  


对于视频而言，一个AVPacket通常只包含一个压缩视频帧。而对于音频而言，一个AVPacket可能包含多个完整的音频压缩帧。AVPacket也可以不包含压缩编码数据，而只包含side data，这种包可以称为空packet。例如，编码结束后只需要更新一些参数时就可以发空packet。  

AVPacket对象可以在栈上分配，注意此处指的是AVPacket对象本身。而AVPacket中包含的数据缓冲区是通过av_malloc()在堆上分配的。  
<font color=red>TODO: AVPacket对象在栈上分配，原理不清楚，待研究</font>  


```c
/**
 * This structure stores compressed data. It is typically exported by demuxers
 * and then passed as input to decoders, or received as output from encoders and
 * then passed to muxers.
 *
 * For video, it should typically contain one compressed frame. For audio it may
 * contain several compressed frames. Encoders are allowed to output empty
 * packets, with no compressed data, containing only side data
 * (e.g. to update some stream parameters at the end of encoding).
 *
 * AVPacket is one of the few structs in FFmpeg, whose size is a part of public
 * ABI. Thus it may be allocated on stack and no new fields can be added to it
 * without libavcodec and libavformat major bump.
 *
 * The semantics of data ownership depends on the buf field.
 * If it is set, the packet data is dynamically allocated and is
 * valid indefinitely until a call to av_packet_unref() reduces the
 * reference count to 0.
 *
 * If the buf field is not set av_packet_ref() would make a copy instead
 * of increasing the reference count.
 *
 * The side data is always allocated with av_malloc(), copied by
 * av_packet_ref() and freed by av_packet_unref().
 *
 * @see av_packet_ref
 * @see av_packet_unref
 */
typedef struct AVPacket {
    /**
     * A reference to the reference-counted buffer where the packet data is
     * stored.
     * May be NULL, then the packet data is not reference-counted.
     */
    AVBufferRef *buf;
    /**
     * Presentation timestamp in AVStream->time_base units; the time at which
     * the decompressed packet will be presented to the user.
     * Can be AV_NOPTS_VALUE if it is not stored in the file.
     * pts MUST be larger or equal to dts as presentation cannot happen before
     * decompression, unless one wants to view hex dumps. Some formats misuse
     * the terms dts and pts/cts to mean something different. Such timestamps
     * must be converted to true pts/dts before they are stored in AVPacket.
     */
    int64_t pts;
    /**
     * Decompression timestamp in AVStream->time_base units; the time at which
     * the packet is decompressed.
     * Can be AV_NOPTS_VALUE if it is not stored in the file.
     */
    int64_t dts;
    uint8_t *data;
    int   size;
    int   stream_index;
    /**
     * A combination of AV_PKT_FLAG values
     */
    int   flags;
    /**
     * Additional packet data that can be provided by the container.
     * Packet can contain several types of side information.
     */
    AVPacketSideData *side_data;
    int side_data_elems;

    /**
     * Duration of this packet in AVStream->time_base units, 0 if unknown.
     * Equals next_pts - this_pts in presentation order.
     */
    int64_t duration;

    int64_t pos;                            ///< byte position in stream, -1 if unknown

#if FF_API_CONVERGENCE_DURATION
    /**
     * @deprecated Same as the duration field, but as int64_t. This was required
     * for Matroska subtitles, whose duration values could overflow when the
     * duration field was still an int.
     */
    attribute_deprecated
    int64_t convergence_duration;
#endif
} AVPacket;
```
用户不应直接使用AVBuffer。  
**音视频数据缓冲区**  
- `uint8_t *data`:  
- `int size`:  
  数据缓冲区地址与大小。音视频编码压缩数据存储于此片内存区域。此内存区域由`AVBufferRef *buf`管理。  
- `AVBufferRef *buf`:  
  数据缓冲区引用，也可叫引用计数缓冲区。对上一字段`uint8_t *data`指向的内存区域提供引用计数等管理机制。  
  AVBufferRef对数据缓冲区提供了管理机制，用户不应直接访问数据缓冲区。参考“[ffmpeg数据结构AVBuffer]()”  
  如果`buf`值为NULL，则`data`指向的数据缓冲区不使用引用计数机制。`av_packet_ref(dst, src)`将执行数据缓冲区的拷贝，而非仅仅增加缓冲区引用计数。  
  如果`buf`值非NULL，则`data`指向的数据缓冲区使用引用计数机制。`av_packet_ref(dst, src)`将不拷贝缓冲区，而仅增加缓冲区引用计数。`av_packet_unref()`将数据缓冲区引用计数减1，当缓冲区引用计数为0时，缓冲区内存被FFmpeg回收。  
  对于`struct AVPacket pkt`对象，如果`pkt.buf`值非NULL，则有pkt.data == pkt.buf->data == pkt.buf->buffer.data  
**额外类型数据**  
- `AVPacketSideData *side_data`  
- `int side_data_elems`  
  由容器(container)提供的额外包数据。<font color=red>TODO: 待研究</font>  
**packet属性**  
- `int64_t pts`:  
  显示时间戳。单位time_base，帧率的倒数。  
- `int64_t dts`:  
  解码时间戳。单位time_base，帧率的倒数。  
- `int stream_index`:  
  当前包(packet)所有流(stream)的索引(index)。  
- `int flags`:  
  packet标志位。比如是否关键帧等。  
- `int64_t duration`:  
  当前包解码后的帧播放持续的时长。单位timebase。值等于下一帧pts减当前帧pts。  
- `int64_t pos`:  
  当前包在流中的位置，单位字节。  

## 2. 关键函数实现  
这里列出的几个关键函数，主要是为了帮助理解`struct AVPacket`数据结构  
### 2.1 av_packet_ref()  
```c
int av_packet_ref(AVPacket *dst, const AVPacket *src)
{
    int ret;

    ret = av_packet_copy_props(dst, src);
    if (ret < 0)
        return ret;

    if (!src->buf) {
        ret = packet_alloc(&dst->buf, src->size);
        if (ret < 0)
            goto fail;
        if (src->size)
            memcpy(dst->buf->data, src->data, src->size);

        dst->data = dst->buf->data;
    } else {
        dst->buf = av_buffer_ref(src->buf);
        if (!dst->buf) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        dst->data = src->data;
    }

    dst->size = src->size;

    return 0;
fail:
    av_packet_free_side_data(dst);
    return ret;
}
```
av_packet_ref()作了处理如下:  
a) 如果src->buf为NULL，则将src缓冲区拷贝到新创建的dst缓冲区，注意src缓冲区不支持引用计数，但新建的dst缓冲区是支持引用计数的，因为dst->buf不为NULL。  
b) 如果src->buf不为空，则dst与src共用缓冲区，调用`av_buffer_ref()`增加缓冲区引用计数即可。`av_buffer_ref()`分析参考“[ffmpeg数据结构AVBuffer]()”  

### 2.2 av_packet_unref()  
```c
void av_packet_unref(AVPacket *pkt)
{
    av_packet_free_side_data(pkt);
    av_buffer_unref(&pkt->buf);
    av_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
}
```
`av_packet_unref()`注销`AVPacket *pkt`对象，并调用`av_buffer_unref(&pkt->buf);`将缓冲区引用计数减1。  
`av_buffer_unref()`中将缓冲区引用计数减1后，若缓冲区引用计数变成0，则回收缓冲区内存。`av_buffer_unref()`分析参考“[ffmpeg数据结构AVBuffer]()”  

## 3. 参考资料  
[1] [FFmpeg数据结构：AVPacket解析](https://www.cnblogs.com/wangguchangqing/p/5790705.html), <https://www.cnblogs.com/wangguchangqing/p/5790705.html>  

## 4. 修改记录  
2018-12-14  V1.0  初稿  