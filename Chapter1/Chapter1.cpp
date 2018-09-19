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
	//av_register_all();//FFMPEG4.0中已经弃用了
	//初始化SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		printf("Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}

	AVFormatContext *pFormatCtx = NULL;
	pFormatCtx = avformat_alloc_context();//初始化AVFormatContext
	// 打开视频文件，获取一些头部信息存入pFormatCtx中
	if (avformat_open_input(&pFormatCtx, argv[1], NULL, NULL) != 0)
		return -1; // Couldn’t open file
	//查找流信息，这里会进一步的获取流的信息存入pFormatCtx
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
	{
		avformat_close_input(&pFormatCtx);
		return -1;
	}
	//输出文件的一些信息
	av_dump_format(pFormatCtx, 0, argv[1], 0);
	//现在 pFormatCtx->streams 仅仅是一组大小为 pFormatCtx->nb_streams 的指针,
	//还没有流的数据，所以要先找到音视频的数据
	int videoStream = -1;
	for (int i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)//视频流
		{
			videoStream = i;
		}
		//if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)//音频流
		//{
		//	audioStream = i;
		//}
		//if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_SUBTITLE)//字幕流
		//{
		//	titleStream = i;
		//}
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
	//aCodecCtx = pFormatCtx->streams[audioStream]->codec;//音频流的编解码信息指针，这是4.0以前的获取方式

	//找到了流中的编解码信息，那么就查找解码器
	AVCodec *pCodec=NULL;
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

	//保存 24 位 RGB 色的 PPM 文件，我们必需把帧的格式从原来的转换为 RGB
	AVFrame *pFrameRGB = NULL;//帧转变成RGB数据
	pFrameRGB = av_frame_alloc();
	//即使我们申请了一帧的内存，当转换的时候，我们仍然需要一个地方来放置原始的数据。我们使用
	//av_image_get_buffer_size 来获得我们需要的大小，然后手工申请内存空间
	uint8_t *buffer;
	int numBytes;
	numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 1);
	buffer = (unsigned char *)av_malloc(numBytes);
	//使用 av_image_fill_arrays 来把帧和我们新申请的内存来结合
	av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, buffer,
		AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 1);

	//YUV的数据,这里暂时不需要YUV的数据，SDL显示视频的时候需要
	//AVFrame *pFrameYUV = NULL;//帧转变成YUV数据
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


	//到这里已经申请了存放解码后一帧数据的内存pFrame，也申请了转换后的RGB和YUV的内存，这里需要哪个就选择哪个
	//接下来将要做的是通过读取包来读取整个视频流，然后把它解码成帧，最后转换格式并且保存
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
		//上面那个函数被弃用了，Use avcodec_send_packet() and avcodec_receive_frame().
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
				//将一帧数据转换成RGB数据，并且保存下来
				sws_scale(img_convert_ctx, (const unsigned char * const *)pFrame->data, pFrame->linesize, 0,
					pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);

				//保存5帧的数据,在项目文件中会生成frame1.ppm....等文件,可以使用ffplay -i xxx.ppm查看
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

//保存RGB的数据到文件
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


