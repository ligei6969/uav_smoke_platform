#include "dji_camera_stream_decoder.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <string>
#include <thread>

using namespace std::chrono;
static int allocations = 0;
static int parser_mode = 1;
static int parser_calls = 0;
static int decode_calls = 0;
static bool fail_codec_alloc = false;
static bool fail_codec_open = false;
static bool fail_parser_alloc = false;
static int fail_frame_alloc_at = 0;
static int frame_calls = 0;
extern "C" {
void avcodec_register_all() {}
void *av_malloc(size_t n) { void *p=std::calloc(1,n); if(p)++allocations; return p; }
void av_free(void *p) { if(p){--allocations; std::free(p);} }
AVCodecContext *avcodec_alloc_context3(const AVCodec *) { return fail_codec_alloc ? nullptr : static_cast<AVCodecContext *>(av_malloc(sizeof(AVCodecContext))); }
AVCodec *avcodec_find_decoder(int) { static AVCodec codec{}; return &codec; }
int avcodec_open2(AVCodecContext *, const AVCodec *, void *) { return fail_codec_open ? -1 : 0; }
int avcodec_close(AVCodecContext *) { return 0; }
AVCodecParserContext *av_parser_init(int) { return fail_parser_alloc ? nullptr : static_cast<AVCodecParserContext *>(av_malloc(sizeof(AVCodecParserContext))); }
void av_parser_close(AVCodecParserContext *p) { av_free(p); }
AVFrame *av_frame_alloc() { ++frame_calls; return frame_calls==fail_frame_alloc_at ? nullptr : static_cast<AVFrame *>(av_malloc(sizeof(AVFrame))); }
void av_init_packet(AVPacket *p) { p->data=nullptr; p->size=0; }
void av_free_packet(AVPacket *) {}
int av_parser_parse2(AVCodecParserContext *, AVCodecContext *, uint8_t **p,int *n,const uint8_t *b,int len,int64_t,int64_t,int64_t) {
    ++parser_calls; *p=const_cast<uint8_t *>(b); *n=0;
    if(parser_mode==0)return 0;
    if(parser_mode<0)return -1;
    if(parser_mode==2)return len+1;
    if(parser_mode==3 && parser_calls==1){*n=len;return 0;}
    if(parser_mode==4){*n=len;return 0;}
    return len;
}
int avcodec_decode_video2(AVCodecContext *,AVFrame *,int *got,const AVPacket *) {++decode_calls;*got=0;return 0;}
int avpicture_get_size(int,int w,int h) {return w*h*3;}
int avpicture_fill(AVPicture *,const uint8_t *,int,int,int) {return 0;}
SwsContext *sws_getContext(int,int,int,int,int,int,int,void *,void *,void *) {return static_cast<SwsContext *>(av_malloc(sizeof(SwsContext)));}
void sws_freeContext(SwsContext *p) {av_free(p);}
int sws_scale(SwsContext *,const uint8_t *const *,const int *,int,int,uint8_t *const *,const int *) {return 0;}
}

static void ExpectReady(std::future<void> &f) {
    // A deadlock is a test failure, not an unbounded test hang.
    if(f.wait_for(seconds(3))!=std::future_status::ready) {
        std::cerr<<"operation deadlocked"<<std::endl; std::_Exit(2);
    }
    f.get();
}

static void ImageWaitTests() {
    DJICameraImageHandler handler;
    CameraRGBImage image;
    assert(!handler.getNewImageWithLock(image,-1));
    auto begin=steady_clock::now();
    assert(!handler.getNewImageWithLock(image,1050));
    auto elapsed=duration_cast<milliseconds>(steady_clock::now()-begin).count();
    assert(elapsed>=950 && elapsed<2500); // original nanosecond overflow returns immediately
    uint8_t pixels[]={1,2,3};
    auto waiting=std::async(std::launch::async,[&]{assert(handler.getNewImageWithLock(image,1000));});
    std::this_thread::sleep_for(milliseconds(30));
    handler.writeNewImageWithLock(pixels,3,1,1);
    ExpectReady(waiting);
    assert(image.width==1 && image.height==1 && image.rawData[2]==3);
    handler.writeNewImageWithLock(nullptr,3,1,1);
    assert(!handler.getNewImageWithLock(image,0));
}

