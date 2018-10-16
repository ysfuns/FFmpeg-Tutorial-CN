// ff_sdl_audio.cpp : �������̨Ӧ�ó������ڵ㡣
//

#include "stdafx.h"



extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "SDL2/SDL.h"
#include "libavutil/imgutils.h"
#include "libswresample/swresample.h"
#include "SDL2/SDL_thread.h"


#include "libavformat/avio.h"
#include <libavutil/avstring.h>
}
////rtmp://live.hkstv.hk.lxdns.com/live/hks
////rtsp://184.72.239.149/vod/mp4://BigBuckBunny_175k.mov
////http://live.hkstv.hk.lxdns.com/live/hks/playlist.m3u8
////"D:\\1.mp4"


#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "swscale.lib")
#pragma comment(lib, "swresample.lib")

#pragma comment(lib, "SDL2.lib")
#pragma comment(lib, "SDL2main.lib")


#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio
#define VIDEO_PICTURE_QUEUE_SIZE 1

#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

#define FF_ALLOC_EVENT   (SDL_USEREVENT)
#define FF_REFRESH_EVENT (SDL_USEREVENT + 1)
#define FF_QUIT_EVENT (SDL_USEREVENT + 2)

//���У���ŵ�������Ƶ����ǰ������
typedef struct PacketQueue
{
	AVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	SDL_mutex *mutex;//�������������ͽ���
	SDL_cond *cond;//���������������źź͵ȴ��ź�
} PacketQueue;
//typedef struct VideoPicture
//{
//	SDL_Texture *bmp;
//	AVFrame *pFrameYUV;
//	int width, height; /* source height & width */
//	int allocated;
//} VideoPicture;



typedef struct VideoPicture
{
	SDL_Texture *sdlTexture;
	AVFrame *pFrameYUV;
	int width, height; /* source height & width */
	int allocated;
} VideoPicture;


typedef struct VideoState
{
	AVFormatContext *pFormatCtx;//������
	int videoStream, audioStream;//����Ƶ���ı�־
	AVStream *audio_st;//��Ƶ��AVStream
	PacketQueue audioq;//�����Ƶ���ݵĶ���
	uint8_t audio_buf[MAX_AUDIO_FRAME_SIZE * 2];//��Ƶ�Ļ���
	unsigned int audio_buf_size;
	unsigned int audio_buf_index;
	AVPacket audio_pkt;
	uint8_t *audio_pkt_data;
	int audio_pkt_size;


	AVCodecContext *aCodecCtx;
	AVCodecContext *vCodecCtx;

	AVStream *video_st;//��Ƶ��AVStream
	PacketQueue videoq;
	VideoPicture pictq[VIDEO_PICTURE_QUEUE_SIZE];
	int pictq_size, pictq_rindex, pictq_windex;
	SDL_mutex *pictq_mutex;
	SDL_cond *pictq_cond;
	SDL_Thread *parse_tid;//�߳�ָ��
	SDL_Thread *video_tid;//
	char filename[1024];
	int quit;

	AVIOContext     *io_context;

} VideoState;

SwrContext *au_convert_ctx;
SDL_Window *screen;
int screen_w = 1280;
int screen_h = 720;
SDL_Renderer *sdlRenderer;
//SDL_Texture  *sdlTexture;
SDL_Rect     sdlRect;

SwsContext *img_convert_ctx;

int quit = 0;
void packet_queue_init(PacketQueue *q);
int packet_queue_put(PacketQueue *q, AVPacket *pkt);
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block);
void audio_callback(void *userdata, Uint8 *stream, int len);
int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size);
int stream_component_open(VideoState *is, int stream_index);


int video_thread(void *arg);
int queue_picture(VideoState *is, AVFrame *pFrame);
void alloc_picture(void *userdata);
static void schedule_refresh(VideoState *is, int delay);
static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque);
void video_refresh_timer(void *userdata);
void video_display(VideoState *is);
int decode_interrupt_cb(void);
int decode_thread(void *arg);
VideoState *global_video_state;

