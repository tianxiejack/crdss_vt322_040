/*
 * main.cpp
 *
 *  Created on: 2018年8月23日
 *      Author: fsmdn121
 */
#include <glew.h>
#include <glut.h>
#include <freeglut_ext.h>
#include <linux/videodev2.h>
#include "MultiChVideo.hpp"
#include "main.h"
#include "cuda_convert.cuh"
#include "osa_image_queue.h"
#include "gluVideoWindow.hpp"
#include "gluVideoWindowSecondScreen.hpp"

static int curChannel = 0;
static OSA_BufHndl *imgQ[SYS_CHN_CNT];

static void processFrame_cap(int cap_chid,unsigned char *src, struct v4l2_buffer capInfo, int format)
{
	char WindowName[64]={0};
	char strTmp[64];

	Mat img;

	if(capInfo.flags & V4L2_BUF_FLAG_ERROR){
		//OSA_printf("[%d]ch%d V4L2_BUF_FLAG_ERROR", OSA_getCurTimeInMsec(), cap_chid);
		//return;
	}
	if(cap_chid>=SYS_CHN_CNT)
		return;

	if(curChannel == cap_chid)
	{
		if(format==V4L2_PIX_FMT_YUYV)
		{
			//OSA_printf("%s ch%d %d", __func__, cap_chid, OSA_getCurTimeInMsec());
			static int a = 0;
			if(!a)
			{
				Mat temp;
				OSA_BufInfo* info = image_queue_getEmpty(imgQ[cap_chid]);
				if(info != NULL)
				{
					img = Mat(SYS_CHN_HEIGHT(cap_chid),SYS_CHN_WIDTH(cap_chid),CV_8UC3, info->physAddr);
					temp = Mat(SYS_CHN_HEIGHT(cap_chid),SYS_CHN_WIDTH(cap_chid),CV_8UC2,src);
					//putText(temp, strTmp, cv::Point(50,50), CV_FONT_HERSHEY_COMPLEX, 1,cvScalar(255),1);
					cuConvert_yuv2bgr_yuyv_async(cap_chid, temp, img, CUT_FLAG_devAlloc);
					info->chId = cap_chid;
					info->channels = img.channels();
					info->width = img.cols;
					info->height = img.rows;
					info->timestampCap = (uint64)capInfo.timestamp.tv_sec*1000000000ul
							+ (uint64)capInfo.timestamp.tv_usec*1000ul;
					info->timestamp = (uint64_t)getTickCount();
					image_queue_putFull(imgQ[cap_chid], info);
				}
				else
				{
					OSA_printf("[%d]%s. ch%d %ld.%ld queue overflow!\n", OSA_getCurTimeInMsec(), __func__,cap_chid,
							capInfo.timestamp.tv_sec, capInfo.timestamp.tv_usec);
				}
			}
			//a ^=1;
		}
		else if(format==V4L2_PIX_FMT_GREY)
		{
			//OSA_printf("%s ch%d %d", __func__, cap_chid, OSA_getCurTimeInMsec());
			static int b = 0;
			if(!b)
			{
				OSA_BufInfo* info = image_queue_getEmpty(imgQ[cap_chid]);
				if(info != NULL)
				{
					img = Mat(SYS_CHN_HEIGHT(cap_chid),SYS_CHN_WIDTH(cap_chid),CV_8UC1, info->physAddr);
					cudaMemcpy(info->physAddr, src, img.cols*img.rows, cudaMemcpyHostToDevice);
					info->chId = cap_chid;
					info->channels = img.channels();
					info->width = img.cols;
					info->height = img.rows;
					info->timestampCap = (uint64)capInfo.timestamp.tv_sec*1000000000
							+ (uint64)capInfo.timestamp.tv_usec*1000;
					info->timestamp = (uint64_t)getTickCount();
					//OSA_printf("[%d] %s. ch%d %ld.%ld", OSA_getCurTimeInMsec(), __func__,
					//		cap_chid, capInfo.timestamp.tv_sec, capInfo.timestamp.tv_usec);
					image_queue_putFull(imgQ[cap_chid], info);
				}
				else
				{
					OSA_printf("[%d]%s. ch%d %ld.%ld queue overflow!\n", OSA_getCurTimeInMsec(), __func__,cap_chid,
							capInfo.timestamp.tv_sec, capInfo.timestamp.tv_usec);
				}
			}
			//b ^= 1;
		}
	}
}

static void keyboard_event(unsigned char key, int x, int y)
{
	switch(key)
	{
	case '0':
		curChannel = 0;

		break;
	case '1':
		curChannel = 1;

		break;
	case 'q':
	case 27:
		glutLeaveMainLoop();
		break;
	default:
		break;
	}
}

static int callback_process(void *handle, int chId, Mat frame, struct v4l2_buffer capInfo, int format)
{
	/*if(chId == 0){
		static int cc = 0;
		if(cc%100 != 0){
			cc++;
			return 0;
		}
		cc++;
		Mat md = cv::Mat(frame.rows, frame.cols, CV_8UC3);
		cvtColor(frame, md, CV_YUV2BGR_YUYV);
		//cudaMemcpy(md.data,info->physAddr, info->width*info->height*info->channels, cudaMemcpyDeviceToHost);
		imshow("md", md);
		waitKey(1);
		return 0;
	}*/

	processFrame_cap(chId, frame.data, capInfo, format);
	return 0;
}

static void renderCall(int displayId, int stepIdx, int stepSub, int context)
{

}

int main_glu(int argc, char **argv)
{
	cuConvertInit(SYS_CHN_CNT);

    VWIND_Prm vwinPrm;
    memset(&vwinPrm, 0, sizeof(vwinPrm));
    vwinPrm.disFPS = SYS_DIS0_FPS;
    vwinPrm.memType = memtype_cudev;
    vwinPrm.nChannels = SYS_CHN_CNT;
    vwinPrm.bFullScreen = true;
    vwinPrm.disSched = 0.35;
	for(int i=0; i<SYS_CHN_CNT; i++){
		vwinPrm.channelInfo[i].w = SYS_CHN_WIDTH(i);
		vwinPrm.channelInfo[i].h = SYS_CHN_HEIGHT(i);
		vwinPrm.channelInfo[i].c = 3;
		vwinPrm.channelInfo[i].fps = SYS_CHN_FPS(i);
	}
	vwinPrm.renderfunc = renderCall;
	CGluVideoWindow gluWnd(cv::Rect(SYS_DIS0_X, SYS_DIS0_Y, SYS_DIS0_WIDTH, SYS_DIS0_HEIGHT));
	gluWnd.Create(vwinPrm);

#ifdef SYS_DIS1
	vwinPrm.disFPS = SYS_DIS1_FPS;
	CGluVideoWindowSecond gluWnd2(cv::Rect(SYS_DIS1_X, SYS_DIS1_Y, SYS_DIS1_WIDTH, SYS_DIS1_HEIGHT));
	gluWnd2.Create(vwinPrm);
#endif

	for(int chId=0; chId<SYS_CHN_CNT; chId++){
		imgQ[chId] = gluWnd.m_bufQue[chId];
	}

	MultiChVideo MultiCh;
	MultiCh.m_user = NULL;
	MultiCh.m_usrFunc = callback_process;
	MultiCh.creat();
	MultiCh.run();
 	//OSA_printf("getTickFrequency() = %f", getTickFrequency());

	//glutSetCursor(GLUT_CURSOR_NONE);
	glutMainLoop();

	cuConvertUinit();

	return 0;
}
