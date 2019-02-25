本文为作者原创，转载请注明出处：<https://www.cnblogs.com/leisure_chn/p/10434209.html>  

## 1. overlay技术简介  
overlay技术又称视频叠加技术。overlay视频技术使用非常广泛，常见的例子有，电视屏幕右上角显示的电视台台标，以及画中画功能，画中画是指在一个大的视频播放窗口中还存在一个小播放窗口，两个窗口不同的视频内容同时播放。  

overlay技术中涉及两个窗口，通常把较大的窗口称作背景窗口，较小的窗口称作前景窗口，背景窗口或前景窗口里都可以播放视频或显示图片。FFmpeg中使用overlay滤镜可实现视频叠加技术。  

overlay滤镜说明如下：
```
描述：前景窗口(第二输入)覆盖在背景窗口(第一输入)的指定位置。

语法：overlay[=x:y[[:rgb={0, 1}]] 参数x和y是可选的，默认为0。rgb参数是可选的，其值为0或1，默认为0。

参数说明：
    x               从左上角的水平坐标，默认值为0
    y               从左上角的垂直坐标，默认值为0
    rgb             值为0表示输入颜色空间不改变，默认为0；值为1表示将输入的颜色空间设置为RGB

变量说明：如下变量可用在x和y的表达式中
    main_w或W       主输入(背景窗口)宽度
    main_h或H       主输入(背景窗口)高度
    overlay_w或w    overlay输入(前景窗口)宽度
    overlay_h或h    overlay输入(前景窗口)高度
```

