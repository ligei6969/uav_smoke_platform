#ifndef TEST_FFMPEG_API_H
#define TEST_FFMPEG_API_H
#include <stdint.h>
#include <stddef.h>
#define AV_CODEC_ID_H264 27
#define AV_CODEC_FLAG2_SHOW_ALL 1
#define AV_PIX_FMT_RGB24 2
#define AV_NOPTS_VALUE INT64_MIN
typedef struct AVCodecContext { int thread_count; int flags2; int pix_fmt; } AVCodecContext;
typedef struct AVCodec { int unused; } AVCodec;
typedef struct AVCodecParserContext { int unused; } AVCodecParserContext;
typedef struct AVFrame { int width; int height; uint8_t *data[4]; int linesize[4]; } AVFrame;
typedef AVFrame AVPicture;
typedef struct AVPacket { uint8_t *data; int size; } AVPacket;
void avcodec_register_all(void);
AVCodecContext *avcodec_alloc_context3(const AVCodec *);
AVCodec *avcodec_find_decoder(int);
int avcodec_open2(AVCodecContext *, const AVCodec *, void *);
int avcodec_close(AVCodecContext *);
AVCodecParserContext *av_parser_init(int);
void av_parser_close(AVCodecParserContext *);
AVFrame *av_frame_alloc(void);
void av_free(void *);
void *av_malloc(size_t);
int avpicture_get_size(int, int, int);
int avpicture_fill(AVPicture *, const uint8_t *, int, int, int);
void av_init_packet(AVPacket *);
void av_free_packet(AVPacket *);
int av_parser_parse2(AVCodecParserContext *, AVCodecContext *, uint8_t **, int *,
                     const uint8_t *, int, int64_t, int64_t, int64_t);
int avcodec_decode_video2(AVCodecContext *, AVFrame *, int *, const AVPacket *);
#endif
