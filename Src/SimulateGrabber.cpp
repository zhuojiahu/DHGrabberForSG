/************************************************************************/
/* Copyright (c) 2007, 中国大恒集团北京图像视觉技术分公司视觉系统部     
/* All rights reserved.													
/*																		
/* 文件名称： SimulateGrabber.cpp
/* 摘要： 图像采集卡模拟器, 使用配置文件及已存在的图像文件实现模拟定时采图
/*
/* 当前版本： 3.0
/* 作者： 江卓佳
/* 完成日期： 2009年12月18日	// [2009-12-18 by jzj]: add
/*
/* 当前版本： 2.0
/* 作者： 江卓佳
/* 完成日期： 2007年11月13日
/************************************************************************/

#include "stdafx.h"
#include "SimulateGrabber.h"
#include "FileOperate.h"
#include "Image.h"
#include "SpendTime.h"// [2008-1-22 by jzj]

#include "SGParamDlg.h"

// 避免调用对话框时调884警告
class tempRoutingFrame 
{
   CFrameWnd* m_pFrame;

public:

   tempRoutingFrame(CFrameWnd * pWnd= NULL)
   {
      // Save current value
      m_pFrame = AfxGetThreadState()->m_pRoutingFrame;
      // Set to value passed in. NULL by default.
      AfxGetThreadState()->m_pRoutingFrame = pWnd;
   }

   ~tempRoutingFrame()
   {
      // Restore m_pRoutingFrame to original value.
      AfxGetThreadState()->m_pRoutingFrame = m_pFrame;
   }
};

CSimulateGrabber::CSimulateGrabber()
{
	//////////////////////////////////////////////////////////////////////////
	// 变量初始化

	m_LastErrorInfo.nErrorCode = 0;
	memset(m_LastErrorInfo.strErrorDescription, 0, GBMaxTextLen);
	memset(m_LastErrorInfo.strErrorRemark, 0, GBMaxTextLen);
	
	m_nCardSN = 0;	//图像卡序号
	m_CallBackFunc  = NULL;	//回调函数指针
	m_Context = NULL;	//存放调用初始化函数的对象的this指针

	m_pImageBuffer = NULL;	//供外面处理用的图像缓冲区指针
	m_nImageWidth = 0;	//图像宽度 (单位:字节)
	m_nImageHeight = 0;	//图像高度 (单位:字节)
	m_nImageByteCount = 0;	//图像象素大小 (单位:字节)
	m_nImageBuffSize = 0;	//图像缓冲区大小 (单位:字节)
	m_bPlaneRGB = FALSE;		//是否分通道
	m_pPlaneRGBBuffer = NULL;	//供外面处理用的分通道图像缓冲区

	m_bInited = FALSE;	//图像卡是否已经初始化

	m_bGrab = FALSE;//是否采集图像
	m_bGrabbing = FALSE;//是否正在采集图像
	m_nGrabSpeed = 0;//采集速度(单位: 毫秒/张)

	m_bSnap = FALSE;// 是否采集一帧图像 // [2009-12-28 by jzj]: add

	m_pBmpInfo = NULL;

	m_bKillServiceThread = FALSE;

	m_nCapturedFieldCount = 0;//采集帧记数
	
	m_bPreReadImage = FALSE;//是否预先读图像到内存中
	m_nImageNum = 0;//图像张数
	for(int i = 0; i < SGMaxImageNum; i++)
	{
		m_PreReadImageBuffArray[i] = NULL;
	}
	
	m_bImagesOK = FALSE;//图像是否已准备好
	m_bUpdateImagesFolder = FALSE;//更新图像所在文件夹
	
	m_nMaxImageWidth = 0;
	m_nMaxImageHeight = 0;
	m_nMaxImageByteCount = 0;

	m_bLoopGrab = FALSE;// 是否循环采集// [2008-10-23 by jzj]

	m_bResetPosition = FALSE;// 是否重置预读位置// [2008-12-4 by jzj]: add

	m_iGrabberTypeSN = 0;	// 采集卡类型编号// [2009-12-18 by jzj]: add
	//
	//////////////////////////////////////////////////////////////////////////
}

CSimulateGrabber::~CSimulateGrabber()
{
	m_FileFinder.Close();

	if(m_bPreReadImage == FALSE)
	{
		if(m_pImageBuffer != NULL)
		{
			delete []m_pImageBuffer;
			m_pImageBuffer = NULL;
		}
		
		if(m_pPlaneRGBBuffer != NULL)
		{
			delete []m_pPlaneRGBBuffer;
			m_pPlaneRGBBuffer = NULL;
		}
	}
	else
	{
		for(int i = 0; i < m_nImageNum; i++)
		{
			if(m_PreReadImageBuffArray[i] != NULL)
			{
				delete []m_PreReadImageBuffArray[i];
				m_PreReadImageBuffArray[i] = NULL;
			}
		}
	}	

	if(m_pBmpInfo != NULL)
	{
		delete m_pBmpInfo;
		m_pBmpInfo = NULL;
	}
}

//初始化图像卡
BOOL CSimulateGrabber::Init(const s_GBINITSTRUCT* pInitParam)
{
	s_SGINITCARDSTRUCT InitCardParam;
	InitCardParam.CardSN = pInitParam->nGrabberSN;
	memcpy(InitCardParam.sInitFile, pInitParam->strGrabberFile, GBMaxFileNameLen);
	InitCardParam.CallBackFunc = pInitParam->CallBackFunc;
	InitCardParam.Context = pInitParam->Context;

	m_iGrabberTypeSN = pInitParam->iGrabberTypeSN;	// 采集卡类型编号// [2009-12-18 by jzj]: add
	m_strDeviceName = pInitParam->strDeviceName;
	BOOL bRet = InitCard(&InitCardParam);

	return bRet;
}

