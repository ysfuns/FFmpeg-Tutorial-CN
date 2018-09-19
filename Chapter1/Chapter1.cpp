/********************************************************************




*******************************************************************/
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

void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame);

int main(int argc, char** argv)
{
	int ret = -1;
	//av_register_all();//FFMPEG4.0���Ѿ�������
	//��ʼ��SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		printf("Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}

	AVFormatContext *pFormatCtx = NULL;
	pFormatCtx = avformat_alloc_context();//��ʼ��AVFormatContext
	// ����Ƶ�ļ�����ȡһЩͷ����Ϣ����pFormatCtx��
	if (avformat_open_input(&pFormatCtx, argv[1], NULL, NULL) != 0)
		return -1; // Couldn��t open file
	//��������Ϣ��������һ���Ļ�ȡ������Ϣ����pFormatCtx
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
	{
		avformat_close_input(&pFormatCtx);
		return -1;
	}
	//����ļ���һЩ��Ϣ
	av_dump_format(pFormatCtx, 0, argv[1], 0);
	//���� pFormatCtx->streams ������һ���СΪ pFormatCtx->nb_streams ��ָ��,
	//��û���������ݣ�����Ҫ���ҵ�����Ƶ������
	int videoStream = -1;
	for (int i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)//��Ƶ��
		{
			videoStream = i;
		}
		//if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)//��Ƶ��
		//{
		//	audioStream = i;
		//}
		//if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_SUBTITLE)//��Ļ��
		//{
		//	titleStream = i;
		//}
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
	//aCodecCtx = pFormatCtx->streams[audioStream]->codec;//��Ƶ���ı������Ϣָ�룬����4.0��ǰ�Ļ�ȡ��ʽ

	//�ҵ������еı������Ϣ����ô�Ͳ��ҽ�����
	AVCodec *pCodec=NULL;
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

	//���� 24 λ RGB ɫ�� PPM �ļ������Ǳ����֡�ĸ�ʽ��ԭ����ת��Ϊ RGB
	AVFrame *pFrameRGB = NULL;//֡ת���RGB����
	pFrameRGB = av_frame_alloc();
	//��ʹ����������һ֡���ڴ棬��ת����ʱ��������Ȼ��Ҫһ���ط�������ԭʼ�����ݡ�����ʹ��
	//av_image_get_buffer_size �����������Ҫ�Ĵ�С��Ȼ���ֹ������ڴ�ռ�
	uint8_t *buffer;
	int numBytes;
	numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 1);
	buffer = (unsigned char *)av_malloc(numBytes);
	//ʹ�� av_image_fill_arrays ����֡��������������ڴ������
	av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, buffer,
		AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 1);

	//YUV������,������ʱ����ҪYUV�����ݣ�SDL��ʾ��Ƶ��ʱ����Ҫ
	//AVFrame *pFrameYUV = NULL;//֡ת���YUV����
	//pFrameYUV = av_frame_alloc();
	//uint8_t *out_buffer;
	//out_buffer = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1));
	//av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, out_buffer,
	//	AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);

	struct SwsContext *img_convert_ctx;
	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGB24,
		SWS_BICUBIC, NULL, NULL, NULL);
	//img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
	//	pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P,
	//	SWS_BICUBIC, NULL, NULL, NULL);


	//�������Ѿ������˴�Ž����һ֡���ݵ��ڴ�pFrame��Ҳ������ת�����RGB��YUV���ڴ棬������Ҫ�ĸ���ѡ���ĸ�
	//��������Ҫ������ͨ����ȡ������ȡ������Ƶ����Ȼ����������֡�����ת����ʽ���ұ���
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
		//avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, packet);
		//�����Ǹ������������ˣ�Use avcodec_send_packet() and avcodec_receive_frame().
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
				//��һ֡����ת����RGB���ݣ����ұ�������
				sws_scale(img_convert_ctx, (const unsigned char * const *)pFrame->data, pFrame->linesize, 0,
					pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);

				//����5֡������,����Ŀ�ļ��л�����frame1.ppm....���ļ�,����ʹ��ffplay -i xxx.ppm�鿴
				if (++i <= 5)
					SaveFrame(pFrameRGB, pCodecCtx->width, pCodecCtx->height, i);
			}
			else if (ret == AVERROR_EOF) 
			{
				avcodec_flush_buffers(pCodecCtx);
				break;
			}
		} while (ret != AVERROR(EAGAIN));
	}


	av_free(buffer);
	av_frame_free(&pFrame);
	av_frame_free(&pFrameRGB);
	avcodec_flush_buffers(pCodecCtx);
	avformat_close_input(&pFormatCtx);
	av_packet_unref(packet);

	getchar();
    return 0;
}

//����RGB�����ݵ��ļ�
void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame) 
{
	FILE *pFile;
	char szFilename[32];
	int y;
	// Open file
	sprintf_s(szFilename, "frame%d.ppm", iFrame);
	fopen_s(&pFile,szFilename, "wb");
	if (pFile == NULL)
		return;
	// Write header
	fprintf(pFile, "P6\n%d %d\n255\n", width, height);
	// Write pixel data
	for (y = 0; y < height; y++)
		fwrite(pFrame->data[0] + y*pFrame->linesize[0], 1, width * 3, pFile);
	// Close file
	fclose(pFile);
}


