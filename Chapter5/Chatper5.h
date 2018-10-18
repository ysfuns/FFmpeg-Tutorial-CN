#pragma once
extern "C" {
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "libavutil/time.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libswresample/swresample.h"
#include "SDL2/SDL.h"
#include "SDL2/SDL_thread.h"
}

const char program_name[] = "ffplay";
const int program_birth_year = 2003;
#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
#define MIN_FRAMES 25
#define EXTERNAL_CLOCK_MIN_FRAMES 2
#define EXTERNAL_CLOCK_MAX_FRAMES 10

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

/* Step size for volume control in dB */
#define SDL_VOLUME_STEP (0.75)

/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.04
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1
/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 10

/* external clock speed adjustment constants for realtime sources based on buffer fullness */
#define EXTERNAL_CLOCK_SPEED_MIN  0.900
#define EXTERNAL_CLOCK_SPEED_MAX  1.010
#define EXTERNAL_CLOCK_SPEED_STEP 0.001

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   20

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
#define REFRESH_RATE 0.01

/* NOTE: the size must be big enough to compensate the hardware audio buffersize size */
/* TODO: We assume that a decoded and resampled frame fits into this buffer */
#define SAMPLE_ARRAY_SIZE (8 * 65536)

#define CURSOR_HIDE_DELAY 1000000

#define USE_ONEPASS_SUBTITLE_RENDER 1

static unsigned sws_flags = SWS_BICUBIC;

typedef struct MyAVPacketList {
	AVPacket pkt;
	struct MyAVPacketList *next;
	int serial;
} MyAVPacketList;

typedef struct PacketQueue {
	MyAVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	int64_t duration;
	int abort_request;
	int serial;
	SDL_mutex *mutex;
	SDL_cond *cond;
} PacketQueue;

#define VIDEO_PICTURE_QUEUE_SIZE 3 //
#define SUBPICTURE_QUEUE_SIZE 16
#define SAMPLE_QUEUE_SIZE 9
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))

typedef struct AudioParams {
	int freq;
	int channels;
	int64_t channel_layout;
	enum AVSampleFormat fmt;
	int frame_size;
	int bytes_per_sec;
} AudioParams;

typedef struct Clock {
	double pts;           /* clock base */
	double pts_drift;     /* clock base minus time at which we updated the clock */
	double last_updated;
	double speed;
	int serial;           /* clock is based on a packet with this serial */
	int paused;
	int *queue_serial;    /* pointer to the current packet queue serial, used for obsolete clock detection */
} Clock;

/* Common struct for handling all types of decoded data and allocated render buffers. */
typedef struct Frame {
	AVFrame *frame;
	AVSubtitle sub;
	int serial;
	double pts;           /* presentation timestamp for the frame */
	double duration;      /* estimated duration of the frame */
	int64_t pos;          /* byte position of the frame in the input file */
	int width;
	int height;
	int format;
	AVRational sar;
	int uploaded;
	int flip_v;
} Frame;

typedef struct FrameQueue {
	Frame queue[FRAME_QUEUE_SIZE];
	int rindex;
	int windex;
	int size;
	int max_size;
	int keep_last;
	int rindex_shown;
	SDL_mutex *mutex;
	SDL_cond *cond;
	PacketQueue *pktq;
} FrameQueue;

//音视频同步的方式，默认用音频同步
enum {
	AV_SYNC_AUDIO_MASTER, /* default choice */
	AV_SYNC_VIDEO_MASTER,
	AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};

typedef struct Decoder {
	AVPacket pkt;
	PacketQueue *queue;
	AVCodecContext *avctx;
	int pkt_serial;
	int finished;
	int packet_pending;
	SDL_cond *empty_queue_cond;
	int64_t start_pts;
	AVRational start_pts_tb;
	int64_t next_pts;
	AVRational next_pts_tb;
	SDL_Thread *decoder_tid;
} Decoder;