//关闭图像卡
BOOL CSimulateGrabber::Close()
{
	BOOL bRet = CloseCard();

	return bRet;
}

//开始采集
BOOL CSimulateGrabber::StartGrab()
{
	if(m_bInited == FALSE)
	{
		m_LastErrorInfo.nErrorCode = SGErrorCodeNoInit;
		sprintf(m_LastErrorInfo.strErrorDescription, "对象未初始化，不能进行该操作！");
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：StartGrab()");
		return FALSE;
	}
	
	if(m_bPreReadImage == TRUE)// [2008-12-4 by jzj]: add
	{
		m_bResetPosition = TRUE;// [2008-12-4 by jzj]: add
	}
	else
	{
		m_bUpdateImagesFolder = TRUE;// [2008-10-23 by jzj]为了实现重新搜索
	}

	Sleep(1000);
	m_bGrab = TRUE;//是否采集图像

	return TRUE;
}

//停止采集
BOOL CSimulateGrabber::StopGrab()
{
	if(m_bInited == FALSE)
	{
		m_LastErrorInfo.nErrorCode = SGErrorCodeNoInit;
		sprintf(m_LastErrorInfo.strErrorDescription, "对象未初始化，不能进行该操作！");
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：StopGrab()");
		return FALSE;
	}

	m_bGrab = FALSE;//是否采集图像
	
	return TRUE;
}

//单帧采集
BOOL CSimulateGrabber::Snapshot()
{
	// [2009-12-28 by jzj]: add

	if(m_bInited == FALSE)
	{
		m_LastErrorInfo.nErrorCode = SGErrorCodeNoInit;
		sprintf(m_LastErrorInfo.strErrorDescription, "对象未初始化，不能进行该操作！");
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：StartGrab()");
		return FALSE;
	}
	
	m_bSnap = TRUE;// 是否采集一帧图像
	m_bGrab = TRUE;// 是否采集图像

	return TRUE;
}

//得到参数
BOOL CSimulateGrabber::GetParamInt(GBParamID Param, int &nOutputVal)
{
	if(m_bInited == FALSE)
	{
		m_LastErrorInfo.nErrorCode = SGErrorCodeNoInit;
		sprintf(m_LastErrorInfo.strErrorDescription, "对象未初始化，不能进行该操作！");
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：GetParamInt(GBParamID Param, int &nOutputVal)");
		return FALSE;
	}

	switch(Param)
	{
	case GBImageWidth:
		nOutputVal = m_nImageWidth;
		break;
	case GBImageHeight:
		nOutputVal = m_nImageHeight;
		break;
	case GBImagePixelSize:
		nOutputVal = m_nImageByteCount;
		break;
	case GBImageBufferSize:
		nOutputVal = m_nImageBuffSize;
		break;
	case GBImageBufferAddr:
		{
			if (m_bPlaneRGB)
			{
				nOutputVal = ((__int64)m_pPlaneRGBBuffer) & 0xFFFFFFFF;
			}
			else
			{
				nOutputVal = ((__int64)m_pImageBuffer) & 0xFFFFFFFF;
			}

// 			if(m_bPlaneRGB)
// 			{
// 				nOutputVal = (int)m_pPlaneRGBBuffer;
// 			}
// 			else
// 			{
// 				nOutputVal = (int)m_pImageBuffer;
// 			}
		}
		break;			
	case GBGrabberTypeSN:		// 采集卡类型编号	// [2009-12-18 by jzj]: add
		nOutputVal = m_iGrabberTypeSN;
		break;
	case GBImageBufferAddr2:
		{
			if (m_bPlaneRGB)
			{
				nOutputVal = ((__int64)m_pPlaneRGBBuffer) >> 32;
			}
			else
			{
				nOutputVal = ((__int64)m_pImageBuffer) >> 32;
			}

// 			if (m_bPlaneRGB)
// 			{
// 				nOutputVal = (int)m_pPlaneRGBBuffer + 2;
// 			}
// 			else
// 			{
// 				nOutputVal = (int)m_pImageBuffer + 2;;
// 			}
		}
		break;
	default:
		m_LastErrorInfo.nErrorCode = SGErrorCodeParamIll;
		sprintf(m_LastErrorInfo.strErrorDescription, "参数不合法！");
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：GetParamInt(GBParamID Param, int &nOutputVal)");
		return FALSE;
	}

	return TRUE;
}

//设置参数
BOOL CSimulateGrabber::SetParamInt(GBParamID Param, int nInputVal)
{
	if(m_bInited == FALSE)
	{
		m_LastErrorInfo.nErrorCode = SGErrorCodeNoInit;
		sprintf(m_LastErrorInfo.strErrorDescription, "对象未初始化，不能进行该操作！");
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：SetParamInt(GBParamID Param, int nInputVal)");
		return FALSE;
	}

	ASSERT(FALSE);

	m_LastErrorInfo.nErrorCode = SGErrorCodeThisFuncDisable;
	sprintf(m_LastErrorInfo.strErrorDescription, "该操作无效！");
	sprintf(m_LastErrorInfo.strErrorRemark, "位置：SetParamInt(GBParamID Param, int nInputVal)");
	return FALSE;
}