int main(int argc, char* argv[])
{
	SDL_Event event;
	VideoState *is;
	is = (VideoState *)av_mallocz(sizeof(VideoState));
	av_strlcpy(is->filename, argv[1], sizeof(is->filename));
	is->pictq_mutex = SDL_CreateMutex();
	is->pictq_cond = SDL_CreateCond();

	//SDL��
	//1.��ʼ�� SDL_Init
	//2.�������� SDL_Window
	//3.���ڴ��ڴ�����Ⱦ�� SDL_Renderer
	//4.���� SDL_Texture
	//5.�������� SDL_UpdateTexture()
	//6.���Ƶ���Ⱦ�� SDL_RenderCopy()
	//7.��ʾ SDL_RenderPresent()
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) 
	{
		fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
		exit(1);
	}
	//2.��������
	screen = SDL_CreateWindow("Chapter4-player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screen_w, screen_h, SDL_WINDOW_OPENGL);
	//3.���ڴ��ڴ�����Ⱦ��
	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	//4.������Ⱦ����������
	//sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,screen_w, screen_h);
	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screen_w;
	sdlRect.h = screen_h;

	//����ϵͳ��ָ���ĺ��������� FF_REFRESH_-EVENT �¼���������ʵ�ʶ�����ʱ����ص���Ƶˢ�º�����
	schedule_refresh(is, 40);
	//�����߳�
	is->parse_tid = SDL_CreateThread(decode_thread,NULL, is);
	if (!is->parse_tid) 
	{
		av_free(is);
		return -1;
	}
	//�¼���ѭ�������̵߳Ĳ��ϵĲ�׽�¼�
	for (;;) 
	{
		SDL_WaitEvent(&event);
		switch (event.type) {
		case FF_QUIT_EVENT:
		case SDL_QUIT:
			is->quit = 1;
			/*
			* If the video has finished playing, then both the picture and
			* audio queues are waiting for more data.  Make them stop
			* waiting and terminate normally.
			*/
			SDL_CondSignal(is->audioq.cond);
			SDL_CondSignal(is->videoq.cond);
			SDL_Quit();
			return 0;
			break;
		case FF_ALLOC_EVENT:
			alloc_picture(event.user.data1);
			break;
		case FF_REFRESH_EVENT:
			video_refresh_timer(event.user.data1);
			break;
		default:
			break;
		}
	}
	return 0;
}

//��ʼ��һ�����У�mutex��cond�����������̺߳ͷ����źŵ�
void packet_queue_init(PacketQueue *q)
{
	memset(q, 0, sizeof(PacketQueue));
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
}
//���������������
int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
	AVPacketList *pkt1;
	if (av_packet_make_refcounted(pkt) < 0)
	{
		return -1;
	}
	pkt1 = (AVPacketList*)av_malloc(sizeof(AVPacketList));
	if (!pkt1)
		return -1;
	pkt1->pkt = *pkt;
	pkt1->next = NULL;
	SDL_LockMutex(q->mutex);
	if (!q->last_pkt)
		q->first_pkt = pkt1;
	else
		q->last_pkt->next = pkt1;
	q->last_pkt = pkt1;
	q->nb_packets++;
	q->size += pkt1->pkt.size;
	SDL_CondSignal(q->cond);
	SDL_UnlockMutex(q->mutex);
	return 0;
}
//
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
	AVPacketList *pkt1;
	int ret;
	SDL_LockMutex(q->mutex);
	for (;;)
	{
		if (quit)
		{
			ret = -1;
			break;
		}
		pkt1 = q->first_pkt;
		if (pkt1)
		{
			q->first_pkt = pkt1->next;
			if (!q->first_pkt)
				q->last_pkt = NULL;
			q->nb_packets--;
			q->size -= pkt1->pkt.size;
			*pkt = pkt1->pkt;
			av_free(pkt1);
			ret = 1;
			break;
		}
		else if (!block)
		{
			ret = 0;
			break;
		}
		else
		{
			SDL_CondWait(q->cond, q->mutex);
		}
	}
	SDL_UnlockMutex(q->mutex);
	return ret;
}

