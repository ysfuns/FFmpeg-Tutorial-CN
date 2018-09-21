// ff_sdl_audio.cpp : 定义控制台应用程序的入口点。
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

#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio

//Output PCM
//#define OUTPUT_PCM 1
//Use SDL
#define USE_SDL 1

//Buffer:
//|-----------|-------------|
//chunk-------pos---len-----|
static  Uint8  *audio_chunk;
static  Uint32  audio_len;
static  Uint8  *audio_pos;

/* The audio function callback takes the following parameters:
* stream: A pointer to the audio buffer to be filled
* len: The length (in bytes) of the audio buffer
*/
void  fill_audio(void *udata, Uint8 *stream, int len) {
	//SDL 2.0
	printf("fill_audio len = %d\n", len);
	SDL_memset(stream, 0, len);
	if (audio_len == 0)
		return;

	len = (len > audio_len ? audio_len : len);	/*  Mix  as  much  data  as  possible  */

	SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
	audio_pos += len;
	audio_len -= len;
}
//-----------------


int main(int argc, char* argv[])
{
	AVFormatContext	*pFormatCtx;
	int				i, audioStream,videoStream;
	AVCodecContext	*aCodecCtx = NULL;
	AVCodecContext	*pCodecCtx = NULL;
	AVCodec         *pCodec;
	AVCodec			*aCodec;
	AVPacket		*packet;
	uint8_t			*out_buffer;
	uint8_t			*out_buffer_audio;
	AVFrame			*pFrame;
	SDL_AudioSpec wanted_spec;
	SDL_Event event;
	int ret;
	uint32_t len = 0;
	int got_picture;
	int index = 0;
	int64_t in_channel_layout;
	struct SwrContext *au_convert_ctx;

	FILE *pFile = NULL;


	//av_register_all();
	//avformat_network_init();
	pFormatCtx = avformat_alloc_context();
	//Open
	if (avformat_open_input(&pFormatCtx, argv[1], NULL, NULL) != 0) {
		printf("Couldn't open input stream.\n");
		return -1;
	}
	// Retrieve stream information
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		printf("Couldn't find stream information.\n");
		return -1;
	}
	// Dump valid information onto standard error
	av_dump_format(pFormatCtx, 0, argv[1], false);

	// Find the first audio stream
	audioStream = -1, videoStream=-1;
	for (i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) 
		{
			audioStream = i;
		}
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videoStream = i;
		}
	}

	if (audioStream == -1) 
	{
		printf("Didn't find a audio stream.\n");
		return -1;
	}

	// Get a pointer to the codec context for the audio stream
	//aCodecCtx = pFormatCtx->streams[audioStream]->codec;
	aCodecCtx = avcodec_alloc_context3(NULL);
	avcodec_parameters_to_context(aCodecCtx, pFormatCtx->streams[audioStream]->codecpar);

	pCodecCtx = avcodec_alloc_context3(NULL);
	avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStream]->codecpar);

	// Find the decoder for the audio stream
	aCodec = avcodec_find_decoder(aCodecCtx->codec_id);
	if (aCodec == NULL) {
		printf("Codec not found.\n");
		return -1;
	}

	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);

	// Open codec
	if (avcodec_open2(aCodecCtx, aCodec, NULL) < 0) {
		printf("Could not open codec.\n");
		return -1;
	}
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
		printf("Could not open codec.\n");
		return -1;
	}

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

	//3.基于窗口创建渲染器
	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	//4.基于渲染器创建纹理
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
		screen_w, screen_h);
	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screen_w;
	sdlRect.h = screen_h;

	packet = (AVPacket *)av_malloc(sizeof(AVPacket));
	av_init_packet(packet);

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

	out_buffer_audio = (uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE * 2);
	pFrame = av_frame_alloc();
	//SDL------------------
#if USE_SDL

	//SDL_AudioSpec
	wanted_spec.freq = out_sample_rate;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.channels = out_channels;
	wanted_spec.silence = 0;
	wanted_spec.samples = out_nb_samples;
	wanted_spec.callback = fill_audio;
	wanted_spec.userdata = aCodecCtx;

	if (SDL_OpenAudio(&wanted_spec, NULL) < 0) {
		printf("can't open audio.\n");
		return -1;
	}
#endif

	//FIX:Some Codec's Context Information is missing
	in_channel_layout = av_get_default_channel_layout(aCodecCtx->channels);
	//Swr

	au_convert_ctx = swr_alloc();
	au_convert_ctx = swr_alloc_set_opts(au_convert_ctx, out_channel_layout, out_sample_fmt, out_sample_rate,
		in_channel_layout, aCodecCtx->sample_fmt, aCodecCtx->sample_rate, 0, NULL);
	swr_init(au_convert_ctx);



	//YUV的数据,这里暂时不需要YUV的数据，SDL显示视频的时候需要
	AVFrame *pFrameYUV = NULL;//帧转变成YUV数据
	pFrameYUV = av_frame_alloc();
	out_buffer = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1));
	av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer,
		AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);

	SwsContext *img_convert_ctx;
	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

	//Play
	SDL_PauseAudio(0);

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
					//SDL_Delay(40);
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
			ret = avcodec_send_packet(aCodecCtx, packet);//发送编码数据包
			if (ret < 0)
			{
				continue;
			}
			do
			{
				ret = avcodec_receive_frame(aCodecCtx, pFrame);//接收解码数据包
				if (ret < 0)
				{
					break;
				}
				else if (ret == 0)//成功解码
				{
					int convert_len = swr_convert(au_convert_ctx, &out_buffer_audio, MAX_AUDIO_FRAME_SIZE, (const uint8_t **)pFrame->data, pFrame->nb_samples);
					printf("decode len = %d, convert_len = %d\n", packet->size, convert_len);
					index++;
				}
			} while (ret != AVERROR(EAGAIN));

#if USE_SDL
			while (audio_len > 0)//Wait until finish
				SDL_Delay(1);

			//Set audio buffer (PCM data)
			audio_chunk = (Uint8 *)out_buffer_audio;
			//Audio buffer length
			audio_len = out_buffer_size;
			audio_pos = audio_chunk;

#endif
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

	//swr_free(&au_convert_ctx);

#if USE_SDL
	SDL_CloseAudio();//Close SDL
	SDL_Quit();
#endif

#if OUTPUT_PCM
	fclose(pFile);
#endif
	av_free(out_buffer);
	avcodec_close(aCodecCtx);
	avformat_close_input(&pFormatCtx);

	return 0;
}