//调用参数对话框
void CSimulateGrabber::CallParamDialog()
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState()); 
	
#ifdef _DEBUG
	tempRoutingFrame rframe;// Workaround for ASSERT in WINCORE.CPP 884 (CWnd::AssertValid)
#endif

	CSGParamDlg dlg;
	dlg.SetOwner(this);
	dlg.m_nGrabSpeed = m_nGrabSpeed;
	dlg.m_bIsPlaneRGB = m_bPlaneRGB;
	dlg.m_strImagesPath = m_strImageFileFolder;
	
	dlg.m_nImageWidth = m_nImageWidth;
	dlg.m_nImageHeight = m_nImageHeight;
	dlg.m_nChannelNum = m_nImageByteCount;
	
	dlg.DoModal();
}

//得到出错信息
void CSimulateGrabber::GetLastErrorInfo(s_GBERRORINFO *pErrorInfo)
{
	ASSERT(pErrorInfo != NULL);
	
	pErrorInfo->nErrorCode = m_LastErrorInfo.nErrorCode;
	sprintf(pErrorInfo->strErrorDescription, m_LastErrorInfo.strErrorDescription);
	sprintf(pErrorInfo->strErrorRemark, m_LastErrorInfo.strErrorRemark);
}

//////////////////////////////////////////////////////////////////////////
//模拟采集卡专用接口

