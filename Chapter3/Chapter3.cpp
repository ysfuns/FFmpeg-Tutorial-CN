// ������Ƶ����
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
//���У���ŵ�������Ƶ����ǰ������
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
	//����ļ���һЩ��Ϣ
	av_dump_format(pFormatCtx, 0, argv[1], 0);
	int videoStream = -1;
	int audioStream = -1;
	for (int i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)//��Ƶ��
		{
			videoStream = i;
		}
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)//��Ƶ
		{
			audioStream = i;
		}
	}
	if (videoStream == -1 || audioStream == -1)
	{
		avformat_close_input(&pFormatCtx);
		return -1;// û���ҵ�����Ƶ��
	}

	/*------------------------------��Ƶ������--------------------------------------------*/
	AVCodecContext *pCodecCtx = NULL;//��Ƶ���ı������Ϣָ��,������ffmpeg4.0��api��ȡ��ʽ
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
		fprintf(stderr, "�Ҳ���������!\n");
		return -1; // Codec not found
	}

	//�ҵ��������ˣ��ʹ���
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
		pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);


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


	/*------------------------------��Ƶ������--------------------------------------------*/
	AVCodecContext *aCodecCtx = NULL;//��Ƶ���ı������Ϣָ��,������ffmpeg4.0��api��ȡ��ʽ
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
	wanted_spec.freq = aCodecCtx->sample_rate;//������
	wanted_spec.format = AUDIO_S16SYS;//��ʽ��S16SYS�е�S�����з��ŵ�signed��16��ʾÿ��������16λ�ģ�SYS��ʾ��С�˵�˳������ʹ�õ�ϵͳ��ͬ
	wanted_spec.channels = aCodecCtx->channels;//��Ƶ��ͨ����
	wanted_spec.silence = 0;//����������ʾ������ֵ����Ϊ��Ƶ�������з��ŵģ����� 0 ��Ȼ�������ֵ��
	wanted_spec.samples = aCodecCtx->frame_size;;//���ǵ�������Ҫ������Ƶ��ʱ���������� SDL ����������Ƶ�������ĳߴ磬ffplayʹ�õ���1024
	wanted_spec.callback = audio_callback;//�ص�����
	wanted_spec.userdata = aCodecCtx;//SDL�����ص��������еĲ���
	if (SDL_OpenAudio(&wanted_spec, &spec) < 0) 
	{
		fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
		return -1;
	}
	//PacketQueue audioq;//��Ƶ���ݵĶ���
	packet_queue_init(&audioq);//��ʼ���������
	SDL_PauseAudio(0);//����

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



	//�������Ѿ������˴�Ž����һ֡���ݵ��ڴ�pFrame��Ҳ������ת�����RGB��YUV���ڴ棬������Ҫ�ĸ���ѡ���ĸ�
	//��������Ҫ������ͨ����ȡ������ȡ������Ƶ����Ȼ����������֡��Ȼ��ת����YUV����ͨ��SDL��Ⱦ����
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
				//����;���
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
				else if (len1 ==0)//�ɹ�����
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