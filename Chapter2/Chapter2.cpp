// 通过SDL把视频显示出来
//

#include "stdafx.h"
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "SDL2/SDL.h"
#include "libavutil/imgutils.h"
}


#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "swscale.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "avcodec.lib")

#pragma comment(lib, "SDL2.lib")
#pragma comment(lib, "SDL2main.lib")

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
	for (int i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)//视频流
		{
			videoStream = i;
		}
	}
	if (videoStream == -1)
	{
		avformat_close_input(&pFormatCtx);
		return -1;// 没有找到视频流
	}

	// Get a pointer to the codec context for the video stream
	//流中关于编解码器的信息存入pCodecCtx
	AVCodecContext *pCodecCtx = NULL;//视频流的编解码信息指针,以下是ffmpeg4.0的api获取方式
	pCodecCtx = avcodec_alloc_context3(NULL);   /* It should be freed with avcodec_free_context() */
	ret = avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStream]->codecpar);
	if (ret < 0)
	{
		printf("[error]avcodec_parameters_to_context: %s\n", ret);
		avcodec_free_context(&pCodecCtx);
		avformat_close_input(&pFormatCtx);
		return -1;
	}
	//找到了流中的编解码信息，那么就查找解码器
	AVCodec *pCodec = NULL;
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL)
	{
		fprintf(stderr, "找不到解码器!\n");
		return -1; // Codec not found
	}

	//找到解码器了，就打开它
	// Open codec
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
		pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P,SWS_BICUBIC, NULL, NULL, NULL);


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


	//到这里已经申请了存放解码后一帧数据的内存pFrame，也申请了转换后的RGB和YUV的内存，这里需要哪个就选择哪个
	//接下来将要做的是通过读取包来读取整个视频流，然后把它解码成帧，然后转换成YUV并且通过SDL渲染出来
	AVPacket *packet = NULL;
	packet = (AVPacket *)av_malloc(sizeof(AVPacket));
	av_init_packet(packet);
	int i = 0;
	while (av_read_frame(pFormatCtx, packet) >= 0)
	{
		if (packet->stream_index != videoStream)
		{
			av_packet_unref(packet);
			av_init_packet(packet);
			continue;
		}
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


	av_free(out_buffer);
	av_frame_free(&pFrame);
	av_frame_free(&pFrameYUV);
	avcodec_flush_buffers(pCodecCtx);
	avformat_close_input(&pFormatCtx);
	av_packet_unref(packet);

	getchar();
	return 0;
    return 0;
}