//初始化图像卡
BOOL CSimulateGrabber::InitCard(const s_SGINITCARDSTRUCT* pInitCardParam)
{	
	ASSERT(pInitCardParam != NULL);
	ASSERT(pInitCardParam->CallBackFunc != NULL);
	ASSERT(pInitCardParam->Context != NULL);

	if(pInitCardParam == NULL || pInitCardParam->CallBackFunc == NULL || pInitCardParam->Context == NULL)
	{
		m_LastErrorInfo.nErrorCode = SGErrorCodeParamIll;
		sprintf(m_LastErrorInfo.strErrorDescription, "参数不合法！");
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：InitCard()");
		return FALSE;
	}
	
	if(m_bInited == TRUE)
	{
		CloseCard();
		m_bInited = FALSE;
	}
	
	//////////////////////////////////////////////////////////////////////////
	//
	
	m_nCardSN = 0;	//图像卡序号
	m_CallBackFunc  = NULL;	//回调函数指针
	m_Context = NULL;	//存放调用初始化函数的对象的this指针

	m_pImageBuffer = NULL;	//供外面处理用的图像缓冲区指针
	m_nImageWidth = 0;	//图像宽度 (单位:字节)
	m_nImageHeight = 0;	//图像高度 (单位:字节)
	m_nImageByteCount = 0;	//图像象素大小 (单位:字节)
	m_nImageBuffSize = 0;	//图像缓冲区大小 (单位:字节)
	m_bPlaneRGB = FALSE;		//是否分通道
	m_pPlaneRGBBuffer = NULL;	//供外面处理用的分通道图像缓冲区

	m_bInited = FALSE;	//图像卡是否已经初始化

	m_bGrab = FALSE;//是否采集图像
	m_bGrabbing = FALSE;//是否正在采集图像
	m_nGrabSpeed = 0;//采集速度(单位: 毫秒/张)

	m_bSnap = FALSE;// 是否采集一帧图像// [2009-12-28 by jzj]: add
	
	//分配位图信息空间
	m_pBmpInfo = (PBITMAPINFO)new BYTE[sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD)];

	m_bKillServiceThread = FALSE;

	m_nCapturedFieldCount = 0;//采集帧记数
	
	m_bPreReadImage = FALSE;//是否预先读图像到内存中
	m_nImageNum = 0;//图像张数
	for(int i = 0; i < SGMaxImageNum; i++)
	{
		m_PreReadImageBuffArray[i] = NULL;
	}
	
	m_bImagesOK = FALSE;//图像是否已准备好
	m_bUpdateImagesFolder = FALSE;//更新图像所在文件夹
	
	m_nMaxImageWidth = 0;
	m_nMaxImageHeight = 0;
	m_nMaxImageByteCount = 0;

	m_bLoopGrab = FALSE;// 是否循环采集// [2008-10-23 by jzj]

	m_bResetPosition = FALSE;// 是否重置预读位置// [2008-12-4 by jzj]: add
	//
	//////////////////////////////////////////////////////////////////////////
	
	m_nCardSN = pInitCardParam->CardSN;
	memcpy(m_sInitFile, pInitCardParam->sInitFile, GBMaxFileNameLen);
	if(!IsFileExist(m_sInitFile))
	{
		m_LastErrorInfo.nErrorCode = SGErrorCodeInitFileNoExist;
		sprintf(m_LastErrorInfo.strErrorDescription, "初始化文件不存在！");
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：InitCard()");
		return FALSE;
	}
	
	//从配置文件中拷参数
	GetPrivateProfileString("采图参数", "图像文件所在文件夹", "NULL", m_strImageFileFolder.GetBuffer(100), 100, m_sInitFile);
	m_nImageWidth = GetPrivateProfileInt("采图参数", "图像宽度", -1, m_sInitFile);
	m_nImageHeight = GetPrivateProfileInt("采图参数", "图像高度", -1, m_sInitFile);
	m_nImageByteCount = GetPrivateProfileInt("采图参数", "每象素字节量", -1, m_sInitFile);	
	m_bPlaneRGB = GetPrivateProfileInt("采图参数", "是否分通道", -1, m_sInitFile);
	m_nGrabSpeed = GetPrivateProfileInt("采图参数", "采集速度", -1, m_sInitFile);
	m_bPreReadImage = GetPrivateProfileInt("采图参数", "是否用预读模式", -1, m_sInitFile);
	m_nImageNum = GetPrivateProfileInt("采图参数", "图像张数", -1, m_sInitFile);
	m_bLoopGrab = GetPrivateProfileInt("采图参数", "是否循环采集", -1, m_sInitFile);// [2008-10-23 by jzj]

	if(!IsPathExist(m_strImageFileFolder))
	{
		m_LastErrorInfo.nErrorCode = SGErrorCodeInitFileParamIll;
		sprintf(m_LastErrorInfo.strErrorDescription, "配置文件参数有误：图像文件所在文件夹 不存在！");
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：InitCard()");
		return FALSE;
	}
	if(m_nImageWidth < 1 || m_nImageHeight < 1)
	{
		m_LastErrorInfo.nErrorCode = SGErrorCodeInitFileParamIll;
		sprintf(m_LastErrorInfo.strErrorDescription, "配置文件参数有误：图像宽高 不合法！（正常值：1 -- n）");
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：InitCard()");
		return FALSE;
	}
	if(m_nImageByteCount < 1 || m_nImageByteCount > 4)
	{
		m_LastErrorInfo.nErrorCode = SGErrorCodeInitFileParamIll;
		sprintf(m_LastErrorInfo.strErrorDescription, "配置文件参数有误：每象素字节量 不合法！（正常值：1 -- 4）");
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：InitCard()");
		return FALSE;
	}
	if(m_bPlaneRGB < 0 || m_bPlaneRGB > 1)
	{
		m_LastErrorInfo.nErrorCode = SGErrorCodeInitFileParamIll;
		sprintf(m_LastErrorInfo.strErrorDescription, "配置文件参数有误：是否分通道 不合法！（正常值：0 -- 1）");
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：InitCard()");
		return FALSE;
	}
	if(m_nGrabSpeed < 1 || m_nGrabSpeed > 60000)
	{
		m_LastErrorInfo.nErrorCode = SGErrorCodeInitFileParamIll;
		sprintf(m_LastErrorInfo.strErrorDescription, "配置文件参数有误：采集速度 不合法！（正常值：1 -- 60000）");
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：InitCard()");
		return FALSE;
	}
	if(m_bPreReadImage < 0 || m_bPreReadImage > 1)
	{
		m_LastErrorInfo.nErrorCode = SGErrorCodeInitFileParamIll;
		sprintf(m_LastErrorInfo.strErrorDescription, "配置文件参数有误：是否用预读模式 不合法！（正常值：0 -- 1）");
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：InitCard()");
		return FALSE;
	}
	if(m_bPreReadImage == TRUE && (m_nImageNum < 1 || m_nImageNum > SGMaxImageNum))
	{
		m_LastErrorInfo.nErrorCode = SGErrorCodeInitFileParamIll;
		sprintf(m_LastErrorInfo.strErrorDescription, "配置文件参数有误：图像张数 不合法！（正常值：1 -- %d）", SGMaxImageNum);
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：InitCard()");
		return FALSE;
	}
	if(m_bPlaneRGB == TRUE && m_nImageByteCount != 3)
	{
		m_LastErrorInfo.nErrorCode = SGErrorCodeInitFileParamIll;
		sprintf(m_LastErrorInfo.strErrorDescription, "配置文件参数有误：分通道时每象素字节量必须为3！");
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：InitCard()");
		return FALSE;
	}
	if(m_bLoopGrab < 0 || m_bLoopGrab > 1)// [2008-10-23 by jzj]
	{
		m_LastErrorInfo.nErrorCode = SGErrorCodeInitFileParamIll;
		sprintf(m_LastErrorInfo.strErrorDescription, "配置文件参数有误：是否循环采集 不合法！（正常值：0 -- 1）");
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：InitCard()");
		return FALSE;
	}
	
	BOOL bRet = FALSE;
	CString strWildcard; 
	strWildcard.Format("%s\\*.bmp", m_strImageFileFolder); 
	bRet = m_FileFinder.FindFile(strWildcard);
	if(!bRet)
	{
		m_LastErrorInfo.nErrorCode = SGErrorCodeInitFileParamIll;
		sprintf(m_LastErrorInfo.strErrorDescription, "配置文件参数有误：%s下没有BMP文件！", m_strImageFileFolder);
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：InitCard()");
		return FALSE;
	}
	
	m_nMaxImageWidth = m_nImageWidth;
	m_nMaxImageHeight = m_nImageHeight;
	m_nMaxImageByteCount = m_nImageByteCount;
	
	m_CallBackFunc = pInitCardParam->CallBackFunc;
	m_Context = pInitCardParam->Context;
	
	m_nImageBuffSize = m_nImageWidth * m_nImageHeight * m_nImageByteCount;	

	if(m_bPreReadImage == FALSE)//非预读模式
	{
		m_pImageBuffer = new BYTE[m_nImageBuffSize];
		if(m_bPlaneRGB)
		{
			m_pPlaneRGBBuffer = new BYTE[m_nImageBuffSize];
		}
	}
	else//预读图像模式
	{
		static BYTE *pTempConvertBuff = NULL;

		for(int i = 0; i < m_nImageNum; i++)
		{
			m_PreReadImageBuffArray[i] = new BYTE[m_nImageBuffSize];
			
			//////////////////////////////////////////////////////////////////////////
			//读图像到内存

			CString strFilePath;
			BOOL bLastFile = FALSE;
			
			bLastFile = !m_FileFinder.FindNextFile();
			strFilePath = m_FileFinder.GetFilePath();
			
			BOOL bRet = OpenBMPFile(strFilePath, m_pBmpInfo, m_PreReadImageBuffArray[i]);
			if(bRet == FALSE)
			{
				m_LastErrorInfo.nErrorCode = SGErrorCodeReadBMPFileFail;
				sprintf(m_LastErrorInfo.strErrorDescription, "读取图像%s失败!", strFilePath);
				sprintf(m_LastErrorInfo.strErrorRemark, "位置：InitCard()");
				
				return FALSE;
			}
			else
			{
				if(m_pBmpInfo->bmiHeader.biWidth != m_nImageWidth
					|| m_pBmpInfo->bmiHeader.biHeight != m_nImageHeight
					|| m_pBmpInfo->bmiHeader.biBitCount != m_nImageByteCount * 8)
				{
					m_LastErrorInfo.nErrorCode = SGErrorCodeInitFileParamIll;
					sprintf(m_LastErrorInfo.strErrorDescription, "%s文件参数或所选图像有误！", m_sInitFile);
					sprintf(m_LastErrorInfo.strErrorRemark, "位置：InitCard()");
					
					return FALSE;
				}
			}
			
			if(bLastFile)
			{
				m_FileFinder.FindFile(strWildcard);
			}
			//
			//////////////////////////////////////////////////////////////////////////
			
			if(m_bPlaneRGB)
			{
				if(pTempConvertBuff == NULL)
				{
					pTempConvertBuff = new BYTE[m_nImageBuffSize];
				}
				
				CImage myImage;
				myImage.ConvertRGBToPlaneRGB(pTempConvertBuff, m_PreReadImageBuffArray[i], m_nImageWidth, m_nImageHeight);

				BYTE *pTempChangeBuff = NULL;
				pTempChangeBuff = m_PreReadImageBuffArray[i];
				m_PreReadImageBuffArray[i] = pTempConvertBuff;
				pTempConvertBuff = pTempChangeBuff;
			}
		}
		
		if(pTempConvertBuff != NULL)
		{
			delete []pTempConvertBuff;
			pTempConvertBuff = NULL;
		}		
	}	
	
	m_nCapturedFieldCount = 0;//采集帧记数
	m_bInited = TRUE;
	
	//建立采集服务线程
	HANDLE hThread = ::CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ServiceThread, this, 0, &m_dwThreadId);
	::SetThreadPriority(hThread, THREAD_PRIORITY_HIGHEST);

	return TRUE;
}