void audio_callback(void *userdata, Uint8 *stream, int len)
{
	//��Ƶ�Ļص�����
	SDL_memset(stream, 0, len);
	printf("fill_audio len = %d\n", len);
	//AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
	static uint8_t audio_buf[MAX_AUDIO_FRAME_SIZE *  2];
	VideoState *is = (VideoState *)userdata;
	audio_decode_frame(is, audio_buf, sizeof(audio_buf));
	SDL_MixAudio(stream, audio_buf, len, SDL_MIX_MAXVOLUME);
}

int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size)
{
	static AVPacket pkt;
	static uint8_t *audio_pkt_data = NULL;
	static int audio_pkt_size = 0;
	int len1, data_size;
	AVFrame *aFrame = NULL;
	aFrame = av_frame_alloc();
	AVCodecContext *aCodecCtx = is->aCodecCtx;

	for (;;)
	{
		while (audio_pkt_size > 0)
		{
			len1 = avcodec_send_packet(aCodecCtx, &pkt);//���ͱ������ݰ�
			if (len1 < 0)
			{
				audio_pkt_size = 0;
				break;
			}
			do
			{
				len1 = avcodec_receive_frame(aCodecCtx, aFrame);//���ս������ݰ�
				if (len1 < 0)
				{
					audio_pkt_size = 0;
					break;
				}
				else if (len1 == 0)//�ɹ�����
				{
					data_size = swr_convert(au_convert_ctx, &audio_buf, MAX_AUDIO_FRAME_SIZE, (const uint8_t **)aFrame->data, aFrame->nb_samples);
					printf("decode len = %d, convert_len = %d\n", audio_pkt_size, data_size);

					//data_size = av_samples_get_buffer_size(NULL, aCodecCtx->channels, aFrame->nb_samples,
					//	aCodecCtx->sample_fmt, 1);
					//memcpy(audio_buf, (const uint8_t **)aFrame->data[0], data_size);
				}
			} while (len1 != AVERROR(EAGAIN));

			if (data_size <= 0)
			{
				continue;
			}
			//printf("decode len = %d, convert_len = %d\n", audio_pkt_size, data_size);
			return data_size;
		}
		if (pkt.data)
			av_packet_unref(&pkt);
		if (quit)
		{
			return -1;
		}
		if (packet_queue_get(&is->audioq, &pkt, 1) < 0)
		{
			return -1;
		}
		audio_pkt_data = pkt.data;
		audio_pkt_size = pkt.size;
	}
}

int stream_component_open(VideoState *is, int stream_index) 
{
	AVFormatContext *pFormatCtx = is->pFormatCtx;
	AVCodecContext *codecCtx;
	AVCodec *codec;
	SDL_AudioSpec wanted_spec, spec;
	if (stream_index < 0 || stream_index >= pFormatCtx->nb_streams) 
	{
		return -1;
	}
	// Get a pointer to the codec context for the video stream
	codecCtx = avcodec_alloc_context3(NULL);
	avcodec_parameters_to_context(codecCtx, pFormatCtx->streams[stream_index]->codecpar);



	if (codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) 
	{

		uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
		int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);


		AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
		int64_t in_channel_layout = av_get_default_channel_layout(codecCtx->channels);
		au_convert_ctx = swr_alloc();
		au_convert_ctx = swr_alloc_set_opts(au_convert_ctx, out_channel_layout, out_sample_fmt, codecCtx->sample_rate,
			in_channel_layout, codecCtx->sample_fmt, codecCtx->sample_rate, 0, NULL);
		swr_init(au_convert_ctx);



		is->aCodecCtx = codecCtx;
		// Set audio settings from codec info
		wanted_spec.freq = codecCtx->sample_rate;
		wanted_spec.format = AUDIO_S16SYS;
		wanted_spec.channels = out_channels;
		wanted_spec.silence = 0;
		wanted_spec.samples = codecCtx->frame_size;
		wanted_spec.callback = audio_callback;
		wanted_spec.userdata = is;
		if (SDL_OpenAudio(&wanted_spec, NULL) < 0) 
		{
			fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
			return -1;
		}
	}
	codec = avcodec_find_decoder(codecCtx->codec_id);
	if (!codec || (avcodec_open2(codecCtx, codec,NULL) < 0)) 
	{
		fprintf(stderr, "Unsupported codec!\n");
		return -1;
	}
	switch (codecCtx->codec_type) 
	{
	case AVMEDIA_TYPE_AUDIO:
		is->audioStream = stream_index;
		is->audio_st = pFormatCtx->streams[stream_index];
		is->audio_buf_size = 0;
		is->audio_buf_index = 0;
		memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
		printf("��ʼ����Ƶ����\n");
		packet_queue_init(&is->audioq);
		SDL_PauseAudio(0);
		break;
	case AVMEDIA_TYPE_VIDEO:
		is->videoStream = stream_index;
		is->video_st = pFormatCtx->streams[stream_index];
		printf("��ʼ����Ƶ����\n");
		packet_queue_init(&is->videoq);
		//is->video_tid = SDL_CreateThread(video_thread,NULL, is);

		is->vCodecCtx = codecCtx;
		printf("������Ƶ�����߳�\n");
		is->video_tid = SDL_CreateThread(video_thread, NULL, is);

		break;
	default:
		break;
	}
}

