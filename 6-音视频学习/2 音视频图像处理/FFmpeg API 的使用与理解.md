[FFmpeg原理介绍 · FFmpeg原理](https://ffmpeg.xianwaizhiyin.net/)

[(66 封私信 / 83 条消息) 刻意练习FFmpeg系列：通过思维导图快速了解FFmpeg源码整体结构体 - 知乎](https://zhuanlan.zhihu.com/p/143195044)

# FFmpeg API 的使用与理解

## 序章：建立宏观认知

### 1. FFmpeg的设计哲学

FFmpeg是一个极其庞大和复杂的库，但其底层设计遵循着清晰且一致的哲学。理解这些核心思想，是掌握其众多API和结构体的关键。其设计哲学可以概括为四大支柱：**分层解耦、模块化架构、数据驱动配置、以及精细的内存性能控制**。是一条模块化的多媒体处理流水线

#### 1.1 分层解耦

FFmpeg将一个复杂的多媒体处理任务严格划分为三个独立的层次，每一层都专注于自身的核心职责，实现了高度的“关注点分离”（Separation of Concerns）。

*   **协议层 (Protocol Layer)**
    *   **职责**: 负责数据的输入与输出，处理原始字节流的获取与写入。它不关心字节流的内容和格式。
    *   **核心结构体**: `AVIOContext`
    *   **工作范围**: 处理 `file://`, `http://`, `rtsp://` 等协议，或自定义的内存I/O。

*   **格式层/容器层 (Format/Container Layer)**
    *   **职责**: 负责解封装（Demuxing）或封装（Muxing）媒体容器（如 MP4, MKV, FLV）。它理解容器的结构，能将文件分离成独立的流（`AVStream`），并将压缩数据打包（`AVPacket`），反之亦然。它不关心包内的具体编码格式。
    *   **核心结构体**: `AVFormatContext`, `AVStream`, `AVPacket`

*   **编码层/数据层 (Codec/Data Layer)**
    *   **职责**: 负责处理媒体数据的具体内容，即解码（Decoding）和编码（Encoding）。它接收压缩数据包（`AVPacket`），输出原始数据帧（`AVFrame`），反之亦然。它不关心数据来自何种容器。
    *   **核心结构体**: `AVCodecContext`, `AVFrame`, `AVCodec`

这种分层设计使得每一层都可以独立地发展和替换，极大地增强了系统的灵活性和可维护性。

**不同的层次之间通过 `avPacket` 和 `avFrame` 相互联系起来**

#### 1.2 模块化架构

FFmpeg的整个生态系统是基于可插拔的组件构建的，它更像一个框架，而非一个单一的程序。

*   **Muxers/Demuxers**: 每个 `AVInputFormat` 和 `AVOutputFormat` 都是一个独立的（解）封装器模块。
*   **Codecs**: 每个 `AVCodec` 都是一个独立的编解码器模块。
*   **Protocols**: 每种I/O协议（file, http, tcp等）都是一个独立的模块。
*   **Filters**: `libavfilter`库中的每个功能（缩放、裁剪、水印等）都是一个可以自由链接组合的滤镜模块。

这种架构允许开发者按需编译和注册所需组件，并且极大地便利了第三方开发者为FFmpeg贡献新的格式、编码器或协议支持。

#### 1.3 数据驱动配置

FFmpeg倾向于使用数据结构来承载配置信息，而不是通过大量的独立函数进行设置。

*   **核心上下文结构体**: 诸如 `AVCodecContext` 这样的庞大结构体，其成员变量就是编解码器的配置项。开发者通过直接修改这些成员（如 `width`, `height`, `bit_rate`）来完成配置，然后调用一个统一的初始化函数（如 `avcodec_open2()`）使配置生效。

*   **`AVDictionary`**: 对于更灵活的、甚至是编解码器私有的选项，FFmpeg提供了`AVDictionary`键值对系统。这使得API本身保持稳定，即使底层模块增加了新的配置项，上层调用代码也无需改动，只需在字典中添加新的键值对即可。

这种设计模式使得API更加简洁和稳定，同时提供了极高的灵活性。

#### 1.4 精细的内存与性能控制

作为一个高性能的C语言库，FFmpeg将内存管理的权力和责任都交给了开发者。

*   **手动生命周期管理**: 所有核心结构体（如`AVFormatContext`, `AVCodecContext`等）都遵循 `_alloc()` / `_free()` 的模式，需要开发者手动管理其创建和销毁。

*   **引用计数 (Reference Counting)**: 这是FFmpeg性能优化的核心机制。对于数据量庞大的`AVPacket`和`AVFrame`，FFmpeg通过内部的`AVBufferRef`实现引用计数。`_ref()`和`_unref()`系列API允许在不进行实际数据拷贝的情况下，安全地传递和共享数据，极大地提升了处理效率。

## 第一章：核心数据结构

### 2. 总控室：`AVFormatContext`

`AVFormatContext` 是FFmpeg中进行解封装（demuxing）与封装（muxing）操作的**全局上下文**。它是整个处理流程的起点和中心，包含了媒体文件的所有格式级信息。在任何文件I/O操作中，这个结构体都是必须的第一个核心对象。

#### 2.1 作用

*   **对于解封装（输入）**: `AVFormatContext` 存储了从媒体文件头部读取的所有信息，包括容器格式、时长、码率、流的数量以及每个流的元数据。
*   **对于封装（输出）**: `AVFormatContext` 用于配置输出文件的所有参数，包括容器格式、要写入的流以及全局元数据。

#### 2.2 核心成员解析

以下是在实际开发中最常接触和关心的成员，分为三类：

##### 2.2.1 格式与私有数据

*   `struct AVInputFormat *iformat;`
    *   **作用**: **仅在解封装（demuxing）时使用**。它是一个指针，指向一个描述输入文件格式的结构体（例如，`mov,mp4,m4a,3gp,3g2,mj2`格式的 demuxer）。
    *   **如何设置**: 由 `avformat_open_input()` 自动探测并设置。

*   `struct AVOutputFormat *oformat;`
    *   **作用**: **仅在封装（muxing）时使用**。它是一个指针，指向一个描述输出文件格式的结构体（例如，`mp4` 格式的 muxer）。
    *   **如何设置**: 由开发者在调用 `avformat_alloc_output_context2()` 时指定。

*   `void *priv_data;`
    *   **作用**: 指向属于特定格式的私有数据。例如，`image2` demuxer可能会在这里存储帧率信息，而`hls` demuxer则会在这里存储播放列表（playlist）相关的数据。这些是通用结构无法覆盖的、特定格式才有的配置。

##### 2.2.2 I/O 与行为控制

*   `AVIOContext *pb;`
    *   **作用**: 这是进行底层I/O操作的上下文。它抽象了数据源，无论是本地文件还是网络流，上层都通过它来读取或写入字节。
    *   **如何设置**: 在解码时，通常由 `avformat_open_input()` 内部创建；在编码时，需要您自己通过 `avio_open()` 创建并赋值给它。

*   `char filename[1024];`
    *   **作用**: 存储输入或输出的文件名/URL。

*   `int flags;`
    *   **作用**: 控制（解）封装器行为的标志位。例如：
        *   `AVFMT_FLAG_GENPTS`: 让FFmpeg尝试生成缺失的PTS。
        *   `AVFMT_FLAG_NOBUFFER`: 减少缓冲，适用于低延迟场景。
        *   `AVFMT_FLAG_DISCARD_CORRUPT`: 丢弃损坏的帧。

##### 2.2.3 核心内容信息

*   `unsigned int nb_streams;`
    *   **作用**: **非常重要**。表示该媒体文件中流的数量。

*   `AVStream **streams;`
    *   **作用**: **极其重要**。这是一个指针数组，数组的每个元素都是一个 `AVStream*` 指针，指向一个描述具体流信息的 `AVStream` 结构体。`nb_streams` 告诉我们这个数组有多少个有效元素。

*   `int64_t duration;`
    *   **作用**: 整个媒体文件的总时长，单位是 `AV_TIME_BASE`（一个内部高精度时间单位，通常是1,000,000分之一秒）。

*   `int64_t bit_rate;`
    *   **作用**: 文件的总码率（bits per second）。

*   `AVDictionary *metadata;`
    *   **作用**: 文件的全局元数据（描述数据的数据），如标题(`title`)、作者(`artist`)、专辑(`album`)等，以键值对形式存储。

#### 2.3 关键概念

`streams`数组的角色：

`AVFormatContext->streams` 是一个**指针数组**，其每个元素 `streams[i]` 是一个指向 `AVStream` 结构体的指针。

*   **`streams` 数组本身不存储任何实际的媒体数据**（如视频像素或音频样本）。
*   它扮演的角色是**流的元数据索引**。每个 `AVStream` 结构体详细描述了对应流的属性（如编码格式、分辨率、时间基等）。
*   实际的媒体数据（压缩后）存在于 `AVPacket` 中。当你调用 `av_read_frame()` 时，FFmpeg会返回一个 `AVPacket`，其 `stream_index` 成员会明确指出这个数据包属于 `streams` 数组中的哪一个流。

简而言之，`AVFormatContext` 通过 `streams` 数组管理和描述了文件包含的所有媒体轨道，但真正的媒体数据是通过 `AVPacket` 在这些轨道上进行传输的。

### 3. 流的规格书：`AVStream`

`AVStream` 是描述媒体文件中单个独立流（或称轨道，Track）的结构体。如果 `AVFormatContext` 是文件的“总说明书”，那么每个 `AVStream` 就是其中关于视频、音频或字幕等具体轨道的“分册技术规格”。它在格式层（libavformat）和编码层（libavcodec）之间起到了关键的桥梁作用。

#### 3.1 作用

*   **描述流属性**: `AVStream` 包含了关于一个流的所有静态信息，例如编码格式、时间基、平均帧率、时长等。
*   **参数传递**: 它的核心成员 `codecpar` 负责将从容器中解析出的解码参数，安全地传递给解码器进行初始化。

#### 3.2 核心成员解析

##### 3.2.1 身份标识

*   `int index;`
    *   **作用**: **极其重要**。该流在 `AVFormatContext->streams` 数组中的索引，从0开始。这是在程序中唯一标识和访问一个流最常用的方式。

*   `int id;`
    *   **作用**: 在特定容器格式（如MPEG-TS）内部使用的流标识符（如PID）。通常在应用层面，我们更多地使用 `index`。

##### 3.2.2 编码核心参数

*   `AVCodecParameters *codecpar;`
    *   **作用**: **`AVStream` 的核心**。这是一个指向 `AVCodecParameters` 结构体的指针，包含了初始化编解码器所需的所有**静态元数据**。它是一个纯数据容器，用于解耦 `libavformat` 和 `libavcodec`。其内部关键成员包括：
        *   `codec_type`: 媒体类型 (`AVMEDIA_TYPE_VIDEO`, `AVMEDIA_TYPE_AUDIO`, etc.)。
        *   `codec_id`: 具体的编解码器ID (`AV_CODEC_ID_H264`, `AV_CODEC_ID_AAC`, etc.)。
        *   `width`, `height`: [视频] 图像的宽和高。
        *   `format`: [视频] 像素格式 (`AVPixelFormat`)；[音频] 采样格式 (`AVSampleFormat`)。
        *   `extradata`, `extradata_size`: 额外的编解码器数据，如H.264的SPS/PPS。

*   `attribute_deprecated AVCodecContext *codec;`
    *   **作用**: **已废弃成员**。在旧版FFmpeg中曾用于直接关联 `AVCodecContext`。现代FFmpeg开发中**应忽略此成员，并始终使用 `codecpar`**。

##### 3.2.3 时间信息

*   `AVRational time_base;`
    *   **作用**: **极其重要**。定义了此流中 `AVPacket` 时间戳 (`pts`, `dts`, `duration`) 的单位（时间刻度）。它是一个分数，表示一个时间戳单位等于多少秒。

*   `int64_t duration;`
    *   **作用**: 此流的总时长，单位是 `time_base`。

*   `int64_t start_time;`
    *   **作用**: 此流的起始时间戳，单位是 `time_base`。

*   `AVRational avg_frame_rate;`
    *   **作用**: 流的平均帧率。可用于参考，但精确的时间计算应基于 `time_base` 和 `pts`。

##### 3.2.4 元数据与布局

*   `AVDictionary *metadata;`
    *   **作用**: 存储此流特有的元数据，如音轨的语言(`language`)或视频的旋转(`rotate`)信息。

*   `int disposition;`
    *   **作用**: 一组标志位，用于描述流的用途。例如：
        *   `AV_DISPOSITION_DEFAULT`: 是否为默认播放的轨道。
        *   `AV_DISPOSITION_FORCED`: 是否为强制字幕。
        *   `AV_DISPOSITION_ATTACHED_PIC`: 是否为封面图片。

#### 3.3 关系解析

*   **`AVStream` vs `AVCodecContext`：蓝图与车间**
    `AVStream` (通过其成员 `codecpar`) 提供了创建和配置 `AVCodecContext` 所需的全部**静态参数**，它是一份“蓝图”。而 `AVCodecContext` 是根据这份蓝图构建出来的、一个**动态的、可执行解码/编码操作的实例**（“车间”）。`avcodec_parameters_to_context()` 函数是连接二者的桥梁。

*   **`AVStream` vs `AVPacket`：轨道与列车**
    `AVStream` 定义了一条数据流的“轨道”属性，特别是它的时间坐标系 (`time_base`)。`AVPacket` 则是承载着压缩数据的“列车”，行驶在这条轨道上。`AVPacket` 中的 `stream_index` 成员指明了它属于哪条 `AVStream` “轨道”，其 `pts` 和 `dts` 必须由该 `AVStream` 的 `time_base` 来解释才有意义。

### 4. 编解码车间：`AVCodecContext`

`AVCodecContext` 是FFmpeg中执行编解码操作的核心数据结构。它不仅包含了编解码所需的所有配置参数，还维护着编解码过程中的内部状态和缓冲区。如果说 `AVStream` 提供了静态的“蓝图”，那么 `AVCodecContext` 就是一个根据蓝图构建起来的、**动态的、可进行实际工作的“生产车间”**。

#### 4.1 作用

*   **对于解码**: `AVCodecContext` 接收压缩数据包 (`AVPacket`)，并将其解码为原始数据帧 (`AVFrame`)。
*   **对于编码**: `AVCodecContext` 接收原始数据帧 (`AVFrame`)，并将其编码为压缩数据包 (`AVPacket`)。

它是所有编解码API（如 `avcodec_send_packet`, `avcodec_receive_frame`）的主要操作对象。

#### 4.2 核心成员解析

`AVCodecContext` 的成员众多，可以分为以下几类：

##### 4.2.1 身份与核心配置

*   `const AVCodec *codec;`
    *   **作用**: 指向该上下文所使用的 `AVCodec` 结构体，明确了具体的编解码器实现。
    *   **如何设置**: 在调用 `avcodec_open2()` 时作为参数传入。
*   `enum AVCodecID codec_id;` / `enum AVMediaType codec_type;`
    *   **作用**: 编解码器的唯一ID和媒体类型。
    *   **如何设置**:
        *   **[解码]** 由 `avcodec_parameters_to_context()` 从 `AVCodecParameters` 自动拷贝。
        *   **[编码]** 由开发者手动设置。
*   `int64_t bit_rate;`
    *   **作用**: **[编码]** 目标平均码率 (bits per second)，是码率控制的关键参数。
    *   **如何设置**: 由开发者在编码前手动设置。
*   `AVRational time_base;`
    *   **作用**: 定义了送入或送出编解码器的 `AVFrame` 的 `pts` 单位。
    *   **如何设置**:
        *   **[解码]** 由 `libavcodec` 在 `avcodec_open2()` 时根据流信息自动设置。
        *   **[编码]** **必须由开发者**在 `avcodec_open2()` 之前手动设置。
*   `int flags;` / `int flags2;`
    *   **作用**: 控制编解码器行为的标志位。
        - `AV_CODEC_FLAG_GLOBAL_HEADER`: 指示编码器将SPS/PPS等全局信息单独存放在 extradata 中，而不是每个关键帧都带一份。MP4等格式需要这个。
    *   **如何设置**: 主要在编码时由开发者根据需求设置。
*   `void *priv_data;`
    *   **作用**: 指向编解码器私有选项的结构体。用于设置标准参数之外的、特定于某个编解码器（如 `libx264`）的参数。
    *   **如何设置**: 通过 `AVDictionary` 传递给 `avcodec_open2()`，或在打开后使用 `av_opt_set()` 进行设置。

##### 4.2.2 视频专用配置

*   `int width, height;`
    *   **作用**: 视频帧的宽度和高度（==是真实图像尺寸，不是内存对齐尺寸==）。
    *   **如何设置**:
        *   **[解码]** 由 `avcodec_parameters_to_context()` 从 `AVCodecParameters` 自动拷贝。
        *   **[编码]** 由开发者手动设置。

*   `enum AVPixelFormat pix_fmt;`
    *   **作用**: 像素格式（如 `AV_PIX_FMT_YUV420P`）。
    *   **如何设置**:
        *   **[解码]** 由 `avcodec_parameters_to_context()` 从 `AVCodecParameters` 自动拷贝。
        *   **[编码]** 由开发者手动设置。

*   `int gop_size;` / `int max_b_frames;`
    *   **作用**: **[编码]** 关键帧间隔和最大B帧数量。
    *   **如何设置**: 由开发者在编码前手动设置。

##### 4.2.3 音频专用配置

*   `int sample_rate;`
*   `enum AVSampleFormat sample_fmt;`
*   `uint64_t channel_layout;` / `int channels;`
    *   **作用**: 音频的核心参数：采样率、采样格式、声道布局和声道数。
    *   **如何设置**:
        *   **[解码]** 由 `avcodec_parameters_to_context()` 从 `AVCodecParameters` 自动拷贝。
        *   **[编码]** 由开发者手动设置。

*   `int frame_size;`
    *   **作用**: **[编码]** 编码器期望单个 `AVFrame` 包含的样本数（例如AAC通常为1024）。
    *   **如何设置**:
        *   **[解码]** 由解码器在 `avcodec_open2()` 时设置，告知用户解码后每帧的样本数。
        *   **[编码]** 对于固定帧大小的编码器，由开发者设置。

#### 4.3 关系解析

*   **`AVCodecContext` vs `AVCodecParameters`：动态实例 vs. 静态描述**
    `AVCodecParameters` 是一个纯粹的数据容器，用于在 `libavformat` 和 `libavcodec` 之间安全地传递编解码器元数据，它本身**不能执行任何操作**。`AVCodecContext` 则是一个功能完备的**工作实体**，它包含了运行时的状态、内部缓冲区和指向具体编解码算法的函数指针。
    *   **[解码]** 流程: `AVStream->codecpar`  --( `avcodec_parameters_to_context()` )--> `AVCodecContext`
    *   **[编码]** 流程: `AVCodecContext`  --( `avcodec_parameters_from_context()` )--> `AVStream->codecpar`

*   **`AVCodecContext` vs `AVCodec`：实例 vs. 类/模板**
    `AVCodec` 是一个全局的、只读的结构体，它代表了一类编解码器（如`libx264`）的“模板”。它定义了该编解码器的名称、能力以及所有操作的**函数指针**（`init`, `send_frame`, `receive_packet` 等）。
    `AVCodecContext` 则是 `AVCodec` 的一个**具体实例**。当你通过 `avcodec_open2(codec_ctx, codec, ...)` 将两者关联时，`AVCodecContext` 才真正被初始化，并准备好使用 `AVCodec` 提供的算法来处理数据。你可以为同一个 `AVCodec` 创建多个独立的 `AVCodecContext` 实例。

### 5. 压缩数据容器：`AVPacket`

`AVPacket` 是FFmpeg数据流转中的核心结构体之一，其核心职责是**存储和管理编码后的压缩数据**。它是连接格式层（`libavformat`）和编码层（`libavcodec`）之间数据传输的标准化载体。

#### 5.1 作用

*   **作为Demuxer的输出**: `av_read_frame()` 从媒体文件中解析出属于特定流的、一段完整的压缩数据，并将其封装在 `AVPacket` 中返回。
*   **作为Decoder的输入**: `avcodec_send_packet()` 将 `AVPacket` 中包含的压缩数据送入解码器进行解码。
*   **作为Encoder的输出**: `avcodec_receive_packet()` 从编码器获取编码完成的压缩数据，并以 `AVPacket` 的形式返回。
*   **作为Muxer的输入**: `av_write_frame()` 或 `av_interleaved_write_frame()` 将 `AVPacket` 送入封装器，写入到输出媒体文件中。

简而言之，`AVPacket` 是FFmpeg流水线上流动的、标准化的“压缩数据包”。

#### 5.2 核心成员解析

##### 5.2.1 数据指针与大小

*   `uint8_t *data;`
    *   **作用**: 指向存储压缩数据缓冲区的起始地址。

*   `int size;`
    *   **作用**: `data` 指针所指向的有效数据的字节大小。

*   `AVBufferRef *buf;`
    *   **作用**: **极其重要**的内存管理成员。它通过引用计数机制管理 `data` 指向的内存缓冲区。
        *   **如果 `buf` 非NULL**: `AVPacket` 的数据由FFmpeg的缓冲区系统管理，`av_packet_ref()` 会高效地增加引用计数，而不会拷贝数据。
        *   **如果 `buf` 为NULL**: 数据由外部管理，`av_packet_ref()` 会触发一次深拷贝来创建新的FFmpeg管理的缓冲区。

##### 5.2.2 时间戳与时长

*   `int64_t pts;` (Presentation Time Stamp)
    
    *   **作用**: **显示时间戳**。指示该数据包解码后的帧应该在何时被显示。其单位由其所属 `AVStream` 的 `time_base` 决定。
    
*   `int64_t dts;` (Decompression Time Stamp)
    
    *   **作用**: **解码时间戳**。指示该数据包应该在何时被解码。对于存在B帧的视频流，`dts` 和 `pts` 的顺序可能不同。其单位同样由 `AVStream->time_base` 决定。
    
*   `int64_t duration;`
    *   **作用**: 该数据包的播放时长，单位是 `AVStream->time_base`。
    
*   **时间基上下文**:
    
    - **[解码流程]** 当`AVPacket`由`av_read_frame()`从`AVFormatContext`中读取出来时，其时间戳的单位是 **`AVStream->time_base`**。这些时间戳可以直接送给解码器，解码器内部会处理后续的时间逻辑。
    
    - **[编码流程]** 当`AVPacket`由`avcodec_receive_packet()`从`AVCodecContext`中生成时，其时间戳的单位**默认继承自编码器的 `AVCodecContext->time_base`**。由于封装器（Muxer）要求的时间基是`AVStream->time_base`，这两者通常不同。因此，**开发者必须在将Packet写入文件前，手动进行时间基转换**。
    
    - **关键实践 (编码时)**:
    
      ```c
      // packet 是从 avcodec_receive_packet(codec_ctx, packet) 获取的
      // 此时 packet->pts, dts, duration 的单位是 codec_ctx->time_base
      av_packet_rescale_ts(packet,        // 要转换的包
                           codec_ctx->time_base, // 源时间基
                           stream->time_base);   // 目标时间基
      // 现在 packet 的时间戳已经转换完成，可以安全地写入文件
      av_interleaved_write_frame(format_ctx, packet);
      ```

##### 5.2.3 身份与属性

*   `int stream_index;`
    *   **作用**: **核心成员**。表明该数据包属于 `AVFormatContext->streams` 数组中的哪一个流。这是进行分发（demuxing）和路由（routing）的依据。

*   `int flags;`
    *   **作用**: 描述数据包属性的标志位。最常用的值是 `AV_PKT_FLAG_KEY`，表示该包包含一个**关键帧（Keyframe）**。

*   `int64_t pos;`
    *   **作用**: 该数据包在媒体文件或流中的字节位置偏移量，对seek操作和调试非常有用。

#### 5.3 关键概念：`AVPacket` 是压缩数据的直接载体

*   **`AVPacket` 包含的是编码数据**：其 `data` 成员直接指向一块内存，其中存储的就是一段连续的、压缩后的编码数据，例如H.264的NALU序列或AAC的ADTS帧。
*   **数据量**:
    *   对于视频，一个 `AVPacket` 通常包含一个完整的压缩视频帧。
    *   对于音频，一个 `AVPacket` 可能包含一个或多个压缩音频帧。
*   **生命周期**: `AVPacket` 自身及其数据缓冲区的生命周期是通过 `av_packet_alloc`/`av_packet_free` 和 `av_packet_ref`/`av_packet_unref` 来管理的，必须严格配对使用以避免内存泄漏或悬挂指针。

### 6. 原始数据画布：`AVFrame`

`AVFrame` 是FFmpeg中用于存储**解码后原始（未压缩）数据**的结构体。与存储压缩数据的`AVPacket`相对，`AVFrame`是媒体数据在被消费（如渲染显示、音频播放、滤镜处理）或被编码前的最终形态。

#### 6.1 作用

*   **作为Decoder的输出**: `avcodec_receive_frame()` 从解码器获取解码完成的原始数据，并存储在 `AVFrame` 中。
*   **作为Encoder的输入**: 开发者创建并填充 `AVFrame` 结构体，然后通过 `avcodec_send_frame()` 将其送入编码器进行压缩。
*   **作为Filter的输入/输出**: `libavfilter` 模块处理的对象就是 `AVFrame`，对原始数据进行各种变换（如缩放、裁剪、混音等）。

`AVFrame` 的设计非常通用，能够同时描述视频像素数据和音频样本数据，但其成员的解释方式在两种模式下有显著差异。

#### 6.2 核心成员解析

##### 6.2.1 通用核心成员

*   `uint8_t *data[AV_NUM_DATA_POINTERS];`
    *   **作用**: **核心中的核心**。指针数组，用于指向实际的媒体数据平面（plane）。

*   `int linesize[AV_NUM_DATA_POINTERS];`
    *   **作用**: 与 `data` 数组一一对应，描述了每个数据平面的“步长”（stride）或大小。

*   `AVBufferRef *buf[AV_NUM_DATA_POINTERS];`
    *   **作用**: 用于内存管理的引用计数缓冲数组。`data` 指针所指向的实际内存，就是由这些 `AVBufferRef` 管理的。

*   `int64_t pts;`
    *   **作用**: **显示时间戳 (Presentation Time Stamp)**。指示这一帧应该在何时被显示或播放。
    *   **单位 (Time Base)**:
        *   **[解码]** 解码器输出的`AVFrame`，其`pts`的单位通常是其来源流的 **`AVStream->time_base`**。FFmpeg解码流程倾向于保持原始流的时间基，以避免不必要的转换和精度损失。
        *   **[编码]** 开发者送入编码器的`AVFrame`，其`pts`的单位**必须**是开发者为 **`AVCodecContext->time_base`** 所设置的那个值，并在编码成 `AVPacket` 后，调用 `av_packet_rescale_ts()` 将其时间基单位转换为 `AVStream->time_base`。

*   `int format;`
    *   **作用**: 指定了原始数据的具体格式。
        *   **[视频]** 值是 `enum AVPixelFormat` 之一，如 `AV_PIX_FMT_YUV420P`。
        *   **[音频]** 值是 `enum AVSampleFormat` 之一，如 `AV_SAMPLE_FMT_FLTP`。

*   `int key_frame;`
    *   **作用**: 标志位 (1或0)，指示这是否是一个关键帧。

##### 6.2.2 视频模式 (The Canvas)

当`AVFrame`用于存储视频数据时，它描述了一张图像。

*   `int width, height;`
    *   **作用**: **有效图像区域**的宽度和高度，单位是像素。这是上层应用应该关心的逻辑尺寸。

*   `data` 和 `linesize` 的解释 (以`YUV420p`为例):
    视频数据通常是**平面（Planar）**存储的，即Y、U、V三个分量分别存储在不同的内存区域。
    *   `data[0]`: 指向 **Y (亮度) 分量**平面。
    *   `data[1]`: 指向 **U (色度) 分量**平面。
    *   `data[2]`: 指向 **V (色度) 分量**平面。
    *   `linesize[0]`: Y平面的**步长（stride）**。即从图像的一行像素到下一行像素，在内存中需要跨越的字节数。为了内存对齐和性能优化，**`linesize[0]` 通常大于等于 `width`**。
    *   `linesize[1]`, `linesize[2]`: U和V平面的步长。

##### 6.2.3 音频模式 (The Score)

当`AVFrame`用于存储音频数据时，它描述了一段连续的音频样本。

*   `int nb_samples;`
    *   **作用**: **核心成员**。表示该帧中**每个声道**包含的样本数量 (e.g., AAC为1024)。

*   `int sample_rate;`
    *   **作用**: 采样率 (e.g., 44100 Hz)。

*   `uint64_t channel_layout;` / `int channels;`
    *   **作用**: 声道布局和声道数。

*   `data` 和 `linesize` 的解释:
    *   **打包格式 (Packed)**, e.g., `AV_SAMPLE_FMT_S16`: 所有声道的数据交织存储。
        *   `data[0]`: 指向唯一的、交织的数据缓冲区。
        *   `linesize[0]`: 存储整个数据缓冲区的**总大小**（in bytes）。
    *   **平面格式 (Planar)**, e.g., `AV_SAMPLE_FMT_FLTP`: 每个声道的数据分开存储。
        *   `data[0]`: 指向第一个声道的数据。
        *   `data[1]`: 指向第二个声道的数据，以此类推。
        *   `linesize[0]`: 存储**单个声道平面**的大小（in bytes）。

#### 6.3 关键概念：`AVFrame` 是原始数据的直接载体

*   **`AVFrame` 包含的是解码后的数据**：其 `data` 成员指向的内存中，存储的是可以直接用于渲染、播放或进一步处理的原始像素/样本数据。
*   **内存布局复杂性**: 与 `AVPacket` 的单一连续内存块不同，`AVFrame` 的数据可以由多个不连续的内存块（平面）组成，其内存布局由 `format`, `linesize` 等成员共同描述。
*   **生命周期**: `AVFrame` 同样采用引用计数机制进行高效的内存管理，其生命周期由 `av_frame_alloc`/`av_frame_free` 和 `av_frame_ref`/`av_frame_unref` 严格控制。

## 第二章：时间基 (Time Base)

在FFmpeg中，所有的时间计算都构建在一个精确且可靠的基础上，这个基础就是“时间基”（Time Base）。理解时间基是掌握FFmpeg中时间戳处理、音视频同步以及seek操作等高级功能的先决条件。

### 7. 理解时间基 `AVRational`

#### 7.1 什么是时间基

时间基（Time Base）是一个**分数**，它定义了时间戳（Timestamp）整数值的**基本单位**。它回答了这样一个问题：“一个时间戳的数值‘1’，代表了多长的时间？”

例如，一个时间戳`pts = 90000`本身只是一个整数。只有当它关联了一个时间基，比如`time_base = {1, 90000}`，我们才能计算出它所代表的实际时间：

`时间(秒) = pts * time_base = 90000 * (1 / 90000) = 1.0 秒`

时间基就是FFmpeg世界中的“时间刻度”。

#### 7.2 `AVRational`

FFmpeg没有直接使用浮点数（如 `double`）来表示时间，因为浮点数在连续计算中会产生累积的精度误差，这对于需要精确同步的音视频系统是不可接受的。

为了实现无损的时间计算，FFmpeg定义了`AVRational`结构体：

```c
// 定义于 libavutil/rational.h
typedef struct AVRational {
    int num; // Numerator (分子)
    int den; // Denominator (分母)
}
```

- **作用**: 用一个分子和一个分母来精确地表示一个有理数。

- **示例**:

  - 25 FPS的视频帧率，其时间基可表示为 `{1, 25}`。
  - 48 kHz的音频采样率，其时间基可表示为 `{1, 48000}`。
  - MPEG-TS标准中常见的高精度时间基为 `{1, 90000}`。

  通过始终使用整数进行分数运算，FFmpeg保证了所有时间戳转换和计算的最高精度。

#### 7.3 `AV_TIME_BASE`

为了方便在不同的、任意的时间基之间进行转换，FFmpeg定义了一个内部统一的、高精度的全局时间基。

```c
#define AV_TIME_BASE 1000000
#define AV_TIME_BASE_Q (AVRational){1, AV_TIME_BASE}
```

- **`AV_TIME_BASE`**: 其值是1,000,000。
- **`AV_TIME_BASE_Q`**: `AVRational`形式，即 `{1, 1000000}`。
- **含义**: 这个时间基的单位是**微秒 (microsecond)**。
- **作用**: 它常被用作一个可靠的“中间货币”。当需要将时间戳从一个复杂的时间基 `tb_src` 转换到另一个 `tb_dst` 时，一个安全的方式是：
  1. **作为转换的“中间人”**: 它常被用作一个可靠的中间单位。当需要将时间戳从一个复杂的时间基 `tb_src` 转换到另一个 `tb_dst` 时，一个安全的方式是先转换到 `AV_TIME_BASE_Q`，再转到目标。
  2. **作为全局时间的统一单位**: 一些顶层结构体中的时间成员会使用 `AV_TIME_BASE` 作为其单位，以提供一个与具体容器格式无关的、标准化的时间值。最典型的例子就是 **`AVFormatContext->duration`**。FFmpeg在通过`avformat_find_stream_info()`解析媒体文件时，会读取流自身的时长（单位是 `AVStream->time_base`），然后将其**转换**为微秒，并存储在 `AVFormatContext->duration` 中。

### 8. 不同层级的时间基

FFmpeg的设计哲学是分层的，其时间体系也遵循这一原则。在不同的处理层级，存在着不同的时间基，它们各自服务于该层级的特定任务。理解这些时间基的区别与联系，是掌握FFmpeg时间处理的关键。最核心的两个时间基是 `AVStream->time_base` 和 `AVCodecContext->time_base`。

**核心观点：这两个时间基角色不同，职责不同，绝不能混为一谈。**

#### 8.1 `AVStream->time_base`

容器的语言

*   **角色**: 定义了存储在**`AVPacket`**中时间戳（`pts`, `dts`, `duration`）的单位。这是媒体容器格式（如 MP4, MKV）所规定的“官方”时间刻度。
*   **范畴**: 属于**格式层 (libavformat)**。
*   **作用**:
    1.  **[解码]** 当你通过 `av_read_frame()` 从文件中读取一个`AVPacket`时，该`AVPacket`内的时间戳就是以此为单位。
    2.  **[编码]** 当你通过 `av_interleaved_write_frame()` 向文件写入一个`AVPacket`时，该`AVPacket`内的时间戳**必须**被转换成以此为单位。
*   **设置者**:
    *   **[解码]** 由**解封装器 (Demuxer)** 在 `avformat_open_input()` 或 `avformat_find_stream_info()` 期间，从容器的元数据中读取并设置。开发者不应修改它。
    *   **[编码]** 由**封装器 (Muxer)** 在 `avformat_write_header()` 时最终确定。开发者可以建议一个值，但Muxer有最终决定权。

#### 8.2 `AVCodecContext->time_base`

编解码器的工作语言

*   **角色**: 定义了**`AVFrame`**中时间戳（`pts`）的单位。这是编解码器内部工作时使用的“节拍”，通常与帧率或采样率紧密相关。
*   **范畴**: 属于**编码层 (libavcodec)**。
*   **作用与设置者**:
    *   **[编码]** **必须由开发者在 `avcodec_open2()` 之前手动设置**。它明确告知编码器，你接下来提供的`AVFrame`的时间戳是以何种单位计量的。如果不设置，编码器将无法正确计算码率和时间信息，并在编码成 `AVPacket` 后，调用 `av_packet_rescale_ts()` 将其时间基单位转换为 `AVStream->time_base`。
    *   **[解码]** **此字段的直接使用已被废弃**。在现代FFmpeg解码流程中，解码器输出的 `AVFrame` 的 `pts` 通常会**保持其来源流的 `AVStream->time_base`**，以避免不必要的转换。虽然 `AVCodecContext->time_base` 可能会被解码器内部设置为某个值（如帧率的倒数）用于其内部计算，但开发者不应依赖它来解释输出`AVFrame`的时间戳。

#### 8.3 工作流中的交互

##### 解码流程

`AVPacket` (`AVStream->time_base`)  -> [Decoder] -> `AVFrame` (`AVStream->time_base`)

1.  `av_read_frame()` 返回的 `AVPacket`，其 `pts` 单位是 `stream->time_base`。
2.  `avcodec_send_packet()` / `avcodec_receive_frame()` 完成解码。
3.  解码器输出的 `AVFrame`，其 `pts` 单位**仍然是 `stream->time_base`**。整个解码链条上的时间基保持了一致性。

##### 编码流程

`AVFrame` (`AVCodecContext->time_base`) -> [Encoder] -> `AVPacket` (`AVCodecContext->time_base`) -> **[开发者转换]** -> `AVPacket` (`AVStream->time_base`)

1.  开发者创建 `AVFrame`，并根据自己为 `codec_ctx->time_base` 设定的值来填充 `frame->pts`。
2.  `avcodec_send_frame()` / `avcodec_receive_packet()` 完成编码。
3.  编码器输出的 `AVPacket`，其 `pts` 单位**继承自 `codec_ctx->time_base`**。
4.  **关键步骤**: 在调用 `av_interleaved_write_frame()` 之前，开发者**必须**使用 `av_packet_rescale_ts()` 将 `packet` 的时间戳从 `codec_ctx->time_base` 转换到 `stream->time_base`。

#### 8.4 总结表格

| 特性             | `AVStream->time_base` | `AVCodecContext->time_base`    |
| :--------------- | :-------------------- | :----------------------------- |
| **角色**         | 容器的“官方”时间刻度  | 编解码器的工作“节拍器”         |
| **范畴**         | 格式层 (libavformat)  | 编码层 (libavcodec)            |
| **作用对象**     | `AVPacket`            | `AVFrame` (主要在编码输入时)   |
| **设置者(解码)** | Demuxer (libavformat) | Decoder (libavcodec, 内部使用) |
| **设置者(编码)** | Muxer (libavformat)   | **开发者 (User)**              |

### 9. 时间基转换核心API

在FFmpeg中，由于不同层级和不同格式可能使用各自独立的时间基，因此在这些时间基之间进行精确、无损的时间值转换，是进行同步和处理的日常操作。`libavutil` 模块提供了一套强大的API来完成这些任务。

#### 9.1 核心转换函数

这是FFmpeg时间戳转换的基石，几乎所有时间转换都依赖于此。

```c
int64_t av_rescale_q(int64_t a, AVRational bq, AVRational cq);
int64_t av_rescale_q_rnd(int64_t a, AVRational bq, AVRational cq, enum AVRounding rnd);
```

- **作用**: 安全地计算 `a * (bq / cq)` 的值。它内部使用64位整数运算来避免溢出和精度损失，比直接使用浮点数计算 `a * (double)bq.num / bq.den / ((double)cq.num / cq.den)` 要精确得多。

- **参数**:

  - `a`: 需要被转换的时间戳或时长值。
  - `bq`: **源时间基 (Source Time Base)**，即 `a` 的单位。
  - `cq`: **目标时间基 (Destination Time Base)**，即希望得到的单位。
  - **`_rnd` 版本**: 允许指定取整策略 (`enum AVRounding`)，如 `AV_ROUND_NEAR_INF` (四舍五入) 或 `AV_ROUND_DOWN` (向下取整)。这在需要精确控制舍入行为的场景（如帧率转换）中非常重要。

- **使用示例：计算视频帧的显示时间（秒）**

  ```c
  AVStream *stream = format_ctx->streams[video_stream_index];
  AVFrame *frame = ...; // 解码得到的帧
  // frame->pts 的单位是 stream->time_base
  // 目标单位是秒，即时间基 {1, 1}
  // 为了更高精度，我们先转到微秒 AV_TIME_BASE_Q = {1, 1000000}，这是一个整数->整数的转换
  int64_t pts_in_us = av_rescale_q(frame->pts, stream->time_base, AV_TIME_BASE_Q);
  // 将微秒值（整数）转换为秒（浮点数），应直接使用除法，防止小数截断
  double pts_in_seconds = (double)pts_in_us / AV_TIME_BASE;
  ```

#### 9.2 `AVPacket`的便捷转换工具

这是一个专门为`AVPacket`设计的便捷API，它封装了`av_rescale_q`，一次性转换包内所有的时间相关成员。

```c
void av_packet_rescale_ts(AVPacket *pkt, AVRational tb_src, AVRational tb_dst);
```

- **作用**: 将 `pkt` 内部的 `pts`, `dts`, `duration` 三个成员的值，从源时间基 `tb_src` 转换到目标时间基 `tb_dst`。
- **典型应用**: 在编码流程中，将从编码器（`avcodec_receive_packet`）得到的、以`AVCodecContext->time_base`为单位的`AVPacket`，转换为封装器（`av_interleaved_write_frame`）所要求的、以`AVStream->time_base`为单位的`AVPacket`。

#### 9.3 安全的时间戳比较

直接比较两个具有不同时间基的时间戳整数是无意义的。此API提供了安全的比较方法。

```c
int av_compare_ts(int64_t ts_a, AVRational tb_a, int64_t ts_b, AVRational tb_b);
```

-  **作用**: 比较 `ts_a` (单位 `tb_a`) 和 `ts_b` (单位 `tb_b`) 的早晚。它内部会将两者转换到一个公共的高精度时间基上再进行整数比较。
- **返回值**:
  - `< 0` : 如果 `ts_a` 早于 `ts_b`。
  - `== 0` : 如果 `ts_a` 等于 `ts_b`。
  - `> 0` : 如果 `ts_a` 晚于 `ts_b`。

#### 9.4 分数到浮点数的转换

```c
double av_q2d(AVRational a);
```

- **作用**: 将一个`AVRational`分数转换成`double`浮点数。

- **使用场景**: **主要用于调试、日志打印或最终的用户界面显示**。在FFmpeg内部的核心计算逻辑中，应**避免**使用此函数，以防止引入浮点精度问题。

- **示例：**

  ```c
  printf("Stream time_base: %d/%d (%f seconds per unit)\n",
         stream->time_base.num,
         stream->time_base.den,
         av_q2d(stream->time_base));
  ```

## 第三章：引用计数

FFmpeg作为一个高性能的多媒体处理库，其内存管理机制是其设计的精髓之一。为了在处理庞大的视频和音频数据时避免不必要的内存拷贝，FFmpeg广泛采用了**引用计数（Reference Counting）**技术。理解这一机制，对于编写高效且无内存泄漏的FFmpeg应用程序至关重要。

### 10. `AVPacket` vs `AVFrame` 内存管理异同

`AVPacket`（压缩数据）和`AVFrame`（原始数据）都采用了引用计数来管理其内部的数据缓冲区，它们遵循相同的核心思想，但因其数据结构的差异而在实现细节上有所不同。

#### 10.1 核心思想

核心思想：分离结构体与数据，共享数据缓冲区

两者内存管理的核心思想是一致的：

1.  **分离管理**: 将**结构体本身**（一个几十字节的小对象）的内存与**数据缓冲区**（一块可能非常大的内存区域）的内存分开管理。
2.  **共享数据**: 通过一个名为`AVBufferRef`的内部结构来包裹真正的数据缓冲区。`AVBufferRef`中包含一个引用计数值和一个指向数据的指针。
3.  **避免拷贝**: 当需要复制`AVPacket`或`AVFrame`时，程序只需复制其结构体成员，并增加其引用的`AVBufferRef`的计数值即可，而无需拷贝庞大的数据缓冲区本身。只有当最后一个引用被释放时，数据缓冲区才会被真正销毁。

这个机制是`_ref()`和`_unref()`系列API能够高效工作的根本原因。

#### 10.2 模型差异

两者最根本的差异源于它们所管理的数据形态不同：

*   `AVPacket`的数据是一块**单一、连续的内存块**。
*   `AVFrame`的数据是**多个、可能不连续的内存平面（planes）**。

这个差异导致了它们在引用计数模型上的不同实现：

##### `AVPacket`: 单缓冲区模型

`AVPacket`的管理模型非常直接，它只有一个`AVBufferRef`成员：

*   `AVBufferRef *buf;`

这个**唯一的`buf`**负责管理`AVPacket->data`所指向的**那一整块**压缩数据缓冲区。一个`AVPacket`在任何时候都只引用一个数据缓冲区。

##### `AVFrame`: 多缓冲区模型

`AVFrame`为了支持复杂的数据布局（如视频的YUV平面或音频的多声道平面），采用了更灵活的模型，它拥有一个`AVBufferRef`**数组**：

*   `AVBufferRef *buf[AV_NUM_DATA_POINTERS];`

这意味着一个`AVFrame`可以**同时管理和引用多个独立的数据缓冲区**。
*   **对于平面视频（Planar Video）**: 通常，解码器会分配一大块连续内存，然后让`frame->data[0]` (Y), `frame->data[1]` (U), `frame->data[2]` (V) 指向这块大内存的不同偏移位置。此时，只有`frame->buf[0]`会指向管理这整块内存的`AVBufferRef`，其余`buf`成员为NULL。但在某些情况下，Y,U,V平面也可能被分配在三块独立的内存中，此时`buf[0]`, `buf[1]`, `buf[2]`会分别指向三个不同的`AVBufferRef`。
*   **对于平面音频（Planar Audio）**: 每个声道的数据都可能存储在独立的内存中。一个8声道的平面音频帧，其`buf[0]`到`buf[7]`就可能指向8个不同的`AVBufferRef`。

#### 10.3 API行为差异

这个模型上的差异直接影响了相关API的行为：

*   `av_packet_ref(dst, src)` / `av_packet_unref(pkt)`:
    这些函数的操作对象是`AVPacket`中**唯一的`buf`成员**。它们增加或减少该`AVBufferRef`的引用计数。

*   `av_frame_ref(dst, src)` / `av_frame_unref(frame)`:
    这些函数会**遍历`AVFrame`的`buf`数组**，对其中**每一个非NULL**的`AVBufferRef`执行增加或减少引用计数的操作。

**总结表格**

| 特性                  | AVPacket                   | AVFrame                           |
| :-------------------- | :------------------------- | :-------------------------------- |
| **数据形态**          | 单一、连续的压缩数据块     | 多个、可能不连续的原始数据平面    |
| **`AVBufferRef`成员** | `AVBufferRef *buf;` (一个) | `AVBufferRef *buf[8];` (一个数组) |
| **管理模型**          | **单缓冲区模型**           | **多缓冲区模型**                  |
| **API行为**           | 操作单个`buf`              | 遍历并操作`buf`数组               |

### 11. 生命周期辨析

在FFmpeg的内存管理中，`_alloc`/`_free` 和 `_ref`/`_unref` 这两对API分别管理着不同对象的生命周期。清晰地辨析它们的职责，是避免内存泄漏和悬挂指针问题的基础。

#### 11.1 操作对象的区别

这两组API的核心区别在于它们操作的目标完全不同：

*   **`_alloc` / `_free` 系列：管理【结构体本身】的生命周期**
    *   **API**: `av_packet_alloc()`, `av_packet_free()`, `av_frame_alloc()`, `av_frame_free()`
    *   **作用对象**: `AVPacket` 或 `AVFrame` 这些**结构体自身**所占用的（通常是几十字节的）小块内存。
    *   **行为**:
        *   `_alloc()`: 在堆上 `malloc` 一块内存用于存放结构体，并将其所有字段初始化为默认/零值。
        *   `_free()`: 释放结构体本身的内存。在释放前，它会**自动调用一次 `_unref()`** 来确保其引用的数据缓冲区被正确处理。

*   **`_ref` / `_unref` 系列：管理【数据缓冲区】的生命周期**
    *   **API**: `av_packet_ref()`, `av_packet_unref()`, `av_frame_ref()`, `av_frame_unref()`
    *   **作用对象**: `AVPacket` 或 `AVFrame` 内部 `data` 指针所指向的、由 `AVBufferRef` 管理的**数据缓冲区**（可能非常大）。
    *   **行为**:
        *   `_ref(dst, src)`: 增加 `src` 数据缓冲区的引用计数，并让 `dst` 也指向它。
        *   `_unref(pkt)`: 减少数据缓冲区的引用计数。
            *   如果计数**大于0**，数据缓冲区**保持不变**。
            *   如果计数**等于0**，数据缓冲区被**释放**。
            *   同时，`_unref` 会将结构体的所有字段**重置为初始状态**，使其可以被安全地复用，但**不会释放结构体本身**。

**一句话总结：`_alloc`/`_free` 是创建和销毁“容器”，而 `_ref`/`_unref` 是管理“容器”里的“内容物”。**

#### 11.2 典型使用场景分析

##### 场景一：解码循环

```c
// 1. 初始化阶段: 分配“容器”
AVPacket *pkt = av_packet_alloc();
AVFrame *frame = av_frame_alloc();
// ... 错误检查 ...

// ... 打开文件和解码器 ...

// 2. 主处理循环
while (av_read_frame(format_ctx, pkt) == 0) {
    if (avcodec_send_packet(codec_ctx, pkt) < 0) { /* ... */ }

    // (A) AVPacket 内存管理:
    // 必须调用unref清空pkt，以便在下一次av_read_frame中复用。
    // 这会释放pkt对数据缓冲区的引用。如果解码器内部ref了该pkt，
    // 缓冲区不会被销毁；否则，它会被释放。
    av_packet_unref(pkt);

    while (avcodec_receive_frame(codec_ctx, frame) == 0) {
        // (B) AVFrame 内存管理:
        // 此处不需要调用av_frame_unref(frame)。
        // avcodec_receive_frame API保证在填充frame前会先调用unref。
        // 它自动处理了frame的复用和旧数据缓冲区的释放。
        
        // ... 使用 frame (渲染、分析等) ...
    }
}

// 3. 清理阶段: 销毁“容器”
av_packet_free(&pkt);
av_frame_free(&frame);
// ... 关闭解码器和文件 ...
```

**解码时如何防止内存泄漏**:

- **AVPacket**: 关键在于循环中**每次使用后都调用 av_packet_unref(pkt)**。这确保了av_read_frame在每次迭代中分配的数据缓冲区，其引用计数最终能被正确减少。
- **AVFrame**: 关键在于**信任avcodec_receive_frame**，它已经为你处理了unref。如果你在这里画蛇添足地再次调用unref，反而可能会过早地释放掉你正在使用的帧数据。
- **结构体本身**: 循环结束后，**必须调用 av_packet_free() 和 av_frame_free()** 来释放最初_alloc的结构体内存。

##### 场景二：编码循环

```c++
// 1. 初始化阶段: 分配“容器”
AVPacket *pkt = av_packet_alloc();
AVFrame *frame = av_frame_alloc();
// ... 错误检查 ...

// ... 初始化编码器 ...

// 为frame设置固定属性并分配一次初始缓冲区
frame->format = codec_ctx->pix_fmt;
frame->width  = codec_ctx->width;
frame->height = codec_ctx->height;
if (av_frame_get_buffer(frame, 0) < 0) { /* 错误处理 */ }

// 2. 主处理循环
for (int i = 0; i < num_frames_to_encode; ++i) {
    // (A) AVFrame 内存管理:
    // 在写入新数据前，确保缓冲区是可写的。
    // 如果编码器仍在使用上一个frame的数据(ref_count > 1)，此时说明上一个frame可能是B帧
    // 此函数会自动分配新缓冲区，避免数据竞争。
    if (av_frame_make_writable(frame) < 0) { break; }

    // 现在可以安全地复写frame的数据区
    fill_yuv_data_into_frame_buffers(frame);
    frame->pts = i;

    if (avcodec_send_frame(codec_ctx, frame) < 0) { /* ... */ }

    while (avcodec_receive_packet(codec_ctx, pkt) == 0) {
        // (B) AVPacket 内存管理:
        // 与解码时一样，处理完pkt后必须unref，以备复用。
        av_packet_unref(pkt);
    }
}
// ... 冲刷编码器 ...

// 3. 清理阶段: 销毁“容器”
av_packet_free(&pkt);
av_frame_free(&frame);
// ... 关闭编码器和文件 ...
```

**编码时如何防止内存泄漏**:

- **AVFrame**: 关键在于**发送前调用 av_frame_make_writable(frame)**。这个函数是防止数据竞争和在必要时（而不是每次都）重新分配缓冲区的“智能”方式。如果你不使用它，而选择每次都unref+get_buffer，虽然安全但效率较低。如果你直接复写而不做任何处理，则会产生bug。
- **AVPacket**: 与解码时一样，**循环中每次使用后都调用 av_packet_unref(pkt)**。
- **结构体本身**: 循环结束后，**必须调用 av_packet_free() 和 av_frame_free()**。

## 第四章：核心工作流与API实践

掌握了核心的数据结构和时间体系后，本章的目标是将这些知识串联起来，形成完整的处理流程。首先，我们将解构FFmpeg的API命名规律和分类方法，这能极大地提升您阅读和理解代码的速度。

### 13. API命名规律与分类

FFmpeg的API设计非常规范和系统化，一旦掌握其内在逻辑，即使是初次遇到的函数，也能快速推断出其作用和归属。

#### 13.1 API命名规律

FFmpeg的函数命名普遍遵循一个清晰的模式：`av[模块]_[名词]_[动词]`

1.  **统一前缀 `av`**:
    *   所有公开API都以`av`开头。

2.  **模块 (Module) 标识**:
    *   `avformat_...`: 属于 **libavformat** 模块，负责格式（解）封装，主要操作 `AVFormatContext`, `AVStream`。
    *   `avcodec_...`: 属于 **libavcodec** 模块，负责编解码，主要操作 `AVCodecContext`, `AVPacket`, `AVFrame`。
    *   `avio_...`: 属于 **libavio** 模块，负责底层I/O。
    *   `avfilter_...`: 属于 **libavfilter** 模块，负责滤镜处理。
    *   `sws_...` / `swr_...`: 分别属于 **libswscale** (图像转换) 和 **libswresample** (音频重采样)。
    *   `avutil_` 或 `av_`: 属于 **libavutil** 模块，提供通用的工具函数（如内存、字典、日志、数学运算等）。

3.  **操作对象 (名词)**:
    *   函数名通常包含其主要操作的结构体名称（去掉`AV`前缀），如 `packet`, `frame`, `format_context`。

4.  **执行动作 (动词)**:
    *   `_alloc()`: 分配结构体内存。
    *   `_free()`: 释放结构体内存。
    *   `_init()`: 初始化（通常被更具体的函数替代）。
    *   `_open()` / `_close()`: 打开/关闭一个组件或文件，涉及复杂的状态初始化或清理。
    *   `_find_...()`: 查找某个组件，如 `avcodec_find_decoder`。
    *   `_send_...()` / `_receive_...()`: 新的异步API风格，表示数据流动方向。
    *   `_copy()` / `_clone()`: 复制数据或结构。
    *   `_from_...()` / `_to_...()`: 在不同结构体之间转换/拷贝参数，如 `avcodec_parameters_from_context`。
    *   `_by_...`: 表示查找依据，如 `_by_name`。

5.  **版本后缀**:
    *   函数名结尾的数字（如 `avcodec_open2`, `avcodec_alloc_context3`）表示API的演进版本。**始终应该使用数字最大的那个版本**，因为它通常设计更优、功能更强。

#### 13.2 API分类

我们可以将庞大的API集合按照其在工作流中的作用，划分为以下五类：

1.  **资源管理类 (Resource Management)**
    *   **职责**: 负责核心结构体的内存分配、释放和引用计数。
    *   **示例**: `avformat_alloc_context()`, `avcodec_free_context()`, `av_frame_ref()`, `av_packet_unref()`。

2.  **初始化与准备类 (Initialization & Setup)**
    *   **职责**: 将分配好的结构体从“空壳”状态转变为“准备就绪”状态。这是工作流的入口，通常是复杂且关键的步骤。
    *   **示例**: `avformat_open_input()`, `avformat_find_stream_info()`, `avcodec_parameters_to_context()`, `avcodec_open2()`, `avformat_write_header()`。

3.  **数据流处理类 (Data Flow Processing)**
    *   **职责**: 在处理循环中被反复调用，负责在流水线的各个环节之间传递数据。
    *   **示例**: `av_read_frame()`, `avcodec_send_packet()`, `avcodec_receive_frame()`, `avcodec_send_frame()`, `avcodec_receive_packet()`, `av_interleaved_write_frame()`。

4.  **控制与动作类 (Control & Action)**
    *   **职责**: 执行特定操作，如跳转或刷新，改变处理流程。
    *   **示例**: `av_seek_frame()`, `avcodec_flush_buffers()`。

5.  **查询与工具类 (Query & Utility)**
    *   **职责**: 获取信息或执行辅助性转换，通常不改变核心工作流的状态。
    *   **示例**: `av_dump_format()`, `av_get_profile_name()`, `av_dict_set()`, `av_rescale_q()`。

### 14. 解码工作流 (视频为例)

本节将以一个典型的视频解码流程为例，将之前介绍的核心结构体、API和概念串联起来，形成一个完整、可执行的步骤指南。

#### 14.1 准备阶段

这是解码前的所有设置工作，目标是创建一个完全配置好、准备接收数据的解码器。

1.  **分配 `AVFormatContext`**
    *   **API**: `avformat_alloc_context()`
    *   **作用**: 创建一个 `AVFormatContext` 结构体，作为后续所有文件操作的上下文。

2.  **打开输入文件**
    *   **API**: `avformat_open_input()`
    *   **作用**: 打开指定的媒体文件（本地或URL），探测其容器格式，读取文件头，并填充 `AVFormatContext` 的基本信息（如 `iformat`, `nb_streams`）。它会为每个流创建 `AVStream` 结构体。

3.  **查找流信息**
    *   **API**: `avformat_find_stream_info()`
    *   **作用**: 读取媒体文件的一小部分数据包，以获取更详细的流信息，并填充到每个 `AVStream` 的 `codecpar` 成员中（如视频的宽高、像素格式，音频的采样率等）。对于没有文件头的格式（如MPEG-TS），此步骤至关重要。

4.  **查找并选择视频流与解码器**
    *   **API**: `av_find_best_stream()` (推荐) 或手动遍历 `AVFormatContext->streams`
    *   **作用**: 在所有流中找到所需的视频流（`AVMEDIA_TYPE_VIDEO`），并获取其索引 `video_stream_index`。
    *   **API**: `avcodec_find_decoder()`
    *   **作用**: 根据视频流的 `codecpar->codec_id` 找到对应的解码器 `AVCodec`。

5.  **创建并配置解码器上下文**
    *   **API**: `avcodec_alloc_context3()`
    *   **作用**: 为找到的解码器 `AVCodec` 分配一个 `AVCodecContext` 结构体。
    *   **API**: `avcodec_parameters_to_context()`
    *   **作用**: **关键步骤**。将视频流 `AVStream->codecpar` 中的参数，完整地拷贝到新分配的 `AVCodecContext` 中。

6.  **打开解码器**
    *   **API**: `avcodec_open2()`
    *   **作用**: 使用配置好的 `AVCodecContext` 和找到的 `AVCodec`，真正初始化解码器。此步骤会分配内部缓冲区并使解码器进入可工作状态。

#### 14.2 循环处理阶段

这是解码的核心循环，不断地从文件读取压缩数据并送入解码器，然后从解码器获取原始数据。

1.  **分配 `AVPacket` 和 `AVFrame`**
    *   **API**: `av_packet_alloc()`, `av_frame_alloc()`
    *   **作用**: 在循环开始前，创建可复用的 `AVPacket` 和 `AVFrame` 对象。

2.  **读取数据包**
    *   **API**: `av_read_frame()`
    *   **作用**: 在 `while` 循环中调用。从输入文件中读取一个数据包，并将其内容填充到 `AVPacket` 中。函数会自动处理不同流的数据。你需要检查 `packet->stream_index` 是否等于你之前找到的 `video_stream_index`。

3.  **发送数据包到解码器**
    *   **API**: `avcodec_send_packet()`
    *   **作用**: 将属于视频流的 `AVPacket` 发送给解码器。这是一个异步操作，函数会立即返回。解码器可能会缓存几个包后再开始输出。
    *   **注意**: 每次发送后，应调用 `av_packet_unref()` 来清空`AVPacket`，以便复用。

4.  **从解码器接收帧**
    *   **API**: `avcodec_receive_frame()`
    *   **作用**: 在一个内部 `while` 循环中调用。尝试从解码器获取一个解码完成的 `AVFrame`。
    *   **返回值处理**:
        *   返回 `0` (成功): 意味着成功获取一帧，你可以处理这个 `frame`（如渲染、保存等）。
        *   返回 `AVERROR(EAGAIN)`: 意味着解码器需要更多的数据包才能输出下一帧。你应该 `break` 这个内部循环，去外层调用 `av_read_frame()` 读取下一个包。
        *   返回 `AVERROR_EOF`: 意味着码流已经结束，解码器中所有缓存的帧都已被取出。

#### 14.3 收尾阶段

1.  **冲刷（Flush）解码器**
    *   **API**: `avcodec_send_packet(codec_ctx, NULL);`
    *   **作用**: 在主读取循环结束后，向解码器发送一个`NULL`的`packet`。这会告知解码器码流已经结束。
    *   之后，继续在一个 `while` 循环中调用 `avcodec_receive_frame()`，直到它返回 `AVERROR_EOF`，以确保取出解码器中所有缓存的帧。

2.  **释放资源**
    *   **API**:
        *   `avcodec_free_context()`
        *   `avformat_close_input()` (注意它会释放`AVFormatContext`本身)
        *   `av_packet_free()`
        *   `av_frame_free()`
    *   **作用**: 按照与分配相反的顺序，释放所有分配的结构体和上下文，防止内存泄漏。

### 15. 编码工作流 (视频为例)

本节将详细描述一个典型的视频编码与封装流程。与解码相反，这个流程从原始数据（`AVFrame`）开始，最终生成一个包含压缩数据（`AVPacket`）的媒体文件。

#### 15.1 准备阶段

这是编码开始前的所有设置工作，目标是创建一个完全配置好的编码器和封装器。

1.  **创建输出上下文**
    *   **API**: `avformat_alloc_output_context2()`
    *   **作用**: 根据指定的格式短名称（如 "mp4"）或文件名后缀，创建一个 `AVFormatContext` 并为其关联合适的封装器（`oformat`）。

2.  **查找编码器**
    *   **API**: `avcodec_find_encoder()` 或 `avcodec_find_encoder_by_name()`
    *   **作用**: 根据 `AVCodecID` (如 `AV_CODEC_ID_H264`) 或名称 (如 "libx264") 找到一个 `AVCodec` 编码器。

3.  **添加新流到输出上下文**
    *   **API**: `avformat_new_stream()`
    *   **作用**: 在 `AVFormatContext` 中创建一个新的 `AVStream` 来承载即将编码的视频数据。

4.  **创建并配置编码器上下文**
    *   **API**: `avcodec_alloc_context3()`
    *   **作用**: 为找到的编码器 `AVCodec` 分配一个 `AVCodecContext` 结构体。
    *   **手动设置参数**: 这是**编码流程的关键**。你必须**手动**为 `AVCodecContext` 的成员赋值，以定义编码输出的属性，例如：
        *   `height`, `width`: 视频分辨率
        *   `pix_fmt`: 输入的原始视频像素格式
        *   `bit_rate`: 目标码率
        *   `time_base`: **必须设置**，定义了你将要传入的 `AVFrame` 的PTS单位，通常与帧率相关（如 `{1, 25}`）。
        *   `gop_size`, `max_b_frames`: GOP大小和B帧数量等编码参数。
    *   **全局头**: 如果封装格式需要（如MP4），设置 `AVCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER`。

5.  **打开编码器**
    *   **API**: `avcodec_open2()`
    *   **作用**: 使用配置好的 `AVCodecContext` 和 `AVCodec`，初始化编码器。

6.  **将编码器参数拷贝到流**
    *   **API**: `avcodec_parameters_from_context()`
    *   **作用**: **关键步骤**。将 `avcodec_open2()` 后可能被编码器调整或填充的最终参数，从 `AVCodecContext` 拷贝回 `AVStream->codecpar`。这是为了让封装器知道如何写入正确的流信息。

7.  **打开输出文件IO**
    *   **API**: `avio_open()`
    *   **作用**: 除非是 `AVFMT_NOFILE` 格式，否则需要为 `AVFormatContext->pb` 打开一个可写的 `AVIOContext`。

8.  **写入文件头**
    *   **API**: `avformat_write_header()`
    *   **作用**: 将容器的头部信息和所有流的元数据（来自`AVStream->codecpar`）写入到输出文件中。

#### 15.2 循环处理阶段

这是编码的核心循环，不断地生成原始数据帧，送入编码器，然后从编码器获取压缩数据包并写入文件。

1.  **创建并分配 `AVFrame`**
    *   **API**: `av_frame_alloc()`, `av_frame_get_buffer()`
    *   **作用**: 在循环开始前，创建一个可复用的 `AVFrame`，并为其设置好格式、宽高，然后分配数据缓冲区。

2.  **创建并填充原始数据帧**
    *   **作用**: 在 `for` 或 `while` 循环中，生成或读取一帧原始视频数据（如YUV），并填充到 `AVFrame` 的 `data` 数组中。
    *   **设置PTS**: 为每一帧设置一个递增的 `pts`，其单位必须是你之前为 `AVCodecContext->time_base` 设定的值。

3.  **发送帧到编码器**
    *   **API**: `avcodec_send_frame()`
    *   **作用**: 将填充好的 `AVFrame` 发送给编码器。这是一个异步操作。
    *   **注意**: 编码器可能会持有该帧的引用，因此在复用 `AVFrame` 前，必须确保其数据缓冲区是可写的（例如，使用 `av_frame_make_writable()`）。

4.  **从编码器接收数据包**
    *   **API**: `avcodec_receive_packet()`
    *   **作用**: 在一个内部 `while` 循环中调用。尝试从编码器获取一个编码完成的 `AVPacket`。由于B帧和内部缓存，一次 `send_frame` 可能不会立即产生一个 `packet`。
    *   **返回值处理**: 与解码类似，处理 `0`, `AVERROR(EAGAIN)`, `AVERROR_EOF`。

5.  **转换时间基并写入文件**
    *   **API**: `av_packet_rescale_ts()`
    *   **作用**: **关键步骤**。`avcodec_receive_packet()` 返回的 `AVPacket`，其时间戳单位是 `AVCodecContext->time_base`。必须将其转换为封装器要求的 `AVStream->time_base`。
    *   **API**: `av_interleaved_write_frame()`
    *   **作用**: 将转换好时间基的 `AVPacket` 写入文件，该函数会处理数据包的交织，以确保音视频同步。

6.  **释放 `AVPacket`**
    *   **API**: `av_packet_unref()`
    *   **作用**: 每次写入文件后，清空 `AVPacket` 以便在下一次 `avcodec_receive_packet()` 调用中复用。

#### 15.3 收尾阶段

1.  **冲刷（Flush）编码器**
    *   **API**: `avcodec_send_frame(codec_ctx, NULL);`
    *   **作用**: 在主循环结束后，向编码器发送一个`NULL`的`frame`，告知其数据已全部发送完毕。
    *   之后，继续在一个 `while` 循环中调用 `avcodec_receive_packet()`，直到它返回 `AVERROR_EOF`，以取出编码器中所有缓存的数据包。

2.  **写入文件尾**
    *   **API**: `av_write_trailer()`
    *   **作用**: 写入容器的尾部信息（如索引、时长等），并最终完成文件封装。

3.  **释放资源**
    *   **API**:
        *   `avcodec_free_context()`
        *   `avio_closep(&format_ctx->pb)`
        *   `avformat_free_context()`
        *   `av_packet_free()`
        *   `av_frame_free()`
    *   **作用**: 按照与分配相反的顺序，释放所有资源。

### 16. 音频流与视频流编解码的差异

FFmpeg的API设计提供了一套统一的、与媒体类型无关的宏观工作流。无论是解码还是编码，其API调用顺序（如`send/receive`模型）对于音频和视频是完全一致的。

然而，在微观层面，即具体的参数设置、数据处理和时间戳计算上，两者存在本质的区别。理解这些差异是正确处理不同媒体类型的关键。

#### 16.1 参数设置差异

在配置`AVCodecContext`和`AVFrame`时，两者的关注点完全不同。

| 参数                      | 视频 (Video)                              | 音频 (Audio)                                                 |
| :------------------------ | :---------------------------------------- | :----------------------------------------------------------- |
| **`codec_id`**            | e.g., `AV_CODEC_ID_H264`                  | e.g., `AV_CODEC_ID_AAC`                                      |
| **`width`, `height`**     | **核心参数** (e.g., 1920, 1080)           | **无意义**                                                   |
| **`pix_fmt`**             | **核心参数** (e.g., `AV_PIX_FMT_YUV420P`) | **无意义**                                                   |
| **`sample_rate`**         | **无意义**                                | **核心参数** (e.g., 44100, 48000)                            |
| **`sample_fmt`**          | **无意义**                                | **核心参数** (e.g., `AV_SAMPLE_FMT_FLTP`)                    |
| **`channel_layout`**      | **无意义**                                | **核心参数** (e.g., `AV_CH_LAYOUT_STEREO`)                   |
| **`time_base` (编码时)**  | 通常与**帧率**相关 (e.g., `{1, 25}`)      | 通常与**采样率**相关 (e.g., `{1, 48000}`)                    |
| **`frame_size` (编码时)** | 无意义 (视频帧大小由图像决定)             | **重要**，对于固定帧大小的编码器（如AAC），必须设置（e.g., 1024） |

#### 16.2 `AVFrame` 数据填充与理解差异

这是两者在实践中最显著的区别。

*   **视频 `AVFrame`**:
    *   你关心的是 `width`, `height`。
    *   你需要填充的是 `data[0]`, `data[1]`, `data[2]`... 指向的**Y, U, V 等图像平面**。
    *   你需要正确处理 `linesize`（步长）来进行内存访问。
    *   一个`AVFrame`代表一帧**图像**。

*   **音频 `AVFrame`**:
    *   你关心的是 `nb_samples`, `sample_rate`, `channel_layout`。
    *   你需要根据 `sample_fmt` 是**打包(Packed)还是平面(Planar)**格式，来正确地填充 `data` 数组。
    *   一个`AVFrame`代表一个包含 `nb_samples` 个采样点的**音频块**。

#### 16.3 PTS 计算差异 (编码时)

由于时间和数据量的基本单位不同，两者PTS的计算方式也不同。

*   **视频PTS**: 通常是基于**帧计数器**生成。PTS的增量是固定的（对于恒定帧率视频）。
    
    ```c
    // 假设 AVCodecContext->time_base = {1, 25} (25 FPS)
    frame->pts = frame_count; // PTS 序列: 0, 1, 2, 3, ...
    frame_count++;
    ```
    
*   **音频PTS**: 必须基于**已发送的样本总数**生成。PTS的增量等于每帧的样本数`nb_samples`。
    ```c
    // 假设 AVCodecContext->time_base = {1, 48000} (48kHz)
    // 且每帧 frame->nb_samples = 1024
    frame->pts = total_samples_sent; // PTS 序列: 0, 1024, 2048, 3072, ...
    total_samples_sent += frame->nb_samples;
    ```
    这种基于样本的精确计算是保证音视频同步的关键。

**总结**:

尽管FFmpeg提供了统一的API调用流程，但开发者必须根据所处理的媒体类型，在**参数配置**、**`AVFrame`数据操作**和**时间戳生成**这三个核心方面，采用完全不同的逻辑和方法。

## 附录

### A. 常用工具函数与调试技巧

本附录介绍了一些在FFmpeg开发过程中极其有用的工具函数。熟练使用它们，可以极大地提升开发和调试效率。

#### A.1 `av_dump_format()`

*   **作用**:
    打印一个`AVFormatContext`中包含的详细信息，是调试媒体文件的首选工具。其输出内容与命令行工具 `ffmpeg -i <filename>` 的信息高度相似，包括容器格式、时长、码率、所有流的详细参数（编码、分辨率、帧率、采样率等）以及元数据。

*   **函数原型**:
    ```c
    void av_dump_format(AVFormatContext *ic,
                        int index,
                        const char *url,
                        int is_output);
    ```

*   **参数说明**:
    *   `ic`: 需要打印信息的`AVFormatContext`。
    *   `index`: 通常与多文件处理相关，对于单个文件，直接传入`0`即可。
    *   `url`: 将要被一同打印的文件名或URL字符串。
    *   `is_output`: 标志位。`0`表示这是一个输入上下文（demuxing），`1`表示这是一个输出上下文（muxing）。

*   **使用示例**:
    在成功打开一个输入文件并找到流信息后，调用此函数可以验证所有参数是否被正确解析。
    ```c
    AVFormatContext *format_ctx = NULL;
    if (avformat_open_input(&format_ctx, "input.mp4", NULL, NULL) < 0) { /* ... */ }
    if (avformat_find_stream_info(format_ctx, NULL) < 0) { /* ... */ }
    
    // 在控制台打印 "input.mp4" 的所有格式和流信息
    av_dump_format(format_ctx, 0, "input.mp4", 0); 
    ```

#### A.2 `av_strerror()`

*   **作用**:
    将FFmpeg API返回的负数错误码转换为人类可读的错误信息字符串。这是进行错误处理和定位问题的必备函数。

*   **函数原型**:
    ```c
    int av_strerror(int errnum, char *errbuf, size_t errbuf_size);
    ```

*   **参数说明**:
    *   `errnum`: FFmpeg函数返回的错误码。
    *   `errbuf`: 用于存储错误字符串的缓冲区。
    *   `errbuf_size`: 缓冲区的大小。

*   **使用示例**:
    当一个FFmpeg函数调用失败时，使用此函数来获取详细的错误描述。
    ```c
    int ret = avformat_open_input(&format_ctx, "non_existent_file.mp4", NULL, NULL);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        fprintf(stderr, "Could not open input file: %s\n", err_buf);
        return ret;
    }
    ```

#### A.3 `av_log_set_level()`

*   **作用**:
    设置FFmpeg内部日志的输出级别。在调试时，将其设置为`AV_LOG_DEBUG`或`AV_LOG_VERBOSE`，可以看到FFmpeg在执行每一步操作时详细的内部状态和决策过程，对于解决棘手问题非常有帮助。

*   **函数原型**:
    ```c
    void av_log_set_level(int level);
    ```

*   **参数说明**:
    *   `level`: 日志级别常量，常用的有：
        *   `AV_LOG_QUIET`: 关闭所有日志输出。
        *   `AV_LOG_ERROR`: 只显示致命错误。
        *   `AV_LOG_WARNING`: 显示错误和警告。
        *   `AV_LOG_INFO`: 显示标准信息（默认级别）。
        *   `AV_LOG_VERBOSE`: 显示更详细的信息。
        *   `AV_LOG_DEBUG`: 显示所有调试信息，信息量最大。

*   **使用示例**:
    在程序初始化时设置，以开启详细的调试日志。
    ```c
    int main() {
        // 在调用任何FFmpeg函数之前，设置日志级别
        av_log_set_level(AV_LOG_DEBUG);
        
        av_log(NULL, AV_LOG_INFO, "FFmpeg log level is set to debug.\n");
        
        // ... 你的FFmpeg代码 ...
        return 0;
    }
    ```

### B. 关键概念词汇表

本词汇表旨在为FFmpeg学习和开发中遇到的核心术语提供简洁、精确的定义。

*   **PTS (Presentation Time Stamp)**
    > **显示时间戳**。一个附加在`AVPacket`或`AVFrame`上的元数据，用于指示该帧应该在何时被呈现（显示或播放）给用户。其单位由相应的时间基（`time_base`）决定。PTS是决定内容播放顺序的唯一标准。

*   **DTS (Decompression Time Stamp)**
    > **解码时间戳**。一个附加在`AVPacket`上的元数据，用于指示该压缩数据包应该在何时被送入解码器。对于不包含B帧的视频流，DTS通常等于PTS。在包含B帧的视频流中，由于解码顺序和显示顺序不同，DTS将与PTS不同，并且解码器必须严格按照DTS的递增顺序来处理数据包。

*   **Coded Video Sequence (编码视频序列)**

    >  **H.264标准术语 (见标准 3.30节)**。指一个由**IDR访问单元**开始，并包含其后所有非IDR访问单元，直至下一个IDR访问单元（不含）的序列。这是一个**完全独立、可解码的单元**，解码此序列无需参考序列外的任何帧。它在概念上与“Closed GOP”等同。

*   **I/P/B/IDR 帧 (I/P/B/IDR Frames) - H.264语境**
    
    > *   **I帧 (Intra-coded Frame)**: **帧内编码帧**。它不依赖于任何其他帧，包含了完整的图像信息，可以被独立解码。然而，在H.264中，一个普通的I帧**并不保证**其后的P/B帧不会参考它之前的帧。
    > *   **IDR帧 (Instantaneous Decoder Refresh Frame)**: **即时解码器刷新帧**。这是一种**特殊且更严格的I帧**。当解码器遇到一个IDR帧时，它会清空其参考帧缓冲区。IDR帧保证了它之后的所有帧都不会参考它之前的任何帧。因此，**IDR帧是H.264中真正的、可靠的随机存取（seek）点**。
    > *   **P帧 (Predicted Frame)**: **前向预测编码帧**。它依赖于其前面的I帧或P帧来进行解码，通过存储与参考帧的差异和运动矢量来压缩数据。
    > *   **B帧 (Bi-directionally predicted Frame)**: **双向预测编码帧**。它同时依赖于其前面和后面的帧来进行解码，具有最高的压缩率。B帧的存在导致了视频解码顺序（DTS）与显示顺序（PTS）不一致。
    
*   **GOP (Group of Pictures) 与 Coded Video Sequence**
    
    > 虽然H.264标准本身并不正式使用“GOP”这个术语（它使用如“Coded Video Sequence”等更精确的术语），但“GOP”在FFmpeg和整个行业中被广泛用作一个**实用的高级概念**，特指以一个关键帧（在H.264中通常是**IDR帧**）开始的一个图像序列。
    > *   **`gop_size`**: 这是FFmpeg编码器的一个参数（如 `libx264` 的 `-g` 选项），它控制了两个IDR帧之间的**最大帧数**。
    > *   **Closed GOP vs. Open GOP**:
    >     *   **Closed GOP (封闭GOP)**: 在FFmpeg的语境下，这通常指以**IDR帧**开始的GOP。它与H.264的`Coded Video Sequence` 概念完全对应，是自包含的，为精确seek提供了保障。这是大多数编码器（如libx264）的默认行为。
    >     *   **Open GOP (开放GOP)**: 以一个**普通的（非IDR的）I帧**开始。该序列头部的P/B帧可能仍会参考上一个序列的帧。这能略微提升压缩效率，但代价是牺牲了精准的随机存取能力，因为这个I帧不是一个“刷新点”。
    
*   **Planar vs Packed**
    
    > 描述多通道或多分量数据（如视频的YUV或音频的立体声）在内存中的存储方式。
    > *   **Packed (打包模式)**: 所有分量的数据交织在一起存储于一块连续的内存中。例如，双声道16位音频的样本会以 `L R L R L R ...` 的顺序排列。在`AVFrame`中，通常只有`data[0]`指向这个缓冲区。
    > *   **Planar (平面模式)**: 每个分量的数据被独立存储在各自的内存区域（平面）中。例如，YUV视频的Y、U、V分量分别位于三块不同的内存区域。在`AVFrame`中，`data[0]`, `data[1]`, `data[2]`会分别指向这三个平面。
    
*   **Muxing vs Demuxing**
    > *   **Demuxing (解封装/解复用)**: 读取一个媒体容器文件（如`input.mp4`），解析其结构，并将其中包含的、交织在一起的多个媒体流（如视频、音频）分离成独立的、只包含压缩数据的`AVPacket`流。这是**解码**流程的第一步。
    > *   **Muxing (封装/复用)**: 将来自不同编码器的、独立的`AVPacket`流（如视频、音频），按照特定容器格式（如`output.mkv`）的规范，交织并写入到一个单一的输出文件中。这是**编码**流程的最后一步。
