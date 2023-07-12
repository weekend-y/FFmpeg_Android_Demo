# FFmpeg_Android_Demo
Android平台上使用FFmpeg进行一些音视频相关处理开发的demo集合仓库
1、mydemo1_helloworld
  -- 第一个demo，集成ffmpeg的so到安卓中，并能正常使用；
2、mydemo2_videoPlay
  -- 纯视频播放demo，使用旧接口
3、mydemo3_videoPlay2
  -- 纯视频播放demo，使用新接口
4、demo4_videoDecode
  -- 视频解码demo，将mp4格式文件解码为yuv原始数据文件
5、demo5_videoEncode
  -- 视频编码demo，将yuv原始数据编码为H264格式文件
6、demo6_audioDecode
  -- 音频解码demo，将mp3格式音频文件解码为pcm原始音频数据
7、demo7_audioEncode
  -- 音频编码demo，将pcm原始音频数据编码为AAC格式的音频文件
8、demo8_byAudioTrack
  -- 纯音频播放demo，对mp3文件解码并播放，采用AudioTrack方式播放
9、demo9_byOpenSLES
  -- 纯音频播放demo，对mp3文件解码并播放，采用OpenSLES方式播放