int video_thread(void *arg) 
{
	VideoState *is = (VideoState *)arg;
	AVPacket pkt1, *packet = &pkt1;
	int len1, frameFinished,ret;
	AVFrame *pFrame;
	//AVCodecContext *pCodecCtx;
	pFrame = av_frame_alloc();
	for (;;) 
	{
		if (packet_queue_get(&is->videoq, packet, 1) < 0) 
		{
			// means we quit getting packets
			break;
		}
		ret = avcodec_send_packet(is->vCodecCtx, packet);
		if (ret < 0)
			continue;
		do 
		{
			ret = avcodec_receive_frame(is->vCodecCtx, pFrame);
			if (ret < 0)
				break;
			else if (ret == 0) //�ɹ��Ļ�ȡ���˽����һ֡����
			{
				if (queue_picture(is, pFrame) < 0)
				{
					break;
				}
			}
			else if (ret == AVERROR_EOF)
			{
				avcodec_flush_buffers(is->vCodecCtx);
				break;
			}
		} while (ret != AVERROR(EAGAIN));

		av_packet_unref(packet);
	}
	av_free(pFrame);
	return 0;
}

int queue_picture(VideoState *is, AVFrame *pFrame) 
{

	VideoPicture *vp;
	AVPicture pict;

	/* wait until we have space for a new pic */
	SDL_LockMutex(is->pictq_mutex);
	while (is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE &&
		!is->quit) 
	{
		SDL_CondWait(is->pictq_cond, is->pictq_mutex);
	}
	SDL_UnlockMutex(is->pictq_mutex);

	if (is->quit)
		return -1;

	// windex is set to 0 initially
	vp = &is->pictq[is->pictq_windex];

	/* allocate or resize the buffer! */
	if (!vp->sdlTexture ||
		vp->width != is->video_st->codecpar->width ||
		vp->height != is->video_st->codecpar->height) 
	{
		SDL_Event event;
		vp->allocated = 0;
		/* we have to do it in the main thread */
		event.type = FF_ALLOC_EVENT;
		event.user.data1 = is;
		SDL_PushEvent(&event);

		/* wait until we have a picture allocated */
		SDL_LockMutex(is->pictq_mutex);
		while (!vp->allocated && !is->quit) 
		{
			SDL_CondWait(is->pictq_cond, is->pictq_mutex);
		}
		SDL_UnlockMutex(is->pictq_mutex);
		if (is->quit) 
		{
			return -1;
		}
	}
	/* We have a place to put our picture on the queue */

	if (vp->sdlTexture)
	{
		//SDL_LockTexture(vp->bmp);
		//֡����תYUV����
		//vp->pFrameYUV = av_frame_alloc();
		//uint8_t *out_buffer = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, is->vCodecCtx->width, is->vCodecCtx->height, 1));
		//av_image_fill_arrays(vp->pFrameYUV->data, vp->pFrameYUV->linesize, out_buffer,
		//	AV_PIX_FMT_YUV420P, is->vCodecCtx->width, is->vCodecCtx->height, 1);

		//SwsContext *img_convert_ctx;
		//img_convert_ctx = sws_getContext(is->vCodecCtx->width, is->vCodecCtx->height, is->vCodecCtx->pix_fmt,
		//	is->vCodecCtx->width, is->vCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

		//��һ֡����ת����YUV����
		sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0,
			is->vCodecCtx->height, vp->pFrameYUV->data, vp->pFrameYUV->linesize);


		/* now we inform our display thread that we have a pic ready */
		if (++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) 
		{
			is->pictq_windex = 0;
		}
		SDL_LockMutex(is->pictq_mutex);
		is->pictq_size++;
		SDL_UnlockMutex(is->pictq_mutex);
	}
	return 0;
}