//关闭图像卡
BOOL CSimulateGrabber::CloseCard()
{	
	if(m_bInited == FALSE)
	{
		return TRUE;
	}	

	m_bGrab = FALSE;//是否采集图像
	m_bKillServiceThread = TRUE;	
	//m_evtReset.SetEvent();// [2008-1-22 by jzj]

	TRACE("CSimulateCard_Kill\n");
	WaitForSingleObject(m_ServiceThreadDeadEvent, INFINITE);
	TRACE("CSimulateCard_Dead\n");

	if(m_bPreReadImage == FALSE)
	{
		if(m_pImageBuffer != NULL)
		{
			delete []m_pImageBuffer;
			m_pImageBuffer = NULL;
		}
		
		if(m_pPlaneRGBBuffer != NULL)
		{
			delete []m_pPlaneRGBBuffer;
			m_pPlaneRGBBuffer = NULL;
		}
	}
	else
	{
		for(int i = 0; i < m_nImageNum; i++)
		{
			if(m_PreReadImageBuffArray[i] != NULL)
			{
				delete []m_PreReadImageBuffArray[i];
				m_PreReadImageBuffArray[i] = NULL;
			}
		}
	}

	if(m_pBmpInfo != NULL)
	{
		delete m_pBmpInfo;
		m_pBmpInfo = NULL;
	}

	m_bInited = FALSE;
	return TRUE;
}

//采集服务线程
DWORD CSimulateGrabber::ServiceThread(LPVOID lpParam)
{
	CSimulateGrabber* This = (CSimulateGrabber*)lpParam;
	
	DWORD dwRet = 0;
	if(This->m_bPreReadImage == TRUE)
	{
		This->PreReadImageServiceThreadFunc();
	}
	else
	{
		This->ServiceThreadFunc();
	}

	return dwRet;
}

