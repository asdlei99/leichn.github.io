
在FFmpeg中，滤镜(filter)处理的是未压缩的原始音视频数据(RGB/YUV视频帧，PCM音频帧等)。一个滤镜的输出可以连接到另一个滤镜的输入，多个滤镜可以连接起来，构成滤镜链/滤镜图，各种滤镜的组合为FFmpeg提供了丰富的音视频处理功能。  

比较常用的滤镜有：scale、trim、overlay、rotate、movie、yadif。scale滤镜用于缩放，trim滤镜用于帧级剪切，overlay滤镜用于视频叠加，rotate滤镜实现旋转，movie滤镜可以加载第三方的视频，yadif滤镜可以去隔行。

## 1. 滤镜的构成及命令行用法  
本节内容节选自“[FFmpeg使用基础](https://www.cnblogs.com/leisure_chn/p/10297002.html)”，翻译整理自《FFmpeg Basics》及官网文档“[Documentation-ffmpeg](http://ffmpeg.org/ffmpeg.html)”。

在多媒体处理中，术语**滤镜(filter)**指的是修改未编码的原始音视频数据帧的一种软件工具。滤镜分为音频滤镜和视频滤镜。FFmpeg提供了很多内置滤镜，可以用很多方式将这些滤镜组合使用。通过一些复杂指令，可以将解码后的帧从一个滤镜引向另一个滤镜。这简化了媒体处理，因为有损编解码器对媒体流进行多次解码和编码操作会降低总体质量，而引入滤镜后，不需要多次解码编码操作，相关处理可以使用多个滤镜完成，而滤镜处理的是原始数据，不会造成数据损伤。  

### 1.1 滤镜的使用  
FFmpeg的libavfilter库提供了滤镜API，支持多路输入和多路输出。  

滤镜(filter)的语法为：  
`[input_link_lable1][input_link_lable2]... filter_name=parameters [output_link_lable1][output_link_lable12]...`  
上述语法中，输入输出都有**连接标号(link lable)**，连接符号是可选项，输入连接标号表示滤镜的输入，输出连接标号表示滤镜的输出。连接标号通常用在滤镜图中，通常前一个滤镜的输出标号会作为后一个滤镜的输入标号，通过同名的标号将滤镜及滤镜链连接起来。连接标号的用法参考4.3.2节示例。  

示例1：  
`ffplay -f lavfi -i testsrc -vf transpose=1`  
“-vf”(同“-filter:v”)选项表示使用视频滤镜，“transpose=1”是滤镜，此行命令表示使用transpose视频滤镜产生一个顺时针旋转90度的测试图案  

示例2：  
`ffmpeg -i input.mp3 -af atempo=0.8 output.mp3`  
“-af”(同“-filter:a”)选项表示使用音频滤镜，“atempo=0.8”是滤镜，此行命令表示使用atempo音频滤镜将输入音频速率降低到80%后写入输出文件  

注意：有些滤镜只会修改帧属性而不会修改帧内容。例如，fps滤镜，setpts滤镜等。  

### 1.2 滤镜链的使用  
**滤镜链(filterchain)**是以逗号分隔的滤镜(filter)序列，语法如下：  
`filter1,fiter2,filter3,...,filterN-2,filterN-1,filterN`  
滤镜链中如果有空格，需要将滤镜链用双引号括起来，因为命令行中空格是分隔参数用的。  

示例1：  
`ffmpeg -i input.mpg -vf hqdn3d,pad=2*iw output.mp4`  
“hqdn3d,pad=2*iw”是filterchain，第一个filter是“hqdn3d”(降噪)；第二个filter是“pad=2*iw”(将图像宽度填充到输入宽度的2倍)。此行命令表示，将输入视频经降噪处理后，再填充视频宽度为输入宽度的2倍。  

### 1.3 滤镜图的使用  
**滤镜图(filtergraph)**通常是以分号分隔的滤镜链(filterchain)序列。滤镜图分为简单滤镜图和复杂滤镜图。  
滤镜图(filtergraph)的语法如下：  
`filter1;fiter2;filter3;...;filterN-2;filterN-1;filterN`  

#### 1.3.1 简单滤镜图  
简单滤镜图(filtergraph)只能处理单路输入流和单路输出流，而且要求输入和输出具有相同的流类型。  
简单滤镜图由-filter选项指定。简单滤镜图示意图如下：  
```
 _______        _____________________        ________
|       |      |                     |      |        |
| input | ---> | simple filter graph | ---> | output |
|_______|      |_____________________|      |________|

```

#### 1.3.2 复杂滤镜图  
复杂滤镜图(filtergraph)用于简单滤镜图处理不了的场合。比如，多路输入流和(或)多路输出流，或者输出流与输入流类型不同。  
有些特殊的滤镜(filter)本身就属于复杂滤镜图，用-filter_complex选项或-lavfi选项指定，如overlay滤镜和amix滤镜就是复杂滤镜图。overlay滤镜有两个视频输入和一个视频输出，将两个输入视频混合在一起。而amix滤镜则是将两个输入音频混合在一起。  
复杂滤镜图(filtergraph)示意图如下：  
```
 _________
|         |
| input 0 |\                    __________
|_________| \                  |          |
             \   _________    /| output 0 |
              \ |         |  / |__________|
 _________     \| complex | /
|         |     |         |/
| input 1 |---->| filter  |\
|_________|     |         | \   __________
               /| graph   |  \ |          |
              / |         |   \| output 1 |
 _________   /  |_________|    |__________|
|         | /
| input 2 |/
|_________|

```

示例1：  
`ffmpeg -i INPUT -vf "split [main][tmp]; [tmp] crop=iw:ih/2:0:0, vflip [flip]; [main][flip] overlay=0:H/2" OUTPUT`  
上例中"split [main][tmp]; [tmp] crop=iw:ih/2:0:0, vflip [flip]; [main][flip] overlay=0:H/2"是复杂滤镜图，由三个滤镜链构成(分号分隔)，第二个滤镜链“[tmp] crop=iw:ih/2:0:0, vflip [flip]”由两个滤镜构成(逗号分隔)。第一个滤镜链中：滤镜split产生两个输出[main]和[tmp]；第二个滤镜链中：[tmp]作为crop滤镜的输入，[flip]作为vflip滤镜的输出，crop滤镜输出连接到vflip滤镜的输入；第三个滤镜链中：[main]和[flip]作为overlay滤镜的输入。整行命令实现的功能是：将输入分隔为两路，其中一路经过裁剪和垂直翻转后，再与另一路混合，生成输出文件。示意图如下所示：  
```
                [main]
input --> split ---------------------> overlay --> output
            |                             ^
            |[tmp]                  [flip]|
            +-----> crop --> vflip -------+

```

#### 1.3.3 滤镜图中的连接标号  
在滤镜图中可以使用连接标号(link lable)，连接标号表示特定滤镜/滤镜链的输入或输出，参4.1节。  

例如，我们想要把一个经过降噪处理后的输出文件与输入原文件进行比较，如果不使用带连接标号的滤镜图，我们需要至少两条命令：  
`ffmpeg -i input.mpg -vf hqdn3d,pad=2*iw output.mp4`  
`ffmpeg -i output.mp4 -i input.mpg -filter_complex overlay=w compare.mp4`  

如果使用带有连接标号的滤镜图，则一条命令就可以了：  
`ffplay -i i.mpg -vf split[a][b];[a]pad=2*iw[A];[b]hqdn3d[B];[A][B]overlay=w`  

### 1.4 滤镜使用总结  
滤镜(广义)通常以滤镜链(filterchain, 以逗号分隔的滤镜序列)和滤镜图(filtergraph, 以分号分隔的滤镜序列)的形式使用。滤镜链由滤镜构成，滤镜图由滤镜链构成，这样可以提供复杂多样的组合方式以应对不同的应用场景。  
滤镜(狭义)是滤镜链的简单特例，滤镜链是滤镜图的简单特例。注意这里滤镜(狭义)、滤镜链、滤镜图之间不是继承的关系，而是组合的关系，比如，一个滤镜图可以只包含一个滤镜链，而一个滤镜链也可以只包含一个滤镜，这种特例情况下，一个滤镜图仅由单个滤镜构成。FFmpeg的命令行中，滤镜(广义)的出现形式有滤镜(狭义)、滤镜链、滤镜图三种形式，但滤镜(狭义)和滤镜链可以看作是特殊的滤镜图，因此，为了简便，FFmpeg的命令行中滤镜相关选项，只针对滤镜图(filtergraph)概念，分为如下两类：  
针对简单滤镜图的选项：“-vf”等同“-filter:v”，“-af”等同“-filter:a”  
针对复杂滤镜图的选项：“-lavfi”等价“-filter_complex”  

## 2. 滤镜数据结构与API简介  
待补充  

### 2.1 struct AVFilter
```c  
/**
 * Filter definition. This defines the pads a filter contains, and all the
 * callback functions used to interact with the filter.
 */
typedef struct AVFilter {
    const char *name;
    const char *description;
    const AVFilterPad *inputs;
    const AVFilterPad *outputs;
    const AVClass *priv_class;
    int flags;
    
    // private API
    ......
} AVFilter;
```

### 2.2 struct AVFilterContext  
```c  
/** An instance of a filter */
struct AVFilterContext {
    const AVClass *av_class;        ///< needed for av_log() and filters common options

    const AVFilter *filter;         ///< the AVFilter of which this is an instance

    char *name;                     ///< name of this filter instance

    AVFilterPad   *input_pads;      ///< array of input pads
    AVFilterLink **inputs;          ///< array of pointers to input links
    unsigned    nb_inputs;          ///< number of input pads

    AVFilterPad   *output_pads;     ///< array of output pads
    AVFilterLink **outputs;         ///< array of pointers to output links
    unsigned    nb_outputs;         ///< number of output pads

    void *priv;                     ///< private data for use by the filter

    struct AVFilterGraph *graph;    ///< filtergraph this filter belongs to

    ......
};
```

### 2.3 struct AVFilterGraph  
```c  
typedef struct AVFilterGraph {
    const AVClass *av_class;
    AVFilterContext **filters;
    unsigned nb_filters;

    ......
} AVFilterGraph;
```

### 2.4 struct AVFilterLink  
```c  
/**
 * A link between two filters. This contains pointers to the source and
 * destination filters between which this link exists, and the indexes of
 * the pads involved. In addition, this link also contains the parameters
 * which have been negotiated and agreed upon between the filter, such as
 * image dimensions, format, etc.
 *
 * Applications must not normally access the link structure directly.
 * Use the buffersrc and buffersink API instead.
 * In the future, access to the header may be reserved for filters
 * implementation.
 */
struct AVFilterLink {
    AVFilterContext *src;       ///< source filter
    AVFilterPad *srcpad;        ///< output pad on the source filter

    AVFilterContext *dst;       ///< dest filter
    AVFilterPad *dstpad;        ///< input pad on the dest filter
    
    ......
}
```

### 2.5 struct AVFilterInOut  
```c  
/**
 * A linked-list of the inputs/outputs of the filter chain.
 *
 * This is mainly useful for avfilter_graph_parse() / avfilter_graph_parse2(),
 * where it is used to communicate open (unlinked) inputs and outputs from and
 * to the caller.
 * This struct specifies, per each not connected pad contained in the graph, the
 * filter context and the pad index required for establishing a link.
 */
typedef struct AVFilterInOut {
    /** unique name for this input/output in the list */
    char *name;

    /** filter context associated to this input/output */
    AVFilterContext *filter_ctx;

    /** index of the filt_ctx pad to use for linking */
    int pad_idx;

    /** next input/input in the list, NULL if this is the last */
    struct AVFilterInOut *next;
} AVFilterInOut;
```

### 2.6 avfilter_graph_create_filter()  
```c  
/**
 * Create and add a filter instance into an existing graph.
 * The filter instance is created from the filter filt and inited
 * with the parameters args and opaque.
 *
 * In case of success put in *filt_ctx the pointer to the created
 * filter instance, otherwise set *filt_ctx to NULL.
 *
 * @param name the instance name to give to the created filter instance
 * @param graph_ctx the filter graph
 * @return a negative AVERROR error code in case of failure, a non
 * negative value otherwise
 */
int avfilter_graph_create_filter(AVFilterContext **filt_ctx, const AVFilter *filt,
                                 const char *name, const char *args, void *opaque,
                                 AVFilterGraph *graph_ctx);

```

### 2.7 avfilter_graph_parse_ptr()  
```c  
/**
 * Add a graph described by a string to a graph.
 *
 * In the graph filters description, if the input label of the first
 * filter is not specified, "in" is assumed; if the output label of
 * the last filter is not specified, "out" is assumed.
 *
 * @param graph   the filter graph where to link the parsed graph context
 * @param filters string to be parsed
 * @param inputs  pointer to a linked list to the inputs of the graph, may be NULL.
 *                If non-NULL, *inputs is updated to contain the list of open inputs
 *                after the parsing, should be freed with avfilter_inout_free().
 * @param outputs pointer to a linked list to the outputs of the graph, may be NULL.
 *                If non-NULL, *outputs is updated to contain the list of open outputs
 *                after the parsing, should be freed with avfilter_inout_free().
 * @return non negative on success, a negative AVERROR code on error
 */
int avfilter_graph_parse_ptr(AVFilterGraph *graph, const char *filters,
                             AVFilterInOut **inputs, AVFilterInOut **outputs,
                             void *log_ctx);
```

### 2.8 avfilter_graph_config()  
```c  
/**
 * Check validity and configure all the links and formats in the graph.
 *
 * @param graphctx the filter graph
 * @param log_ctx context used for logging
 * @return >= 0 in case of success, a negative AVERROR code otherwise
 */
int avfilter_graph_config(AVFilterGraph *graphctx, void *log_ctx);
```

### 2.9 av_buffersrc_add_frame_flags()  
```c  
/**
 * Add a frame to the buffer source.
 *
 * By default, if the frame is reference-counted, this function will take
 * ownership of the reference(s) and reset the frame. This can be controlled
 * using the flags.
 *
 * If this function returns an error, the input frame is not touched.
 *
 * @param buffer_src  pointer to a buffer source context
 * @param frame       a frame, or NULL to mark EOF
 * @param flags       a combination of AV_BUFFERSRC_FLAG_*
 * @return            >= 0 in case of success, a negative AVERROR code
 *                    in case of failure
 */
av_warn_unused_result
int av_buffersrc_add_frame_flags(AVFilterContext *buffer_src,
                                 AVFrame *frame, int flags);
```

### 2.10 av_buffersink_get_frame()  
```
/**
 * Get a frame with filtered data from sink and put it in frame.
 *
 * @param ctx pointer to a context of a buffersink or abuffersink AVFilter.
 * @param frame pointer to an allocated frame that will be filled with data.
 *              The data must be freed using av_frame_unref() / av_frame_free()
 *
 * @return
 *         - >= 0 if a frame was successfully returned.
 *         - AVERROR(EAGAIN) if no frames are available at this point; more
 *           input frames must be added to the filtergraph to get more output.
 *         - AVERROR_EOF if there will be no more output frames on this sink.
 *         - A different negative AVERROR code in other failure cases.
 */
int av_buffersink_get_frame(AVFilterContext *ctx, AVFrame *frame);
```

## 3. 滤镜API使用方法    

在代码中使用滤镜，主要分为两个步骤：  
[1]. 滤镜的初始化配置：根据滤镜参数，配置生成滤镜图，此滤镜图供下一步骤使用  
[2]. 使用滤镜处理原始音视频帧：向滤镜图提供输入帧(AVFrame)，从滤镜图取出经处理后的输出帧(AVFrame)  

```c  
1. init_filters()                   // 配置生成可用的滤镜图，由用户编写  
2. av_buffersrc_add_frame_flags()   // 向滤镜图提供输入帧，API函数  
3. av_buffersink_get_frame()        // 从滤镜图取出处理后的输出帧，API函数  
```

本节节选的代码示例选自：  
[https://github.com/leichn/exercises/blob/master/source/ffmpeg/ffmpeg_vfilter/video_filter.c](https://github.com/leichn/exercises/blob/master/source/ffmpeg/ffmpeg_vfilter/video_filter.c)  

### 3.1 滤镜配置  
在代码中，滤镜配置比滤镜使用复杂，滤镜配置代码如下：  
```c  
// 功能：创建配置一个滤镜图，在后续滤镜处理中，可以往此滤镜图输入数据并从滤镜图获得输出数据
// filters_descr：输入参数，形如“transpose=cclock,pad=iw+80:ih:40”
// @vfmt：输入参数，描述提供给待生成滤镜图的视频帧和格式
// @fctx：输出参数，返回生成滤镜图的信息，供调用者使用
int init_filters(const char *filters_descr, const input_vfmt_t *vfmt, filter_ctx_t *fctx)
{
    int ret = 0;

    // 1. 配置滤镜图输入端和输出端 
    fctx->filter_graph = avfilter_graph_alloc();
    if (!fctx->filter_graph)
    {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    char args[512];
    char *p_args = NULL;
    if (vfmt != NULL)
    {
        /* buffer video source: the decoded frames from the decoder will be inserted here. */
        // args是buffersrc滤镜的参数
        snprintf(args, sizeof(args),
                 "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                 vfmt->width, vfmt->height, vfmt->pix_fmt, 
                 vfmt->time_base.num, vfmt->time_base.den, 
                 vfmt->sar.num, vfmt->sar.den);
        p_args = args;
    }
    ret = avfilter_graph_create_filter(&fctx->bufsrc_ctx, bufsrc, "in",
                                       p_args, NULL, fctx->filter_graph);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }

    const AVFilter *bufsink = avfilter_get_by_name("buffersink");
    ret = avfilter_graph_create_filter(&fctx->bufsink_ctx, bufsink, "out",
                                       NULL, NULL, fctx->filter_graph);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        goto end;
    }

#if 0   // 因为后面显示视频帧时有sws_scale()进行图像格式转换，故此处不设置滤镜输出格式也可
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUYV422, AV_PIX_FMT_NONE };
    // 设置输出像素格式为pix_fmts[]中指定的格式(如果要用SDL显示，则这些格式应是SDL支持格式)
    ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts,
                              AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
        goto end;
    }
#endif
    // 1. end


    // 2. 将filters_descr描述的滤镜图添加到filter_graph滤镜图中
    AVFilterInOut *outputs = avfilter_inout_alloc();
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = fctx->bufsrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    AVFilterInOut *inputs  = avfilter_inout_alloc();
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = fctx->bufsink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    ret = avfilter_graph_parse_ptr(fctx->filter_graph, filters_descr,
                                   &inputs, &outputs, NULL);
    if (ret < 0)
    {
        goto end;
    }
    // 2. end

    // 3. 配置filtergraph滤镜图，建立滤镜间的连接
    ret = avfilter_graph_config(fctx->filter_graph, NULL);
    if (ret < 0)
    {
        goto end;
    }
    // 3. end

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}
```

**函数参数说明：**  
输入参数`const char *filters_descr`以字符串形式提供滤镜选项，例如参数为`transpose=cclock,pad=iw+80:ih:40`时，表示将视频帧逆时针旋转90度，然后在视频左右各填充40像素的黑边。

输入参数`input_vfmt_t *vfmt`用于描述提供给滤镜图的视频帧和格式，在配置滤镜图中的第一个滤镜buffer时需要为滤镜提供参数，就是从vfmt参数转换得到。  
input_vfmt_t为自定义数据结构，定义如下：  
```c  
typedef struct {
    int width;
    int height;
    enum AVPixelFormat pix_fmt;
    AVRational time_base;
    AVRational sar;
    AVRational frame_rate;
}   input_vfmt_t;
```

输出参数`filter_ctx_t *fctx`用于返回生成滤镜图的信息，供调用者使用。  
filter_ctx_t为自定义数据结构，定义如下：  
```c  
typedef struct {
    AVFilterContext *bufsink_ctx;
    AVFilterContext *bufsrc_ctx;
    AVFilterGraph   *filter_graph;
}   filter_ctx_t;
```
此结构中三个成员：bufsrc_ctx用于滤镜图的输入，bufsink_ctx用于滤镜图的输出，filter_graph用于销毁滤镜图。  
<font color-red>TODO: 一个滤镜图可能含多个滤镜链，即可能有多个输入节点(bufsrc_ctx)或多个输出节点(bufsink_ctx)，此数据结构应改进为支持多输入和多输出</font>  

函数实现的几个步骤如下：  
### 3.1.1 配置滤镜图输入端和输出端  
buffer滤镜和buffersink滤镜是两个特殊的滤镜，分别用于滤镜图的输入端和输出端，为应用程序提供滤镜图数据输入和输出的功能。

一个滤镜图可能由多个滤镜链构成，每个滤镜链的输入节点就是buffer滤镜，输出节点是buffersink滤镜。因此一个滤镜图可能有多个输入节点(buffer滤镜)，也可能有多个输出节点(buffersink滤镜)。  

**buffer滤镜**  
在命令行中输入`ffmpeg -h filter=buffer`查看buffer滤镜的帮助信息，如下：  
```
$ ffmpeg -h filter=buffer
ffmpeg version 4.1 Copyright (c) 2000-2018 the FFmpeg developers
Filter buffer
  Buffer video frames, and make them accessible to the filterchain.
    Inputs:
        none (source filter)
    Outputs:
       #0: default (video)
buffer AVOptions:
  width             <int>        ..FV..... (from 0 to INT_MAX) (default 0)
  video_size        <image_size> ..FV.....
  height            <int>        ..FV..... (from 0 to INT_MAX) (default 0)
  pix_fmt           <pix_fmt>    ..FV..... (default none)
  sar               <rational>   ..FV..... sample aspect ratio (from 0 to DBL_MAX) (default 0/1)
  pixel_aspect      <rational>   ..FV..... sample aspect ratio (from 0 to DBL_MAX) (default 0/1)
  time_base         <rational>   ..FV..... (from 0 to DBL_MAX) (default 0/1)
  frame_rate        <rational>   ..FV..... (from 0 to DBL_MAX) (default 0/1)
  sws_param         <string>     ..FV.....
```
buffer滤镜用作滤镜链的输入节点。buffer滤镜缓冲视频帧，滤镜链可以从buffer滤镜中取得视频帧数据。  
在上述帮助信息中，Inputs和Outputs指滤镜的输入引脚和输出引脚。buffer滤镜是滤镜链中的第一个滤镜，因此只有输出引脚而无输入引脚。  

滤镜(AVFilter)需要通过滤镜实例(AVFilterContext)引用，为buffer滤镜创建的滤镜实例是fctx->bufsrc_ctx，用户通过往fctx->bufsrc_ctx填入视频帧来为滤镜链提供输入。  
为buffer滤镜创建滤镜实例时需要提供参数，buffer滤镜需要的参数在帮助信息中的“buffer AVOptions”部分列出，由vfmt输入参数提供，如下：  
```
    char args[512];
    char *p_args = NULL;
    if (vfmt != NULL)
    {
        // args是buffersrc滤镜的参数
        snprintf(args, sizeof(args),
                 "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                 vfmt->width, vfmt->height, vfmt->pix_fmt, 
                 vfmt->time_base.num, vfmt->time_base.den, 
                 vfmt->sar.num, vfmt->sar.den);
        p_args = args;
    }
    // buffer滤镜：缓冲视频帧，作为滤镜图的输入
    const AVFilter *bufsrc  = avfilter_get_by_name("buffer");
    // 为buffersrc滤镜创建滤镜实例buffersrc_ctx，命名为"in"
    // 将新创建的滤镜实例buffersrc_ctx添加到滤镜图filter_graph中
    ret = avfilter_graph_create_filter(&fctx->bufsrc_ctx, bufsrc, "in",
                                       p_args, NULL, fctx->filter_graph);
```

**buffersink滤镜**  
在命令行中输入`ffmpeg -h filter=buffersink`查看buffersink滤镜的帮助信息，如下：  
```
$  ffmpeg -h filter=buffersink
ffmpeg version 4.1 Copyright (c) 2000-2018 the FFmpeg developers
Filter buffersink
  Buffer video frames, and make them available to the end of the filter graph.
    Inputs:
       #0: default (video)
    Outputs:
        none (sink filter)
buffersink AVOptions:
  pix_fmts          <binary>     ..FV..... set the supported pixel formats
```
buffersink滤镜用作滤镜链的输出节点。滤镜链处理后的视频帧可以缓存到buffersink滤镜中。  
buffersink滤镜是滤镜链中的最后一个滤镜，因此只有输入引脚而无输出引脚。  

为buffersink滤镜创建的滤镜实例是fctx->bufsink_ctx，用户可以从fctx->bufsink_ctx中读视频帧来获得滤镜链的输出。  
通过帮助信息可以看到，buffersink滤镜参数只有一个“pix_fmt”，用于设置滤镜链输出帧的像素格式列表，这个像素格式有多种，以限制输出帧格式不超过指定的范围。  
```c  
    // buffersink滤镜：缓冲视频帧，作为滤镜图的输出
    const AVFilter *bufsink = avfilter_get_by_name("buffersink");
    // 为buffersink滤镜创建滤镜实例buffersink_ctx，命名为"out"
    // 将新创建的滤镜实例buffersink_ctx添加到滤镜图filter_graph中
    ret = avfilter_graph_create_filter(&fctx->bufsink_ctx, bufsink, "out",
                                       NULL, NULL, fctx->filter_graph);

#if 0   // 因为后面显示视频帧时有sws_scale()进行图像格式转换，故此处不设置滤镜输出格式也可
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUYV422, AV_PIX_FMT_NONE };
    // 设置输出像素格式为pix_fmts[]中指定的格式(如果要用SDL显示，则这些格式应是SDL支持格式)
    ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts,
                              AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
#endif
```

将buffer滤镜和buffsink滤镜添加进滤镜图中后，如下图所示：  
![pic0](https://leichn.github.io/img/ffmpeg_filter/pic0.jpg "pic0")  

### 3.1.2 将filters_descr描述的滤镜插入滤镜图中
解析滤镜选项(filters_descr)，将解析得到的滤镜插入第1步构造的滤镜图中，并与滤镜图输入端和输出端连接起来  
```
    // 设置滤镜图的端点，将filters_descr描述的滤镜图连接到此滤镜图
    // 两个滤镜图的连接是通过端点(AVFilterInOut)连接完成的
    // 端点数据结构AVFilterInOut主要用于avfilter_graph_parse()系列函数

    // outputs变量意指buffersrc_ctx滤镜的输出引脚(output pad)
    // src缓冲区(buffersrc_ctx滤镜)的输出必须连到filters_descr中第一个
    // 滤镜的输入；filters_descr中第一个滤镜的输入标号未指定，故默认为
    // "in"，此处将buffersrc_ctx的输出标号也设为"in"，就实现了同标号相连
    AVFilterInOut *outputs = avfilter_inout_alloc();
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = fctx->bufsrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    // inputs变量意指buffersink_ctx滤镜的输入引脚(input pad)
    // sink缓冲区(buffersink_ctx滤镜)的输入必须连到filters_descr中最后
    // 一个滤镜的输出；filters_descr中最后一个滤镜的输出标号未指定，故
    // 默认为"out"，此处将buffersink_ctx的输出标号也设为"out"，就实现了
    // 同标号相连
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = fctx->bufsink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    // 将filters_descr描述的滤镜图添加到filter_graph滤镜图中
    // 调用前：filter_graph包含两个滤镜buffersrc_ctx和buffersink_ctx
    // 调用后：filters_descr描述的滤镜图插入到filter_graph中，buffersrc_ctx连接到filters_descr
    //         的输入，filters_descr的输出连接到buffersink_ctx，filters_descr只进行了解析而不
    //         建立内部滤镜间的连接。filters_desc与filter_graph间的连接是利用AVFilterInOut inputs
    //         和AVFilterInOut outputs连接起来的，AVFilterInOut是一个链表，最终可用的连在一起的
    //         滤镜链/滤镜图就是通过这个链表串在一起的。
    ret = avfilter_graph_parse_ptr(fctx->filter_graph, filters_descr,
                                   &inputs, &outputs, NULL);
```
filters_descr描述的滤镜如下图所示：  
![pic1](https://leichn.github.io/img/ffmpeg_filter/pic0.jpg "pic1")  

调用avfilter_graph_parse_ptr()后，滤镜图如下所示：  
![pic2](https://leichn.github.io/img/ffmpeg_filter/pic2.jpg "pic2")  

### 3.1.3. 建立滤镜连接
调用`avfilter_graph_config()`将上一步得到的滤镜图进行配置，建立滤镜间的连接，此步完成后即生了一个可用的滤镜图，如下图所示：  
![pic3](https://leichn.github.io/img/ffmpeg_filter/pic0.jpg "pic3")  

## 3.2 使用滤镜处理原始帧  
配置好滤镜后，可在音视频处理过程中使用滤镜。使用滤镜比配置滤镜简单很多，主要调用如下两个API函数：  
1. 调用`av_buffersrc_add_frame_flags()`将音视频帧发送给滤镜  
2. 调用`av_buffersink_get_frame()`取得经滤镜处理后的音视频帧  

## 4. 滤镜API应用实例分析  

滤镜接收原始音视频帧，经过各效果的滤镜处理后输出的仍然是原始音视频帧。在滤镜API应用实例中，核心内容是“滤镜配置”和“滤镜使用”两个部分，滤镜接收什么样的输入源不重要，滤镜的输出做处理也不重要。不同的输入源，及不同的输出处理方式仅仅是为了加深对滤镜API使用的理解，以及方便观察滤镜的处理效果。  

滤镜的输入可以是解码器的输出、原始YUV文件及测试图。本文三个示例只针对视频滤镜：  
示例1：编码器的输出作为滤镜的输入，滤镜的输出简单处理，无法观察滤镜效果。  
示例2：编码器的输出作为滤镜的输入，滤镜的输出可以播放，可直观观察滤镜效果。  
示例3：测试图作为滤镜的输入，滤镜的输出可以播放，可直接观察滤镜效果。  

### 4.1 示例1：官方例程  
[https://github.com/FFmpeg/FFmpeg/blob/n4.1/doc/examples/filtering_video.c](https://github.com/FFmpeg/FFmpeg/blob/n4.1/doc/examples/filtering_video.c)  
官方例程实现的功能是：打开一个视频文件，解码后经过滤镜处理，然后以简单灰度模式在命令窗口中播放视频帧。  

例程中使用的滤镜选项是`const char *filter_descr = "scale=78:24,transpose=cclock";`，表示先用scale滤镜将视频帧缩放到78x24像素，再用transpose滤镜将图像逆时针旋转90度。  

简述一下例程的步骤：  
1. 打开视频文件，调用`open_input_file()`实现  
2. 初始化滤镜，调用`init_filters()`实现  
3. 解码得到视频帧，调用`avcodec_send_packet()`和`avcodec_receive_frame()`获得解码后的原始视频帧  
4. 将视频帧发给滤镜，调用`av_buffersrc_add_frame_flags()`实现  
5. 从滤镜输出端取视频帧，调用`av_buffersink_get_frame()`实现  
6. 播放视频帧，调用`display_frame()`实现

例程核心是滤镜相关的代码，因此视频帧播放部分做了简化处理。  

### 4.2 示例2：可播放版本  
官方示例主要演示滤镜API的使用方法，代码量较少，简化了视频播放部分，这样使得滤镜的处理效果无法直观观察。示例2针对此问题，在官方代码基础上增加了正常的视频播放效果。  

代码说明：在[https://github.com/leichn/exercises/blob/master/source/ffmpeg/ffmpeg_vfilter/](https://github.com/leichn/exercises/blob/master/source/ffmpeg/ffmpeg_vfilter/)目录下有如下几个文件  
```
vfilter_filesrc.c   示例2：输入源为视频文件，经滤镜处理后播放
vfilter_testsrc.c   示例3：输入源为测试图，经滤镜处理后播放
video_filter.c      滤镜处理功能
video_play.c        视频播放功能
Makefile
```
video_filter.c封装了滤镜处理相关代码，详参本文第3节。  
video_play.c实现了视频播放功能，本例无需过多关注，实现原理可参考如下两篇文章：  
[FFmpeg简易播放器的实现-视频播放](https://www.cnblogs.com/leisure_chn/p/10047035.html)  
[ffplay源码分析5-图像显示](https://www.cnblogs.com/leisure_chn/p/10311376.html)  
vfilter_filesrc.c是示例2的主程序，实现了打开视频文件，解码，滤镜处理，播放的主流程  

编译：`make vf_file`，将生成vf_file可执行文件  
测试：`./vf_file ./ring.flv -vf crop=iw/2:ih,pad=iw*2:ih`，滤镜选项`-vf crop=iw/2:ih,pad=iw*2:ih`表示先将视频裁剪为一半宽度，再填充为二倍宽度，预期结果为视频的右半部分为黑边。  
测试文件下载：[ring.flv](https://github.com/leichn/blog_resources/blob/master/video/ring.flv)  
未经滤镜处理和经过滤镜处理的视频效果对比如下两图所示：  
![ring](https://leichn.github.io/img/ffmpeg_filter/ring.jpg "ring")  
![ring_vf](https://leichn.github.io/img/ffmpeg_filter/ring_vf.jpg "ring_vf")  

### 4.3 示例3：测试图作输入源  
示例3使用测试图(test pattern)作为滤镜的输入，测试图(test pattern)是由FFmpeg内部产生的测试图案，用于测试非常方便。  
因测试图直接输出原始视频帧，不需解码器，因此示例3中用到AVFilter库，不需要用到AVFormat库。  

3.2节源码目录中vfilter_testsrc.c就是用于示例3的主程序，实现了构建测试源，滤镜处理，播放的主流程。除滤镜输入源的获取方式与示例2不同之外，其他过程并无不同。  

示例3增加的关键内容是构造测试源，参考vfilter_testsrc.c中如下函数：  
```c  
// @filter [i]  产生测试图案的filter
// @vfmt   [i]  @filter的参数
// @fctx   [o]  用户定义的数据类型，输出供调用者使用
static int open_testsrc(const char *filter, const input_vfmt_t *vfmt, filter_ctx_t *fctx)
{
    int ret = 0;

    // 分配一个滤镜图filter_graph
    fctx->filter_graph = avfilter_graph_alloc();
    if (!fctx->filter_graph)
    {
        return AVERROR(ENOMEM);
    }

    // source滤镜：合法值有"testsrc"/"smptebars"/"color"/...
    const AVFilter *bufsrc  = avfilter_get_by_name(filter);
    // 为buffersrc滤镜创建滤镜实例buffersrc_ctx，命名为"in"
    // 将新创建的滤镜实例buffersrc_ctx添加到滤镜图filter_graph中
    ret = avfilter_graph_create_filter(&fctx->bufsrc_ctx, bufsrc, "in",
                                       NULL, NULL, fctx->filter_graph);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Cannot create filter testsrc\n");
        goto end;
    }

    // "buffersink"滤镜：缓冲视频帧，作为滤镜图的输出
    const AVFilter *bufsink = avfilter_get_by_name("buffersink");
    /* buffer video sink: to terminate the filter chain. */
    // 为buffersink滤镜创建滤镜实例buffersink_ctx，命名为"out"
    // 将新创建的滤镜实例buffersink_ctx添加到滤镜图filter_graph中
    ret = avfilter_graph_create_filter(&fctx->bufsink_ctx, bufsink, "out",
                                       NULL, NULL, fctx->filter_graph);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Cannot create filter buffersink\n");
        goto end;
    }

    if ((ret = avfilter_link(fctx->bufsrc_ctx, 0, fctx->bufsink_ctx, 0)) < 0)
    {
        goto end;
    }


    // 验证有效性并配置filtergraph中所有连接和格式
    ret = avfilter_graph_config(fctx->filter_graph, NULL);
    if (ret < 0)
    {
        goto end;
    }

    vfmt->pix_fmt = av_buffersink_get_format(fctx->bufsink_ctx);
    vfmt->width = av_buffersink_get_w(fctx->bufsink_ctx);
    vfmt->height = av_buffersink_get_h(fctx->bufsink_ctx);
    vfmt->sar = av_buffersink_get_sample_aspect_ratio(fctx->bufsink_ctx);
    vfmt->time_base = av_buffersink_get_time_base(fctx->bufsink_ctx);
    vfmt->frame_rate = av_buffersink_get_frame_rate(fctx->bufsink_ctx);

    av_log(NULL, AV_LOG_INFO, "probe video format: "
           "%dx%d, pix_fmt %d, SAR %d/%d, tb {%d, %d}, rate {%d, %d}\n",
           vfmt->width, vfmt->height, vfmt->pix_fmt,
           vfmt->sar.num, vfmt->sar.den,
           vfmt->time_base.num, vfmt->time_base.den,
           vfmt->frame_rate.num, vfmt->frame_rate.den);

    return 0;

end:
    avfilter_graph_free(&fctx->filter_graph);
    return ret;
}
```
测试源的本质是使用FFmpeg提供的用于产生测试图案的滤镜来生成视频数据。具体到代码实现层面，将testsrc/smptebars等滤镜代替常用的buffer滤镜作为源滤镜，然后直接与buffersink滤镜相连，以输出没底数据，如下图：  
![pic4](https://leichn.github.io/img/ffmpeg_filter/pic2.jpg "pic4")  

编译：`make vf_test`，将生成vf_test可执行文件  
测试：测试滤镜选项`-vf transpose=cclock,pad=iw+80:ih:40`，表示先将视频逆时针旋转90度，然后将视频左右两边各增加40像素宽度的黑边  
1. 使用“testsrc”测试图作输入源  
运行如下命令：  
```
ffplay -f lavfi -i testsrc
```
无滤镜处理的效果如图所示：  
![testsrc](https://leichn.github.io/img/ffmpeg_filter/testsrc.jpg "testsrc")  

运行带滤镜选项的ffplay命令：  
```
ffplay -f lavfi -i testsrc -vf transpose=cclock,pad=iw+80:ih:40
```
运行带滤镜选项的测试程序：  
```
./vf_test testsrc -vf transpose=cclock,pad=iw+80:ih:40
```
经滤镜处理的效果如图所示：  
![testsrc_vf](https://leichn.github.io/img/ffmpeg_filter/testsrc_vf.jpg "testsrc_vf")  


2. 使用“smptebars”测试图作输入源  
运行如下命令：  
```
ffplay -f lavfi -i smptebars
```
无滤镜处理的效果如图所示：  
![smptebars](https://leichn.github.io/img/ffmpeg_filter/smptebars.jpg "smptebars")  

运行带滤镜选项的ffplay命令：  
```
ffplay -f lavfi -i smptebars -vf transpose=cclock,pad=iw+80:ih:40
```
运行带滤镜选项的测试程序：  
```
./vf_test smptebars -vf transpose=cclock,pad=iw+80:ih:40
```
经滤镜处理的效果如图所示：  
![smptebars_vf](https://leichn.github.io/img/ffmpeg_filter/smptebars_vf.jpg "smptebars_vf")  

## 5. 参考资料  
[1] 刘歧，[FFmpeg Filter深度应用](https://yq.aliyun.com/articles/628153?utm_content=m_1000014065)，<https://yq.aliyun.com/articles/628153?utm_content=m_1000014065>  

## 6. 修改纪录  
2019-02-24  V1.0  初稿  
