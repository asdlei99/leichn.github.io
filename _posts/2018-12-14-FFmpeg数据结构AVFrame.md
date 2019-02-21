本文为作者原创，转载请注明出处：<https://www.cnblogs.com/leisure_chn/p/10404502.html>  

本文基于FFmpeg 4.1版本。  

## 1. 数据结构定义  
struct AVFrame定义于<libavutil/frame.h>  
```c  
struct AVFrame frame;
```
AVFrame中存储的是经过解码后的原始数据。在解码中，AVFrame是解码器的输出；在编码中，AVFrame是编码器的输入。下图中，“decoded frames”的数据类型就是AVFrame：  
```
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

```

AVFrame数据结构非常重要，它的成员非常多，导致数据结构定义篇幅很长。下面引用的数据结构定义中省略冗长的注释以及大部分成员，先总体说明AVFrame的用法，然后再将一些重要成员摘录出来单独进行说明：  
```c
/**
 * This structure describes decoded (raw) audio or video data.
 *
 * AVFrame must be allocated using av_frame_alloc(). Note that this only
 * allocates the AVFrame itself, the buffers for the data must be managed
 * through other means (see below).
 * AVFrame must be freed with av_frame_free().
 *
 * AVFrame is typically allocated once and then reused multiple times to hold
 * different data (e.g. a single AVFrame to hold frames received from a
 * decoder). In such a case, av_frame_unref() will free any references held by
 * the frame and reset it to its original clean state before it
 * is reused again.
 *
 * The data described by an AVFrame is usually reference counted through the
 * AVBuffer API. The underlying buffer references are stored in AVFrame.buf /
 * AVFrame.extended_buf. An AVFrame is considered to be reference counted if at
 * least one reference is set, i.e. if AVFrame.buf[0] != NULL. In such a case,
 * every single data plane must be contained in one of the buffers in
 * AVFrame.buf or AVFrame.extended_buf.
 * There may be a single buffer for all the data, or one separate buffer for
 * each plane, or anything in between.
 *
 * sizeof(AVFrame) is not a part of the public ABI, so new fields may be added
 * to the end with a minor bump.
 *
 * Fields can be accessed through AVOptions, the name string used, matches the
 * C structure field name for fields accessible through AVOptions. The AVClass
 * for AVFrame can be obtained from avcodec_get_frame_class()
 */
typedef struct AVFrame {
    uint8_t *data[AV_NUM_DATA_POINTERS];
    int linesize[AV_NUM_DATA_POINTERS];
    uint8_t **extended_data;
    int width, height;
    int nb_samples;
    int format;
    int key_frame;
    enum AVPictureType pict_type;
    AVRational sample_aspect_ratio;
    int64_t pts;
    ......
} AVFrame;
```
AVFrame的用法：  
1. AVFrame对象必须调用av_frame_alloc()在堆上分配，注意此处指的是AVFrame对象本身，AVFrame对象必须调用av_frame_free()进行销毁。  
2. AVFrame中包含的数据缓冲区是  
3. AVFrame通常只需分配一次，然后可以多次重用，每次重用前应调用av_frame_unref()将frame复位到原始的干净可用的状态。