enum ShowMode {
	SHOW_MODE_NONE = -1, SHOW_MODE_VIDEO = 0, SHOW_MODE_WAVES, SHOW_MODE_RDFT, SHOW_MODE_NB
};

typedef struct VideoState {
	// Demux解复用线程，读视频文件stream线程，得到AVPacket，并对packet入栈
	SDL_Thread *read_tid;
	AVInputFormat *iformat;//文件的输入格式
	int abort_request;
	int force_refresh;
	int paused;//控制视频暂停或播放标志位
	int last_paused;
	int queue_attachments_req;
	int seek_req;//进度控制标志
	int seek_flags;
	int64_t seek_pos;
	int64_t seek_rel;
	int read_pause_return;
	AVFormatContext *ic;
	int realtime;//直播和点播？0是点播，1是rtsp的实时流

	Clock audclk;//音频时钟
	Clock vidclk;//视频时钟
	Clock extclk;//外部时钟

	FrameQueue pictq;
	FrameQueue subpq;
	FrameQueue sampq;

	Decoder auddec;
	Decoder viddec;
	Decoder subdec;

	int audio_stream;

	int av_sync_type;

	double audio_clock;
	int audio_clock_serial;
	double audio_diff_cum; /* used for AV difference average computation */
	double audio_diff_avg_coef;
	double audio_diff_threshold;
	int audio_diff_avg_count;
	AVStream *audio_st;//音频流
	PacketQueue audioq;//音频packet队列
	int audio_hw_buf_size;
	uint8_t *audio_buf;
	uint8_t *audio_buf1;
	unsigned int audio_buf_size; /* in bytes */
	unsigned int audio_buf1_size;
	int audio_buf_index; /* in bytes */
	int audio_write_buf_size;
	int audio_volume;
	int muted;
	struct AudioParams audio_src;

	struct AudioParams audio_tgt;
	struct SwrContext *swr_ctx;
	int frame_drops_early;
	int frame_drops_late;

	ShowMode show_mode;
	int16_t sample_array[SAMPLE_ARRAY_SIZE];
	int sample_array_index;
	int last_i_start;
	RDFTContext *rdft;
	int rdft_bits;
	FFTSample *rdft_data;
	int xpos;
	double last_vis_time;
	SDL_Texture *vis_texture;
	SDL_Texture *sub_texture;
	SDL_Texture *vid_texture;

	int subtitle_stream;
	AVStream *subtitle_st;//字幕流
	PacketQueue subtitleq;//字幕packet队列

	double frame_timer;
	double frame_last_returned_time;
	double frame_last_filter_delay;
	int video_stream;
	AVStream *video_st;//视频流
	PacketQueue videoq;//视频packet队列
	double max_frame_duration;      // maximum duration of a frame - above this, we consider the jump a timestamp discontinuity
	struct SwsContext *img_convert_ctx;
	struct SwsContext *sub_convert_ctx;
	int eof;

	char *filename;
	int width, height, xleft, ytop;
	int step;

	int last_video_stream, last_audio_stream, last_subtitle_stream;

	SDL_cond *continue_read_thread;
} VideoState;

/* options specified by the user */
static AVInputFormat *file_iformat;
static char *input_filename;
static const char *window_title;
static int default_width = 640;
static int default_height = 480;
static int screen_width = 0;
static int screen_height = 0;
static int audio_disable;
static int video_disable;
static int subtitle_disable;
static const char* wanted_stream_spec[AVMEDIA_TYPE_NB] = { 0 };
static int seek_by_bytes = -1;
static int display_disable;
static int borderless;
static int startup_volume = 100;
static int show_status = 1;
static int av_sync_type = AV_SYNC_AUDIO_MASTER;
static int64_t start_time = AV_NOPTS_VALUE;
static int64_t duration = AV_NOPTS_VALUE;
static int fast = 0;
static int genpts = 0;
static int lowres = 0;
static int decoder_reorder_pts = -1;
static int autoexit;
static int exit_on_keydown;
static int exit_on_mousedown;
static int loop = 1;
static int framedrop = -1;
static int infinite_buffer = -1;
static enum ShowMode show_mode = SHOW_MODE_NONE;
static const char *audio_codec_name;
static const char *subtitle_codec_name;
static const char *video_codec_name;
double rdftspeed = 0.02;
static int64_t cursor_last_shown;
static int cursor_hidden = 0;