//采集服务线程函数
DWORD CSimulateGrabber::ServiceThreadFunc()
{
	BOOL bRet = FALSE;
	CString strWildcard; 
	strWildcard.Format("%s\\*.bmp", m_strImageFileFolder); 
	bRet = m_FileFinder.FindFile(strWildcard);
	if(!bRet)
	{
		m_LastErrorInfo.nErrorCode = SGErrorCodeInitFileParamIll;
		sprintf(m_LastErrorInfo.strErrorDescription, "%s下没有BMP文件！", m_strImageFileFolder);
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：ServiceThreadFunc()");
		
		s_GBSIGNALINFO SignalInfo;
		SignalInfo.iGrabberTypeSN = m_iGrabberTypeSN;// [2009-12-18 by jzj]: add
		SignalInfo.Context = m_Context;
		SignalInfo.nGrabberSN = m_nCardSN;
		SignalInfo.nErrorCode = m_LastErrorInfo.nErrorCode;
		SignalInfo.nFrameCount = 0;
		sprintf(SignalInfo.strDescription, m_LastErrorInfo.strErrorDescription);
		m_CallBackFunc(&SignalInfo);//调用回调函数对错误进行处理

		m_bImagesOK = FALSE;
	}
	else
	{
		m_bImagesOK = TRUE;
	}
	
	CSpendTime mySpendTime;// [2008-1-22 by jzj]
	mySpendTime.Start();// [2008-1-22 by jzj]

	while(!m_bKillServiceThread)
	{	
		if(m_bUpdateImagesFolder)
		{
			m_bUpdateImagesFolder = FALSE;
			
			strWildcard.Format("%s\\*.bmp", m_strImageFileFolder); 
			bRet = m_FileFinder.FindFile(strWildcard);
			if(!bRet)
			{
				m_LastErrorInfo.nErrorCode = SGErrorCodeReadBMPFileFail;
				sprintf(m_LastErrorInfo.strErrorDescription, "%s下没有BMP文件！", m_strImageFileFolder);
				sprintf(m_LastErrorInfo.strErrorRemark, "位置：ServiceThreadFunc()");
				
				s_GBSIGNALINFO SignalInfo;
				SignalInfo.iGrabberTypeSN = m_iGrabberTypeSN;// [2009-12-18 by jzj]: add
				SignalInfo.Context = m_Context;
				SignalInfo.nGrabberSN = m_nCardSN;
				SignalInfo.nErrorCode = m_LastErrorInfo.nErrorCode;
				SignalInfo.nFrameCount = 0;
				sprintf(SignalInfo.strDescription, m_LastErrorInfo.strErrorDescription);
				m_CallBackFunc(&SignalInfo);//调用回调函数对错误进行处理
				
				m_bImagesOK = FALSE;
			}
			else
			{
				m_bImagesOK = TRUE;
			}
		}

		if(m_bImagesOK == FALSE)
		{
			Sleep(10);
			continue;
		}
		
		if(m_bGrab)
		{
			static int iLostTime = 0;// here!!!

			mySpendTime.End();// [2008-1-22 by jzj]
			if(mySpendTime.GetMillisecondInt() >= m_nGrabSpeed - iLostTime)// 允许采下一帧图 // [2008-1-22 by jzj]
			{
				m_bGrabbing = TRUE;
				
				CString strFilePath;
				BOOL bLastFile = FALSE;
				
				bLastFile = !m_FileFinder.FindNextFile();
				strFilePath = m_FileFinder.GetFilePath();
				
				BOOL bRet = OpenBMPFile(strFilePath, m_pBmpInfo, m_pImageBuffer);
				if(bRet == FALSE)
				{
					m_LastErrorInfo.nErrorCode = SGErrorCodeReadBMPFileFail;
					sprintf(m_LastErrorInfo.strErrorDescription, "读取图像%s失败!", strFilePath);
					sprintf(m_LastErrorInfo.strErrorRemark, "位置：ServiceThreadFunc()");
					
					s_GBSIGNALINFO SignalInfo;
					SignalInfo.iGrabberTypeSN = m_iGrabberTypeSN;// [2009-12-18 by jzj]: add
					SignalInfo.Context = m_Context;
					SignalInfo.nGrabberSN = m_nCardSN;
					SignalInfo.nErrorCode = m_LastErrorInfo.nErrorCode;
					SignalInfo.nFrameCount = 0;
					sprintf(SignalInfo.strDescription, m_LastErrorInfo.strErrorDescription);
					m_CallBackFunc(&SignalInfo);//调用回调函数对错误进行处理
				}
				else
				{
					if(m_pBmpInfo->bmiHeader.biWidth != m_nImageWidth
						|| m_pBmpInfo->bmiHeader.biHeight != m_nImageHeight
						|| m_pBmpInfo->bmiHeader.biBitCount != m_nImageByteCount * 8)
					{
						m_LastErrorInfo.nErrorCode = SGErrorCodeInitFileParamIll;
						sprintf(m_LastErrorInfo.strErrorDescription, "%s文件参数或所选图像有误！", m_sInitFile);
						sprintf(m_LastErrorInfo.strErrorRemark, "位置：ServiceThreadFunc()");
						
						s_GBSIGNALINFO SignalInfo;
						SignalInfo.iGrabberTypeSN = m_iGrabberTypeSN;// [2009-12-18 by jzj]: add
						SignalInfo.Context = m_Context;
						SignalInfo.nGrabberSN = m_nCardSN;
						SignalInfo.nErrorCode = m_LastErrorInfo.nErrorCode;
						SignalInfo.nFrameCount = 0;
						sprintf(SignalInfo.strDescription, m_LastErrorInfo.strErrorDescription);
						m_CallBackFunc(&SignalInfo);//调用回调函数对错误进行处理
					}
					else
					{
						m_nCapturedFieldCount++;
						
						if(m_bPlaneRGB)
						{
							CImage myImage;
							myImage.ConvertRGBToPlaneRGB(m_pPlaneRGBBuffer, m_pImageBuffer, m_nImageWidth, m_nImageHeight);
						}
						
						s_GBSIGNALINFO SignalInfo;
						SignalInfo.iGrabberTypeSN = m_iGrabberTypeSN;// [2009-12-18 by jzj]: add
						SignalInfo.Context = m_Context;
						SignalInfo.nGrabberSN = m_nCardSN;
						SignalInfo.nErrorCode = 0;
						SignalInfo.nFrameCount = m_nCapturedFieldCount;
						sprintf(SignalInfo.strDescription, "%s", m_FileFinder.GetFileName().Left(GBMaxFileNameLen));
						m_CallBackFunc(&SignalInfo);//调用回调函数对图像进行处理
					}
				}
				
				m_bGrabbing = FALSE;

				// [2009-12-28 by jzj]: add
				if(m_bSnap)
				{
					m_bSnap = FALSE;
					m_bGrab = FALSE;
				}
				
				if(bLastFile)
				{
					m_FileFinder.FindFile(strWildcard);

					if(m_bLoopGrab == FALSE)// [2008-10-24 by jzj]
					{
						m_bGrab = FALSE;
					}
				}

				mySpendTime.Start();// [2008-1-22 by jzj]
			}// if(mySpendTime.GetMillisecondInt() >= m_nGrabSpeed)// 允许采下一帧图 // [2008-1-22 by jzj]
		}

		//WaitForSingleObject(m_evtReset, iWaitTime);// [2008-1-22 by jzj]
		Sleep(1);// [2008-1-22 by jzj]
	}

	m_ServiceThreadDeadEvent.SetEvent();
	return 0;
}

