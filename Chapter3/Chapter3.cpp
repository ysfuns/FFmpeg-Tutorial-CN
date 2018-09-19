// 增加音频播放
//

#include "stdafx.h"
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "SDL2/SDL.h"
#include "libavutil/imgutils.h"
#include "libswresample/swresample.h"
}


#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "swscale.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "avcodec.lib")

#pragma comment(lib, "swresample.lib")

#pragma comment(lib, "SDL2.lib")
#pragma comment(lib, "SDL2main.lib")

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000
//队列，存放的是音视频解码前的数据
typedef struct PacketQueue
{
	AVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	SDL_mutex *mutex;
	SDL_cond *cond;
} PacketQueue;

PacketQueue audioq;
SwrContext *au_convert_ctx;

int quit = 0;
void packet_queue_init(PacketQueue *q);
int packet_queue_put(PacketQueue *q, AVPacket *pkt);
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block);
void audio_callback(void *userdata, Uint8 *stream, int len);
int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size);

int main(int argc, char** argv)
{
	int ret = -1;
	AVFormatContext *pFormatCtx = NULL;
	pFormatCtx = avformat_alloc_context();
	if (avformat_open_input(&pFormatCtx, argv[1], NULL, NULL) != 0)
		return -1;
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
	{
		avformat_close_input(&pFormatCtx);
		return -1;
	}
	//输出文件的一些信息
	av_dump_format(pFormatCtx, 0, argv[1], 0);
	int videoStream = -1;
	int audioStream = -1;
	for (int i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)//视频流
		{
			videoStream = i;
		}
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)//音频
		{
			audioStream = i;
		}
	}
	if (videoStream == -1 || audioStream == -1)
	{
		avformat_close_input(&pFormatCtx);
		return -1;// 没有找到音视频流
	}

	/*------------------------------视频解码器--------------------------------------------*/
	AVCodecContext *pCodecCtx = NULL;//视频流的编解码信息指针,以下是ffmpeg4.0的api获取方式
	pCodecCtx = avcodec_alloc_context3(NULL);
	ret = avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStream]->codecpar);
	if (ret < 0)
	{
		printf("[error]avcodec_parameters_to_context: %s\n", ret);
		avcodec_free_context(&pCodecCtx);
		avformat_close_input(&pFormatCtx);
		return -1;
	}
	AVCodec *pCodec = NULL;
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL)
	{
		fprintf(stderr, "找不到解码器!\n");
		return -1; // Codec not found
	}

	//找到解码器了，就打开它
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
	{
		fprintf(stderr, "打开解码器失败!\n");
		avcodec_free_context(&pCodecCtx);
		avformat_close_input(&pFormatCtx);
		return -1; // Could not open codec
	}
	AVFrame *pFrame = NULL;//存储解码后帧数据
	pFrame = av_frame_alloc();

	//YUV的数据,这里暂时不需要YUV的数据，SDL显示视频的时候需要
	AVFrame *pFrameYUV = NULL;//帧转变成YUV数据
	pFrameYUV = av_frame_alloc();
	uint8_t *out_buffer;
	out_buffer = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1));
	av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer,
		AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);

	SwsContext *img_convert_ctx;
	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);


	//SDL，
	//1.初始化 SDL_Init
	//2.创建窗口 SDL_Window
	//3.基于窗口创建渲染器 SDL_Renderer
	//4.纹理 SDL_Texture
	//5.更新纹理 SDL_UpdateTexture()
	//6.复制到渲染器 SDL_RenderCopy()
	//7.显示 SDL_RenderPresent()
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))//1.初始化SDL
	{
		printf("Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}
	int screen_w, screen_h = 0;
	SDL_Window   *screen;
	SDL_Renderer *sdlRenderer;
	SDL_Texture  *sdlTexture;
	SDL_Rect     sdlRect;

	//screen_w = pCodecCtx->width;
	//screen_h = pCodecCtx->height;
	screen_w = 1280;
	screen_h = 720;
	//2.创建窗口
	screen = SDL_CreateWindow("Chapter2-player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screen_w, screen_h, SDL_WINDOW_OPENGL);
	if (!screen)
	{
		printf("SDL: could not create window - %s\n", SDL_GetError());
		av_free(out_buffer);
		av_frame_free(&pFrame);
		av_frame_free(&pFrameYUV);
		avcodec_free_context(&pCodecCtx);
		avformat_close_input(&pFormatCtx);
		return -1;
	}
	//3.基于窗口创建渲染器
	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	//4.基于渲染器创建纹理
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
		screen_w, screen_h);
	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screen_w;
	sdlRect.h = screen_h;


	/*------------------------------音频解码器--------------------------------------------*/
	AVCodecContext *aCodecCtx = NULL;//视频流的编解码信息指针,以下是ffmpeg4.0的api获取方式
	aCodecCtx = avcodec_alloc_context3(NULL);
	ret = avcodec_parameters_to_context(aCodecCtx, pFormatCtx->streams[audioStream]->codecpar);
	AVCodec *aCodec = NULL;
	aCodec = avcodec_find_decoder(aCodecCtx->codec_id);
	if (!aCodec) 
	{
		fprintf(stderr, "Unsupported codec!\n");
		return -1;
	}
	avcodec_open2(aCodecCtx, aCodec,NULL);
	SDL_AudioSpec   wanted_spec, spec;
	wanted_spec.freq = aCodecCtx->sample_rate;//采样率
	wanted_spec.format = AUDIO_S16SYS;//格式，S16SYS中的S代表有符号的signed，16表示每个样本是16位的，SYS表示大小端的顺序是与使用的系统相同
	wanted_spec.channels = aCodecCtx->channels;//音频的通道数
	wanted_spec.silence = 0;//这是用来表示静音的值。因为音频采样是有符号的，所以 0 当然就是这个值。
	wanted_spec.samples = aCodecCtx->frame_size;;//这是当我们想要更多音频的时候，我们想让 SDL 给出来的音频缓冲区的尺寸，ffplay使用的是1024
	wanted_spec.callback = audio_callback;//回调函数
	wanted_spec.userdata = aCodecCtx;//SDL供给回调函数运行的参数
	if (SDL_OpenAudio(&wanted_spec, &spec) < 0) 
	{
		fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
		return -1;
	}
	//PacketQueue audioq;//音频数据的队列
	packet_queue_init(&audioq);//初始化这个队列
	SDL_PauseAudio(0);//播放

	//Out Audio Param
	uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
	//nb_samples: AAC-1024 MP3-1152
	int out_nb_samples = aCodecCtx->frame_size;
	AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
	//AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_FLTP;
	int out_sample_rate = aCodecCtx->sample_rate;
	int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);
	//Out Buffer Size
	int out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, out_sample_fmt, 1);
	int64_t in_channel_layout = av_get_default_channel_layout(aCodecCtx->channels);
	au_convert_ctx = swr_alloc();
	au_convert_ctx = swr_alloc_set_opts(au_convert_ctx, out_channel_layout, out_sample_fmt, out_sample_rate,
		in_channel_layout, aCodecCtx->sample_fmt, aCodecCtx->sample_rate, 0, NULL);
	swr_init(au_convert_ctx);



	//到这里已经申请了存放解码后一帧数据的内存pFrame，也申请了转换后的RGB和YUV的内存，这里需要哪个就选择哪个
	//接下来将要做的是通过读取包来读取整个视频流，然后把它解码成帧，然后转换成YUV并且通过SDL渲染出来
	AVPacket *packet = NULL;
	packet = (AVPacket *)av_malloc(sizeof(AVPacket));
	av_init_packet(packet);
	int i = 0;
	SDL_Event event;
	while (av_read_frame(pFormatCtx, packet) >= 0)
	{
		if (packet->stream_index == videoStream)
		{
			ret = avcodec_send_packet(pCodecCtx, packet);
			if (ret < 0)
				continue;
			/* 获取帧数据*/
			do {
				ret = avcodec_receive_frame(pCodecCtx, pFrame);
				if (ret < 0)
					break;
				else if (ret == 0) //成功的获取到了解码后一帧数据
				{
					//将一帧数据转换成YUV数据
					sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0,
						pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);

					//SDL渲染YUV数据 
					//SDL_UpdateTexture(sdlTexture, &sdlRect, pFrameYUV->data[0], pFrameYUV->linesize[0]);
					//5.设置纹理的数据
					SDL_UpdateYUVTexture(sdlTexture, &sdlRect,
						pFrameYUV->data[0], pFrameYUV->linesize[0],
						pFrameYUV->data[1], pFrameYUV->linesize[1],
						pFrameYUV->data[2], pFrameYUV->linesize[2]);
					//清除渲染器的数据
					SDL_RenderClear(sdlRenderer);
					//6.把纹理的数据复制给渲染器
					SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &sdlRect);
					//7.显示
					SDL_RenderPresent(sdlRenderer);
					//延迟40毫秒，相当于一秒播放25帧图片
					SDL_Delay(40);
				}
				else if (ret == AVERROR_EOF)
				{
					avcodec_flush_buffers(pCodecCtx);
					break;
				}
			} while (ret != AVERROR(EAGAIN));
		}
		else if (packet->stream_index == audioStream)
		{
			packet_queue_put(&audioq, packet);
		}
		else
		{
			av_packet_unref(packet);
			av_init_packet(packet);
			continue;
		}
		SDL_PollEvent(&event);
		switch (event.type) 
		{
		case SDL_QUIT:
			SDL_Quit();
			exit(0);
			break;
		default:
			break;
		}
	}


	av_free(out_buffer);
	av_frame_free(&pFrame);
	av_frame_free(&pFrameYUV);
	avcodec_flush_buffers(pCodecCtx);
	avformat_close_input(&pFormatCtx);
	av_packet_unref(packet);
	SDL_Quit();

	getchar();
	return 0;
}