static int autorotate = 1;
static int find_stream_info = 1;

/* current context */
static int is_full_screen;
static int64_t audio_callback_time;

static AVPacket flush_pkt;

#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_RendererInfo renderer_info = { 0 };
static SDL_AudioDeviceID audio_dev;

static const struct TextureFormatEntry {
	enum AVPixelFormat format;
	int texture_fmt;
} sdl_texture_format_map[] = {
	{ AV_PIX_FMT_RGB8,           SDL_PIXELFORMAT_RGB332 },
	{ AV_PIX_FMT_RGB444,         SDL_PIXELFORMAT_RGB444 },
	{ AV_PIX_FMT_RGB555,         SDL_PIXELFORMAT_RGB555 },
	{ AV_PIX_FMT_BGR555,         SDL_PIXELFORMAT_BGR555 },
	{ AV_PIX_FMT_RGB565,         SDL_PIXELFORMAT_RGB565 },
	{ AV_PIX_FMT_BGR565,         SDL_PIXELFORMAT_BGR565 },
	{ AV_PIX_FMT_RGB24,          SDL_PIXELFORMAT_RGB24 },
	{ AV_PIX_FMT_BGR24,          SDL_PIXELFORMAT_BGR24 },
	{ AV_PIX_FMT_0RGB32,         SDL_PIXELFORMAT_RGB888 },
	{ AV_PIX_FMT_0BGR32,         SDL_PIXELFORMAT_BGR888 },
	{ AV_PIX_FMT_NE(RGB0, 0BGR), SDL_PIXELFORMAT_RGBX8888 },
	{ AV_PIX_FMT_NE(BGR0, 0RGB), SDL_PIXELFORMAT_BGRX8888 },
	{ AV_PIX_FMT_RGB32,          SDL_PIXELFORMAT_ARGB8888 },
	{ AV_PIX_FMT_RGB32_1,        SDL_PIXELFORMAT_RGBA8888 },
	{ AV_PIX_FMT_BGR32,          SDL_PIXELFORMAT_ABGR8888 },
	{ AV_PIX_FMT_BGR32_1,        SDL_PIXELFORMAT_BGRA8888 },
	{ AV_PIX_FMT_YUV420P,        SDL_PIXELFORMAT_IYUV },
	{ AV_PIX_FMT_YUYV422,        SDL_PIXELFORMAT_YUY2 },
	{ AV_PIX_FMT_UYVY422,        SDL_PIXELFORMAT_UYVY },
	{ AV_PIX_FMT_NONE,           SDL_PIXELFORMAT_UNKNOWN },
};