//����������
void alloc_picture(void *userdata) 
{
	printf("��������\n");
	VideoState *is = (VideoState *)userdata;
	VideoPicture *vp;
	vp = &is->pictq[is->pictq_windex];
	if (vp->sdlTexture)
	{
		SDL_DestroyTexture(vp->sdlTexture);
	}
	// Allocate a place to put our YUV image on that screen
	vp->sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
		screen_w, screen_h);
	vp->pFrameYUV = av_frame_alloc();
	uint8_t *out_buffer = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, is->vCodecCtx->width, is->vCodecCtx->height, 1));
	av_image_fill_arrays(vp->pFrameYUV->data, vp->pFrameYUV->linesize, out_buffer,
		AV_PIX_FMT_YUV420P, is->vCodecCtx->width, is->vCodecCtx->height, 1);
	/*SwsContext *img_convert_ctx;*/
	img_convert_ctx = sws_getContext(is->vCodecCtx->width, is->vCodecCtx->height, is->vCodecCtx->pix_fmt,
		is->vCodecCtx->width, is->vCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);



	vp->width = is->video_st->codecpar->width;
	vp->height = is->video_st->codecpar->height;
	SDL_LockMutex(is->pictq_mutex);
	vp->allocated = 1;
	SDL_CondSignal(is->pictq_cond);
	SDL_UnlockMutex(is->pictq_mutex);
}

//��delay�������лص�����
static void schedule_refresh(VideoState *is, int delay) 
{
	SDL_AddTimer(delay, sdl_refresh_timer_cb, is);
}

//��FF_REFRESH_EVENT ������� SDL_USEREVENT+1��Ҫע
//���һ�����ǵ����� 0 ��ʱ��SDL ֹͣ��ʱ�������ǻص��Ͳ����ٷ���
static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque) 
{
	SDL_Event event;
	event.type = FF_REFRESH_EVENT;
	event.user.data1 = opaque;
	SDL_PushEvent(&event);
	return 0; /* 0 means stop timer */
}

//����Ƶ���ݴ�ͼ�������ȡ��
void video_refresh_timer(void *userdata) 
{
	VideoState *is = (VideoState *)userdata;
	VideoPicture *vp;
	if (is->video_st) 
	{
		if (is->pictq_size == 0) 
		{
			schedule_refresh(is, 1);
		}
		else 
		{
			vp = &is->pictq[is->pictq_rindex];
			/* Timing code goes here */
			schedule_refresh(is, 40);
			/* show the picture! */
			video_display(is);
			/* update queue for next picture! */
			if (++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) 
			{
				is->pictq_rindex = 0;
			}
			SDL_LockMutex(is->pictq_mutex);
				is->pictq_size--;
			SDL_CondSignal(is->pictq_cond);
			SDL_UnlockMutex(is->pictq_mutex);
		}
	}
	else 
	{
		schedule_refresh(is, 100);
	}
}