//预读图像的采集服务线程函数
DWORD CSimulateGrabber::PreReadImageServiceThreadFunc()
{	
	CSpendTime mySpendTime;// [2008-1-22 by jzj]
	mySpendTime.Start();// [2008-1-22 by jzj]
	
	int nPosition = 0;// [2008-12-4 by jzj]: add

	while(!m_bKillServiceThread)
	{	
		if(m_bResetPosition == TRUE)// [2008-12-4 by jzj]: add
		{
			m_bResetPosition = FALSE;	
			nPosition = 0;
		}

		if(m_bGrab)
		{
			mySpendTime.End();// [2008-1-22 by jzj]
			if(mySpendTime.GetMillisecondInt() >= m_nGrabSpeed)// 允许采下一帧图 // [2008-1-22 by jzj]
			{
				//int nPosition = 0;// [2008-12-4 by jzj]: delete
				
				m_bGrabbing = TRUE;
				
				//nPosition = m_nCapturedFieldCount % m_nImageNum;// [2008-12-4 by jzj]: delete
				m_pImageBuffer = m_PreReadImageBuffArray[nPosition];
				
				if(m_pImageBuffer == NULL)
				{
					m_LastErrorInfo.nErrorCode = SGErrorCodeReadImageFromMemFail;
					sprintf(m_LastErrorInfo.strErrorDescription, "从内存中读取图像失败!");
					sprintf(m_LastErrorInfo.strErrorRemark, "位置：PreReadImageServiceThreadFunc()");
					
					s_GBSIGNALINFO SignalInfo;
					SignalInfo.iGrabberTypeSN = m_iGrabberTypeSN;// [2009-12-18 by jzj]: add
					SignalInfo.Context = m_Context;
					SignalInfo.nGrabberSN = m_nCardSN;
					SignalInfo.nErrorCode = m_LastErrorInfo.nErrorCode;
					SignalInfo.nFrameCount = 0;
					sprintf(SignalInfo.strDescription, m_LastErrorInfo.strErrorDescription);
					m_CallBackFunc(&SignalInfo);//调用回调函数对错误进行处理
				}
				else
				{
					m_nCapturedFieldCount++;				
					
					if(m_bPlaneRGB)
					{
						m_pPlaneRGBBuffer = m_pImageBuffer;
					}
					
					s_GBSIGNALINFO SignalInfo;
					SignalInfo.iGrabberTypeSN = m_iGrabberTypeSN;// [2009-12-18 by jzj]: add
					SignalInfo.Context = m_Context;
					SignalInfo.nGrabberSN = m_nCardSN;
					SignalInfo.nErrorCode = 0;
					SignalInfo.nFrameCount = m_nCapturedFieldCount;
					sprintf(SignalInfo.strDescription, "%s", m_strImageFileNameArray[nPosition].Left(GBMaxFileNameLen));
					m_CallBackFunc(&SignalInfo);//调用回调函数对图像进行处理
				}	
				
				m_bGrabbing = FALSE;

				// [2009-12-28 by jzj]: add
				if(m_bSnap)
				{
					m_bSnap = FALSE;
					m_bGrab = FALSE;
				}

				// [2008-12-4 by jzj]: add				
				nPosition++;
				if(nPosition >= m_nImageNum)
				{
					nPosition = 0;
					
					if(m_bLoopGrab == FALSE)
					{
						m_bGrab = FALSE;
					}
				}
				
				mySpendTime.Start();// [2008-1-22 by jzj]
			}// if(mySpendTime.GetMillisecondInt() >= m_nGrabSpeed)// 允许采下一帧图 // [2008-1-22 by jzj]
		}

		//WaitForSingleObject(m_evtReset, m_nGrabSpeed);// [2008-1-22 by jzj]
		Sleep(1);// [2008-1-22 by jzj]
	}

	m_ServiceThreadDeadEvent.SetEvent();
	return 0;
}

