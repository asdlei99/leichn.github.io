本文为作者原创，转载请注明出处：<https://www.cnblogs.com/leisure_chn/p/10399048.html>  

AVBuffer是FFmpeg中很常用的一种缓冲区，缓冲区使用引用计数(reference-counted)机制。  
AVBufferRef则对AVBuffer缓冲区提供了一层封装，最主要的是作引用计数处理，实现了一种安全机制。用户不应直接访问AVBuffer，应通过AVBufferRef来访问AVBuffer，以保证安全。  
FFmpeg中很多基础的数据结构都包含了AVBufferRef成员，来间接使用AVBuffer缓冲区。  
本文使用的FFmpeg版本号为FFmpeg 4.1。  

AVBuffer和AVBufferRef结构体定义及操作函数位于libavutil中的buffer.h、buffer_internal.h、buffer.c三个文件中。需要关注的要点是**AVBufferRef和AVBuffer的关系**以及**缓冲区引用计数的概念**。  
## 1. 数据结构定义  
### 1.1 struct AVBuffer  
struct AVBuffer定义于“libavutil/buffer_internal.h”，buffer_internal.h位于FFmpeg工程源码中，而FFmpeg提供的开发库头文件中并无此文件，因此这是一个内部数据结构，不向用户开放，用户不应直接访问AVBuffer，应通过AVBufferRef来访问AVBuffer，以保证安全。  
```c
struct AVBuffer {
    uint8_t *data; /**< data described by this buffer */
    int      size; /**< size of data in bytes */

    /**
     *  number of existing AVBufferRef instances referring to this buffer
     */
    atomic_uint refcount;

    /**
     * a callback for freeing the data
     */
    void (*free)(void *opaque, uint8_t *data);

    /**
     * an opaque pointer, to be used by the freeing callback
     */
    void *opaque;

    /**
     * A combination of BUFFER_FLAG_*
     */
    int flags;
};
```
- data: 缓冲区地址  
- size: 缓冲区大小  
- refcount: 引用计数值  
- free: 用于释放缓冲区内存的回调函数  
- opaque: 提供给free回调函数的参数  
- flags: 缓冲区标志  

### 1.2 struct AVBufferRef
struct AVBufferRef定义于buffer.h中：  
```c
/**
 * A reference to a data buffer.
 *
 * The size of this struct is not a part of the public ABI and it is not meant
 * to be allocated directly.
 */
typedef struct AVBufferRef {
    AVBuffer *buffer;

    /**
     * The data buffer. It is considered writable if and only if
     * this is the only reference to the buffer, in which case
     * av_buffer_is_writable() returns 1.
     */
    uint8_t *data;
    /**
     * Size of data in bytes.
     */
    int      size;
} AVBufferRef;
```
- buffer: AVBuffer
- data: 缓冲区地址，实际等于buffer->data
- size: 缓冲区大小，实际等于buffer->size

## 2. 关键函数实现  
### 2.1 av_buffer_alloc()  
```c
AVBufferRef *av_buffer_alloc(int size)
{
    AVBufferRef *ret = NULL;
    uint8_t    *data = NULL;

    data = av_malloc(size);
    if (!data)
        return NULL;

    ret = av_buffer_create(data, size, av_buffer_default_free, NULL, 0);
    if (!ret)
        av_freep(&data);

    return ret;
}
```
av_buffer_alloc()作了如下处理:  
a) 使用av_malloc分配缓冲区  
b) 调用av_buffer_create()创建`AVBuffer AVBufferRef::*buffer`成员，用于管理AVBuffer缓冲区  
c) 返回`AVBufferRef *`对象  

### 2.2 av_buffer_create()  
```c
AVBufferRef *av_buffer_create(uint8_t *data, int size,
                              void (*free)(void *opaque, uint8_t *data),
                              void *opaque, int flags)
{
    AVBufferRef *ref = NULL;
    AVBuffer    *buf = NULL;

    buf = av_mallocz(sizeof(*buf));
    if (!buf)
        return NULL;

    buf->data     = data;
    buf->size     = size;
    buf->free     = free ? free : av_buffer_default_free;
    buf->opaque   = opaque;

    atomic_init(&buf->refcount, 1);

    if (flags & AV_BUFFER_FLAG_READONLY)
        buf->flags |= BUFFER_FLAG_READONLY;

    ref = av_mallocz(sizeof(*ref));
    if (!ref) {
        av_freep(&buf);
        return NULL;
    }

    ref->buffer = buf;
    ref->data   = data;
    ref->size   = size;

    return ref;
}
```
av_buffer_create()是一个比较核心的函数，从其实现代码很容易看出AVBufferRef和AVBuffer这间的关系。  
函数主要功能就是初始化`AVBuffer AVBufferRef::*buffer`成员，即为上述清单`ref->buffer`各字段赋值，最终，`AVBufferRef *ref`全部构造完毕，将之返回。  

其中`void (*free)(void *opaque, uint8_t *data)`参数赋值为`av_buffer_default_free`，实现如下。其实就是直接调用了`av_free`回收内存。  
```c

void av_buffer_default_free(void *opaque, uint8_t *data)
{
    av_free(data);
}
```

### 2.3 av_buffer_ref()  
```c 
AVBufferRef *av_buffer_ref(AVBufferRef *buf)
{
    AVBufferRef *ret = av_mallocz(sizeof(*ret));

    if (!ret)
        return NULL;

    *ret = *buf;

    atomic_fetch_add_explicit(&buf->buffer->refcount, 1, memory_order_relaxed);

    return ret;
}
```
av_buffer_ref()处理如下：  
a) `*ret = *buf;`一句将buf各成员值赋值给ret中对应成员，buf和ret将共用同一份AVBuffer缓冲区  
b) `atomic_fetch_add_explicit(...);`一句将AVBuffer缓冲区引用计数加1  
注意此处的关键点：**共用缓冲区(缓冲区不拷贝)，缓冲区引用计数加1**  

### 2.4 av_buffer_unref()  
```c  
static void buffer_replace(AVBufferRef **dst, AVBufferRef **src)
{
    AVBuffer *b;

    b = (*dst)->buffer;

    if (src) {
        **dst = **src;
        av_freep(src);
    } else
        av_freep(dst);

    if (atomic_fetch_add_explicit(&b->refcount, -1, memory_order_acq_rel) == 1) {
        b->free(b->opaque, b->data);
        av_freep(&b);
    }
}

void av_buffer_unref(AVBufferRef **buf)
{
    if (!buf || !*buf)
        return;

    buffer_replace(buf, NULL);
}
```
av_buffer_unref()处理如下：  
a) 回收`AVBufferRef **buf`内存  
b) 将`(*buf)->buffer`(即AVBAVBufferRef的成员AVBuffer)的引用计数减1，若引用计数为0，则通过`b->free(b->opaque, b->data);`调用回调函数回收AVBuffer缓冲区内存
注意此处的关键点：**销毁一个AVBufferRef时，将其AVBuffer缓冲区引用计数减1，若缓冲区引用计数变为0，则将缓冲区也回收**，这很容易理解，只有当缓冲区不被任何对象引用时，缓冲区才能被销毁  

## 3. 修改记录  
2018-12-13  V1.0  初稿  