//初始化一个队列，mutex和cond是用来锁定线程和发送信号的
void packet_queue_init(PacketQueue *q) 
{
	memset(q, 0, sizeof(PacketQueue));
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
}
//往队列中填充数据
int packet_queue_put(PacketQueue *q, AVPacket *pkt) 
{
	AVPacketList *pkt1;
	if (av_packet_make_refcounted(pkt)<0)
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
	SDL_memset(stream, 0, len);

	printf("fill_audio len = %d\n", len);
	AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
	int len1, audio_size;
	static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
	static unsigned int audio_buf_size = 0;
	static unsigned int audio_buf_index = 0;
	while (len > 0) 
	{
		if (audio_buf_index >= audio_buf_size) 
		{
			/* We have already sent all our data; get more */
			audio_size = audio_decode_frame(aCodecCtx, audio_buf,sizeof(audio_buf));
			if (audio_size < 0) 
			{
				//出错就静音
				audio_buf_size = 1024;
				memset(audio_buf, 0, audio_buf_size);
			}
			else 
			{
				audio_buf_size = audio_size;
			}
			audio_buf_index = 0;
		}
		len1 = audio_buf_size - audio_buf_index;
		if (len1 > len)
			len1 = len;
		memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
		len -= len1;
		stream += len1;
		audio_buf_index += len1;
	}
}