overlay滤镜相关参数示意图如下：  
![overlay_filter](https://leichn.github.io/img/ffmpeg_overlay/overlay_filter.png "overlay_filter")  

## 2. 命令行用法  
可先参考“[FFmpeg使用基础](https://www.cnblogs.com/leisure_chn/p/10297002.html)”了解命令行用法基础  

overlay命令行基本格式如下：  
```
ffmpeg -i input1 -i input2 -filter_complex overlay=x:y output
```
input1是背景窗口输入源，input2是前景窗口输入源。  

### 2.1 视频中叠加图标  
背景窗口视频素材下载：[ring.mp4](https://github.com/leichn/blog_resources/blob/master/video/ring.mp4)  
![ring.mp4](https://leichn.github.io/img/ffmpeg_overlay/ring.jpg "ring.mp4")  
视频分辨率是768x432(此分辨率适用于平板电脑，宽高比为16:9)，上下黑边的像素高度是56，播放时长为37.97秒。关于分辨率与黑边的相关内容可参考如下：  
[为什么很多人把视频上下加黑条当做“电影感”？](https://www.zhihu.com/question/274830832)  
[用于编码视频文件的视频预设](https://help.adobe.com/zh_CN/scene7/using/WSE86ACF2B-BD50-4c48-A1D7-9CD4405B62D0.html)  

前景窗口图标素材下载：[ring_100x87.png](https://github.com/leichn/blog_resources/blob/master/video/ring.mp4)  
![ring.png](https://leichn.github.io/img/ffmpeg_overlay/ring_100x87.png "ring.png")  
图标分辨率是100x87。图标格式为PNG格式，当然选用其他格式的图片作图标也是可以的，但PNG图标具有透明背景，更适合用作图标。  

#### 2.1.1 直接叠加图标  
将图标叠加于视频右上角  
```
ffmpeg -i ring.mp4 -i ring_100x87.png -filter_complex overlay=W-w:56 -max_muxing_queue_size 1024 ring_logo_t.mp4
```
效果如下：  
![ring_logo_t.jpg](https://leichn.github.io/img/ffmpeg_overlay/ring_logo_t.jpg "ring_logo_t.jpg")  

将图标叠加于视频右下角  
```
ffmpeg -i ring.mp4 -i ring_100x87.png -filter_complex overlay=W-w:H-h-56 -max_muxing_queue_size 1024 ring_logo_b.mp4
```
效果如下：  
![ring_logo_b.jpg](https://leichn.github.io/img/ffmpeg_overlay/ring_logo_b.jpg "ring_logo_b.jpg")  

#### 2.1.2 延时叠加图标  
如下，背景窗口播放8.6秒后，图标开始显示。注意“-itsoffset 8.6”作为第二个输入文件的输入选项，参数位置不能放错。  
```
ffmpeg -i ring.mp4 -itsoffset 8.6 -i ring_100x87.png -filter_complex overlay=W-w:56 -max_muxing_queue_size 1024 ring_logo_delay.mp4
```

### 2.2 视频中叠加视频——画中画  
视频中叠加视频即为画中画功能。注意两个视频仅图像部分会叠加在一起，声音是不会叠加的，有一个视频的声音会消失。  

#### 2.2.1 叠加计时器  
找一个计时器小视频，将之叠加到背景视频上。我们可以从测试源中获取这个计时器视频。先运行如下命令：  
```
ffplay -f lavfi -i testsrc
```

视频无法贴在本文里，那运行截图命令，从视频中截取一张图：  
```
ffmpeg -ss 00:00:12 -f lavfi -i testsrc -frames:v 1 -f image2 testsrc.jpg
```
效果如下：  
![testsrc.jpg](https://leichn.github.io/img/ffmpeg_overlay/testsrc.jpg "testsrc.jpg")  

我们把计时器那一小块视频裁剪下来，运行如下命令：  
```
ffmpeg -ss 00:00:10 -t 20 -f lavfi -i testsrc -vf crop=61:52:224:94 timer.h264
```
此命令主要用到了crop视频滤镜，说明一下：  
“-vf crop=61:52:224:94”表示裁剪一块位于(224,94)坐标处宽为61像素高为52像素的视频块  
“-ss 00:00:10 -t 20”表示从10秒处开始裁剪，裁剪时长为20秒  

将计时器视频timer.h264叠加到背景视频ring.mp4里：  
```
ffmpeg -i ring.mp4 -i timer.h264 -filter_complex overlay=W-w:0 -max_muxing_queue_size 1024 ring_timer.mp4
```

效果如下：  
![ring_timer.jpg](https://leichn.github.io/img/ffmpeg_overlay/ring_timer.jpg "ring_timer.jpg")  

看一下视频叠加过程中FFmpeg在控制台中的打印信息，关注流的处理：  
```
$ ffmpeg -i ring.mp4 -i timer.h264 -filter_complex overlay=W-w:0 -max_muxing_queue_size 1024 ring_timer.mp4
......
Input #0, mov,mp4,m4a,3gp,3g2,mj2, from 'ring.mp4':
  Metadata:
    ......
  Duration: 00:00:37.97, start: 0.032000, bitrate: 515 kb/s
    Stream #0:0(chi): Video: h264 (avc1 / 0x31637661), none, 768x432, 488 kb/s, 23 fps, 23 tbr, 23k tbn, 46k tbc (default)
    Metadata:
      handler_name    : 1348358526.h264#video:fps=23 - Imported with GPAC 0.5.1-DEV-rev4127
    Stream #0:1(chi): Audio: aac (HE-AAC) (mp4a / 0x6134706D), 48000 Hz, stereo, fltp, 95 kb/s (default)
    Metadata:
      handler_name    : GPAC ISO Audio Handler
Input #1, h264, from 'timer.h264':
  Duration: N/A, bitrate: N/A
    Stream #1:0: Video: h264 (High 4:4:4 Predictive), yuv444p(progressive), 61x52 [SAR 1:1 DAR 61:52], 25 fps, 25 tbr, 1200k tbn, 50 tbc
Stream mapping:
  Stream #0:0 (h264) -> overlay:main (graph 0)
  Stream #1:0 (h264) -> overlay:overlay (graph 0)
  overlay (graph 0) -> Stream #0:0 (libx264)
  Stream #0:1 -> #0:1 (aac (native) -> aac (native))
......
```
看“Stream mapping”部分可以看出：  
输入源1视频流(Stream #0:0)和输入源2视频流(Stream #1:0)叠加到输出视频流(Stream #0:0)  
输入源1音频流(Stream #0:1)拷贝到输出音频流(Stream #0:1)  

视频开始几秒处播放有些异常，声音播放几秒后图像才开始播放，原因不太清楚。  

## 3. API用法  

使用滤镜API编程，解析不同的滤镜选项，以达到和命令行中输入命令同样的效果。  

例程使用“[FFmpeg滤镜API用法与实例解析](https://www.cnblogs.com/leisure_chn/p/10429145.html)”中第4.2节的示例程序  
代码目录[https://github.com/leichn/exercises/blob/master/source/ffmpeg/ffmpeg_vfilter/](https://github.com/leichn/exercises/blob/master/source/ffmpeg/ffmpeg_vfilter/)  
下载代码，进入代码目录，在命令行运行`make vf_file`命令，将生成vf_file可执行文件  
在命令行运行`./vf_file ring.flv -vf "movie=ring_100x87.png[logo];[in][logo]overlay=W-w:56"`  
测试效果为：  
![ring_logo_t.jpg](https://leichn.github.io/img/ffmpeg_overlay/ring_logo_t.jpg "ring_logo_t.jpg")  
因为例程尚不支持多输入的方式，所以上述测试命令中借助了movie滤镜来加载第二个输入，这条命令和下面这条命令效果是一样的  
`ffplay ring.mp4 -i ring_100x87.png -filter_complex overlay=W-w:56`  

## 4. 遗留问题  
第3节例程不支持多输入方式，借助了movie滤镜变通实现，多输入情况下API如何编程？待分析如下命令中多输入选项的解析处理方式：  
`ffplay ring.mp4 -i ring_100x87.png -filter_complex overlay=W-w:56`

## 5. 参考资料  
[1] [为什么很多人把视频上下加黑条当做“电影感”？](https://www.zhihu.com/question/274830832)  
[2] [用于编码视频文件的视频预设](https://help.adobe.com/zh_CN/scene7/using/WSE86ACF2B-BD50-4c48-A1D7-9CD4405B62D0.html)  

## 6. 修改记录  
2019-02-16  V1.0  首次整理  