//��Ⱦ֡����
void video_display(VideoState *is) 
{
	printf("video����display\n");
	SDL_Rect rect;
	VideoPicture *vp;

	vp = &is->pictq[is->pictq_rindex];

	rect.x = 0;
	rect.y = 0;
	rect.w = screen_w;
	rect.h = screen_h;


	if (vp->sdlTexture)
	{
		//5.�������������
		SDL_UpdateYUVTexture(vp->sdlTexture, &rect,
			vp->pFrameYUV->data[0], vp->pFrameYUV->linesize[0],
			vp->pFrameYUV->data[1], vp->pFrameYUV->linesize[1],
			vp->pFrameYUV->data[2], vp->pFrameYUV->linesize[2]);
		//�����Ⱦ��������
		SDL_RenderClear(sdlRenderer);
		//6.����������ݸ��Ƹ���Ⱦ��
		SDL_RenderCopy(sdlRenderer, vp->sdlTexture, NULL, &rect);
		//7.��ʾ
		SDL_RenderPresent(sdlRenderer);
	}

}

int decode_interrupt_cb(void *opaqie) 
{
	return (global_video_state && global_video_state->quit);
}

int decode_thread(void *arg) 
{

	VideoState *is = (VideoState *)arg;
	AVFormatContext *pFormatCtx = NULL;
	AVPacket pkt1, *packet = &pkt1;

	int video_index = -1;
	int audio_index = -1;
	int i;

	AVDictionary *io_dict = NULL;
	AVIOInterruptCB callback;

	is->videoStream = -1;
	is->audioStream = -1;

	global_video_state = is;
	// will interrupt blocking functions if we quit!
	callback.callback = decode_interrupt_cb;
	callback.opaque = is;
	if (avio_open2(&is->io_context, is->filename, 0, &callback, &io_dict))
	{
		fprintf(stderr, "Unable to open I/O for %s\n", is->filename);
		return -1;
	}

	// Open video file
	if (avformat_open_input(&pFormatCtx, is->filename, NULL, NULL) != 0)
		return -1; // Couldn't open file

	is->pFormatCtx = pFormatCtx;

	// Retrieve stream information
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
		return -1; // Couldn't find stream information

				   // Dump information about file onto standard error
	av_dump_format(pFormatCtx, 0, is->filename, 0);

	// Find the first video stream

	for (i = 0; i < pFormatCtx->nb_streams; i++) 
	{
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_index < 0) 
		{
			video_index = i;
		}
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_index < 0) 
		{
			audio_index = i;
		}
	}
	if (audio_index >= 0) 
	{
		stream_component_open(is, audio_index);
	}
	if (video_index >= 0) 
	{
		stream_component_open(is, video_index);
	}

	if (is->videoStream < 0 || is->audioStream < 0) 
	{
		fprintf(stderr, "%s: could not open codecs\n", is->filename);
		goto fail;
	}

	// main decode loop

	for (;;) {
		if (is->quit) {
			break;
		}
		// seek stuff goes here
		if (is->audioq.size > MAX_AUDIOQ_SIZE || is->videoq.size > MAX_VIDEOQ_SIZE) 
		{
			//TODO: ��Ƶ����û�б�ȡ��������һֱ���ӣ�������󻺴�ֵ��
			SDL_Delay(10);
			continue;
		}
		if (av_read_frame(is->pFormatCtx, packet) < 0) 
		{
			if (is->pFormatCtx->pb->error == 0) 
			{
				SDL_Delay(100); /* no error; wait for user input */
				continue;
			}
			else 
			{
				break;
			}
		}
		// Is this a packet from the video stream?
		if (packet->stream_index == is->videoStream) 
		{
			packet_queue_put(&is->videoq, packet);
		}
		else if (packet->stream_index == is->audioStream) 
		{
			//printf("������Ƶ����\n");
			packet_queue_put(&is->audioq, packet);
		}
		else 
		{
			av_packet_unref(packet);
		}
	}
	/* all done - wait for it */
	while (!is->quit) 
	{
		SDL_Delay(100);
	}

fail:
	if (1) 
	{
		SDL_Event event;
		event.type = FF_QUIT_EVENT;
		event.user.data1 = is;
		SDL_PushEvent(&event);
	}
	return 0;
}