int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf,int buf_size) 
{
	static AVPacket pkt;
	static uint8_t *audio_pkt_data = NULL;
	static int audio_pkt_size = 0;
	int len1, data_size;
	AVFrame *aFrame = NULL;
	aFrame = av_frame_alloc();
	
	for (;;) 
	{
		while (audio_pkt_size > 0) 
		{
			len1 = avcodec_send_packet(aCodecCtx, &pkt);//发送编码数据包
			if (len1 < 0)
			{
				audio_pkt_size = 0;
				break;
			}
			do 
			{
				len1 = avcodec_receive_frame(aCodecCtx, aFrame);//接收解码数据包
				if (len1 < 0)
				{
					audio_pkt_size = 0;
					break;
				}
				else if (len1 ==0)//成功解码
				{
					//data_size = swr_convert(au_convert_ctx, &audio_buf, MAX_AUDIO_FRAME_SIZE, (const uint8_t **)aFrame->data, aFrame->nb_samples);
					//printf("decode len = %d, convert_len = %d\n", audio_pkt_size, data_size);

					data_size = av_samples_get_buffer_size(NULL, aCodecCtx->channels, aFrame->nb_samples,
						aCodecCtx->sample_fmt, 1);
					memcpy(audio_buf, (const uint8_t **)aFrame->data[0], data_size);
				}
			} while (len1 != AVERROR(EAGAIN));

			if (data_size <= 0)
			{
				continue;
			}
			printf("decode len = %d, convert_len = %d\n", audio_pkt_size, data_size);
			return data_size;


			//data_size = buf_size;
			//len1 = avcodec_decode_audio4(aCodecCtx, (int16_t *)audio_buf, &data_size,
			//	audio_pkt_data, audio_pkt_size);
			//if (len1 < 0) 
			//{
			//	/* if error, skip frame */
			//	audio_pkt_size = 0;
			//	break;
			//}
			//audio_pkt_data += len1;
			//audio_pkt_size -= len1;
			//if (data_size <= 0) 
			//{
			//	/* No data yet, get more frames */
			//	continue;
			//}
			///* We have data, return it and come back for more later */
			//return data_size;
		}
		if (pkt.data)
			av_packet_unref(&pkt);
		if (quit) 
		{
			return -1;
		}
		if (packet_queue_get(&audioq, &pkt, 1) < 0) 
		{
			return -1;
		}
		audio_pkt_data = pkt.data;
		audio_pkt_size = pkt.size;
	}
}