下面将一些重要的成员摘录出来进行说明：  
**data**  
```c  
    /**
     * pointer to the picture/channel planes.
     * This might be different from the first allocated byte
     *
     * Some decoders access areas outside 0,0 - width,height, please
     * see avcodec_align_dimensions2(). Some filters and swscale can read
     * up to 16 bytes beyond the planes, if these filters are to be used,
     * then 16 extra bytes must be allocated.
     *
     * NOTE: Except for hwaccel formats, pointers not needed by the format
     * MUST be set to NULL.
     */
    uint8_t *data[AV_NUM_DATA_POINTERS];
```
存储原始帧数据(未编码的原始图像或音频格式，作为解码器的输出或编码器的输入)。  
data是一个指针数组，数组的每一个元素是一个指针，指向视频中图像的某一plane或音频中某一声道的plane。  
关于图像plane的详细说明参考“[色彩空间与像素格式](https://www.cnblogs.com/leisure_chn/p/10290575.html)”，音频plane的详细说明参数“[ffplay源码解析6-音频重采样 6.1.1节](https://www.cnblogs.com/leisure_chn/p/10312713.html)”。下面简单说明：  
对于packet格式，一幅YUV图像的Y、U、V交织存储在一个plane中，形如YUVYUV...，data[0]指向这个plane；  
                一个双声道的音频帧其左声道L、右声道R交织存储在一个plane中，形如LRLRLR...，data[0]指向这个plane。  
对于planar格式，一幅YUV图像有Y、U、V三个plane，data[0]指向Y plane，data[1]指向U plane，data[2]指向V plane；  
                一个双声道的音频帧有左声道L和右声道R两个plane，data[0]指向L plane，data[1]指向R plane。  

**linesize**  
```c  
    /**
     * For video, size in bytes of each picture line.
     * For audio, size in bytes of each plane.
     *
     * For audio, only linesize[0] may be set. For planar audio, each channel
     * plane must be the same size.
     *
     * For video the linesizes should be multiples of the CPUs alignment
     * preference, this is 16 or 32 for modern desktop CPUs.
     * Some code requires such alignment other code can be slower without
     * correct alignment, for yet other it makes no difference.
     *
     * @note The linesize may be larger than the size of usable data -- there
     * may be extra padding present for performance reasons.
     */
    int linesize[AV_NUM_DATA_POINTERS];
```
对于视频来说，linesize是每行图像的大小(字节数)。注意有对齐要求。  
对于音频来说，linesize是每个plane的大小(字节数)。音频只使用linesize[0]。对于planar音频来说，每个plane的大小必须一样。  
linesize可能会因性能上的考虑而填充一些额外的数据，因此linesize可能比实际对应的音视频数据尺寸要大。  

**extended_data**  
```c  
    /**
     * pointers to the data planes/channels.
     *
     * For video, this should simply point to data[].
     *
     * For planar audio, each channel has a separate data pointer, and
     * linesize[0] contains the size of each channel buffer.
     * For packed audio, there is just one data pointer, and linesize[0]
     * contains the total size of the buffer for all channels.
     *
     * Note: Both data and extended_data should always be set in a valid frame,
     * but for planar audio with more channels that can fit in data,
     * extended_data must be used in order to access all channels.
     */
    uint8_t **extended_data;
```
<font color=red>????extended_data是干啥的????</font>
对于视频来说，直接指向data[]成员。  
对于音频来说，packet格式音频只有一个plane，一个音频帧中各个声道的采样点交织存储在此plane中；planar格式音频每个声道一个plane。在多声道planar格式音频中，必须使用extended_data才能访问所有声道，什么意思？ 
在有效的视频/音频frame中，data和extended_data两个成员都必须设置有效值。  

**width, height**  
```c  
    /**
     * @name Video dimensions
     * Video frames only. The coded dimensions (in pixels) of the video frame,
     * i.e. the size of the rectangle that contains some well-defined values.
     *
     * @note The part of the frame intended for display/presentation is further
     * restricted by the @ref cropping "Cropping rectangle".
     * @{
     */
    int width, height;
```
视频帧宽和高(像素)。  

**nb_samples**  
```c  
    /**
     * number of audio samples (per channel) described by this frame
     */
    int nb_samples;
```
音频帧中单个声道中包含的采样点数。  

**format**
```c  
    /**
     * format of the frame, -1 if unknown or unset
     * Values correspond to enum AVPixelFormat for video frames,
     * enum AVSampleFormat for audio)
     */
    int format;
```
帧格式。如果是未知格式或未设置，则值为-1。  
对于视频帧，此值对应于“enum AVPixelFormat”结构：  
```c  
enum AVPixelFormat {
    AV_PIX_FMT_NONE = -1,
    AV_PIX_FMT_YUV420P,   ///< planar YUV 4:2:0, 12bpp, (1 Cr & Cb sample per 2x2 Y samples)
    AV_PIX_FMT_YUYV422,   ///< packed YUV 4:2:2, 16bpp, Y0 Cb Y1 Cr
    AV_PIX_FMT_RGB24,     ///< packed RGB 8:8:8, 24bpp, RGBRGB...
    AV_PIX_FMT_BGR24,     ///< packed RGB 8:8:8, 24bpp, BGRBGR...
    ......
}
```
对于音频帧，此值对应于“enum AVSampleFormat”格式：  
```c  
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

**key_frame**
```c  
    /**
     * 1 -> keyframe, 0-> not
     */
    int key_frame;
```
视频帧是否是关键帧的标识，1->关键帧，0->非关键帧。  

**pict_type**
```c  
    /**
     * Picture type of the frame.
     */
    enum AVPictureType pict_type;
```
视频帧类型(I、B、P等)。如下：  
```c  
/**
 * @}
 * @}
 * @defgroup lavu_picture Image related
 *
 * AVPicture types, pixel formats and basic image planes manipulation.
 *
 * @{
 */

enum AVPictureType {
    AV_PICTURE_TYPE_NONE = 0, ///< Undefined
    AV_PICTURE_TYPE_I,     ///< Intra
    AV_PICTURE_TYPE_P,     ///< Predicted
    AV_PICTURE_TYPE_B,     ///< Bi-dir predicted
    AV_PICTURE_TYPE_S,     ///< S(GMC)-VOP MPEG-4
    AV_PICTURE_TYPE_SI,    ///< Switching Intra
    AV_PICTURE_TYPE_SP,    ///< Switching Predicted
    AV_PICTURE_TYPE_BI,    ///< BI type
};
```

**sample_aspect_ratio**  
```c  
    /**
     * Sample aspect ratio for the video frame, 0/1 if unknown/unspecified.
     */
    AVRational sample_aspect_ratio;
```
视频帧的宽高比。  

**pts**  
```c  
    /**
     * Presentation timestamp in time_base units (time when frame should be shown to user).
     */
    int64_t pts;
```
显示时间戳。单位是time_base。  

**pkt_pts**  
```c  
#if FF_API_PKT_PTS
    /**
     * PTS copied from the AVPacket that was decoded to produce this frame.
     * @deprecated use the pts field instead
     */
    attribute_deprecated
    int64_t pkt_pts;
#endif
```
此frame对应的packet中的显示时间戳。是从对应packet(解码生成此frame)中拷贝PTS得到此值。  

**pkt_dts**  
```c  
    /**
     * DTS copied from the AVPacket that triggered returning this frame. (if frame threading isn't used)
     * This is also the Presentation time of this AVFrame calculated from
     * only AVPacket.dts values without pts values.
     */
    int64_t pkt_dts;
```
此frame对应的packet中的解码时间戳。是从对应packet(解码生成此frame)中拷贝DTS得到此值。  
如果对应的packet中只有dts而未设置pts，则此值也是此frame的pts。  

**coded_picture_number**  
```c  
    /**
     * picture number in bitstream order
     */
    int coded_picture_number;
```
在编码流中当前图像的序号。  

**display_picture_number**  
```c  
    /**
     * picture number in display order
     */
    int display_picture_number;
```
在显示序列中当前图像的序号。  

**interlaced_frame**  
```c  
    /**
     * The content of the picture is interlaced.
     */
    int interlaced_frame;
```
图像逐行/隔行模式标识。  

**sample_rate**  
```c  
    /**
     * Sample rate of the audio data.
     */
    int sample_rate;
```
音频采样率。  

**channel_layout**  
```c  
    /**
     * Channel layout of the audio data.
     */
    uint64_t channel_layout;
```
音频声道布局。每bit代表一个特定的声道，参考channel_layout.h中的定义，一目了然：  
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
......

/**
 * @}
 * @defgroup channel_mask_c Audio channel layouts
 * @{
 * */
#define AV_CH_LAYOUT_MONO              (AV_CH_FRONT_CENTER)
#define AV_CH_LAYOUT_STEREO            (AV_CH_FRONT_LEFT|AV_CH_FRONT_RIGHT)
#define AV_CH_LAYOUT_2POINT1           (AV_CH_LAYOUT_STEREO|AV_CH_LOW_FREQUENCY)
```

**buf**  
```c  
    /**
     * AVBuffer references backing the data for this frame. If all elements of
     * this array are NULL, then this frame is not reference counted. This array
     * must be filled contiguously -- if buf[i] is non-NULL then buf[j] must
     * also be non-NULL for all j < i.
     *
     * There may be at most one AVBuffer per data plane, so for video this array
     * always contains all the references. For planar audio with more than
     * AV_NUM_DATA_POINTERS channels, there may be more buffers than can fit in
     * this array. Then the extra AVBufferRef pointers are stored in the
     * extended_buf array.
     */
    AVBufferRef *buf[AV_NUM_DATA_POINTERS];
```
此帧的数据可以由AVBufferRef管理，AVBufferRef提供AVBuffer引用机制。这里涉及到**缓冲区引用计数**概念：  
AVBuffer是FFmpeg中很常用的一种缓冲区，缓冲区使用引用计数(reference-counted)机制。  
AVBufferRef则对AVBuffer缓冲区提供了一层封装，最主要的是作引用计数处理，实现了一种安全机制。用户不应直接访问AVBuffer，应通过AVBufferRef来访问AVBuffer，以保证安全。  
FFmpeg中很多基础的数据结构都包含了AVBufferRef成员，来间接使用AVBuffer缓冲区。  
相关内容参考“[FFmpeg数据结构AVBuffer](https://www.cnblogs.com/leisure_chn/p/10399048.html)”  
<font color=red>????帧的数据缓冲区AVBuffer就是前面的data成员，用户不应直接使用data成员，应通过buf成员间接使用data成员。那extended_data又是做什么的呢????</font>  

如果buf[]的所有元素都为NULL，则此帧不会被引用计数。必须连续填充buf[] - 如果buf[i]为非NULL，则对于所有j<i，buf[j]也必须为非NULL。  
每个plane最多可以有一个AVBuffer，一个AVBufferRef指针指向一个AVBuffer，一个AVBuffer引用指的就是一个AVBufferRef指针。  
对于视频来说，buf[]包含所有AVBufferRef指针。对于具有多于AV_NUM_DATA_POINTERS个声道的planar音频来说，可能buf[]存不下所有的AVBbufferRef指针，多出的AVBufferRef指针存储在extended_buf数组中。  

**extended_buf&nb_extended_buf**  
```c  
    /**
     * For planar audio which requires more than AV_NUM_DATA_POINTERS
     * AVBufferRef pointers, this array will hold all the references which
     * cannot fit into AVFrame.buf.
     *
     * Note that this is different from AVFrame.extended_data, which always
     * contains all the pointers. This array only contains the extra pointers,
     * which cannot fit into AVFrame.buf.
     *
     * This array is always allocated using av_malloc() by whoever constructs
     * the frame. It is freed in av_frame_unref().
     */
    AVBufferRef **extended_buf;
    /**
     * Number of elements in extended_buf.
     */
    int        nb_extended_buf;
```
对于具有多于AV_NUM_DATA_POINTERS个声道的planar音频来说，可能buf[]存不下所有的AVBbufferRef指针，多出的AVBufferRef指针存储在extended_buf数组中。  
注意此处的extended_buf和AVFrame.extended_data的不同，AVFrame.extended_data包含所有指向各plane的指针，而extended_buf只包含AVFrame.buf中装不下的指针。  
extended_buf是构造frame时av_frame_alloc()中自动调用av_malloc()来分配空间的。调用av_frame_unref会释放掉extended_buf。
nb_extended_buf是extended_buf中的元素数目。  

**best_effort_timestamp**  
```c  
    /**
     * frame timestamp estimated using various heuristics, in stream time base
     * - encoding: unused
     * - decoding: set by libavcodec, read by user.
     */
    int64_t best_effort_timestamp;
```
????

**pkt_pos**  
```c  
    /**
     * reordered pos from the last AVPacket that has been input into the decoder
     * - encoding: unused
     * - decoding: Read by user.
     */
    int64_t pkt_pos;
```
记录最后一个扔进解码器的packet在输入文件中的位置偏移量。  

**pkt_duration**  
```c  
    /**
     * duration of the corresponding packet, expressed in
     * AVStream->time_base units, 0 if unknown.
     * - encoding: unused
     * - decoding: Read by user.
     */
    int64_t pkt_duration;
```
对应packet的时长，单位是AVStream->time_base。  

**channels**  
```c  
    /**
     * number of audio channels, only used for audio.
     * - encoding: unused
     * - decoding: Read by user.
     */
    int channels;
```
音频声道数量。  

**pkt_size**  
```c  
    /**
     * size of the corresponding packet containing the compressed
     * frame.
     * It is set to a negative value if unknown.
     * - encoding: unused
     * - decoding: set by libavcodec, read by user.
     */
    int pkt_size;
```
对应packet的大小。  

**crop_**  
```c  
    /**
     * @anchor cropping
     * @name Cropping
     * Video frames only. The number of pixels to discard from the the
     * top/bottom/left/right border of the frame to obtain the sub-rectangle of
     * the frame intended for presentation.
     * @{
     */
    size_t crop_top;
    size_t crop_bottom;
    size_t crop_left;
    size_t crop_right;
    /**
     * @}
     */
```
用于视频帧图像裁切。四个值分别为从frame的上/下/左/右边界裁切的像素数。  

## 2. 相关函数使用说明  
### 2.1 av_frame_alloc()  
```c  
/**
 * Allocate an AVFrame and set its fields to default values.  The resulting
 * struct must be freed using av_frame_free().
 *
 * @return An AVFrame filled with default values or NULL on failure.
 *
 * @note this only allocates the AVFrame itself, not the data buffers. Those
 * must be allocated through other means, e.g. with av_frame_get_buffer() or
 * manually.
 */
AVFrame *av_frame_alloc(void);
```
构造一个frame，对象各成员被设为默认值。  
此函数只分配AVFrame对象本身，而不分配AVFrame中的数据缓冲区。  

### 2.2 av_frame_free()  
```c  
/**
 * Free the frame and any dynamically allocated objects in it,
 * e.g. extended_data. If the frame is reference counted, it will be
 * unreferenced first.
 *
 * @param frame frame to be freed. The pointer will be set to NULL.
 */
void av_frame_free(AVFrame **frame);
```
释放一个frame。  

### 2.3 av_frame_ref()  
```c  
/**
 * Set up a new reference to the data described by the source frame.
 *
 * Copy frame properties from src to dst and create a new reference for each
 * AVBufferRef from src.
 *
 * If src is not reference counted, new buffers are allocated and the data is
 * copied.
 *
 * @warning: dst MUST have been either unreferenced with av_frame_unref(dst),
 *           or newly allocated with av_frame_alloc() before calling this
 *           function, or undefined behavior will occur.
 *
 * @return 0 on success, a negative AVERROR on error
 */
int av_frame_ref(AVFrame *dst, const AVFrame *src);
```
为src中的数据建立一个新的引用。  
将src中帧的各属性拷到dst中，并且为src中每个AVBufferRef创建一个新的引用。  
如果src未使用引用计数，则dst中会分配新的数据缓冲区，将将src中缓冲区的数据拷贝到dst中的缓冲区。

### 2.4 av_frame_clone()  
```c  
/**
 * Create a new frame that references the same data as src.
 *
 * This is a shortcut for av_frame_alloc()+av_frame_ref().
 *
 * @return newly created AVFrame on success, NULL on error.
 */
AVFrame *av_frame_clone(const AVFrame *src);
```
创建一个新的frame，新的frame和src使用同一数据缓冲区，缓冲区管理使用引用计数机制。  
本函数相当于av_frame_alloc()+av_frame_ref()

### 2.5 av_frame_unref()  
```c  
/**
 * Unreference all the buffers referenced by frame and reset the frame fields.
 */
void av_frame_unref(AVFrame *frame);
```
解除本frame对本frame中所有缓冲区的引用，并复位frame中各成员。  

### 2.6 av_frame_move_ref()  
```c  
/**
 * Move everything contained in src to dst and reset src.
 *
 * @warning: dst is not unreferenced, but directly overwritten without reading
 *           or deallocating its contents. Call av_frame_unref(dst) manually
 *           before calling this function to ensure that no memory is leaked.
 */
void av_frame_move_ref(AVFrame *dst, AVFrame *src);
```
将src中所有数据拷贝到dst中，并复位src。  
为避免内存泄漏，在调用`av_frame_move_ref(dst, src)`之前应先调用`av_frame_unref(dst)`  。

### 2.7 av_frame_get_buffer()  
```c  
/**
 * Allocate new buffer(s) for audio or video data.
 *
 * The following fields must be set on frame before calling this function:
 * - format (pixel format for video, sample format for audio)
 * - width and height for video
 * - nb_samples and channel_layout for audio
 *
 * This function will fill AVFrame.data and AVFrame.buf arrays and, if
 * necessary, allocate and fill AVFrame.extended_data and AVFrame.extended_buf.
 * For planar formats, one buffer will be allocated for each plane.
 *
 * @warning: if frame already has been allocated, calling this function will
 *           leak memory. In addition, undefined behavior can occur in certain
 *           cases.
 *
 * @param frame frame in which to store the new buffers.
 * @param align Required buffer size alignment. If equal to 0, alignment will be
 *              chosen automatically for the current CPU. It is highly
 *              recommended to pass 0 here unless you know what you are doing.
 *
 * @return 0 on success, a negative AVERROR on error.
 */
int av_frame_get_buffer(AVFrame *frame, int align);
```
为音频或视频数据分配新的缓冲区。  
调用本函数前，帧中的如下成员必须先设置好：  
- format (视频像素格式或音频采样格式)  
- width、height(视频画面和宽和高)  
- nb_samples、channel_layout(音频单个声道中的采样点数目和声道布局)  

本函数会填充AVFrame.data和AVFrame.buf数组，如果有需要，还会分配和填充AVFrame.extended_data和AVFrame.extended_buf。  
对于planar格式，会为每个plane分配一个缓冲区。  

### 2.8 av_frame_copy()  
```c  
/**
 * Copy the frame data from src to dst.
 *
 * This function does not allocate anything, dst must be already initialized and
 * allocated with the same parameters as src.
 *
 * This function only copies the frame data (i.e. the contents of the data /
 * extended data arrays), not any other properties.
 *
 * @return >= 0 on success, a negative AVERROR on error.
 */
int av_frame_copy(AVFrame *dst, const AVFrame *src);
```
将src中的帧数据拷贝到dst中。  
本函数并不会有任何分配缓冲区的动作，调用此函数前dst必须已经使用了和src同样的参数完成了初始化。  
本函数只拷贝帧中的数据缓冲区的内容(data/extended_data数组中的内容)，而不涉及帧中任何其他的属性。  

## 3. 参考资料  
[1] [FFMPEG结构体分析：AVFrame](https://blog.csdn.net/leixiaohua1020/article/details/14214577), <https://blog.csdn.net/leixiaohua1020/article/details/14214577>  

## 4. 修改记录  
2019-01-13  V1.0  初稿  