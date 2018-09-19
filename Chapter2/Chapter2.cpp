// ͨ��SDL����Ƶ��ʾ����
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
	//����ļ���һЩ��Ϣ
	av_dump_format(pFormatCtx, 0, argv[1], 0);
	int videoStream = -1;
	for (int i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)//��Ƶ��
		{
			videoStream = i;
		}
	}
	if (videoStream == -1)
	{
		avformat_close_input(&pFormatCtx);
		return -1;// û���ҵ���Ƶ��
	}

	// Get a pointer to the codec context for the video stream
	//���й��ڱ����������Ϣ����pCodecCtx
	AVCodecContext *pCodecCtx = NULL;//��Ƶ���ı������Ϣָ��,������ffmpeg4.0��api��ȡ��ʽ
	pCodecCtx = avcodec_alloc_context3(NULL);   /* It should be freed with avcodec_free_context() */
	ret = avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStream]->codecpar);
	if (ret < 0)
	{
		printf("[error]avcodec_parameters_to_context: %s\n", ret);
		avcodec_free_context(&pCodecCtx);
		avformat_close_input(&pFormatCtx);
		return -1;
	}
	//�ҵ������еı������Ϣ����ô�Ͳ��ҽ�����
	AVCodec *pCodec = NULL;
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL)
	{
		fprintf(stderr, "�Ҳ���������!\n");
		return -1; // Codec not found
	}

	//�ҵ��������ˣ��ʹ���
	// Open codec
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
	{
		fprintf(stderr, "�򿪽�����ʧ��!\n");
		avcodec_free_context(&pCodecCtx);
		avformat_close_input(&pFormatCtx);
		return -1; // Could not open codec
	}
	AVFrame *pFrame = NULL;//�洢�����֡����
	pFrame = av_frame_alloc();

	//YUV������,������ʱ����ҪYUV�����ݣ�SDL��ʾ��Ƶ��ʱ����Ҫ
	AVFrame *pFrameYUV = NULL;//֡ת���YUV����
	pFrameYUV = av_frame_alloc();
	uint8_t *out_buffer;
	out_buffer = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1));
	av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer,
		AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);

	SwsContext *img_convert_ctx;
	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P,SWS_BICUBIC, NULL, NULL, NULL);


	//SDL��
	//1.��ʼ�� SDL_Init
	//2.�������� SDL_Window
	//3.���ڴ��ڴ�����Ⱦ�� SDL_Renderer
	//4.���� SDL_Texture
	//5.�������� SDL_UpdateTexture()
	//6.���Ƶ���Ⱦ�� SDL_RenderCopy()
	//7.��ʾ SDL_RenderPresent()
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))//1.��ʼ��SDL
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
	//2.��������
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
	//3.���ڴ��ڴ�����Ⱦ��
	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	//4.������Ⱦ����������
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
		screen_w, screen_h);
	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screen_w;
	sdlRect.h = screen_h;


	//�������Ѿ������˴�Ž����һ֡���ݵ��ڴ�pFrame��Ҳ������ת�����RGB��YUV���ڴ棬������Ҫ�ĸ���ѡ���ĸ�
	//��������Ҫ������ͨ����ȡ������ȡ������Ƶ����Ȼ����������֡��Ȼ��ת����YUV����ͨ��SDL��Ⱦ����
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
		/* ��ȡ֡����*/
		do {
			ret = avcodec_receive_frame(pCodecCtx, pFrame);
			if (ret < 0)
				break;
			else if (ret == 0) //�ɹ��Ļ�ȡ���˽����һ֡����
			{
				//��һ֡����ת����YUV����
				sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0,
					pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);

				//SDL��ȾYUV���� 
				//SDL_UpdateTexture(sdlTexture, &sdlRect, pFrameYUV->data[0], pFrameYUV->linesize[0]);
				//5.�������������
				SDL_UpdateYUVTexture(sdlTexture, &sdlRect,
					pFrameYUV->data[0], pFrameYUV->linesize[0],
					pFrameYUV->data[1], pFrameYUV->linesize[1],
					pFrameYUV->data[2], pFrameYUV->linesize[2]);
				//�����Ⱦ��������
				SDL_RenderClear(sdlRenderer);
				//6.����������ݸ��Ƹ���Ⱦ��
				SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &sdlRect);
				//7.��ʾ
				SDL_RenderPresent(sdlRenderer);
				//�ӳ�40���룬�൱��һ�벥��25֡ͼƬ
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