//得到参数
//BOOL CSimulateGrabber::GetParamInt(SGPARAMID Param, PINT32 nOutputVal)
BOOL CSimulateGrabber::GetParamInt(SGPARAMID Param, int& nOutputVal)// [2009-12-17 by jzj]: modify
{
	if(m_bInited == FALSE)
	{
		m_LastErrorInfo.nErrorCode = SGErrorCodeNoInit;
		sprintf(m_LastErrorInfo.strErrorDescription, "对象未初始化，不能进行该操作！");
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：GetParamInt(SGPARAMID Param, PINT32 nOutputVal)");
		return FALSE;
	}

	switch(Param)
	{
	case SG_IMAGE_WIDTH:
		//*nOutputVal = m_nImageWidth;// [2009-12-17 by jzj]: modify
		nOutputVal = m_nImageWidth;
		break;
	case SG_IMAGE_HEIGHT:
		//*nOutputVal = m_nImageHeight;// [2009-12-17 by jzj]: modify
		nOutputVal = m_nImageHeight;
		break;
	case SG_IMAGE_BYTE_COUNT:
		//*nOutputVal = m_nImageByteCount;// [2009-12-17 by jzj]: modify
		nOutputVal = m_nImageByteCount;
		break;
	case SG_IMAGE_BUFFER_SIZE:
		//*nOutputVal = m_nImageBuffSize;// [2009-12-17 by jzj]: modify
		nOutputVal = m_nImageBuffSize;
		break;
	case SG_IMAGE_BUFFER_ADDR:
		{
			if(m_bPlaneRGB)
			{
				//*nOutputVal = (int)m_pPlaneRGBBuffer;// [2009-12-17 by jzj]: modify
				nOutputVal = (int)m_pPlaneRGBBuffer;
			}
			else
			{
				//*nOutputVal = (int)m_pImageBuffer;// [2009-12-17 by jzj]: modify
				nOutputVal = (int)m_pImageBuffer;
			}
		}
		break;
	case SG_IS_PLANERGB:
		//*nOutputVal = m_bPlaneRGB;// [2009-12-17 by jzj]: modify
		nOutputVal = m_bPlaneRGB;
		break;
	case SG_GRAB_SPEED:
		//*nOutputVal = m_nGrabSpeed;// [2009-12-17 by jzj]: modify
		nOutputVal = m_nGrabSpeed;
		break;
	default:
		m_LastErrorInfo.nErrorCode = SGErrorCodeParamIll;
		sprintf(m_LastErrorInfo.strErrorDescription, "参数不合法！");
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：GetParamInt(SGPARAMID Param, PINT32 nOutputVal)");
		return FALSE;
	}

	return TRUE;
}

//设置参数
BOOL CSimulateGrabber::SetParamInt(SGPARAMID Param, int nInputVal)
{
	if(m_bInited == FALSE)
	{
		m_LastErrorInfo.nErrorCode = SGErrorCodeNoInit;
		sprintf(m_LastErrorInfo.strErrorDescription, "对象未初始化，不能进行该操作！");
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：SetParamInt(SGPARAMID Param, int nInputVal)");
		return FALSE;
	}
	
	switch(Param)
	{
	case SG_IS_PLANERGB://改：参数校验
		{
			BOOL bPlaneRGB = nInputVal;
			if(bPlaneRGB)
			{
				if(m_nImageByteCount != 3)
				{
					return FALSE;
				}

				if(m_pPlaneRGBBuffer == NULL)
				{
					m_pPlaneRGBBuffer = new BYTE[m_nImageBuffSize];
				}
			}				
			m_bPlaneRGB = bPlaneRGB;
		}
		break;
	case SG_GRAB_SPEED://改：参数校验
		m_nGrabSpeed = nInputVal;
		//m_evtReset.SetEvent();// [2008-1-22 by jzj]
		break;
	case SG_IMAGE_WIDTH:
		if(nInputVal > m_nMaxImageWidth)
		{
			m_LastErrorInfo.nErrorCode = SGErrorCodeParamIll;
			sprintf(m_LastErrorInfo.strErrorDescription, "参数不合法：宽度应小于等于%d", m_nImageWidth);
			sprintf(m_LastErrorInfo.strErrorRemark, "位置：SetParamInt(SGPARAMID Param, int nInputVal)");
			return FALSE;
		}

		m_nImageWidth = nInputVal;
		break;
	case SG_IMAGE_HEIGHT:
		if(nInputVal > m_nMaxImageHeight)
		{
			m_LastErrorInfo.nErrorCode = SGErrorCodeParamIll;
			sprintf(m_LastErrorInfo.strErrorDescription, "参数不合法：高度应小于等于%d", m_nImageHeight);
			sprintf(m_LastErrorInfo.strErrorRemark, "位置：SetParamInt(SGPARAMID Param, int nInputVal)");
			return FALSE;
		}

		m_nImageHeight = nInputVal;
		break;
	case SG_IMAGE_BYTE_COUNT:
		if(nInputVal > m_nMaxImageByteCount)
		{
			m_LastErrorInfo.nErrorCode = SGErrorCodeParamIll;
			sprintf(m_LastErrorInfo.strErrorDescription, "参数不合法：每像素字节量应小于等于%d", m_nImageByteCount);
			sprintf(m_LastErrorInfo.strErrorRemark, "位置：SetParamInt(SGPARAMID Param, int nInputVal)");
			return FALSE;
		}

		m_nImageByteCount = nInputVal;
		break;
	default:
		m_LastErrorInfo.nErrorCode = SGErrorCodeParamIll;
		sprintf(m_LastErrorInfo.strErrorDescription, "参数不合法！");
		sprintf(m_LastErrorInfo.strErrorRemark, "位置：SetParamInt(SGPARAMID Param, int nInputVal)");
		return FALSE;
	}
	
	return TRUE;
}
//
//////////////////////////////////////////////////////////////////////////