static void InitFailureTests() {
    for(int stage=0;stage<5;++stage) {
        DJICameraStreamDecoder decoder;
        fail_codec_alloc=stage==0; fail_codec_open=stage==1; fail_parser_alloc=stage==2;
        frame_calls=0; fail_frame_alloc_at=stage==3 ? 1 : stage==4 ? 2 : 0;
        assert(!decoder.init());
        assert(allocations==0);
        fail_codec_alloc=fail_codec_open=fail_parser_alloc=false; fail_frame_alloc_at=0;
        auto retry=std::async(std::launch::async,[&]{assert(decoder.init());assert(decoder.init());decoder.cleanup();decoder.cleanup();});
        ExpectReady(retry);
        assert(allocations==0);
    }
}

static void ParserTests() {
    DJICameraStreamDecoder decoder;assert(decoder.init());uint8_t bytes[]={0,0,1,9};
    for(int mode : {0,-1,2,1}) {
        parser_mode=mode;parser_calls=0;
        auto parse=std::async(std::launch::async,[&]{decoder.decodeBuffer(bytes,4);});
        ExpectReady(parse); assert(parser_calls==1);
    }
    for(int mode : {3,4}) {
        parser_mode=mode;parser_calls=decode_calls=0;
        auto parse=std::async(std::launch::async,[&]{decoder.decodeBuffer(bytes,4);});
        ExpectReady(parse);assert(parser_calls==2 && decode_calls==1);
    }
    decoder.decodeBuffer(nullptr,3);decoder.decodeBuffer(bytes,0);
}

static std::atomic<int> delivered{0};
static void ImageCallback(CameraRGBImage image,void *context) {
    assert(context==&delivered && image.rawData.size()==3);++delivered;
}
static void LifecycleTests() {
    auto life=std::async(std::launch::async,[]{
        DJICameraStreamDecoder decoder;assert(decoder.init());
        uint8_t pixels[]={1,2,3};
        assert(decoder.registerCallback(ImageCallback,&delivered));
        decoder.decodedImageHandler.writeNewImageWithLock(pixels,3,1,1);
        for(int i=0;i<100 && delivered.load()==0;++i)std::this_thread::sleep_for(milliseconds(10));
        assert(delivered.load()==1);
        assert(decoder.registerCallback(nullptr,nullptr));
        assert(decoder.registerCallback(ImageCallback,&delivered));
        decoder.cleanup(); // must stop callback before releasing decoder state
    });
    ExpectReady(life);assert(allocations==0);
}
static std::atomic<bool> reentry_returned{false};
static void ReentrantCallback(CameraRGBImage,void *context) {
    auto *decoder=static_cast<DJICameraStreamDecoder *>(context);
    assert(!decoder->init());
    assert(!decoder->registerCallback(nullptr,nullptr));
    decoder->cleanup(); // void API rejects callback-side cleanup without blocking
    reentry_returned.store(true);
}
static void ReentryTests() {
    auto life=std::async(std::launch::async,[]{
        DJICameraStreamDecoder decoder;assert(decoder.init());
        assert(decoder.registerCallback(ReentrantCallback,&decoder));
        uint8_t pixels[]={1,2,3};
        decoder.decodedImageHandler.writeNewImageWithLock(pixels,3,1,1);
        for(int i=0;i<100 && !reentry_returned.load();++i)std::this_thread::sleep_for(milliseconds(10));
        assert(reentry_returned.load());
        assert(decoder.registerCallback(nullptr,nullptr));
    });
    ExpectReady(life);assert(allocations==0);
}
int main(int argc,char **argv) {
    const std::string group=argc>1 ? argv[1] : "all";
    if(group=="image" || group=="all")ImageWaitTests();
    if(group=="init" || group=="all")InitFailureTests();
    if(group=="parser" || group=="all")ParserTests();
    if(group=="lifecycle" || group=="all")LifecycleTests();
    if(group=="reentry" || group=="all")ReentryTests();
    assert(allocations==0);
    std::cout<<"PASS: image wait, 5 init failure/retry paths, parser progress, callback stop/restart, cleanup/destruction"<<std::endl;
}