void do_exit(VideoState *is);
void stream_close(VideoState *is);
void stream_component_close(VideoState *is, int stream_index);
void packet_queue_destroy(PacketQueue *q);
void packet_queue_flush(PacketQueue *q);
void frame_queue_destory(FrameQueue *f);
void frame_queue_unref_item(Frame *vp);
void decoder_abort(Decoder *d, FrameQueue *fq);
void packet_queue_abort(PacketQueue *q);
void frame_queue_signal(FrameQueue *f);
void decoder_destroy(Decoder *d);
VideoState *stream_open(const char *filename, AVInputFormat *iformat);
void refresh_loop_wait_event(VideoState *is, SDL_Event *event);
void event_loop(VideoState *cur_stream);
int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size, int keep_last);
int packet_queue_init(PacketQueue *q);
void init_clock(Clock *c, int *queue_serial);
void set_clock(Clock *c, double pts, int serial);
void set_clock_at(Clock *c, double pts, int serial, double time);
int read_thread(void *arg);
int is_realtime(AVFormatContext *s);
void set_default_window_size(int width, int height, AVRational sar);
void calculate_display_rect(SDL_Rect *rect,int scr_xleft, int scr_ytop, int scr_width, int scr_height,int pic_width, int pic_height, AVRational pic_sar);
int stream_component_open(VideoState *is, int stream_index);
int audio_open(void *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params);
void sdl_audio_callback(void *opaque, Uint8 *stream, int len);
int audio_decode_frame(VideoState *is);
int frame_queue_nb_remaining(FrameQueue *f);
Frame *frame_queue_peek_readable(FrameQueue *f);
void frame_queue_next(FrameQueue *f);
int synchronize_audio(VideoState *is, int nb_samples);
int get_master_sync_type(VideoState *is);
double get_clock(Clock *c);
double get_master_clock(VideoState *is);
void update_sample_display(VideoState *is, short *samples, int samples_size);
void sync_clock_to_slave(Clock *c, Clock *slave);
void decoder_init(Decoder *d, AVCodecContext *avctx, PacketQueue *queue, SDL_cond *empty_queue_cond);
int decoder_start(Decoder *d, int(*fn)(void *), void *arg);
void packet_queue_start(PacketQueue *q);
int packet_queue_put_private(PacketQueue *q, AVPacket *pkt);
int audio_thread(void *arg);
int video_thread(void *arg);
int subtitle_thread(void *arg);
int decoder_decode_frame(Decoder *d, AVFrame *frame, AVSubtitle *sub);
int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial);
Frame *frame_queue_peek_writable(FrameQueue *f);
void frame_queue_push(FrameQueue *f);
int get_video_frame(VideoState *is, AVFrame *frame);
int queue_picture(VideoState *is, AVFrame *src_frame, double pts, double duration, int64_t pos, int serial);
int packet_queue_put(PacketQueue *q, AVPacket *pkt);
void step_to_next_frame(VideoState *is);
void stream_toggle_pause(VideoState *is);
int packet_queue_put_nullpacket(PacketQueue *q, int stream_index);
int stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue);
void stream_seek(VideoState *is, int64_t pos, int64_t rel, int seek_by_bytes);
int decode_interrupt_cb(void *ctx);
void FFMPEG_Callback(void* ptr, int level, const char* fmt, va_list vl);
void video_refresh(void *opaque, double *remaining_time);
void set_clock_speed(Clock *c, double speed);
void check_external_clock_speed(VideoState *is);
void video_display(VideoState *is);
int video_open(VideoState *is);
void video_audio_display(VideoState *s);
void video_image_display(VideoState *is);
Frame *frame_queue_peek_last(FrameQueue *f);
Frame *frame_queue_peek(FrameQueue *f);
int realloc_texture(SDL_Texture **texture, Uint32 new_format, int new_width, int new_height, SDL_BlendMode blendmode, int init_texture);
int upload_texture(SDL_Texture **tex, AVFrame *frame, struct SwsContext **img_convert_ctx);
void get_sdl_pix_fmt_and_blendmode(int format, Uint32 *sdl_pix_fmt, SDL_BlendMode *sdl_blendmode);
double vp_duration(VideoState *is, Frame *vp, Frame *nextvp);
double compute_target_delay(double delay, VideoState *is);
inline int compute_mod(int a, int b);
void update_video_pts(VideoState *is, double pts, int64_t pos, int serial);
Frame *frame_queue_peek_next(FrameQueue *f);
inline void fill_rectangle(int x, int y, int w, int h);
int64_t frame_queue_last_pos(FrameQueue *f);
void toggle_full_screen(VideoState *is);
void toggle_pause(VideoState *is);
void toggle_mute(VideoState *is);
void update_volume(VideoState *is, int sign, double step);
void stream_cycle_channel(VideoState *is, int codec_type);
void toggle_audio_display(VideoState *is);
void seek_chapter(VideoState *is, int incr);