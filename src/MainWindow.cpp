#include "stdafx.h"
#include "TVTest.h"
#include "AppMain.h"
#include "MiscDialog.h"
#include "DialogUtil.h"
#include "DrawUtil.h"
#include "WindowUtil.h"
#include "PseudoOSD.h"
#include "EventInfoPopup.h"
#include "ToolTip.h"
#include "HelperClass/StdUtil.h"
#include "BonTsEngine/TsInformation.h"
#include "resource.h"

#pragma comment(lib,"imm32.lib")	// for ImmAssociateContext(Ex)

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif


#define MAIN_TITLE_TEXT APP_NAME


using namespace TVTest;




static int CalcZoomSize(int Size,int Rate,int Factor)
{
	if (Factor==0)
		return 0;
	return ::MulDiv(Size,Rate,Factor);
}




CBasicViewer::CBasicViewer(CAppMain &App)
	: m_App(App)
	, m_fEnabled(false)
{
}


bool CBasicViewer::Create(HWND hwndParent,int ViewID,int ContainerID,HWND hwndMessage)
{
	m_ViewWindow.Create(hwndParent,
		WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,0,ViewID);
	m_ViewWindow.SetMessageWindow(hwndMessage);
	const CColorScheme *pColorScheme=m_App.ColorSchemeOptions.GetColorScheme();
	Theme::BorderInfo Border;
	pColorScheme->GetBorderInfo(CColorScheme::BORDER_SCREEN,&Border);
	if (!m_App.MainWindow.GetViewWindowEdge())
		Border.Type=Theme::BORDER_NONE;
	m_ViewWindow.SetBorder(&Border);
	m_VideoContainer.Create(m_ViewWindow.GetHandle(),
		WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,0,ContainerID,
		&m_App.CoreEngine.m_DtvEngine);
	m_ViewWindow.SetVideoContainer(&m_VideoContainer);

	m_DisplayBase.SetParent(&m_VideoContainer);
	m_VideoContainer.SetDisplayBase(&m_DisplayBase);

	return true;
}


bool CBasicViewer::EnableViewer(bool fEnable)
{
	if (m_fEnabled!=fEnable) {
		if (fEnable && !m_App.CoreEngine.m_DtvEngine.m_MediaViewer.IsOpen())
			return false;
		if (fEnable || (!fEnable && !m_DisplayBase.IsVisible()))
			m_VideoContainer.SetVisible(fEnable);
		m_App.CoreEngine.m_DtvEngine.m_MediaViewer.SetVisible(fEnable);
		if (!m_App.CoreEngine.EnableMediaViewer(fEnable))
			return false;
		if (m_App.PlaybackOptions.GetMinTimerResolution())
			m_App.CoreEngine.SetMinTimerResolution(fEnable);
		m_fEnabled=fEnable;
		m_App.PluginManager.SendPreviewChangeEvent(fEnable);
	}
	return true;
}


bool CBasicViewer::BuildViewer(BYTE VideoStreamType)
{
	if (VideoStreamType==0) {
		VideoStreamType=m_App.CoreEngine.m_DtvEngine.GetVideoStreamType();
		if (VideoStreamType==STREAM_TYPE_UNINITIALIZED)
			return false;
	}
	LPCWSTR pszVideoDecoder=nullptr;

	switch (VideoStreamType) {
#ifdef BONTSENGINE_MPEG2_SUPPORT
	case STREAM_TYPE_MPEG2_VIDEO:
		pszVideoDecoder=m_App.GeneralOptions.GetMpeg2DecoderName();
		break;
#endif

#ifdef BONTSENGINE_H264_SUPPORT
	case STREAM_TYPE_H264:
		pszVideoDecoder=m_App.GeneralOptions.GetH264DecoderName();
		break;
#endif

#ifdef BONTSENGINE_H265_SUPPORT
	case STREAM_TYPE_H265:
		pszVideoDecoder=m_App.GeneralOptions.GetH265DecoderName();
		break;
#endif

	default:
		if (m_App.CoreEngine.m_DtvEngine.GetAudioStreamNum()==0)
			return false;
		VideoStreamType=STREAM_TYPE_INVALID;
		break;
	}

	if (m_fEnabled)
		EnableViewer(false);

	m_App.Logger.AddLog(TEXT("DirectShow�̏��������s���܂�(%s)..."),
						VideoStreamType==STREAM_TYPE_INVALID?
							TEXT("�f���Ȃ�"):
							TsEngine::GetStreamTypeText(VideoStreamType));

	m_App.CoreEngine.m_DtvEngine.m_MediaViewer.SetAudioFilter(m_App.PlaybackOptions.GetAudioFilterName());
	bool fOK=m_App.CoreEngine.BuildMediaViewer(
		m_VideoContainer.GetHandle(),
		m_VideoContainer.GetHandle(),
		m_App.GeneralOptions.GetVideoRendererType(),
		VideoStreamType,pszVideoDecoder,
		m_App.PlaybackOptions.GetAudioDeviceName());
	if (fOK) {
		m_App.Logger.AddLog(TEXT("DirectShow�̏��������s���܂����B"));
	} else {
		m_App.Core.OnError(&m_App.CoreEngine,TEXT("DirectShow�̏��������ł��܂���B"));
	}

	return fOK;
}


bool CBasicViewer::CloseViewer()
{
	EnableViewer(false);
	m_App.CoreEngine.CloseMediaViewer();
	return true;
}




const BYTE CMainWindow::m_AudioGainList[] = {100, 125, 150, 200};

const CMainWindow::DirectShowFilterPropertyInfo CMainWindow::m_DirectShowFilterPropertyList[] = {
	{CMediaViewer::PROPERTY_FILTER_VIDEODECODER,		CM_VIDEODECODERPROPERTY},
	{CMediaViewer::PROPERTY_FILTER_VIDEORENDERER,		CM_VIDEORENDERERPROPERTY},
	{CMediaViewer::PROPERTY_FILTER_AUDIOFILTER,			CM_AUDIOFILTERPROPERTY},
	{CMediaViewer::PROPERTY_FILTER_AUDIORENDERER,		CM_AUDIORENDERERPROPERTY},
	{CMediaViewer::PROPERTY_FILTER_MPEG2DEMULTIPLEXER,	CM_DEMULTIPLEXERPROPERTY},
};

ATOM CMainWindow::m_atomChildOldWndProcProp=0;


bool CMainWindow::Initialize(HINSTANCE hinst)
{
	WNDCLASS wc;

	wc.style=0;
	wc.lpfnWndProc=WndProc;
	wc.cbClsExtra=0;
	wc.cbWndExtra=0;
	wc.hInstance=hinst;
	wc.hIcon=::LoadIcon(hinst,MAKEINTRESOURCE(IDI_ICON));
	wc.hCursor=::LoadCursor(nullptr,IDC_ARROW);
	wc.hbrBackground=(HBRUSH)(COLOR_3DFACE+1);
	wc.lpszMenuName=nullptr;
	wc.lpszClassName=MAIN_WINDOW_CLASS;
	return ::RegisterClass(&wc)!=0 && CFullscreen::Initialize(hinst);
}


CMainWindow::CMainWindow(CAppMain &App)
	: m_App(App)
	, m_Viewer(App)
	, m_TitleBarManager(this,true)
	, m_SideBarManager(this)
	, m_StatusViewEventHandler(this)
	, m_VideoContainerEventHandler(this)
	, m_ViewWindowEventHandler(this)
	, m_Fullscreen(*this)

	, m_fShowStatusBar(true)
	, m_fPopupStatusBar(true)
	, m_fShowTitleBar(true)
	, m_fCustomTitleBar(true)
	, m_fPopupTitleBar(true)
	, m_fSplitTitleBar(true)
	, m_fShowSideBar(false)
	, m_PanelPaneIndex(1)
	, m_fCustomFrame(false)
	, m_CustomFrameWidth(0)
	, m_ThinFrameWidth(1)
	, m_fViewWindowEdge(true)

	, m_fEnablePlayback(true)

	, m_fStandbyInit(false)
	, m_fMinimizeInit(false)

	, m_WindowSizeMode(WINDOW_SIZE_HD)

	, m_fLockLayout(false)

	, m_fProgramGuideUpdating(false)
	, m_fEpgUpdateChannelChange(false)

	, m_fShowCursor(true)
	, m_fNoHideCursor(false)

	, m_fClosing(false)

	, m_WheelCount(0)
	, m_PrevWheelMode(COperationOptions::WHEEL_MODE_NONE)
	, m_PrevWheelTime(0)

	, m_AspectRatioType(ASPECTRATIO_DEFAULT)
	, m_AspectRatioResetTime(0)
	, m_fForceResetPanAndScan(false)
	, m_DefaultAspectRatioMenuItemCount(0)
	, m_fFrameCut(false)
	, m_ProgramListUpdateTimerCount(0)
	, m_CurEventStereoMode(-1)
	, m_fAlertedLowFreeSpace(false)
	, m_ResetErrorCountTimer(TIMER_ID_RESETERRORCOUNT)
	, m_ChannelNoInputTimeout(3000)
	, m_ChannelNoInputTimer(TIMER_ID_CHANNELNO)
	, m_DisplayBaseEventHandler(this)
{
	// �K���Ƀf�t�H���g�T�C�Y��ݒ�
#ifndef TVTEST_FOR_1SEG
	m_WindowPosition.Width=960;
	m_WindowPosition.Height=540;
#else
	m_WindowPosition.Width=400;
	m_WindowPosition.Height=320;
#endif
	m_WindowPosition.Left=
		(::GetSystemMetrics(SM_CXSCREEN)-m_WindowPosition.Width)/2;
	m_WindowPosition.Top=
		(::GetSystemMetrics(SM_CYSCREEN)-m_WindowPosition.Height)/2;
}


CMainWindow::~CMainWindow()
{
	Destroy();
	if (m_atomChildOldWndProcProp!=0) {
		::GlobalDeleteAtom(m_atomChildOldWndProcProp);
		m_atomChildOldWndProcProp=0;
	}
}


bool CMainWindow::Create(HWND hwndParent,DWORD Style,DWORD ExStyle,int ID)
{
	if (m_pCore->GetAlwaysOnTop())
		ExStyle|=WS_EX_TOPMOST;
	if (!CreateBasicWindow(nullptr,Style,ExStyle,ID,MAIN_WINDOW_CLASS,MAIN_TITLE_TEXT,m_App.GetInstance()))
		return false;
	return true;
}


bool CMainWindow::Show(int CmdShow)
{
	return ::ShowWindow(m_hwnd,m_WindowPosition.fMaximized?SW_SHOWMAXIMIZED:CmdShow)!=FALSE;
}


void CMainWindow::CreatePanel()
{
	m_App.Panel.Form.Create(m_hwnd,WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_App.Panel.Form.SetTabFont(m_App.PanelOptions.GetFont());

	m_App.Panel.InfoPanel.SetProgramInfoRichEdit(m_App.PanelOptions.GetProgramInfoUseRichEdit());
	m_App.Panel.InfoPanel.Create(m_App.Panel.Form.GetHandle(),WS_CHILD | WS_CLIPCHILDREN);
	m_App.Panel.InfoPanel.ShowSignalLevel(!m_App.DriverOptions.IsNoSignalLevel(m_App.CoreEngine.GetDriverFileName()));
	m_App.Panel.Form.AddWindow(&m_App.Panel.InfoPanel,PANEL_ID_INFORMATION,TEXT("���"));

	m_App.Panel.ProgramListPanel.SetEpgProgramList(&m_App.EpgProgramList);
	m_App.Panel.ProgramListPanel.SetVisibleEventIcons(m_App.ProgramGuideOptions.GetVisibleEventIcons());
	m_App.Panel.ProgramListPanel.Create(m_App.Panel.Form.GetHandle(),WS_CHILD | WS_VSCROLL);
	m_App.Panel.Form.AddWindow(&m_App.Panel.ProgramListPanel,PANEL_ID_PROGRAMLIST,TEXT("�ԑg�\"));

	m_App.Panel.ChannelPanel.SetEpgProgramList(&m_App.EpgProgramList);
	m_App.Panel.ChannelPanel.SetLogoManager(&m_App.LogoManager);
	m_App.Panel.ChannelPanel.Create(m_App.Panel.Form.GetHandle(),WS_CHILD | WS_VSCROLL);
	m_App.Panel.Form.AddWindow(&m_App.Panel.ChannelPanel,PANEL_ID_CHANNEL,TEXT("�`�����l��"));

	m_App.Panel.ControlPanel.SetSendMessageWindow(m_hwnd);
	InitControlPanel();
	m_App.Panel.ControlPanel.Create(m_App.Panel.Form.GetHandle(),WS_CHILD);
	m_App.Panel.Form.AddWindow(&m_App.Panel.ControlPanel,PANEL_ID_CONTROL,TEXT("����"));

	m_App.Panel.CaptionPanel.Create(m_App.Panel.Form.GetHandle(),WS_CHILD | WS_CLIPCHILDREN);
	m_App.Panel.Form.AddWindow(&m_App.Panel.CaptionPanel,PANEL_ID_CAPTION,TEXT("����"));

	m_App.PanelOptions.InitializePanelForm(&m_App.Panel.Form);
	m_App.Panel.Frame.Create(m_hwnd,
		dynamic_cast<Layout::CSplitter*>(m_LayoutBase.GetContainerByID(CONTAINER_ID_PANELSPLITTER)),
		CONTAINER_ID_PANEL,&m_App.Panel.Form,TEXT("�p�l��"));

	if (m_fCustomFrame) {
		HookWindows(m_App.Panel.Frame.GetPanel()->GetHandle());
	}
}


bool CMainWindow::InitializeViewer(BYTE VideoStreamType)
{
	const bool fEnableViewer=IsViewerEnabled();

	m_App.CoreEngine.m_DtvEngine.SetTracer(&m_App.StatusView);

	if (m_Viewer.BuildViewer(VideoStreamType)) {
		CMediaViewer &MediaViewer=m_App.CoreEngine.m_DtvEngine.m_MediaViewer;
		TCHAR szText[256];

		m_App.Panel.InfoPanel.SetVideoDecoderName(
			MediaViewer.GetVideoDecoderName(szText,lengthof(szText))?szText:nullptr);
		m_App.Panel.InfoPanel.SetVideoRendererName(
			MediaViewer.GetVideoRendererName(szText,lengthof(szText))?szText:nullptr);
		m_App.Panel.InfoPanel.SetAudioDeviceName(
			MediaViewer.GetAudioRendererName(szText,lengthof(szText))?szText:nullptr);
		if (fEnableViewer)
			m_pCore->EnableViewer(true);
		if (m_fCustomFrame)
			HookWindows(m_Viewer.GetVideoContainer().GetHandle());
	} else {
		FinalizeViewer();
	}

	m_App.CoreEngine.m_DtvEngine.SetTracer(nullptr);
	m_App.StatusView.SetSingleText(nullptr);

	return true;
}


bool CMainWindow::FinalizeViewer()
{
	m_Viewer.CloseViewer();

	m_fEnablePlayback=false;

	m_App.MainMenu.CheckItem(CM_DISABLEVIEWER,true);
	m_App.SideBar.CheckItem(CM_DISABLEVIEWER,true);

	m_App.Panel.InfoPanel.SetVideoDecoderName(nullptr);
	m_App.Panel.InfoPanel.SetVideoRendererName(nullptr);
	m_App.Panel.InfoPanel.SetAudioDeviceName(nullptr);

	return true;
}


bool CMainWindow::OnFullscreenChange(bool fFullscreen)
{
	if (fFullscreen) {
		if (::IsIconic(m_hwnd))
			::ShowWindow(m_hwnd,SW_RESTORE);
		if (!m_Fullscreen.Create(m_hwnd,&m_Viewer))
			return false;
	} else {
		ForegroundWindow(m_hwnd);
		m_Fullscreen.Destroy();
		m_App.CoreEngine.m_DtvEngine.m_MediaViewer.SetViewStretchMode(
			m_fFrameCut?CMediaViewer::STRETCH_CUTFRAME:
						CMediaViewer::STRETCH_KEEPASPECTRATIO);
	}
	m_App.StatusView.UpdateItem(STATUS_ITEM_VIDEOSIZE);
	m_App.Panel.ControlPanel.UpdateItem(CONTROLPANEL_ITEM_VIDEO);
	m_App.MainMenu.CheckItem(CM_FULLSCREEN,fFullscreen);
	m_App.SideBar.CheckItem(CM_FULLSCREEN,fFullscreen);
	m_App.SideBar.CheckItem(CM_PANEL,
		fFullscreen?m_Fullscreen.IsPanelVisible():m_App.Panel.fShowPanelWindow);
	return true;
}


HWND CMainWindow::GetVideoHostWindow() const
{
	if (m_pCore->GetStandby())
		return nullptr;
	if (m_pCore->GetFullscreen())
		return m_Fullscreen.GetHandle();
	return m_hwnd;
}


void CMainWindow::ShowNotificationBar(LPCTSTR pszText,
									  CNotificationBar::MessageType Type,
									  DWORD Duration,bool fSkippable)
{
	m_NotificationBar.SetFont(m_App.OSDOptions.GetNotificationBarFont());
	m_NotificationBar.Show(
		pszText,Type,
		max((DWORD)m_App.OSDOptions.GetNotificationBarDuration(),Duration),
		fSkippable);
}


void CMainWindow::AdjustWindowSize(int Width,int Height,bool fScreenSize)
{
	RECT rcOld,rc;

	GetPosition(&rcOld);

	if (fScreenSize) {
		::SetRect(&rc,0,0,Width,Height);
		m_Viewer.GetViewWindow().CalcWindowRect(&rc);
		Width=rc.right-rc.left;
		Height=rc.bottom-rc.top;
		m_LayoutBase.GetScreenPosition(&rc);
		rc.right=rc.left+Width;
		rc.bottom=rc.top+Height;
		if (m_fShowTitleBar && m_fCustomTitleBar)
			m_TitleBarManager.ReserveArea(&rc,true);
		if (m_fShowSideBar)
			m_SideBarManager.ReserveArea(&rc,true);
		if (m_App.Panel.fShowPanelWindow && !m_App.Panel.IsFloating()) {
			Layout::CSplitter *pSplitter=dynamic_cast<Layout::CSplitter*>(
				m_LayoutBase.GetContainerByID(CONTAINER_ID_PANELSPLITTER));
			rc.right+=pSplitter->GetBarWidth()+pSplitter->GetPaneSize(CONTAINER_ID_PANEL);
		}
		if (m_fShowStatusBar)
			rc.bottom+=m_App.StatusView.CalcHeight(rc.right-rc.left);
		if (m_fCustomFrame)
			::InflateRect(&rc,m_CustomFrameWidth,m_CustomFrameWidth);
		else
			CalcPositionFromClientRect(&rc);
	} else {
		rc.left=rcOld.left;
		rc.top=rcOld.top;
		rc.right=rc.left+Width;
		rc.bottom=rc.top+Height;
	}

	if (::EqualRect(&rc,&rcOld))
		return;

	const HMONITOR hMonitor=::MonitorFromRect(&rcOld,MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi;
	mi.cbSize=sizeof(mi);
	::GetMonitorInfo(hMonitor,&mi);

	if (m_App.ViewOptions.GetNearCornerResizeOrigin()) {
		if (abs(rcOld.left-mi.rcWork.left)>abs(rcOld.right-mi.rcWork.right)) {
			rc.left=rcOld.right-(rc.right-rc.left);
			rc.right=rcOld.right;
		}
		if (abs(rcOld.top-mi.rcWork.top)>abs(rcOld.bottom-mi.rcWork.bottom)) {
			rc.top=rcOld.bottom-(rc.bottom-rc.top);
			rc.bottom=rcOld.bottom;
		}
	}

	// �E�B���h�E�����j�^�̊O�ɏo�Ȃ��悤�ɂ���
	if (rcOld.left>=mi.rcWork.left && rcOld.top>=mi.rcWork.top
			&& rcOld.right<=mi.rcWork.right && rcOld.bottom<=mi.rcWork.bottom) {
		if (rc.right>mi.rcWork.right && rc.left>mi.rcWork.left)
			::OffsetRect(&rc,max(mi.rcWork.right-rc.right,mi.rcWork.left-rc.left),0);
		if (rc.bottom>mi.rcWork.bottom && rc.top>mi.rcWork.top)
			::OffsetRect(&rc,0,max(mi.rcWork.bottom-rc.bottom,mi.rcWork.top-rc.top));
	}

	SetPosition(&rc);
	m_App.Panel.OnOwnerMovingOrSizing(&rcOld,&rc);
}


bool CMainWindow::ReadSettings(CSettings &Settings)
{
	int Left,Top,Width,Height,Value;
	bool f;

	GetPosition(&Left,&Top,&Width,&Height);
	Settings.Read(TEXT("WindowLeft"),&Left);
	Settings.Read(TEXT("WindowTop"),&Top);
	Settings.Read(TEXT("WindowWidth"),&Width);
	Settings.Read(TEXT("WindowHeight"),&Height);
	SetPosition(Left,Top,Width,Height);
	MoveToMonitorInside();
	if (Settings.Read(TEXT("WindowMaximize"),&f))
		SetMaximize(f);

	Settings.Read(TEXT("WindowSize.HD.Width"),&m_HDWindowSize.Width);
	Settings.Read(TEXT("WindowSize.HD.Height"),&m_HDWindowSize.Height);
	Settings.Read(TEXT("WindowSize.1Seg.Width"),&m_1SegWindowSize.Width);
	Settings.Read(TEXT("WindowSize.1Seg.Height"),&m_1SegWindowSize.Height);
	if (m_HDWindowSize.Width==Width && m_HDWindowSize.Height==Height)
		m_WindowSizeMode=WINDOW_SIZE_HD;
	else if (m_1SegWindowSize.Width==Width && m_1SegWindowSize.Height==Height)
		m_WindowSizeMode=WINDOW_SIZE_1SEG;

	if (Settings.Read(TEXT("AlwaysOnTop"),&f))
		m_pCore->SetAlwaysOnTop(f);
	if (Settings.Read(TEXT("ShowStatusBar"),&f))
		SetStatusBarVisible(f);
	Settings.Read(TEXT("PopupStatusBar"),&m_fPopupStatusBar);
	if (Settings.Read(TEXT("ShowTitleBar"),&f))
		SetTitleBarVisible(f);
	Settings.Read(TEXT("PopupTitleBar"),&m_fPopupTitleBar);
	if (Settings.Read(TEXT("PanelDockingIndex"),&Value)
			&& (Value==0 || Value==1))
		m_PanelPaneIndex=Value;
	if (Settings.Read(TEXT("FullscreenPanelWidth"),&Value))
		m_Fullscreen.SetPanelWidth(Value);
	if (Settings.Read(TEXT("ThinFrameWidth"),&Value))
		m_ThinFrameWidth=max(Value,1);
	Value=FRAME_NORMAL;
	if (!Settings.Read(TEXT("FrameType"),&Value)) {
		if (Settings.Read(TEXT("ThinFrame"),&f) && f)	// �ȑO�̃o�[�W�����Ƃ̌݊��p
			Value=FRAME_CUSTOM;
	}
	SetCustomFrame(Value!=FRAME_NORMAL,Value==FRAME_CUSTOM?m_ThinFrameWidth:0);
	if (!m_fCustomFrame && Settings.Read(TEXT("CustomTitleBar"),&f))
		SetCustomTitleBar(f);
	Settings.Read(TEXT("SplitTitleBar"),&m_fSplitTitleBar);
	Settings.Read(TEXT("ClientEdge"),&m_fViewWindowEdge);
	if (Settings.Read(TEXT("ShowSideBar"),&f))
		SetSideBarVisible(f);
	Settings.Read(TEXT("FrameCut"),&m_fFrameCut);

	return true;
}


bool CMainWindow::WriteSettings(CSettings &Settings)
{
	int Left,Top,Width,Height;

	GetPosition(&Left,&Top,&Width,&Height);
	Settings.Write(TEXT("WindowLeft"),Left);
	Settings.Write(TEXT("WindowTop"),Top);
	Settings.Write(TEXT("WindowWidth"),Width);
	Settings.Write(TEXT("WindowHeight"),Height);
	Settings.Write(TEXT("WindowMaximize"),GetMaximize());
	Settings.Write(TEXT("WindowSize.HD.Width"),m_HDWindowSize.Width);
	Settings.Write(TEXT("WindowSize.HD.Height"),m_HDWindowSize.Height);
	Settings.Write(TEXT("WindowSize.1Seg.Width"),m_1SegWindowSize.Width);
	Settings.Write(TEXT("WindowSize.1Seg.Height"),m_1SegWindowSize.Height);
	Settings.Write(TEXT("AlwaysOnTop"),m_pCore->GetAlwaysOnTop());
	Settings.Write(TEXT("ShowStatusBar"),m_fShowStatusBar);
//	Settings.Write(TEXT("PopupStatusBar"),m_fPopupStatusBar);
	Settings.Write(TEXT("ShowTitleBar"),m_fShowTitleBar);
//	Settings.Write(TEXT("PopupTitleBar"),m_fPopupTitleBar);
	Settings.Write(TEXT("PanelDockingIndex"),m_PanelPaneIndex);
	Settings.Write(TEXT("FullscreenPanelWidth"),m_Fullscreen.GetPanelWidth());
	Settings.Write(TEXT("FrameType"),
		!m_fCustomFrame?FRAME_NORMAL:(m_CustomFrameWidth==0?FRAME_NONE:FRAME_CUSTOM));
//	Settings.Write(TEXT("ThinFrameWidth"),m_ThinFrameWidth);
	Settings.Write(TEXT("CustomTitleBar"),m_fCustomTitleBar);
	Settings.Write(TEXT("SplitTitleBar"),m_fSplitTitleBar);
	Settings.Write(TEXT("ClientEdge"),m_fViewWindowEdge);
	Settings.Write(TEXT("ShowSideBar"),m_fShowSideBar);
	Settings.Write(TEXT("FrameCut"),m_fFrameCut);

	return true;
}


bool CMainWindow::SetAlwaysOnTop(bool fTop)
{
	if (m_hwnd!=nullptr) {
		::SetWindowPos(m_hwnd,fTop?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,
					   SWP_NOMOVE | SWP_NOSIZE);
		m_App.MainMenu.CheckItem(CM_ALWAYSONTOP,fTop);
		m_App.SideBar.CheckItem(CM_ALWAYSONTOP,fTop);
	}
	return true;
}


void CMainWindow::ShowPanel(bool fShow)
{
	if (m_App.Panel.fShowPanelWindow==fShow)
		return;

	m_App.Panel.fShowPanelWindow=fShow;

	LockLayout();

	m_App.Panel.Frame.SetPanelVisible(fShow);

	if (!fShow) {
		m_App.Panel.InfoPanel.ResetStatistics();
		//m_App.Panel.ProgramListPanel.ClearProgramList();
		m_App.Panel.ChannelPanel.ClearChannelList();
	}

	if (!m_App.Panel.IsFloating()) {
		// �p�l���̕��ɍ��킹�ăE�B���h�E�T�C�Y���g�k
		Layout::CSplitter *pSplitter=
			dynamic_cast<Layout::CSplitter*>(m_LayoutBase.GetContainerByID(CONTAINER_ID_PANELSPLITTER));
		const int Width=m_App.Panel.Frame.GetDockingWidth()+pSplitter->GetBarWidth();
		RECT rc;

		GetPosition(&rc);
		if (pSplitter->GetPane(0)->GetID()==CONTAINER_ID_PANEL) {
			if (fShow)
				rc.left-=Width;
			else
				rc.left+=Width;
		} else {
			if (fShow)
				rc.right+=Width;
			else
				rc.right-=Width;
		}
		SetPosition(&rc);
		if (!fShow)
			::SetFocus(m_hwnd);
	}

	UpdateLayout();

	if (fShow)
		UpdatePanel();

	m_App.MainMenu.CheckItem(CM_PANEL,fShow);
	m_App.SideBar.CheckItem(CM_PANEL,fShow);
}


void CMainWindow::SetStatusBarVisible(bool fVisible)
{
	if (m_fShowStatusBar!=fVisible) {
		if (!m_pCore->GetFullscreen()) {
			m_fShowStatusBar=fVisible;
			if (m_hwnd!=nullptr) {
				LockLayout();

				RECT rc;

				if (fVisible) {
					// ��u�ςȈʒu�ɏo�Ȃ��悤�Ɍ����Ȃ��ʒu�Ɉړ�
					RECT rcClient;
					::GetClientRect(m_App.StatusView.GetParent(),&rcClient);
					m_App.StatusView.GetPosition(&rc);
					m_App.StatusView.SetPosition(0,rcClient.bottom,rc.right-rc.left,rc.bottom-rc.top);
				}
				m_LayoutBase.SetContainerVisible(CONTAINER_ID_STATUS,fVisible);

				GetPosition(&rc);
				if (fVisible)
					rc.bottom+=m_App.StatusView.GetHeight();
				else
					rc.bottom-=m_App.StatusView.GetHeight();
				SetPosition(&rc);

				UpdateLayout();

				//m_App.MainMenu.CheckItem(CM_STATUSBAR,fVisible);
				m_App.SideBar.CheckItem(CM_STATUSBAR,fVisible);
			}
		}
	}
}


void CMainWindow::SetTitleBarVisible(bool fVisible)
{
	if (m_fShowTitleBar!=fVisible) {
		m_fShowTitleBar=fVisible;
		if (m_hwnd!=nullptr) {
			bool fMaximize=GetMaximize();
			RECT rc;

			LockLayout();

			if (!fMaximize)
				GetPosition(&rc);
			if (!m_fCustomTitleBar)
				SetStyle(GetStyle()^WS_CAPTION,fMaximize);
			else if (!fVisible)
				m_LayoutBase.SetContainerVisible(CONTAINER_ID_TITLEBAR,false);
			if (!fMaximize) {
				int CaptionHeight;

				if (!m_fCustomTitleBar)
					CaptionHeight=::GetSystemMetrics(SM_CYCAPTION);
				else
					CaptionHeight=m_TitleBar.GetHeight();
				if (fVisible)
					rc.top-=CaptionHeight;
				else
					rc.top+=CaptionHeight;
				::SetWindowPos(m_hwnd,nullptr,rc.left,rc.top,
							   rc.right-rc.left,rc.bottom-rc.top,
							   SWP_NOZORDER | SWP_FRAMECHANGED | SWP_DRAWFRAME);
			}
			if (m_fCustomTitleBar && fVisible)
				m_LayoutBase.SetContainerVisible(CONTAINER_ID_TITLEBAR,true);

			UpdateLayout();

			//m_App.MainMenu.CheckItem(CM_TITLEBAR,fVisible);
		}
	}
}


// �^�C�g���o�[��Ǝ��̂��̂ɂ��邩�ݒ�
void CMainWindow::SetCustomTitleBar(bool fCustom)
{
	if (m_fCustomTitleBar!=fCustom) {
		if (!fCustom && m_fCustomFrame)
			SetCustomFrame(false);
		m_fCustomTitleBar=fCustom;
		if (m_hwnd!=nullptr) {
			if (m_fShowTitleBar) {
				if (!fCustom)
					m_LayoutBase.SetContainerVisible(CONTAINER_ID_TITLEBAR,false);
				SetStyle(GetStyle()^WS_CAPTION,true);
				if (fCustom)
					m_LayoutBase.SetContainerVisible(CONTAINER_ID_TITLEBAR,true);
			}
			//m_App.MainMenu.CheckItem(CM_CUSTOMTITLEBAR,fCustom);
			m_App.MainMenu.EnableItem(CM_SPLITTITLEBAR,!m_fCustomFrame && fCustom);
		}
	}
}


// �^�C�g���o�[���p�l���ŕ������邩�ݒ�
void CMainWindow::SetSplitTitleBar(bool fSplit)
{
	if (m_fSplitTitleBar!=fSplit) {
		m_fSplitTitleBar=fSplit;
		if (m_fCustomTitleBar && m_hwnd!=nullptr) {
			Layout::CSplitter *pSideBarSplitter,*pTitleBarSplitter,*pPanelSplitter;
			Layout::CSplitter *pStatusSplitter,*pParentSplitter;

			pSideBarSplitter=dynamic_cast<Layout::CSplitter*>(
				m_LayoutBase.GetContainerByID(CONTAINER_ID_SIDEBARSPLITTER));
			pTitleBarSplitter=dynamic_cast<Layout::CSplitter*>(
				m_LayoutBase.GetContainerByID(CONTAINER_ID_TITLEBARSPLITTER));
			pPanelSplitter=dynamic_cast<Layout::CSplitter*>(
				m_LayoutBase.GetContainerByID(CONTAINER_ID_PANELSPLITTER));
			pStatusSplitter=dynamic_cast<Layout::CSplitter*>(
				m_LayoutBase.GetContainerByID(CONTAINER_ID_STATUSSPLITTER));
			if (pSideBarSplitter==nullptr || pTitleBarSplitter==nullptr
					|| pPanelSplitter==nullptr || pStatusSplitter==nullptr)
				return;
			const int PanelPane=GetPanelPaneIndex();
			if (fSplit) {
				pTitleBarSplitter->ReplacePane(1,pSideBarSplitter);
				pTitleBarSplitter->SetAdjustPane(CONTAINER_ID_SIDEBARSPLITTER);
				pPanelSplitter->ReplacePane(1-PanelPane,pTitleBarSplitter);
				pPanelSplitter->SetAdjustPane(CONTAINER_ID_TITLEBARSPLITTER);
				pParentSplitter=pPanelSplitter;
			} else {
				pPanelSplitter->ReplacePane(1-PanelPane,pSideBarSplitter);
				pPanelSplitter->SetAdjustPane(CONTAINER_ID_SIDEBARSPLITTER);
				pTitleBarSplitter->ReplacePane(1,pPanelSplitter);
				pTitleBarSplitter->SetAdjustPane(CONTAINER_ID_PANELSPLITTER);
				pParentSplitter=pTitleBarSplitter;
			}
			pStatusSplitter->ReplacePane(0,pParentSplitter);
			pStatusSplitter->SetAdjustPane(pParentSplitter->GetID());
			m_LayoutBase.Adjust();
			//m_App.MainMenu.CheckItem(CM_SPLITTITLEBAR,fSplit);
		}
	}
}


// �E�B���h�E�g��Ǝ��̂��̂ɂ��邩�ݒ�
void CMainWindow::SetCustomFrame(bool fCustomFrame,int Width)
{
	if (m_fCustomFrame!=fCustomFrame || (fCustomFrame && m_CustomFrameWidth!=Width)) {
		if (fCustomFrame && Width<0)
			return;
		if (fCustomFrame && !m_fCustomTitleBar)
			SetCustomTitleBar(true);
		m_fCustomFrame=fCustomFrame;
		if (fCustomFrame)
			m_CustomFrameWidth=Width;
		if (m_hwnd!=nullptr) {
			::SetWindowPos(m_hwnd,nullptr,0,0,0,0,
						   SWP_FRAMECHANGED | SWP_DRAWFRAME | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
			CAeroGlass Aero;
			Aero.EnableNcRendering(m_hwnd,!fCustomFrame);
			if (fCustomFrame) {
				HookWindows(m_LayoutBase.GetHandle());
				HookWindows(m_App.Panel.Form.GetHandle());
			}
			/*
			m_App.MainMenu.CheckRadioItem(CM_WINDOWFRAME_NORMAL,CM_WINDOWFRAME_NONE,
				!fCustomFrame?CM_WINDOWFRAME_NORMAL:
					(m_CustomFrameWidth==0?CM_WINDOWFRAME_NONE:CM_WINDOWFRAME_CUSTOM));
			m_App.MainMenu.EnableItem(CM_CUSTOMTITLEBAR,!fCustomFrame);
			m_App.MainMenu.EnableItem(CM_SPLITTITLEBAR,!fCustomFrame && m_fCustomTitleBar);
			*/
		}
	}
}


void CMainWindow::SetSideBarVisible(bool fVisible)
{
	if (m_fShowSideBar!=fVisible) {
		m_fShowSideBar=fVisible;
		if (m_hwnd!=nullptr) {
			LockLayout();

			RECT rc;

			if (fVisible) {
				// ��u�ςȈʒu�ɏo�Ȃ��悤�Ɍ����Ȃ��ʒu�Ɉړ�
				RECT rcClient;
				::GetClientRect(m_App.SideBar.GetParent(),&rcClient);
				m_App.SideBar.GetPosition(&rc);
				m_App.SideBar.SetPosition(0,rcClient.bottom,rc.right-rc.left,rc.bottom-rc.top);
			}
			m_LayoutBase.SetContainerVisible(CONTAINER_ID_SIDEBAR,fVisible);

			GetPosition(&rc);
			RECT rcArea=rc;
			if (fVisible)
				m_SideBarManager.ReserveArea(&rcArea,true);
			else
				m_SideBarManager.AdjustArea(&rcArea);
			rc.right=rc.left+(rcArea.right-rcArea.left);
			rc.bottom=rc.top+(rcArea.bottom-rcArea.top);
			SetPosition(&rc);

			UpdateLayout();

			//m_App.MainMenu.CheckItem(CM_SIDEBAR,fVisible);
		}
	}
}


bool CMainWindow::OnBarMouseLeave(HWND hwnd)
{
	if (m_pCore->GetFullscreen()) {
		m_Fullscreen.OnMouseMove();
		return true;
	}

	POINT pt;

	::GetCursorPos(&pt);
	::ScreenToClient(::GetParent(hwnd),&pt);
	if (hwnd==m_TitleBar.GetHandle()) {
		if (!m_fShowTitleBar) {
			if (!m_fShowSideBar && m_App.SideBar.GetVisible()
					&& m_App.SideBarOptions.GetPlace()==CSideBarOptions::PLACE_TOP) {
				if (::RealChildWindowFromPoint(m_App.SideBar.GetParent(),pt)==m_App.SideBar.GetHandle())
					return false;
			}
			m_TitleBar.SetVisible(false);
			if (!m_fShowSideBar && m_App.SideBar.GetVisible())
				m_App.SideBar.SetVisible(false);
			return true;
		}
	} else if (hwnd==m_App.StatusView.GetHandle()) {
		if (!m_fShowStatusBar) {
			if (!m_fShowSideBar && m_App.SideBar.GetVisible()
					&& m_App.SideBarOptions.GetPlace()==CSideBarOptions::PLACE_BOTTOM) {
				if (::RealChildWindowFromPoint(m_App.SideBar.GetParent(),pt)==m_App.SideBar.GetHandle())
					return false;
			}
			m_App.StatusView.SetVisible(false);
			if (!m_fShowSideBar && m_App.SideBar.GetVisible())
				m_App.SideBar.SetVisible(false);
			return true;
		}
	} else if (hwnd==m_App.SideBar.GetHandle()) {
		if (!m_fShowSideBar) {
			m_App.SideBar.SetVisible(false);
			if (!m_fShowTitleBar && m_TitleBar.GetVisible()
					&& ::RealChildWindowFromPoint(m_TitleBar.GetParent(),pt)!=m_TitleBar.GetHandle())
				m_TitleBar.SetVisible(false);
			if (!m_fShowStatusBar && m_App.StatusView.GetVisible()
					&& ::RealChildWindowFromPoint(m_App.StatusView.GetParent(),pt)!=m_App.StatusView.GetHandle())
				m_App.StatusView.SetVisible(false);
			return true;
		}
	}

	return false;
}


int CMainWindow::GetPanelPaneIndex() const
{
	Layout::CSplitter *pSplitter=
		dynamic_cast<Layout::CSplitter*>(m_LayoutBase.GetContainerByID(CONTAINER_ID_PANELSPLITTER));

	if (pSplitter==nullptr)
		return m_PanelPaneIndex;
	return pSplitter->IDToIndex(CONTAINER_ID_PANEL);
}


LRESULT CMainWindow::OnMessage(HWND hwnd,UINT uMsg,WPARAM wParam,LPARAM lParam)
{
	switch (uMsg) {
	HANDLE_MSG(hwnd,WM_COMMAND,OnCommand);
	HANDLE_MSG(hwnd,WM_TIMER,OnTimer);

	case WM_SIZE:
		OnSizeChanged((UINT)wParam,LOWORD(lParam),HIWORD(lParam));
		return 0;

	case WM_SIZING:
		if (OnSizeChanging((UINT)wParam,reinterpret_cast<LPRECT>(lParam)))
			return TRUE;
		break;

	case WM_GETMINMAXINFO:
		{
			LPMINMAXINFO pmmi=reinterpret_cast<LPMINMAXINFO>(lParam);
			SIZE sz;
			RECT rc;

			m_LayoutBase.GetMinSize(&sz);
			::SetRect(&rc,0,0,sz.cx,sz.cy);
			CalcPositionFromClientRect(&rc);
			pmmi->ptMinTrackSize.x=rc.right-rc.left;
			pmmi->ptMinTrackSize.y=rc.bottom-rc.top;
		}
		return 0;

	case WM_MOVE:
		m_App.OSDManager.OnParentMove();
		return 0;

	case WM_SHOWWINDOW:
		if (!wParam) {
			m_App.OSDManager.ClearOSD();
			m_App.OSDManager.Reset();
		}
		break;

	case WM_RBUTTONDOWN:
		if (m_pCore->GetFullscreen()) {
			m_Fullscreen.OnRButtonDown();
		} else {
			::SendMessage(hwnd,WM_COMMAND,
				MAKEWPARAM(m_App.OperationOptions.GetRightClickCommand(),COMMAND_FROM_MOUSE),0);
		}
		return 0;

	case WM_MBUTTONDOWN:
		if (m_pCore->GetFullscreen()) {
			m_Fullscreen.OnMButtonDown();
		} else {
			::SendMessage(hwnd,WM_COMMAND,
				MAKEWPARAM(m_App.OperationOptions.GetMiddleClickCommand(),COMMAND_FROM_MOUSE),0);
		}
		return 0;

	case WM_NCLBUTTONDOWN:
		if (wParam!=HTCAPTION)
			break;
		ForegroundWindow(hwnd);
	case WM_LBUTTONDOWN:
		if (uMsg==WM_NCLBUTTONDOWN || m_App.OperationOptions.GetDisplayDragMove()) {
			/*
			m_ptDragStartPos.x=GET_X_LPARAM(lParam);
			m_ptDragStartPos.y=GET_Y_LPARAM(lParam);
			::ClientToScreen(hwnd,&m_ptDragStartPos);
			*/
			::GetCursorPos(&m_ptDragStartPos);
			::GetWindowRect(hwnd,&m_rcDragStart);
			::SetCapture(hwnd);
		}
		return 0;

	case WM_NCLBUTTONUP:
	case WM_LBUTTONUP:
		if (::GetCapture()==hwnd)
			::ReleaseCapture();
		return 0;

	case WM_CAPTURECHANGED:
		m_TitleBarManager.EndDrag();
		return 0;

	case WM_MOUSEMOVE:
		OnMouseMove(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
		return 0;

	case WM_LBUTTONDBLCLK:
		::SendMessage(hwnd,WM_COMMAND,
			MAKEWPARAM(m_App.OperationOptions.GetLeftDoubleClickCommand(),COMMAND_FROM_MOUSE),0);
		return 0;

	case WM_SETCURSOR:
		if (OnSetCursor(reinterpret_cast<HWND>(wParam),LOWORD(lParam),HIWORD(lParam)))
			return TRUE;
		break;

	case WM_SYSKEYDOWN:
		if (wParam!=VK_F10)
			break;
	case WM_KEYDOWN:
		{
			int Channel;

			if (wParam>=VK_F1 && wParam<=VK_F12) {
				if (!m_App.Accelerator.IsFunctionKeyChannelChange())
					break;
				Channel=((int)wParam-VK_F1)+1;
			} else if (wParam>=VK_NUMPAD0 && wParam<=VK_NUMPAD9) {
				if (m_ChannelNoInput.fInputting) {
					OnChannelNoInput((int)wParam-VK_NUMPAD0);
					break;
				}
				if (!m_App.Accelerator.IsNumPadChannelChange())
					break;
				if (wParam==VK_NUMPAD0)
					Channel=10;
				else
					Channel=(int)wParam-VK_NUMPAD0;
			} else if (wParam>='0' && wParam<='9') {
				if (m_ChannelNoInput.fInputting) {
					OnChannelNoInput((int)wParam-'0');
					break;
				}
				if (!m_App.Accelerator.IsDigitKeyChannelChange())
					break;
				if (wParam=='0')
					Channel=10;
				else
					Channel=(int)wParam-'0';
			} else if (wParam>=VK_F13 && wParam<=VK_F24
					&& !m_App.ControllerManager.IsControllerEnabled(TEXT("HDUS Remocon"))
					&& (::GetKeyState(VK_SHIFT)<0 || ::GetKeyState(VK_CONTROL)<0)) {
				ShowMessage(TEXT("�����R�����g�p���邽�߂ɂ́A���j���[�� [�v���O�C��] -> [HDUS�����R��] �Ń����R����L���ɂ��Ă��������B"),
							TEXT("���m�点"),MB_OK | MB_ICONINFORMATION);
				break;
			} else {
				break;
			}

			m_App.Core.SwitchChannelByNo(Channel,true);
		}
		return 0;

	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
		{
			bool fHorz=uMsg==WM_MOUSEHWHEEL;

			OnMouseWheel(wParam,lParam,fHorz);
			// WM_MOUSEHWHEEL �� 1��Ԃ��Ȃ��ƌJ��Ԃ������ė��Ȃ��炵��
			return fHorz;
		}

	case WM_MEASUREITEM:
		{
			LPMEASUREITEMSTRUCT pmis=reinterpret_cast<LPMEASUREITEMSTRUCT>(lParam);

			if (pmis->itemID>=CM_ASPECTRATIO_FIRST && pmis->itemID<=CM_ASPECTRATIO_3D_LAST) {
				if (m_App.AspectRatioIconMenu.OnMeasureItem(hwnd,wParam,lParam))
					return TRUE;
				break;
			}
			if (m_App.ChannelMenu.OnMeasureItem(hwnd,wParam,lParam))
				return TRUE;
			if (m_App.FavoritesMenu.OnMeasureItem(hwnd,wParam,lParam))
				return TRUE;
		}
		break;

	case WM_DRAWITEM:
		if (m_App.AspectRatioIconMenu.OnDrawItem(hwnd,wParam,lParam))
			return TRUE;
		if (m_App.ChannelMenu.OnDrawItem(hwnd,wParam,lParam))
			return TRUE;
		if (m_App.FavoritesMenu.OnDrawItem(hwnd,wParam,lParam))
			return TRUE;
		break;

// �E�B���h�E�g��Ǝ��̂��̂ɂ��邽�߂̃R�[�h
	case WM_NCACTIVATE:
		if (m_fCustomFrame)
			return TRUE;
		break;

	case WM_NCCALCSIZE:
		if (m_fCustomFrame) {
			if (wParam!=0) {
				NCCALCSIZE_PARAMS *pnccsp=reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);

				::InflateRect(&pnccsp->rgrc[0],-m_CustomFrameWidth,-m_CustomFrameWidth);
			}
			return 0;
		}
		break;

	case WM_NCPAINT:
		if (m_fCustomFrame) {
			HDC hdc=::GetWindowDC(hwnd);
			RECT rc,rcEmpty;

			::GetWindowRect(hwnd,&rc);
			::OffsetRect(&rc,-rc.left,-rc.top);
			rcEmpty=rc;
			::InflateRect(&rcEmpty,-m_CustomFrameWidth,-m_CustomFrameWidth);
			DrawUtil::FillBorder(hdc,&rc,&rcEmpty,&rc,
								 static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
			::ReleaseDC(hwnd,hdc);
			return 0;
		}
		break;

	case WM_NCHITTEST:
		if (m_fCustomFrame) {
			int x=GET_X_LPARAM(lParam),y=GET_Y_LPARAM(lParam);
			int BorderWidth=m_CustomFrameWidth;
			RECT rc;
			int Code=HTNOWHERE;

			::GetWindowRect(hwnd,&rc);
			if (x>=rc.left && x<rc.left+BorderWidth) {
				if (y>=rc.top) {
					if (y<rc.top+BorderWidth)
						Code=HTTOPLEFT;
					else if (y<rc.bottom-BorderWidth)
						Code=HTLEFT;
					else if (y<rc.bottom)
						Code=HTBOTTOMLEFT;
				}
			} else if (x>=rc.right-BorderWidth && x<rc.right) {
				if (y>=rc.top) {
					if (y<rc.top+BorderWidth)
						Code=HTTOPRIGHT;
					else if (y<rc.bottom-BorderWidth)
						Code=HTRIGHT;
					else if (y<rc.bottom)
						Code=HTBOTTOMRIGHT;
				}
			} else if (y>=rc.top && y<rc.top+BorderWidth) {
				Code=HTTOP;
			} else if (y>=rc.bottom-BorderWidth && y<rc.bottom) {
				Code=HTBOTTOM;
			} else {
				POINT pt={x,y};
				if (::PtInRect(&rc,pt))
					Code=HTCLIENT;
			}
			return Code;
		}
		break;
// �E�B���h�E�g��Ǝ��̂��̂ɂ��邽�߂̃R�[�h�I���

	case WM_INITMENUPOPUP:
		if (OnInitMenuPopup(reinterpret_cast<HMENU>(wParam)))
			return 0;
		break;

	case WM_UNINITMENUPOPUP:
		if (m_App.ChannelMenu.OnUninitMenuPopup(hwnd,wParam,lParam))
			return 0;
		if (m_App.FavoritesMenu.OnUninitMenuPopup(hwnd,wParam,lParam))
			return 0;
		break;

	case WM_MENUSELECT:
		if (m_App.ChannelMenu.OnMenuSelect(hwnd,wParam,lParam))
			return 0;
		if (m_App.FavoritesMenu.OnMenuSelect(hwnd,wParam,lParam))
			return 0;
		break;

	case WM_ENTERMENULOOP:
		m_fNoHideCursor=true;
		return 0;

	case WM_EXITMENULOOP:
		m_fNoHideCursor=false;
		return 0;

	case WM_SYSCOMMAND:
		switch ((wParam&0xFFFFFFF0UL)) {
		case SC_MONITORPOWER:
			if (m_App.ViewOptions.GetNoMonitorLowPower()
					&& m_pCore->IsViewerEnabled())
				return 0;
			break;

		case SC_SCREENSAVE:
			if (m_App.ViewOptions.GetNoScreenSaver()
					&& m_pCore->IsViewerEnabled())
				return 0;
			break;

		case SC_ABOUT:
			{
				CAboutDialog AboutDialog;

				AboutDialog.Show(GetVideoHostWindow());
			}
			return 0;

		case SC_MINIMIZE:
		case SC_MAXIMIZE:
		case SC_RESTORE:
			if (m_pCore->GetFullscreen())
				m_pCore->SetFullscreen(false);
			break;

		case SC_CLOSE:
			SendCommand(CM_CLOSE);
			return 0;
		}
		break;

	case WM_APPCOMMAND:
		{
			int Command=m_App.Accelerator.TranslateAppCommand(wParam,lParam);

			if (Command!=0) {
				SendCommand(Command);
				return TRUE;
			}
		}
		break;

	case WM_INPUT:
		return m_App.Accelerator.OnInput(hwnd,wParam,lParam);

	case WM_HOTKEY:
		{
			int Command=m_App.Accelerator.TranslateHotKey(wParam,lParam);

			if (Command>0)
				PostMessage(WM_COMMAND,Command,0);
		}
		return 0;

	case WM_SETFOCUS:
		m_Viewer.GetDisplayBase().SetFocus();
		return 0;

	case WM_SETTEXT:
		{
			LPCTSTR pszText=reinterpret_cast<LPCTSTR>(lParam);

			m_TitleBar.SetLabel(pszText);
			if (m_pCore->GetFullscreen())
				::SetWindowText(m_Fullscreen.GetHandle(),pszText);
		}
		break;

	case WM_SETICON:
		if (wParam==ICON_SMALL) {
			m_TitleBar.SetIcon(reinterpret_cast<HICON>(lParam));
			m_Fullscreen.SendMessage(uMsg,wParam,lParam);
		}
		break;

	case WM_POWERBROADCAST:
		if (wParam==PBT_APMSUSPEND) {
			m_App.Logger.AddLog(TEXT("�T�X�y���h�ւ̈ڍs�ʒm���󂯂܂����B"));
			if (m_fProgramGuideUpdating) {
				EndProgramGuideUpdate(0);
			} else if (!m_pCore->GetStandby()) {
				StoreTunerResumeInfo();
			}
			SuspendViewer(ResumeInfo::VIEWERSUSPEND_SUSPEND);
			m_App.Core.CloseTuner();
			FinalizeViewer();
		} else if (wParam==PBT_APMRESUMESUSPEND) {
			m_App.Logger.AddLog(TEXT("�T�X�y���h����̕��A�ʒm���󂯂܂����B"));
			if (!m_pCore->GetStandby()) {
				// �x��������������������?
				ResumeTuner();
			}
			ResumeViewer(ResumeInfo::VIEWERSUSPEND_SUSPEND);
		}
		break;

	case WM_DWMCOMPOSITIONCHANGED:
		m_App.OSDOptions.OnDwmCompositionChanged();
		return 0;

	case WM_APP_SERVICEUPDATE:
		// �T�[�r�X���X�V���ꂽ
		TRACE(TEXT("WM_APP_SERVICEUPDATE\n"));
		{
			CServiceUpdateInfo *pInfo=reinterpret_cast<CServiceUpdateInfo*>(lParam);
			int i;

			if (pInfo->m_fStreamChanged) {
				if (m_ResetErrorCountTimer.IsEnabled())
					m_ResetErrorCountTimer.Begin(hwnd,2000);

				m_Viewer.GetDisplayBase().SetVisible(false);
			}

			if (!m_App.Core.IsChannelScanning()
					&& pInfo->m_NumServices>0 && pInfo->m_CurService>=0) {
				const CChannelInfo *pChInfo=m_App.ChannelManager.GetCurrentRealChannelInfo();
				WORD ServiceID,TransportStreamID;

				TransportStreamID=pInfo->m_TransportStreamID;
				ServiceID=pInfo->m_pServiceList[pInfo->m_CurService].ServiceID;
				if (/*pInfo->m_fStreamChanged
						&& */TransportStreamID!=0 && ServiceID!=0
						&& !m_App.CoreEngine.IsNetworkDriver()
						&& (pChInfo==nullptr
						|| ((pChInfo->GetTransportStreamID()!=0
						&& pChInfo->GetTransportStreamID()!=TransportStreamID)
						|| (pChInfo->GetServiceID()!=0
						&& pChInfo->GetServiceID()!=ServiceID)))) {
					// �O������`�����l���ύX���ꂽ���A
					// BonDriver���J���ꂽ�Ƃ��̃f�t�H���g�`�����l��
					m_App.Core.FollowChannelChange(TransportStreamID,ServiceID);
				}
				if (pChInfo!=nullptr && !m_App.CoreEngine.IsNetworkDriver()) {
					// �`�����l���̏����X�V����
					// �Â��`�����l���ݒ�t�@�C���ɂ�NID��TSID�̏�񂪊܂܂�Ă��Ȃ�����
					const WORD NetworkID=pInfo->m_NetworkID;

					if (NetworkID!=0) {
						for (i=0;i<pInfo->m_NumServices;i++) {
							ServiceID=pInfo->m_pServiceList[i].ServiceID;
							if (ServiceID!=0) {
								m_App.ChannelManager.UpdateStreamInfo(
									pChInfo->GetSpace(),
									pChInfo->GetChannelIndex(),
									NetworkID,TransportStreamID,ServiceID);
							}
						}
					}
				}
				m_App.PluginManager.SendServiceUpdateEvent();
			} else if (pInfo->m_fServiceListEmpty && pInfo->m_fStreamChanged
					&& !m_App.Core.IsChannelScanning()
					&& !m_fProgramGuideUpdating) {
				ShowNotificationBar(TEXT("���̃`�����l���͕����x�~���ł�"),
									CNotificationBar::MESSAGE_INFO);
			}

			delete pInfo;

#ifdef NETWORK_REMOCON_SUPPORT
			if (m_App.pNetworkRemocon!=nullptr)
				m_App.pNetworkRemocon->GetChannel(&m_App.NetworkRemoconGetChannel);
#endif
		}
		return 0;

	case WM_APP_SERVICECHANGED:
		TRACE(TEXT("WM_APP_SERVICECHANGED\n"));
		m_App.AddLog(TEXT("�T�[�r�X��ύX���܂����B(SID %d)"),static_cast<int>(wParam));
		m_pCore->UpdateTitle();
		m_App.StatusView.UpdateItem(STATUS_ITEM_CHANNEL);
		return 0;

	case WM_APP_CHANGECASLIBRARY:
		TRACE(TEXT("WM_APP_CHANGECASLIBRARY\n"));
		if (!IsMessageInQueue(hwnd,WM_APP_CHANGECASLIBRARY)) {
			if (m_App.Core.LoadCasLibrary())
				m_App.Core.OpenCasCard(CAppCore::OPEN_CAS_CARD_NOTIFY_ERROR);
		}
		return 0;

#ifdef NETWORK_REMOCON_SUPPORT
	case WM_APP_CHANNELCHANGE:
		TRACE(TEXT("WM_APP_CHANNELCHANGE\n"));
		{
			const CChannelList &List=m_App.pNetworkRemocon->GetChannelList();

			m_App.ChannelManager.SetNetworkRemoconCurrentChannel((int)wParam);
			m_App.StatusView.UpdateItem(STATUS_ITEM_CHANNEL);
			m_App.Panel.ControlPanel.UpdateItem(CONTROLPANEL_ITEM_CHANNEL);
			const int ChannelNo=List.GetChannelNo(m_App.ChannelManager.GetNetworkRemoconCurrentChannel());
			m_App.MainMenu.CheckRadioItem(CM_CHANNELNO_FIRST,CM_CHANNELNO_LAST,
										  CM_CHANNELNO_FIRST+ChannelNo-1);
			m_App.SideBar.CheckRadioItem(CM_CHANNELNO_FIRST,CM_CHANNELNO_LAST,
										 CM_CHANNELNO_FIRST+ChannelNo-1);
		}
		return 0;
#endif

	/*
	case WM_APP_IMAGESAVE:
		{
			::MessageBox(nullptr,TEXT("�摜�̕ۑ��ŃG���[���������܂����B"),nullptr,
						 MB_OK | MB_ICONEXCLAMATION);
		}
		return 0;
	*/

	case WM_APP_TRAYICON:
		switch (lParam) {
		case WM_RBUTTONDOWN:
			{
				CPopupMenu Menu(m_App.GetResourceInstance(),IDM_TRAY);

				Menu.EnableItem(CM_SHOW,
								m_pCore->GetStandby() || IsMinimizeToTray());
				// ���񑩂��K�v�ȗ��R�͈ȉ����Q��
				// http://support.microsoft.com/kb/135788/en-us
				ForegroundWindow(hwnd);				// ����
				Menu.Show(hwnd);
				::PostMessage(hwnd,WM_NULL,0,0);	// ����
			}
			break;

		case WM_LBUTTONUP:
			SendCommand(CM_SHOW);
			break;
		}
		return 0;

	case WM_APP_EXECUTE:
		// �����N���֎~���ɕ����N�����ꂽ
		// (�V�����N�����ꂽ�v���Z�X���瑗���Ă���)
		TRACE(TEXT("WM_APP_EXECUTE\n"));
		{
			ATOM atom=(ATOM)wParam;
			TCHAR szCmdLine[256];

			szCmdLine[0]='\0';
			if (atom!=0) {
				::GlobalGetAtomName(atom,szCmdLine,lengthof(szCmdLine));
				::GlobalDeleteAtom(atom);
			}
			OnExecute(szCmdLine);
		}
		return 0;

	case WM_APP_QUERYPORT:
		// �g���Ă���|�[�g��Ԃ�
		TRACE(TEXT("WM_APP_QUERYPORT\n"));
		if (!m_fClosing && m_App.CoreEngine.IsNetworkDriver()) {
			WORD Port=m_App.ChannelManager.GetCurrentChannel()+
										(m_App.CoreEngine.IsUDPDriver()?1234:2230);
#ifdef NETWORK_REMOCON_SUPPORT
			WORD RemoconPort=m_App.pNetworkRemocon!=nullptr?m_App.pNetworkRemocon->GetPort():0;
			return MAKELRESULT(Port,RemoconPort);
#else
			return MAKELRESULT(Port,0);
#endif
		}
		return 0;

	case WM_APP_FILEWRITEERROR:
		// �t�@�C���̏����o���G���[
		TRACE(TEXT("WM_APP_FILEWRITEERROR\n"));
		ShowErrorMessage(TEXT("�t�@�C���ւ̏����o���ŃG���[���������܂����B"));
		return 0;

	case WM_APP_VIDEOSTREAMTYPECHANGED:
		// �f��stream_type���ς����
		TRACE(TEXT("WM_APP_VIDEOSTREAMTYPECHANGED\n"));
		if (m_fEnablePlayback
				&& !IsMessageInQueue(hwnd,WM_APP_VIDEOSTREAMTYPECHANGED)) {
			BYTE StreamType=static_cast<BYTE>(wParam);

			if (StreamType==m_App.CoreEngine.m_DtvEngine.GetVideoStreamType())
				m_pCore->EnableViewer(true);
		}
		return 0;

	case WM_APP_VIDEOSIZECHANGED:
		// �f���T�C�Y���ς����
		TRACE(TEXT("WM_APP_VIDEOSIZECHANGED\n"));
		/*
			�X�g���[���̉f���T�C�Y�̕ω������m���Ă���A���ꂪ���ۂ�
			�\�������܂łɂ̓^�C�����O�����邽�߁A��Œ������s��
		*/
		m_VideoSizeChangedTimerCount=0;
		::SetTimer(hwnd,TIMER_ID_VIDEOSIZECHANGED,500,nullptr);
		if (m_AspectRatioResetTime!=0
				&& !m_pCore->GetFullscreen()
				&& IsViewerEnabled()
				&& TickTimeSpan(m_AspectRatioResetTime,::GetTickCount())<6000) {
			if (AutoFitWindowToVideo())
				m_AspectRatioResetTime=0;
		}
		return 0;

	case WM_APP_EMMPROCESSED:
		// EMM �������s��ꂽ
		TRACE(TEXT("WM_APP_EMMPROCESSED\n"));
		m_App.Logger.AddLog(wParam!=0?TEXT("EMM�������s���܂����B"):TEXT("EMM�����ŃG���[���������܂����B"));
		return 0;

	case WM_APP_ECMERROR:
		// ECM �����̃G���[����������
		TRACE(TEXT("WM_APP_ECMERROR\n"));
		{
			LPTSTR pszText=reinterpret_cast<LPTSTR>(lParam);

			if (m_App.OSDOptions.IsNotifyEnabled(COSDOptions::NOTIFY_ECMERROR))
				ShowNotificationBar(TEXT("�X�N�����u�������ŃG���[���������܂���"),
									CNotificationBar::MESSAGE_ERROR);
			if (pszText!=nullptr) {
				TCHAR szText[256];
				StdUtil::snprintf(szText,lengthof(szText),
								  TEXT("ECM�����ŃG���[���������܂����B(%s)"),pszText);
				m_App.Logger.AddLog(szText);
				delete [] pszText;
			} else {
				m_App.Logger.AddLog(TEXT("ECM�����ŃG���[���������܂����B"));
			}
		}
		return 0;

	case WM_APP_ECMREFUSED:
		// ECM ���󂯕t�����Ȃ�
		TRACE(TEXT("WM_APP_ECMREFUSED\n"));
		if (m_App.OSDOptions.IsNotifyEnabled(COSDOptions::NOTIFY_ECMERROR)
				&& IsViewerEnabled())
			ShowNotificationBar(TEXT("�_�񂳂�Ă��Ȃ����ߎ����ł��܂���"),
								CNotificationBar::MESSAGE_WARNING,6000);
		return 0;

	case WM_APP_CARDREADERHUNG:
		// �J�[�h���[�_�[���牞��������
		TRACE(TEXT("WM_APP_CARDREADERHUNG\n"));
		if (m_App.OSDOptions.IsNotifyEnabled(COSDOptions::NOTIFY_ECMERROR))
			ShowNotificationBar(TEXT("�J�[�h���[�_�[���牞��������܂���"),
								CNotificationBar::MESSAGE_ERROR,6000);
		m_App.Logger.AddLog(TEXT("�J�[�h���[�_�[���牞��������܂���B"));
		return 0;

	case WM_APP_EPGLOADED:
		// EPG�t�@�C�����ǂݍ��܂ꂽ
		TRACE(TEXT("WM_APP_EPGLOADED\n"));
		if (m_App.Panel.fShowPanelWindow
				&& (m_App.Panel.Form.GetCurPageID()==PANEL_ID_PROGRAMLIST
				 || m_App.Panel.Form.GetCurPageID()==PANEL_ID_CHANNEL)) {
			UpdatePanel();
		}
		return 0;

	case WM_APP_CONTROLLERFOCUS:
		// �R���g���[���̑���Ώۂ��ς����
		TRACE(TEXT("WM_APP_CONTROLLERFOCUS\n"));
		m_App.ControllerManager.OnFocusChange(hwnd,wParam!=0);
		return 0;

	case WM_APP_PLUGINMESSAGE:
		// �v���O�C���̃��b�Z�[�W�̏���
		return CPlugin::OnPluginMessage(wParam,lParam);

	case WM_ACTIVATEAPP:
		{
			bool fActive=wParam!=FALSE;

			m_App.ControllerManager.OnActiveChange(hwnd,fActive);
			if (fActive)
				m_App.BroadcastControllerFocusMessage(hwnd,fActive || m_fClosing,!fActive);
		}
		return 0;

	case WM_DISPLAYCHANGE:
		m_App.CoreEngine.m_DtvEngine.m_MediaViewer.DisplayModeChanged();
		break;

	case WM_THEMECHANGED:
		m_App.ChannelMenu.Destroy();
		m_App.FavoritesMenu.Destroy();
		return 0;

	case WM_CLOSE:
		if (!ConfirmExit())
			return 0;

		m_fClosing=true;

		::SetCursor(::LoadCursor(nullptr,IDC_WAIT));

		m_App.Logger.AddLog(TEXT("�E�B���h�E����Ă��܂�..."));

		::KillTimer(hwnd,TIMER_ID_UPDATE);

		//m_App.CoreEngine.EnableMediaViewer(false);

		m_App.PluginManager.SendCloseEvent();

		m_Fullscreen.Destroy();

		ShowFloatingWindows(false);
		break;

	case WM_ENDSESSION:
		if (!wParam)
			break;
		m_App.Core.SetSilent(true);
	case WM_DESTROY:
		OnDestroy();
		return 0;

	default:
		/*
		if (m_App.ControllerManager.HandleMessage(hwnd,uMsg,wParam,lParam))
			return 0;
		*/
		if (m_App.ResidentManager.HandleMessage(uMsg,wParam,lParam))
			return 0;
		if (m_App.TaskbarManager.HandleMessage(uMsg,wParam,lParam))
			return 0;
	}

	return ::DefWindowProc(hwnd,uMsg,wParam,lParam);
}


bool CMainWindow::OnCreate(const CREATESTRUCT *pcs)
{
	RECT rc;
	GetClientRect(&rc);
	m_LayoutBase.SetPosition(&rc);
	m_LayoutBase.Create(m_hwnd,
						WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);

	m_Viewer.Create(m_LayoutBase.GetHandle(),IDC_VIEW,IDC_VIDEOCONTAINER,m_hwnd);
	m_Viewer.GetViewWindow().SetEventHandler(&m_ViewWindowEventHandler);
	m_Viewer.GetVideoContainer().SetEventHandler(&m_VideoContainerEventHandler);
	m_Viewer.GetDisplayBase().SetEventHandler(&m_DisplayBaseEventHandler);

	m_TitleBar.Create(m_LayoutBase.GetHandle(),
					  WS_CHILD | WS_CLIPSIBLINGS | (m_fShowTitleBar && m_fCustomTitleBar?WS_VISIBLE:0),
					  0,IDC_TITLEBAR);
	m_TitleBar.SetEventHandler(&m_TitleBarManager);
	m_TitleBar.SetLabel(pcs->lpszName);
	m_TitleBar.SetIcon(::LoadIcon(m_App.GetInstance(),MAKEINTRESOURCE(IDI_ICON)));
	m_TitleBar.SetMaximizeMode((pcs->style&WS_MAXIMIZE)!=0);

	m_App.StatusView.AddItem(new CChannelStatusItem);
	m_App.StatusView.AddItem(new CVideoSizeStatusItem);
	m_App.StatusView.AddItem(new CVolumeStatusItem);
	m_App.StatusView.AddItem(new CAudioChannelStatusItem);
	CRecordStatusItem *pRecordStatusItem=new CRecordStatusItem;
	pRecordStatusItem->ShowRemainTime(m_App.RecordOptions.GetShowRemainTime());
	m_App.StatusView.AddItem(pRecordStatusItem);
	m_App.StatusView.AddItem(new CCaptureStatusItem);
	m_App.StatusView.AddItem(new CErrorStatusItem);
	m_App.StatusView.AddItem(new CSignalLevelStatusItem);
	CClockStatusItem *pClockStatusItem=new CClockStatusItem;
	pClockStatusItem->SetTOT(m_App.StatusOptions.GetShowTOTTime());
	m_App.StatusView.AddItem(pClockStatusItem);
	CProgramInfoStatusItem *pProgramInfoStatusItem=new CProgramInfoStatusItem;
	pProgramInfoStatusItem->EnablePopupInfo(m_App.StatusOptions.IsPopupProgramInfoEnabled());
	m_App.StatusView.AddItem(pProgramInfoStatusItem);
	m_App.StatusView.AddItem(new CBufferingStatusItem);
	m_App.StatusView.AddItem(new CTunerStatusItem);
	m_App.StatusView.AddItem(new CMediaBitRateStatusItem);
	m_App.StatusView.AddItem(new CFavoritesStatusItem);
	m_App.StatusView.Create(m_LayoutBase.GetHandle(),
		//WS_CHILD | (m_fShowStatusBar?WS_VISIBLE:0) | WS_CLIPSIBLINGS,
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,0,IDC_STATUS);
	m_App.StatusView.SetEventHandler(&m_StatusViewEventHandler);
	m_App.StatusOptions.ApplyOptions();

	m_NotificationBar.Create(m_Viewer.GetVideoContainer().GetHandle(),
							 WS_CHILD | WS_CLIPSIBLINGS);

	m_App.SideBarOptions.SetEventHandler(&m_App.SideBarOptionsEventHandler);
	m_App.SideBarOptions.ApplySideBarOptions();
	m_App.SideBar.SetEventHandler(&m_SideBarManager);
	m_App.SideBar.Create(m_LayoutBase.GetHandle(),
						 WS_CHILD | WS_CLIPSIBLINGS | (m_fShowSideBar?WS_VISIBLE:0),
						 0,IDC_SIDEBAR);
	m_App.SideBarOptions.SetSideBarImage();

	Layout::CWindowContainer *pWindowContainer;
	Layout::CSplitter *pSideBarSplitter=new Layout::CSplitter(CONTAINER_ID_SIDEBARSPLITTER);
	CSideBarOptions::PlaceType SideBarPlace=m_App.SideBarOptions.GetPlace();
	bool fSideBarVertical=SideBarPlace==CSideBarOptions::PLACE_LEFT
						|| SideBarPlace==CSideBarOptions::PLACE_RIGHT;
	int SideBarWidth=m_App.SideBar.GetBarWidth();
	pSideBarSplitter->SetStyle(Layout::CSplitter::STYLE_FIXED |
		(fSideBarVertical?Layout::CSplitter::STYLE_HORZ:Layout::CSplitter::STYLE_VERT));
	pSideBarSplitter->SetVisible(true);
	pWindowContainer=new Layout::CWindowContainer(CONTAINER_ID_VIEW);
	pWindowContainer->SetWindow(&m_Viewer.GetViewWindow());
	pWindowContainer->SetMinSize(32,32);
	pWindowContainer->SetVisible(true);
	pSideBarSplitter->SetPane(0,pWindowContainer);
	pSideBarSplitter->SetAdjustPane(CONTAINER_ID_VIEW);
	pWindowContainer=new Layout::CWindowContainer(CONTAINER_ID_SIDEBAR);
	pWindowContainer->SetWindow(&m_App.SideBar);
	pWindowContainer->SetMinSize(SideBarWidth,SideBarWidth);
	pWindowContainer->SetVisible(m_fShowSideBar);
	pSideBarSplitter->SetPane(1,pWindowContainer);
	pSideBarSplitter->SetPaneSize(CONTAINER_ID_SIDEBAR,SideBarWidth);
	if (SideBarPlace==CSideBarOptions::PLACE_LEFT
			|| SideBarPlace==CSideBarOptions::PLACE_TOP)
		pSideBarSplitter->SwapPane();

	Layout::CSplitter *pTitleBarSplitter=new Layout::CSplitter(CONTAINER_ID_TITLEBARSPLITTER);
	pTitleBarSplitter->SetStyle(Layout::CSplitter::STYLE_VERT | Layout::CSplitter::STYLE_FIXED);
	pTitleBarSplitter->SetVisible(true);
	Layout::CWindowContainer *pTitleBarContainer=new Layout::CWindowContainer(CONTAINER_ID_TITLEBAR);
	pTitleBarContainer->SetWindow(&m_TitleBar);
	pTitleBarContainer->SetMinSize(0,m_TitleBar.GetHeight());
	pTitleBarContainer->SetVisible(m_fShowTitleBar && m_fCustomTitleBar);
	pTitleBarSplitter->SetPane(0,pTitleBarContainer);
	pTitleBarSplitter->SetPaneSize(CONTAINER_ID_TITLEBAR,m_TitleBar.GetHeight());

	Layout::CSplitter *pPanelSplitter=new Layout::CSplitter(CONTAINER_ID_PANELSPLITTER);
	pPanelSplitter->SetVisible(true);
	Layout::CWindowContainer *pPanelContainer=new Layout::CWindowContainer(CONTAINER_ID_PANEL);
	pPanelContainer->SetMinSize(64,0);
	pPanelSplitter->SetPane(m_PanelPaneIndex,pPanelContainer);

	Layout::CSplitter *pParentSplitter;
	if (m_fSplitTitleBar) {
		pTitleBarSplitter->SetPane(1,pSideBarSplitter);
		pTitleBarSplitter->SetAdjustPane(CONTAINER_ID_SIDEBARSPLITTER);
		pPanelSplitter->SetPane(1-m_PanelPaneIndex,pTitleBarSplitter);
		pPanelSplitter->SetAdjustPane(CONTAINER_ID_TITLEBARSPLITTER);
		pParentSplitter=pPanelSplitter;
	} else {
		pPanelSplitter->SetPane(1-m_PanelPaneIndex,pSideBarSplitter);
		pPanelSplitter->SetAdjustPane(CONTAINER_ID_SIDEBARSPLITTER);
		pTitleBarSplitter->SetPane(1,pPanelSplitter);
		pTitleBarSplitter->SetAdjustPane(CONTAINER_ID_PANELSPLITTER);
		pParentSplitter=pTitleBarSplitter;
	}

	Layout::CSplitter *pStatusSplitter=new Layout::CSplitter(CONTAINER_ID_STATUSSPLITTER);
	pStatusSplitter->SetStyle(Layout::CSplitter::STYLE_VERT | Layout::CSplitter::STYLE_FIXED);
	pStatusSplitter->SetVisible(true);
	pStatusSplitter->SetPane(0,pParentSplitter);
	pStatusSplitter->SetAdjustPane(pParentSplitter->GetID());
	pWindowContainer=new Layout::CWindowContainer(CONTAINER_ID_STATUS);
	pWindowContainer->SetWindow(&m_App.StatusView);
	pWindowContainer->SetMinSize(0,m_App.StatusView.GetHeight());
	pWindowContainer->SetVisible(m_fShowStatusBar);
	pStatusSplitter->SetPane(1,pWindowContainer);
	pStatusSplitter->SetPaneSize(CONTAINER_ID_STATUS,m_App.StatusView.GetHeight());

	m_LayoutBase.SetTopContainer(pStatusSplitter);

	// �N���󋵂�\�����邽�߂ɁA�N�����͏�ɃX�e�[�^�X�o�[��\������
	if (!m_fShowStatusBar) {
		RECT rc;

		GetClientRect(&rc);
		rc.top=rc.bottom-m_App.StatusView.GetHeight();
		m_App.StatusView.SetPosition(&rc);
		m_App.StatusView.SetVisible(true);
		::BringWindowToTop(m_App.StatusView.GetHandle());
	}
	m_App.StatusView.SetSingleText(TEXT("�N����..."));

	m_App.OSDManager.Initialize();
	m_App.OSDManager.SetEventHandler(this);

	if (m_fCustomFrame) {
		CAeroGlass Aero;
		Aero.EnableNcRendering(m_hwnd,false);
		HookWindows(m_LayoutBase.GetHandle());
		//HookWindows(m_App.Panel.Form.GetHandle());
	}

	// IME������
	::ImmAssociateContext(m_hwnd,nullptr);
	::ImmAssociateContextEx(m_hwnd,nullptr,IACE_CHILDREN);

	m_App.MainMenu.Create(m_App.GetResourceInstance());
	m_App.MainMenu.CheckItem(CM_ALWAYSONTOP,m_pCore->GetAlwaysOnTop());
	int Gain,SurroundGain;
	m_App.CoreEngine.GetAudioGainControl(&Gain,&SurroundGain);
	for (int i=0;i<lengthof(m_AudioGainList);i++) {
		if (Gain==m_AudioGainList[i])
			m_App.MainMenu.CheckRadioItem(CM_AUDIOGAIN_FIRST,CM_AUDIOGAIN_LAST,
										  CM_AUDIOGAIN_FIRST+i);
		if (SurroundGain==m_AudioGainList[i])
			m_App.MainMenu.CheckRadioItem(CM_SURROUNDAUDIOGAIN_FIRST,CM_SURROUNDAUDIOGAIN_LAST,
										  CM_SURROUNDAUDIOGAIN_FIRST+i);
	}
	/*
	m_App.MainMenu.CheckRadioItem(CM_STEREO_THROUGH,CM_STEREO_RIGHT,
		CM_STEREO_THROUGH+m_pCore->GetStereoMode());
	*/
	m_App.MainMenu.CheckRadioItem(CM_CAPTURESIZE_FIRST,CM_CAPTURESIZE_LAST,
		CM_CAPTURESIZE_FIRST+m_App.CaptureOptions.GetPresetCaptureSize());
	//m_App.MainMenu.CheckItem(CM_CAPTUREPREVIEW,m_App.CaptureWindow.GetVisible());
	m_App.MainMenu.CheckItem(CM_DISABLEVIEWER,!m_fEnablePlayback);
	m_App.MainMenu.CheckItem(CM_PANEL,m_App.Panel.fShowPanelWindow);
	m_App.MainMenu.CheckItem(CM_1SEGMODE,m_App.Core.Is1SegMode());

	HMENU hSysMenu=::GetSystemMenu(m_hwnd,FALSE);
	::InsertMenu(hSysMenu,0,MF_BYPOSITION | MF_STRING | MF_ENABLED,
				 SC_ABOUT,TEXT("�o�[�W�������(&A)"));
	::InsertMenu(hSysMenu,1,MF_BYPOSITION | MF_SEPARATOR,0,nullptr);

	static const CIconMenu::ItemInfo AspectRatioMenuItems[] = {
		{CM_ASPECTRATIO_DEFAULT,	0},
		{CM_ASPECTRATIO_16x9,		1},
		{CM_ASPECTRATIO_LETTERBOX,	2},
		{CM_ASPECTRATIO_SUPERFRAME,	3},
		{CM_ASPECTRATIO_SIDECUT,	4},
		{CM_ASPECTRATIO_4x3,		5},
		{CM_ASPECTRATIO_32x9,		6},
		{CM_ASPECTRATIO_16x9_LEFT,	7},
		{CM_ASPECTRATIO_16x9_RIGHT,	8},
		{CM_FRAMECUT,				9},
		{CM_PANANDSCANOPTIONS,		10},
	};
	HMENU hmenuAspectRatio=m_App.MainMenu.GetSubMenu(CMainMenu::SUBMENU_ASPECTRATIO);
	m_App.AspectRatioIconMenu.Initialize(hmenuAspectRatio,
										 m_App.GetInstance(),MAKEINTRESOURCE(IDB_PANSCAN),16,
										 AspectRatioMenuItems,lengthof(AspectRatioMenuItems));
	if (m_AspectRatioType<ASPECTRATIO_CUSTOM) {
		m_App.AspectRatioIconMenu.CheckRadioItem(
			CM_ASPECTRATIO_FIRST,CM_ASPECTRATIO_3D_LAST,
			CM_ASPECTRATIO_FIRST+m_AspectRatioType);
	}
	m_DefaultAspectRatioMenuItemCount=::GetMenuItemCount(hmenuAspectRatio);

	m_App.TaskbarManager.Initialize(m_hwnd);

	m_App.NotifyBalloonTip.Initialize(m_hwnd);

	m_App.CoreEngine.m_DtvEngine.m_MediaViewer.SetViewStretchMode(
		(pcs->style&WS_MAXIMIZE)!=0?
			m_App.ViewOptions.GetMaximizeStretchMode():
			m_fFrameCut?CMediaViewer::STRETCH_CUTFRAME:
						CMediaViewer::STRETCH_KEEPASPECTRATIO);

	::SetTimer(m_hwnd,TIMER_ID_UPDATE,UPDATE_TIMER_INTERVAL,nullptr);

	m_fShowCursor=true;
	if (m_App.OperationOptions.GetHideCursor())
		::SetTimer(m_hwnd,TIMER_ID_HIDECURSOR,HIDE_CURSOR_DELAY,nullptr);

	return true;
}


void CMainWindow::OnDestroy()
{
	RECT rc;
	GetPosition(&rc);
	if (m_WindowSizeMode==WINDOW_SIZE_1SEG)
		m_1SegWindowSize=rc;
	else
		m_HDWindowSize=rc;

	m_App.SetEnablePlaybackOnStart(m_fEnablePlayback);
	m_PanelPaneIndex=GetPanelPaneIndex();

	m_App.HtmlHelpClass.Finalize();
	m_pCore->PreventDisplaySave(false);

	m_App.Finalize();

	CBasicWindow::OnDestroy();
}


void CMainWindow::OnSizeChanged(UINT State,int Width,int Height)
{
	const bool fMinimized=State==SIZE_MINIMIZED;
	const bool fMaximized=State==SIZE_MAXIMIZED;

	if (fMinimized) {
		m_App.OSDManager.ClearOSD();
		m_App.OSDManager.Reset();
		m_App.ResidentManager.SetStatus(CResidentManager::STATUS_MINIMIZED,
										CResidentManager::STATUS_MINIMIZED);
		if (m_App.ViewOptions.GetDisablePreviewWhenMinimized()) {
			SuspendViewer(ResumeInfo::VIEWERSUSPEND_MINIMIZE);
		}
	} else if ((m_App.ResidentManager.GetStatus()&CResidentManager::STATUS_MINIMIZED)!=0) {
		SetWindowVisible();
	}

	if (fMaximized && (!m_fShowTitleBar || m_fCustomTitleBar)) {
		HMONITOR hMonitor=::MonitorFromWindow(m_hwnd,MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi;

		mi.cbSize=sizeof(MONITORINFO);
		::GetMonitorInfo(hMonitor,&mi);
		::MoveWindow(m_hwnd,
					 mi.rcWork.left,mi.rcWork.top,
					 mi.rcWork.right-mi.rcWork.left,
					 mi.rcWork.bottom-mi.rcWork.top,
					 TRUE);
		SIZE sz;
		GetClientSize(&sz);
		Width=sz.cx;
		Height=sz.cy;
	}
	m_TitleBar.SetMaximizeMode(fMaximized);

	if (m_fLockLayout || fMinimized)
		return;

	m_LayoutBase.SetPosition(0,0,Width,Height);

	if (!m_pCore->GetFullscreen()) {
		if (State==SIZE_MAXIMIZED) {
			m_App.CoreEngine.m_DtvEngine.m_MediaViewer.SetViewStretchMode(
				m_App.ViewOptions.GetMaximizeStretchMode());
		} else if (State==SIZE_RESTORED) {
			m_App.CoreEngine.m_DtvEngine.m_MediaViewer.SetViewStretchMode(
				m_fFrameCut?CMediaViewer::STRETCH_CUTFRAME:
							CMediaViewer::STRETCH_KEEPASPECTRATIO);
		}
	}

	m_App.StatusView.UpdateItem(STATUS_ITEM_VIDEOSIZE);
	m_App.Panel.ControlPanel.UpdateItem(CONTROLPANEL_ITEM_VIDEO);
}


bool CMainWindow::OnSizeChanging(UINT Edge,RECT *pRect)
{
	RECT rcOld;
	bool fChanged=false;

	GetPosition(&rcOld);
	bool fKeepRatio=m_App.ViewOptions.GetAdjustAspectResizing();
	if (::GetKeyState(VK_SHIFT)<0)
		fKeepRatio=!fKeepRatio;
	if (fKeepRatio) {
		BYTE XAspect,YAspect;

		if (m_App.CoreEngine.m_DtvEngine.m_MediaViewer.GetEffectiveAspectRatio(&XAspect,&YAspect)) {
			RECT rcWindow,rcClient;
			int XMargin,YMargin,Width,Height;

			GetPosition(&rcWindow);
			GetClientRect(&rcClient);
			m_Viewer.GetViewWindow().CalcClientRect(&rcClient);
			if (m_fShowStatusBar)
				rcClient.bottom-=m_App.StatusView.GetHeight();
			if (m_fShowTitleBar && m_fCustomTitleBar)
				m_TitleBarManager.AdjustArea(&rcClient);
			if (m_fShowSideBar)
				m_SideBarManager.AdjustArea(&rcClient);
			if (m_App.Panel.fShowPanelWindow && !m_App.Panel.IsFloating()) {
				Layout::CSplitter *pSplitter=dynamic_cast<Layout::CSplitter*>(
					m_LayoutBase.GetContainerByID(CONTAINER_ID_PANELSPLITTER));
				rcClient.right-=pSplitter->GetPaneSize(CONTAINER_ID_PANEL)+pSplitter->GetBarWidth();
			}
			::OffsetRect(&rcClient,-rcClient.left,-rcClient.top);
			if (rcClient.right<=0 || rcClient.bottom<=0)
				goto SizingEnd;
			XMargin=(rcWindow.right-rcWindow.left)-rcClient.right;
			YMargin=(rcWindow.bottom-rcWindow.top)-rcClient.bottom;
			Width=(pRect->right-pRect->left)-XMargin;
			Height=(pRect->bottom-pRect->top)-YMargin;
			if (Width<=0 || Height<=0)
				goto SizingEnd;
			if (Edge==WMSZ_LEFT || Edge==WMSZ_RIGHT)
				Height=Width*YAspect/XAspect;
			else if (Edge==WMSZ_TOP || Edge==WMSZ_BOTTOM)
				Width=Height*XAspect/YAspect;
			else if (Width*YAspect<Height*XAspect)
				Width=Height*XAspect/YAspect;
			else if (Width*YAspect>Height*XAspect)
				Height=Width*YAspect/XAspect;
			if (Edge==WMSZ_LEFT || Edge==WMSZ_TOPLEFT || Edge==WMSZ_BOTTOMLEFT)
				pRect->left=pRect->right-(Width+XMargin);
			else
				pRect->right=pRect->left+Width+XMargin;
			if (Edge==WMSZ_TOP || Edge==WMSZ_TOPLEFT || Edge==WMSZ_TOPRIGHT)
				pRect->top=pRect->bottom-(Height+YMargin);
			else
				pRect->bottom=pRect->top+Height+YMargin;
			fChanged=true;
		}
	}
SizingEnd:
	m_App.Panel.OnOwnerMovingOrSizing(&rcOld,pRect);
	return fChanged;
}


void CMainWindow::OnMouseMove(int x,int y)
{
	if (::GetCapture()==m_hwnd) {
		// �E�B���h�E�ړ���
		POINT pt;
		RECT rcOld,rc;

		/*
		pt.x=x;
		pt.y=y;
		::ClientToScreen(hwnd,&pt);
		*/
		::GetCursorPos(&pt);
		::GetWindowRect(m_hwnd,&rcOld);
		rc.left=m_rcDragStart.left+(pt.x-m_ptDragStartPos.x);
		rc.top=m_rcDragStart.top+(pt.y-m_ptDragStartPos.y);
		rc.right=rc.left+(m_rcDragStart.right-m_rcDragStart.left);
		rc.bottom=rc.top+(m_rcDragStart.bottom-m_rcDragStart.top);
		bool fSnap=m_App.ViewOptions.GetSnapAtWindowEdge();
		if (::GetKeyState(VK_SHIFT)<0)
			fSnap=!fSnap;
		if (fSnap)
			SnapWindow(m_hwnd,&rc,
					   m_App.ViewOptions.GetSnapAtWindowEdgeMargin(),
					   m_App.Panel.IsAttached()?nullptr:m_App.Panel.Frame.GetHandle());
		SetPosition(&rc);
		m_App.Panel.OnOwnerMovingOrSizing(&rcOld,&rc);
	} else if (!m_pCore->GetFullscreen()) {
		POINT pt={x,y};
		RECT rcClient,rcTitle,rcStatus,rcSideBar,rc;
		bool fShowTitleBar=false,fShowStatusBar=false,fShowSideBar=false;

		m_Viewer.GetViewWindow().GetClientRect(&rcClient);
		MapWindowRect(m_Viewer.GetViewWindow().GetHandle(),m_LayoutBase.GetHandle(),&rcClient);
		if (!m_fShowTitleBar && m_fPopupTitleBar) {
			rc=rcClient;
			m_TitleBarManager.Layout(&rc,&rcTitle);
			if (::PtInRect(&rcTitle,pt))
				fShowTitleBar=true;
		}
		if (!m_fShowStatusBar && m_fPopupStatusBar) {
			rcStatus=rcClient;
			rcStatus.top=rcStatus.bottom-m_App.StatusView.CalcHeight(rcClient.right-rcClient.left);
			if (::PtInRect(&rcStatus,pt))
				fShowStatusBar=true;
		}
		if (!m_fShowSideBar && m_App.SideBarOptions.ShowPopup()) {
			switch (m_App.SideBarOptions.GetPlace()) {
			case CSideBarOptions::PLACE_LEFT:
			case CSideBarOptions::PLACE_RIGHT:
				if (!fShowStatusBar && !fShowTitleBar) {
					m_SideBarManager.Layout(&rcClient,&rcSideBar);
					if (::PtInRect(&rcSideBar,pt))
						fShowSideBar=true;
				}
				break;
			case CSideBarOptions::PLACE_TOP:
				if (!m_fShowTitleBar && m_fPopupTitleBar)
					rcClient.top=rcTitle.bottom;
				m_SideBarManager.Layout(&rcClient,&rcSideBar);
				if (::PtInRect(&rcSideBar,pt)) {
					fShowSideBar=true;
					if (!m_fShowTitleBar && m_fPopupTitleBar)
						fShowTitleBar=true;
				}
				break;
			case CSideBarOptions::PLACE_BOTTOM:
				if (!m_fShowStatusBar && m_fPopupStatusBar)
					rcClient.bottom=rcStatus.top;
				m_SideBarManager.Layout(&rcClient,&rcSideBar);
				if (::PtInRect(&rcSideBar,pt)) {
					fShowSideBar=true;
					if (!m_fShowStatusBar && m_fPopupStatusBar)
						fShowStatusBar=true;
				}
				break;
			}
		}

		if (fShowTitleBar) {
			if (!m_TitleBar.GetVisible()) {
				m_TitleBar.SetPosition(&rcTitle);
				m_TitleBar.SetVisible(true);
				::BringWindowToTop(m_TitleBar.GetHandle());
			}
		} else if (!m_fShowTitleBar && m_TitleBar.GetVisible()) {
			m_TitleBar.SetVisible(false);
		}
		if (fShowStatusBar) {
			if (!m_App.StatusView.GetVisible()) {
				m_App.StatusView.SetPosition(&rcStatus);
				m_App.StatusView.SetVisible(true);
				::BringWindowToTop(m_App.StatusView.GetHandle());
			}
		} else if (!m_fShowStatusBar && m_App.StatusView.GetVisible()) {
			m_App.StatusView.SetVisible(false);
		}
		if (fShowSideBar) {
			if (!m_App.SideBar.GetVisible()) {
				m_App.SideBar.SetPosition(&rcSideBar);
				m_App.SideBar.SetVisible(true);
				::BringWindowToTop(m_App.SideBar.GetHandle());
			}
		} else if (!m_fShowSideBar && m_App.SideBar.GetVisible()) {
			m_App.SideBar.SetVisible(false);
		}

		if (!m_fShowCursor)
			ShowCursor(true);
		if (m_App.OperationOptions.GetHideCursor())
			::SetTimer(m_hwnd,TIMER_ID_HIDECURSOR,HIDE_CURSOR_DELAY,nullptr);
	} else {
		m_Fullscreen.OnMouseMove();
	}
}


bool CMainWindow::OnSetCursor(HWND hwndCursor,int HitTestCode,int MouseMessage)
{
	if (HitTestCode==HTCLIENT) {
		if (hwndCursor==m_hwnd
				|| hwndCursor==m_Viewer.GetVideoContainer().GetHandle()
				|| hwndCursor==m_Viewer.GetViewWindow().GetHandle()
				|| hwndCursor==m_NotificationBar.GetHandle()
				|| CPseudoOSD::IsPseudoOSD(hwndCursor)) {
			::SetCursor(m_fShowCursor?::LoadCursor(nullptr,IDC_ARROW):nullptr);
			return true;
		}
	}

	return false;
}


void CMainWindow::OnCommand(HWND hwnd,int id,HWND hwndCtl,UINT codeNotify)
{
	switch (id) {
	case CM_ZOOMOPTIONS:
		if (m_App.ZoomOptions.Show(GetVideoHostWindow())) {
			m_App.SideBarOptions.SetSideBarImage();
			m_App.ZoomOptions.SaveSettings(m_App.GetIniFileName());
		}
		return;

	case CM_ASPECTRATIO:
		{
			int Command;

			if (m_AspectRatioType>=ASPECTRATIO_CUSTOM)
				Command=CM_ASPECTRATIO_DEFAULT;
			else if (m_AspectRatioType<ASPECTRATIO_32x9)
				Command=CM_ASPECTRATIO_FIRST+
					(m_AspectRatioType+1)%(CM_ASPECTRATIO_LAST-CM_ASPECTRATIO_FIRST+1);
			else
				Command=CM_ASPECTRATIO_3D_FIRST+
					(m_AspectRatioType-ASPECTRATIO_32x9+1)%(CM_ASPECTRATIO_3D_LAST-CM_ASPECTRATIO_3D_FIRST+1);
			SetPanAndScan(Command);
		}
		return;

	case CM_ASPECTRATIO_DEFAULT:
	case CM_ASPECTRATIO_16x9:
	case CM_ASPECTRATIO_LETTERBOX:
	case CM_ASPECTRATIO_SUPERFRAME:
	case CM_ASPECTRATIO_SIDECUT:
	case CM_ASPECTRATIO_4x3:
	case CM_ASPECTRATIO_32x9:
	case CM_ASPECTRATIO_16x9_LEFT:
	case CM_ASPECTRATIO_16x9_RIGHT:
		SetPanAndScan(id);
		return;

	case CM_PANANDSCANOPTIONS:
		{
			bool fSet=false;
			CPanAndScanOptions::PanAndScanInfo CurPanScan;

			if (m_AspectRatioType>=ASPECTRATIO_CUSTOM)
				fSet=m_App.PanAndScanOptions.GetPreset(m_AspectRatioType-ASPECTRATIO_CUSTOM,&CurPanScan);

			if (m_App.PanAndScanOptions.Show(GetVideoHostWindow())) {
				if (fSet) {
					CPanAndScanOptions::PanAndScanInfo NewPanScan;
					int Index=m_App.PanAndScanOptions.FindPresetByID(CurPanScan.ID);
					if (Index>=0 && m_App.PanAndScanOptions.GetPreset(Index,&NewPanScan)) {
						if (NewPanScan.Info!=CurPanScan.Info)
							SetPanAndScan(CM_PANANDSCAN_PRESET_FIRST+Index);
					} else {
						SetPanAndScan(CM_ASPECTRATIO_DEFAULT);
					}
				}
				m_App.PanAndScanOptions.SaveSettings(m_App.GetIniFileName());
			}
		}
		return;

	case CM_FRAMECUT:
		m_fFrameCut=!m_fFrameCut;
		m_App.CoreEngine.m_DtvEngine.m_MediaViewer.SetViewStretchMode(
			m_fFrameCut?CMediaViewer::STRETCH_CUTFRAME:
						CMediaViewer::STRETCH_KEEPASPECTRATIO);
		return;

	case CM_FULLSCREEN:
		m_pCore->ToggleFullscreen();
		return;

	case CM_ALWAYSONTOP:
		m_pCore->SetAlwaysOnTop(!m_pCore->GetAlwaysOnTop());
		return;

	case CM_VOLUME_UP:
	case CM_VOLUME_DOWN:
		{
			const int CurVolume=m_pCore->GetVolume();
			int Volume=CurVolume;

			if (id==CM_VOLUME_UP) {
				Volume+=m_App.OperationOptions.GetVolumeStep();
				if (Volume>CCoreEngine::MAX_VOLUME)
					Volume=CCoreEngine::MAX_VOLUME;
			} else {
				Volume-=m_App.OperationOptions.GetVolumeStep();
				if (Volume<0)
					Volume=0;
			}
			if (Volume!=CurVolume || m_pCore->GetMute())
				m_pCore->SetVolume(Volume);
		}
		return;

	case CM_VOLUME_MUTE:
		m_pCore->SetMute(!m_pCore->GetMute());
		return;

	case CM_AUDIOGAIN_NONE:
	case CM_AUDIOGAIN_125:
	case CM_AUDIOGAIN_150:
	case CM_AUDIOGAIN_200:
		{
			int SurroundGain;

			m_App.CoreEngine.GetAudioGainControl(nullptr,&SurroundGain);
			m_App.CoreEngine.SetAudioGainControl(
				m_AudioGainList[id-CM_AUDIOGAIN_FIRST],SurroundGain);
			m_App.MainMenu.CheckRadioItem(CM_AUDIOGAIN_NONE,CM_AUDIOGAIN_LAST,id);
		}
		return;

	case CM_SURROUNDAUDIOGAIN_NONE:
	case CM_SURROUNDAUDIOGAIN_125:
	case CM_SURROUNDAUDIOGAIN_150:
	case CM_SURROUNDAUDIOGAIN_200:
		{
			int Gain;

			m_App.CoreEngine.GetAudioGainControl(&Gain,nullptr);
			m_App.CoreEngine.SetAudioGainControl(
				Gain,m_AudioGainList[id-CM_SURROUNDAUDIOGAIN_FIRST]);
			m_App.MainMenu.CheckRadioItem(CM_SURROUNDAUDIOGAIN_NONE,CM_SURROUNDAUDIOGAIN_LAST,id);
		}
		return;

	case CM_STEREO_THROUGH:
	case CM_STEREO_LEFT:
	case CM_STEREO_RIGHT:
		m_pCore->SetStereoMode(id-CM_STEREO_THROUGH);
		ShowAudioOSD();
		return;

	case CM_SWITCHAUDIO:
		m_pCore->SwitchAudio();
		ShowAudioOSD();
		return;

	case CM_SPDIF_DISABLED:
	case CM_SPDIF_PASSTHROUGH:
	case CM_SPDIF_AUTO:
		{
			CAudioDecFilter::SpdifOptions Options(m_App.PlaybackOptions.GetSpdifOptions());

			Options.Mode=(CAudioDecFilter::SpdifMode)(id-CM_SPDIF_DISABLED);
			m_App.CoreEngine.SetSpdifOptions(Options);
		}
		return;

	case CM_SPDIF_TOGGLE:
		{
			CAudioDecFilter::SpdifOptions Options(m_App.PlaybackOptions.GetSpdifOptions());

			if (m_App.CoreEngine.m_DtvEngine.m_MediaViewer.IsSpdifPassthrough())
				Options.Mode=CAudioDecFilter::SPDIF_MODE_DISABLED;
			else
				Options.Mode=CAudioDecFilter::SPDIF_MODE_PASSTHROUGH;
			m_App.CoreEngine.SetSpdifOptions(Options);
			m_App.SideBar.CheckItem(CM_SPDIF_TOGGLE,
				Options.Mode==CAudioDecFilter::SPDIF_MODE_PASSTHROUGH);
		}
		return;

	case CM_CAPTURE:
		SendCommand(m_App.CaptureOptions.TranslateCommand(CM_CAPTURE));
		return;

	case CM_COPY:
	case CM_SAVEIMAGE:
		if (IsViewerEnabled()) {
			HCURSOR hcurOld=::SetCursor(::LoadCursor(nullptr,IDC_WAIT));
			BYTE *pDib;

			pDib=static_cast<BYTE*>(m_App.CoreEngine.GetCurrentImage());
			if (pDib==nullptr) {
				::SetCursor(hcurOld);
				ShowMessage(TEXT("���݂̉摜���擾�ł��܂���B\n")
							TEXT("�����_����f�R�[�_��ς��Ă݂Ă��������B"),TEXT("���߂�"),
							MB_OK | MB_ICONEXCLAMATION);
				return;
			}

			CMediaViewer &MediaViewer=m_App.CoreEngine.m_DtvEngine.m_MediaViewer;
			BITMAPINFOHEADER *pbmih=(BITMAPINFOHEADER*)pDib;
			RECT rc;
			int Width,Height,OrigWidth,OrigHeight;
			HGLOBAL hGlobal=nullptr;

			OrigWidth=pbmih->biWidth;
			OrigHeight=abs(pbmih->biHeight);
			if (MediaViewer.GetSourceRect(&rc)) {
				WORD VideoWidth,VideoHeight;

				if (MediaViewer.GetOriginalVideoSize(&VideoWidth,&VideoHeight)
						&& (VideoWidth!=OrigWidth
							|| VideoHeight!=OrigHeight)) {
					rc.left=rc.left*OrigWidth/VideoWidth;
					rc.top=rc.top*OrigHeight/VideoHeight;
					rc.right=rc.right*OrigWidth/VideoWidth;
					rc.bottom=rc.bottom*OrigHeight/VideoHeight;
				}
				if (rc.right>OrigWidth)
					rc.right=OrigWidth;
				if (rc.bottom>OrigHeight)
					rc.bottom=OrigHeight;
			} else {
				rc.left=0;
				rc.top=0;
				rc.right=OrigWidth;
				rc.bottom=OrigHeight;
			}
			if (OrigHeight==1088) {
				rc.top=rc.top*1080/1088;
				rc.bottom=rc.bottom*1080/1088;
			}
			switch (m_App.CaptureOptions.GetCaptureSizeType()) {
			case CCaptureOptions::SIZE_TYPE_ORIGINAL:
				m_App.CoreEngine.GetVideoViewSize(&Width,&Height);
				break;
			case CCaptureOptions::SIZE_TYPE_VIEW:
				{
					WORD w,h;

					MediaViewer.GetDestSize(&w,&h);
					Width=w;
					Height=h;
				}
				break;
			/*
			case CCaptureOptions::SIZE_RAW:
				rc.left=rc.top=0;
				rc.right=OrigWidth;
				rc.bottom=OrigHeight;
				Width=OrigWidth;
				Height=OrigHeight;
				break;
			*/
			case CCaptureOptions::SIZE_TYPE_PERCENTAGE:
				{
					int Num,Denom;

					m_App.CoreEngine.GetVideoViewSize(&Width,&Height);
					m_App.CaptureOptions.GetSizePercentage(&Num,&Denom);
					Width=Width*Num/Denom;
					Height=Height*Num/Denom;
				}
				break;
			case CCaptureOptions::SIZE_TYPE_CUSTOM:
				m_App.CaptureOptions.GetCustomSize(&Width,&Height);
				break;
			}
			hGlobal=ResizeImage((BITMAPINFO*)pbmih,
								pDib+CalcDIBInfoSize(pbmih),&rc,Width,Height);
			::CoTaskMemFree(pDib);
			::SetCursor(hcurOld);
			if (hGlobal==nullptr) {
				return;
			}
			CCaptureImage *pImage=new CCaptureImage(hGlobal);
			const CChannelInfo *pChInfo=m_App.ChannelManager.GetCurrentChannelInfo();
			TCHAR szComment[512],szEventName[256];
			m_App.CaptureOptions.GetCommentText(szComment,lengthof(szComment),
				pChInfo!=nullptr?pChInfo->GetName():nullptr,
				m_App.CoreEngine.m_DtvEngine.GetEventName(szEventName,lengthof(szEventName))>0?szEventName:nullptr);
			pImage->SetComment(szComment);
			m_App.CaptureWindow.SetImage(pImage);
			if (id==CM_COPY) {
				if (!pImage->SetClipboard(hwnd)) {
					ShowErrorMessage(TEXT("�N���b�v�{�[�h�Ƀf�[�^��ݒ�ł��܂���B"));
				}
			} else {
				if (!m_App.CaptureOptions.SaveImage(pImage)) {
					ShowErrorMessage(TEXT("�摜�̕ۑ��ŃG���[���������܂����B"));
				}
			}
			if (!m_App.CaptureWindow.HasImage())
				delete pImage;
		}
		return;

	case CM_CAPTUREPREVIEW:
		{
			if (!m_App.CaptureWindow.GetVisible()) {
				if (!m_App.CaptureWindow.IsCreated()) {
					m_App.CaptureWindow.Create(hwnd,
						WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
							WS_VISIBLE | WS_CLIPCHILDREN,
						WS_EX_TOOLWINDOW);
				} else {
					m_App.CaptureWindow.SetVisible(true);
				}
			} else {
				m_App.CaptureWindow.Destroy();
				m_App.CaptureWindow.ClearImage();
			}

			const bool fVisible=m_App.CaptureWindow.GetVisible();
			m_App.MainMenu.CheckItem(CM_CAPTUREPREVIEW,fVisible);
			m_App.SideBar.CheckItem(CM_CAPTUREPREVIEW,fVisible);
		}
		return;

	case CM_CAPTUREOPTIONS:
		if (IsWindowEnabled(hwnd))
			m_App.ShowOptionDialog(hwnd,COptionDialog::PAGE_CAPTURE);
		return;

	case CM_OPENCAPTUREFOLDER:
		m_App.CaptureOptions.OpenSaveFolder();
		return;

	case CM_RESET:
		m_App.CoreEngine.m_DtvEngine.ResetEngine();
		m_App.PluginManager.SendResetEvent();
		return;

	case CM_RESETVIEWER:
		m_App.CoreEngine.m_DtvEngine.ResetMediaViewer();
		return;

	case CM_REBUILDVIEWER:
		InitializeViewer();
		return;

	case CM_RECORD:
	case CM_RECORD_START:
	case CM_RECORD_STOP:
		if (id==CM_RECORD) {
			if (m_App.RecordManager.IsPaused()) {
				SendCommand(CM_RECORD_PAUSE);
				return;
			}
		} else if (id==CM_RECORD_START) {
			if (m_App.RecordManager.IsRecording()) {
				if (m_App.RecordManager.IsPaused())
					SendCommand(CM_RECORD_PAUSE);
				return;
			}
		} else if (id==CM_RECORD_STOP) {
			if (!m_App.RecordManager.IsRecording())
				return;
		}
		if (m_App.RecordManager.IsRecording()) {
			if (!m_App.RecordManager.IsPaused()
					&& !m_App.RecordOptions.ConfirmStop(GetVideoHostWindow()))
				return;
			m_App.Core.StopRecord();
		} else {
			if (m_App.RecordManager.IsReserved()) {
				if (ShowMessage(
						TEXT("���ɐݒ肳��Ă���^�悪����܂��B\n")
						TEXT("�^����J�n����Ɗ����̐ݒ肪�j������܂��B\n")
						TEXT("�^����J�n���Ă������ł���?"),
						TEXT("�^��J�n�̊m�F"),
						MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2)!=IDOK) {
					return;
				}
			}
			m_App.Core.StartRecord();
		}
		return;

	case CM_RECORD_PAUSE:
		if (m_App.RecordManager.IsRecording()) {
			m_App.RecordManager.PauseRecord();
			m_App.StatusView.UpdateItem(STATUS_ITEM_RECORD);
			m_App.Logger.AddLog(m_App.RecordManager.IsPaused()?TEXT("�^��ꎞ��~"):TEXT("�^��ĊJ"));
			m_App.PluginManager.SendRecordStatusChangeEvent();
		}
		return;

	case CM_RECORDOPTION:
		if (IsWindowEnabled(GetVideoHostWindow())) {
			if (m_App.RecordManager.IsRecording()) {
				if (m_App.RecordManager.RecordDialog(GetVideoHostWindow()))
					m_App.StatusView.UpdateItem(STATUS_ITEM_RECORD);
			} else {
				if (m_App.RecordManager.GetFileName()==nullptr) {
					TCHAR szFileName[MAX_PATH];

					if (m_App.RecordOptions.GetFilePath(szFileName,MAX_PATH))
						m_App.RecordManager.SetFileName(szFileName);
				}
				if (!m_App.RecordManager.IsReserved())
					m_App.RecordOptions.ApplyOptions(&m_App.RecordManager);
				if (m_App.RecordManager.RecordDialog(GetVideoHostWindow())) {
					m_App.RecordManager.SetClient(CRecordManager::CLIENT_USER);
					if (m_App.RecordManager.IsReserved()) {
						m_App.StatusView.UpdateItem(STATUS_ITEM_RECORD);
					} else {
						m_App.Core.StartReservedRecord();
					}
				} else {
					// �\�񂪃L�����Z�����ꂽ�ꍇ���\�����X�V����
					m_App.StatusView.UpdateItem(STATUS_ITEM_RECORD);
				}
			}
		}
		return;

	case CM_RECORDEVENT:
		if (m_App.RecordManager.IsRecording()) {
			m_App.RecordManager.SetStopOnEventEnd(!m_App.RecordManager.GetStopOnEventEnd());
		} else {
			SendCommand(CM_RECORD_START);
			if (m_App.RecordManager.IsRecording())
				m_App.RecordManager.SetStopOnEventEnd(true);
		}
		return;

	case CM_EXITONRECORDINGSTOP:
		m_App.Core.SetExitOnRecordingStop(!m_App.Core.GetExitOnRecordingStop());
		return;

	case CM_OPTIONS_RECORD:
		if (IsWindowEnabled(hwnd))
			m_App.ShowOptionDialog(hwnd,COptionDialog::PAGE_RECORD);
		return;

	case CM_TIMESHIFTRECORDING:
		if (!m_App.RecordManager.IsRecording()) {
			if (m_App.RecordManager.IsReserved()) {
				if (ShowMessage(
						TEXT("���ɐݒ肳��Ă���^�悪����܂��B\n")
						TEXT("�^����J�n����Ɗ����̐ݒ肪�j������܂��B\n")
						TEXT("�^����J�n���Ă������ł���?"),
						TEXT("�^��J�n�̊m�F"),
						MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2)!=IDOK) {
					return;
				}
			}
			m_App.Core.StartRecord(nullptr,nullptr,nullptr,CRecordManager::CLIENT_USER,true);
		}
		return;

	case CM_ENABLETIMESHIFTRECORDING:
		m_App.RecordOptions.EnableTimeShiftRecording(!m_App.RecordOptions.IsTimeShiftRecordingEnabled());
		return;

	case CM_STATUSBARRECORD:
		{
			int Command=m_App.RecordOptions.GetStatusBarRecordCommand();
			if (Command!=0)
				OnCommand(hwnd,Command,nullptr,0);
		}
		return;

	case CM_DISABLEVIEWER:
		m_pCore->EnableViewer(!m_fEnablePlayback);
		return;

	case CM_PANEL:
		if (m_pCore->GetFullscreen()) {
			m_Fullscreen.ShowPanel(!m_Fullscreen.IsPanelVisible());
		} else {
			ShowPanel(!m_App.Panel.fShowPanelWindow);
		}
		return;

	case CM_PROGRAMGUIDE:
		ShowProgramGuide(!m_App.Epg.fShowProgramGuide);
		return;

	case CM_STATUSBAR:
		SetStatusBarVisible(!m_fShowStatusBar);
		return;

	case CM_TITLEBAR:
		SetTitleBarVisible(!m_fShowTitleBar);
		return;

	case CM_SIDEBAR:
		SetSideBarVisible(!m_fShowSideBar);
		return;

	case CM_WINDOWFRAME_NORMAL:
		SetCustomFrame(false);
		return;

	case CM_WINDOWFRAME_CUSTOM:
		SetCustomFrame(true,m_ThinFrameWidth);
		return;

	case CM_WINDOWFRAME_NONE:
		SetCustomFrame(true,0);
		return;

	case CM_CUSTOMTITLEBAR:
		SetCustomTitleBar(!m_fCustomTitleBar);
		return;

	case CM_SPLITTITLEBAR:
		SetSplitTitleBar(!m_fSplitTitleBar);
		return;

	case CM_VIDEOEDGE:
		SetViewWindowEdge(!m_fViewWindowEdge);
		return;

	case CM_VIDEODECODERPROPERTY:
	case CM_VIDEORENDERERPROPERTY:
	case CM_AUDIOFILTERPROPERTY:
	case CM_AUDIORENDERERPROPERTY:
	case CM_DEMULTIPLEXERPROPERTY:
		{
			HWND hwndOwner=GetVideoHostWindow();

			if (hwndOwner==nullptr || ::IsWindowEnabled(hwndOwner)) {
				for (int i=0;i<lengthof(m_DirectShowFilterPropertyList);i++) {
					if (m_DirectShowFilterPropertyList[i].Command==id) {
						m_App.CoreEngine.m_DtvEngine.m_MediaViewer.DisplayFilterProperty(
							m_DirectShowFilterPropertyList[i].Filter,hwndOwner);
						break;
					}
				}
			}
		}
		return;

	case CM_OPTIONS:
		{
			HWND hwndOwner=GetVideoHostWindow();

			if (hwndOwner==nullptr || IsWindowEnabled(hwndOwner))
				m_App.ShowOptionDialog(hwndOwner);
		}
		return;

	case CM_STREAMINFO:
		{
			if (!m_App.StreamInfo.IsVisible()) {
				if (!m_App.StreamInfo.IsCreated())
					m_App.StreamInfo.Create(hwnd);
				else
					m_App.StreamInfo.SetVisible(true);
			} else {
				m_App.StreamInfo.Destroy();
			}

			const bool fVisible=m_App.StreamInfo.IsVisible();
			m_App.MainMenu.CheckItem(CM_STREAMINFO,fVisible);
			m_App.SideBar.CheckItem(CM_STREAMINFO,fVisible);
		}
		return;

	case CM_CLOSE:
		if (m_pCore->GetStandby()) {
			m_pCore->SetStandby(false);
		} else if (m_App.ResidentManager.GetResident()) {
			m_pCore->SetStandby(true);
		} else {
			PostMessage(WM_CLOSE,0,0);
		}
		return;

	case CM_EXIT:
		PostMessage(WM_CLOSE,0,0);
		return;

	case CM_SHOW:
		if (m_pCore->GetStandby()) {
			m_pCore->SetStandby(false);
		} else {
			SetWindowVisible();
		}
		return;

	case CM_CHANNEL_UP:
	case CM_CHANNEL_DOWN:
		{
			int Channel=m_App.ChannelManager.GetNextChannel(id==CM_CHANNEL_UP);

			if (Channel>=0)
				m_App.Core.SwitchChannel(Channel);
		}
		return;

	case CM_CHANNEL_BACKWARD:
	case CM_CHANNEL_FORWARD:
		{
			const CChannelHistory::CChannel *pChannel;

			if (id==CM_CHANNEL_BACKWARD)
				pChannel=m_App.ChannelHistory.Backward();
			else
				pChannel=m_App.ChannelHistory.Forward();
			if (pChannel!=nullptr) {
				m_App.Core.OpenTunerAndSetChannel(pChannel->GetDriverFileName(),pChannel);
			}
		}
		return;

#ifdef _DEBUG
	case CM_UPDATECHANNELLIST:
		// �`�����l�����X�g�̎����X�V(������ɂ͗����Ȃ�)
		//if (m_App.DriverOptions.IsChannelAutoUpdate(m_App.CoreEngine.GetDriverFileName()))
		{
			CTuningSpaceList TuningSpaceList(*m_App.ChannelManager.GetTuningSpaceList());
			std::vector<TVTest::String> MessageList;

			TRACE(TEXT("�`�����l�����X�g�����X�V�J�n\n"));
			if (m_App.ChannelScan.AutoUpdateChannelList(&TuningSpaceList,&MessageList)) {
				m_App.AddLog(TEXT("�`�����l�����X�g�̎����X�V���s���܂����B"));
				for (size_t i=0;i<MessageList.size();i++)
					m_App.AddLog(TEXT("%s"),MessageList[i].c_str());

				TuningSpaceList.MakeAllChannelList();
				m_App.Core.UpdateCurrentChannelList(&TuningSpaceList);

				TCHAR szFileName[MAX_PATH];
				if (!m_App.ChannelManager.GetChannelFileName(szFileName,lengthof(szFileName))
						|| ::lstrcmpi(::PathFindExtension(szFileName),CHANNEL_FILE_EXTENSION)!=0
						|| !::PathFileExists(szFileName)) {
					m_App.CoreEngine.GetDriverPath(szFileName,lengthof(szFileName));
					::PathRenameExtension(szFileName,CHANNEL_FILE_EXTENSION);
				}
				if (TuningSpaceList.SaveToFile(szFileName))
					m_App.AddLog(TEXT("�`�����l���t�@�C���� \"%s\" �ɕۑ����܂����B"),szFileName);
				else
					m_App.AddLog(TEXT("�`�����l���t�@�C�� \"%s\" ��ۑ��ł��܂���B"),szFileName);
			}
		}
		return;
#endif

	case CM_MENU:
		{
			POINT pt;
			bool fDefault=false;

			if (codeNotify==COMMAND_FROM_MOUSE) {
				::GetCursorPos(&pt);
				if (::GetKeyState(VK_SHIFT)<0)
					fDefault=true;
			} else {
				pt.x=0;
				pt.y=0;
				::ClientToScreen(m_Viewer.GetViewWindow().GetHandle(),&pt);
			}
			m_pCore->PopupMenu(&pt,fDefault?CUICore::POPUPMENU_DEFAULT:0);
		}
		return;

	case CM_ACTIVATE:
		{
			HWND hwndHost=GetVideoHostWindow();

			if (hwndHost!=nullptr)
				ForegroundWindow(hwndHost);
		}
		return;

	case CM_MINIMIZE:
		::ShowWindow(hwnd,::IsIconic(hwnd)?SW_RESTORE:SW_MINIMIZE);
		return;

	case CM_MAXIMIZE:
		::ShowWindow(hwnd,::IsZoomed(hwnd)?SW_RESTORE:SW_MAXIMIZE);
		return;

	case CM_1SEGMODE:
		m_App.Core.Set1SegMode(!m_App.Core.Is1SegMode(),true);
		return;

	case CM_HOMEDISPLAY:
		if (!m_App.HomeDisplay.GetVisible()) {
			Util::CWaitCursor WaitCursor;

			m_App.HomeDisplay.SetFont(m_App.OSDOptions.GetDisplayFont(),
									  m_App.OSDOptions.IsDisplayFontAutoSize());
			if (!m_App.HomeDisplay.IsCreated()) {
				m_App.HomeDisplay.SetEventHandler(&m_App.HomeDisplayEventHandler);
				m_App.HomeDisplay.Create(m_Viewer.GetDisplayBase().GetParent()->GetHandle(),
										 WS_CHILD | WS_CLIPCHILDREN);
				if (m_fCustomFrame)
					HookWindows(m_App.HomeDisplay.GetHandle());
			}
			m_App.HomeDisplay.UpdateContents();
			m_Viewer.GetDisplayBase().SetDisplayView(&m_App.HomeDisplay);
			m_Viewer.GetDisplayBase().SetVisible(true);
			m_App.HomeDisplay.Update();
		} else {
			m_Viewer.GetDisplayBase().SetVisible(false);
		}
		return;

	case CM_CHANNELDISPLAY:
		if (!m_App.ChannelDisplay.GetVisible()) {
			Util::CWaitCursor WaitCursor;

			m_App.ChannelDisplay.SetFont(m_App.OSDOptions.GetDisplayFont(),
										 m_App.OSDOptions.IsDisplayFontAutoSize());
			if (!m_App.ChannelDisplay.IsCreated()) {
				m_App.ChannelDisplay.SetEventHandler(&m_App.ChannelDisplayEventHandler);
				m_App.ChannelDisplay.Create(
					m_Viewer.GetDisplayBase().GetParent()->GetHandle(),
					WS_CHILD | WS_CLIPCHILDREN);
				m_App.ChannelDisplay.SetDriverManager(&m_App.DriverManager);
				m_App.ChannelDisplay.SetLogoManager(&m_App.LogoManager);
				if (m_fCustomFrame)
					HookWindows(m_App.ChannelDisplay.GetHandle());
			}
			m_Viewer.GetDisplayBase().SetDisplayView(&m_App.ChannelDisplay);
			m_Viewer.GetDisplayBase().SetVisible(true);
			if (m_App.CoreEngine.IsDriverSpecified()) {
				m_App.ChannelDisplay.SetSelect(
					m_App.CoreEngine.GetDriverFileName(),
					m_App.ChannelManager.GetCurrentChannelInfo());
			}
			m_App.ChannelDisplay.Update();
		} else {
			m_Viewer.GetDisplayBase().SetVisible(false);
		}
		return;

	case CM_ENABLEBUFFERING:
		m_App.CoreEngine.SetPacketBuffering(!m_App.CoreEngine.GetPacketBuffering());
		m_App.PlaybackOptions.SetPacketBuffering(m_App.CoreEngine.GetPacketBuffering());
		return;

	case CM_RESETBUFFER:
		m_App.CoreEngine.m_DtvEngine.ResetBuffer();
		return;

	case CM_RESETERRORCOUNT:
		m_App.CoreEngine.ResetErrorCount();
		m_App.StatusView.UpdateItem(STATUS_ITEM_ERROR);
		m_App.Panel.InfoPanel.UpdateItem(CInformationPanel::ITEM_ERROR);
		m_App.PluginManager.SendStatusResetEvent();
		return;

	case CM_SHOWRECORDREMAINTIME:
		{
			CRecordStatusItem *pItem=
				dynamic_cast<CRecordStatusItem*>(m_App.StatusView.GetItemByID(STATUS_ITEM_RECORD));

			if (pItem!=nullptr) {
				bool fRemain=!m_App.RecordOptions.GetShowRemainTime();
				m_App.RecordOptions.SetShowRemainTime(fRemain);
				pItem->ShowRemainTime(fRemain);
			}
		}
		return;

	case CM_SHOWTOTTIME:
		{
			const bool fTOT=!m_App.StatusOptions.GetShowTOTTime();
			m_App.StatusOptions.SetShowTOTTime(fTOT);

			CClockStatusItem *pItem=
				dynamic_cast<CClockStatusItem*>(m_App.StatusView.GetItemByID(STATUS_ITEM_CLOCK));
			if (pItem!=nullptr)
				pItem->SetTOT(fTOT);
		}
		return;

	case CM_PROGRAMINFOSTATUS_POPUPINFO:
		{
			const bool fEnable=!m_App.StatusOptions.IsPopupProgramInfoEnabled();
			m_App.StatusOptions.EnablePopupProgramInfo(fEnable);

			CProgramInfoStatusItem *pItem=
				dynamic_cast<CProgramInfoStatusItem*>(m_App.StatusView.GetItemByID(STATUS_ITEM_PROGRAMINFO));
			if (pItem!=nullptr)
				pItem->EnablePopupInfo(fEnable);
		}
		return;

	case CM_ADJUSTTOTTIME:
		m_App.TotTimeAdjuster.BeginAdjust();
		return;

	case CM_ZOOMMENU:
	case CM_ASPECTRATIOMENU:
	case CM_CHANNELMENU:
	case CM_SERVICEMENU:
	case CM_TUNINGSPACEMENU:
	case CM_FAVORITESMENU:
	case CM_RECENTCHANNELMENU:
	case CM_VOLUMEMENU:
	case CM_AUDIOMENU:
	case CM_RESETMENU:
	case CM_BARMENU:
	case CM_PLUGINMENU:
	case CM_FILTERPROPERTYMENU:
		{
			int SubMenu=m_App.MenuOptions.GetSubMenuPosByCommand(id);
			POINT pt;

			if (codeNotify==COMMAND_FROM_MOUSE) {
				::GetCursorPos(&pt);
			} else {
				pt.x=0;
				pt.y=0;
				::ClientToScreen(m_Viewer.GetViewWindow().GetHandle(),&pt);
			}
			m_App.MainMenu.PopupSubMenu(SubMenu,TPM_RIGHTBUTTON,pt.x,pt.y,hwnd);
		}
		return;

	case CM_SIDEBAR_PLACE_LEFT:
	case CM_SIDEBAR_PLACE_RIGHT:
	case CM_SIDEBAR_PLACE_TOP:
	case CM_SIDEBAR_PLACE_BOTTOM:
		{
			CSideBarOptions::PlaceType Place=(CSideBarOptions::PlaceType)(id-CM_SIDEBAR_PLACE_FIRST);

			if (Place!=m_App.SideBarOptions.GetPlace()) {
				bool fVertical=
					Place==CSideBarOptions::PLACE_LEFT || Place==CSideBarOptions::PLACE_RIGHT;
				int Pane=
					Place==CSideBarOptions::PLACE_LEFT || Place==CSideBarOptions::PLACE_TOP?0:1;

				m_App.SideBarOptions.SetPlace(Place);
				m_App.SideBar.SetVertical(fVertical);
				Layout::CSplitter *pSplitter=
					dynamic_cast<Layout::CSplitter*>(m_LayoutBase.GetContainerByID(CONTAINER_ID_SIDEBARSPLITTER));
				bool fSwap=pSplitter->IDToIndex(CONTAINER_ID_SIDEBAR)!=Pane;
				pSplitter->SetStyle(
					(fVertical?Layout::CSplitter::STYLE_HORZ:Layout::CSplitter::STYLE_VERT) |
					Layout::CSplitter::STYLE_FIXED,
					!fSwap);
				if (fSwap)
					pSplitter->SwapPane();
			}
		}
		return;

	case CM_SIDEBAROPTIONS:
		if (::IsWindowEnabled(hwnd))
			m_App.ShowOptionDialog(hwnd,COptionDialog::PAGE_SIDEBAR);
		return;

	case CM_DRIVER_BROWSE:
		{
			OPENFILENAME ofn;
			TCHAR szFileName[MAX_PATH],szInitDir[MAX_PATH];
			CFilePath FilePath;

			FilePath.SetPath(m_App.CoreEngine.GetDriverFileName());
			if (FilePath.GetDirectory(szInitDir)) {
				::lstrcpy(szFileName,FilePath.GetFileName());
			} else {
				m_App.GetAppDirectory(szInitDir);
				szFileName[0]='\0';
			}
			InitOpenFileName(&ofn);
			ofn.hwndOwner=GetVideoHostWindow();
			ofn.lpstrFilter=
				TEXT("BonDriver(BonDriver*.dll)\0BonDriver*.dll\0")
				TEXT("���ׂẴt�@�C��\0*.*\0");
			ofn.lpstrFile=szFileName;
			ofn.nMaxFile=lengthof(szFileName);
			ofn.lpstrInitialDir=szInitDir;
			ofn.lpstrTitle=TEXT("BonDriver�̑I��");
			ofn.Flags=OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_EXPLORER;
			if (::GetOpenFileName(&ofn)) {
				m_App.Core.OpenTuner(szFileName);
			}
		}
		return;

	case CM_CHANNELHISTORY_CLEAR:
		m_App.RecentChannelList.Clear();
		return;

	case CM_PANEL_INFORMATION:
	case CM_PANEL_PROGRAMLIST:
	case CM_PANEL_CHANNEL:
	case CM_PANEL_CONTROL:
	case CM_PANEL_CAPTION:
		m_App.Panel.Form.SetCurPageByID(id-CM_PANEL_FIRST);
		return;

	case CM_CHANNELPANEL_UPDATE:
		m_App.Panel.ChannelPanel.UpdateAllChannels(true);
		return;

	case CM_CHANNELPANEL_CURCHANNEL:
		m_App.Panel.ChannelPanel.ScrollToCurrentChannel();
		return;

	case CM_CHANNELPANEL_DETAILPOPUP:
		m_App.Panel.ChannelPanel.SetDetailToolTip(!m_App.Panel.ChannelPanel.GetDetailToolTip());
		return;

	case CM_CHANNELPANEL_SCROLLTOCURCHANNEL:
		m_App.Panel.ChannelPanel.SetScrollToCurChannel(!m_App.Panel.ChannelPanel.GetScrollToCurChannel());
		return;

	case CM_CHANNELPANEL_EVENTS_1:
	case CM_CHANNELPANEL_EVENTS_2:
	case CM_CHANNELPANEL_EVENTS_3:
	case CM_CHANNELPANEL_EVENTS_4:
		m_App.Panel.ChannelPanel.SetEventsPerChannel(id-CM_CHANNELPANEL_EVENTS_1+1);
		return;

	case CM_CHANNELPANEL_EXPANDEVENTS_2:
	case CM_CHANNELPANEL_EXPANDEVENTS_3:
	case CM_CHANNELPANEL_EXPANDEVENTS_4:
	case CM_CHANNELPANEL_EXPANDEVENTS_5:
	case CM_CHANNELPANEL_EXPANDEVENTS_6:
	case CM_CHANNELPANEL_EXPANDEVENTS_7:
	case CM_CHANNELPANEL_EXPANDEVENTS_8:
		m_App.Panel.ChannelPanel.SetEventsPerChannel(-1,id-CM_CHANNELPANEL_EXPANDEVENTS_2+2);
		return;

	case CM_CHANNELNO_2DIGIT:
	case CM_CHANNELNO_3DIGIT:
		{
			int Digits=id==CM_CHANNELNO_2DIGIT?2:3;

			if (m_ChannelNoInput.fInputting) {
				EndChannelNoInput();
				if (Digits==m_ChannelNoInput.Digits)
					return;
			}
			BeginChannelNoInput(Digits);
		}
		return;

	case CM_ADDTOFAVORITES:
		{
			const CChannelInfo *pChannel=m_App.ChannelManager.GetCurrentRealChannelInfo();
			if (pChannel!=nullptr)
				m_App.FavoritesManager.AddChannel(pChannel,m_App.CoreEngine.GetDriverFileName());
		}
		return;

	case CM_ORGANIZEFAVORITES:
		{
			COrganizeFavoritesDialog Dialog(&m_App.FavoritesManager);

			Dialog.Show(GetVideoHostWindow());
		}
		return;

	default:
		if ((id>=CM_ZOOM_FIRST && id<=CM_ZOOM_LAST)
				|| (id>=CM_CUSTOMZOOM_FIRST && id<=CM_CUSTOMZOOM_LAST)) {
			CZoomOptions::ZoomInfo Info;

			if (m_pCore->GetFullscreen())
				m_pCore->SetFullscreen(false);
			if (::IsZoomed(hwnd))
				::ShowWindow(hwnd,SW_RESTORE);
			if (m_App.ZoomOptions.GetZoomInfoByCommand(id,&Info)) {
				if (Info.Type==CZoomOptions::ZOOM_RATE)
					SetZoomRate(Info.Rate.Rate,Info.Rate.Factor);
				else if (Info.Type==CZoomOptions::ZOOM_SIZE)
					AdjustWindowSize(Info.Size.Width,Info.Size.Height);
			}
			return;
		}

		if (id>=CM_AUDIOSTREAM_FIRST && id<=CM_AUDIOSTREAM_LAST) {
			m_pCore->SetAudioStream(id-CM_AUDIOSTREAM_FIRST);
			ShowAudioOSD();
			return;
		}

		if (id>=CM_CAPTURESIZE_FIRST && id<=CM_CAPTURESIZE_LAST) {
			int CaptureSize=id-CM_CAPTURESIZE_FIRST;

			m_App.CaptureOptions.SetPresetCaptureSize(CaptureSize);
			m_App.MainMenu.CheckRadioItem(CM_CAPTURESIZE_FIRST,CM_CAPTURESIZE_LAST,id);
			return;
		}

		if (id>=CM_CHANNELNO_FIRST && id<=CM_CHANNELNO_LAST) {
			m_App.Core.SwitchChannelByNo((id-CM_CHANNELNO_FIRST)+1,true);
			return;
		}

		if (id>=CM_CHANNEL_FIRST && id<=CM_CHANNEL_LAST) {
			m_App.Core.SwitchChannel(id-CM_CHANNEL_FIRST);
			return;
		}

		if (id>=CM_SERVICE_FIRST && id<=CM_SERVICE_LAST) {
			if (m_App.RecordManager.IsRecording()) {
				if (!m_App.RecordOptions.ConfirmServiceChange(
						GetVideoHostWindow(),&m_App.RecordManager))
					return;
			}
			m_App.Core.SetServiceByIndex(id-CM_SERVICE_FIRST,CAppCore::SET_SERVICE_STRICT_ID);
			return;
		}

		if (id>=CM_SPACE_ALL && id<=CM_SPACE_LAST) {
			int Space=id-CM_SPACE_FIRST;

			if (Space!=m_App.ChannelManager.GetCurrentSpace()) {
				const CChannelList *pChannelList=m_App.ChannelManager.GetChannelList(Space);
				if (pChannelList!=nullptr) {
					for (int i=0;i<pChannelList->NumChannels();i++) {
						if (pChannelList->IsEnabled(i)) {
							m_App.Core.SetChannel(Space,i);
							return;
						}
					}
				}
			}
			return;
		}

		if (id>=CM_DRIVER_FIRST && id<=CM_DRIVER_LAST) {
			const CDriverInfo *pDriverInfo=m_App.DriverManager.GetDriverInfo(id-CM_DRIVER_FIRST);

			if (pDriverInfo!=nullptr) {
				if (!m_App.CoreEngine.IsTunerOpen()
						|| !IsEqualFileName(pDriverInfo->GetFileName(),m_App.CoreEngine.GetDriverFileName())) {
					if (m_App.Core.OpenTuner(pDriverInfo->GetFileName())) {
						m_App.Core.RestoreChannel();
					}
				}
			}
			return;
		}

		if (id>=CM_PLUGIN_FIRST && id<=CM_PLUGIN_LAST) {
			CPlugin *pPlugin=m_App.PluginManager.GetPlugin(m_App.PluginManager.FindPluginByCommand(id));

			if (pPlugin!=nullptr)
				pPlugin->Enable(!pPlugin->IsEnabled());
			return;
		}

		if (id>=CM_SPACE_CHANNEL_FIRST && id<=CM_SPACE_CHANNEL_LAST) {
			if (!m_pCore->ConfirmChannelChange())
				return;
			m_pCore->ProcessTunerMenu(id);
			return;
		}

		if (id>=CM_CHANNELHISTORY_FIRST && id<=CM_CHANNELHISTORY_LAST) {
			const CRecentChannelList::CChannel *pChannel=
				m_App.RecentChannelList.GetChannelInfo(id-CM_CHANNELHISTORY_FIRST);

			if (pChannel!=nullptr)
				m_App.Core.OpenTunerAndSetChannel(pChannel->GetDriverFileName(),pChannel);
			return;
		}

		if (id>=CM_FAVORITECHANNEL_FIRST && id<=CM_FAVORITECHANNEL_LAST) {
			CFavoritesManager::ChannelInfo ChannelInfo;

			if (m_App.FavoritesManager.GetChannelByCommand(id,&ChannelInfo)) {
				CAppCore::ChannelSelectInfo SelInfo;

				SelInfo.Channel=ChannelInfo.Channel;
				SelInfo.TunerName=ChannelInfo.BonDriverFileName;
				SelInfo.fUseCurTuner=!ChannelInfo.fForceBonDriverChange;
				m_App.Core.SelectChannel(SelInfo);
			}
			return;
		}

		if (id>=CM_PLUGINCOMMAND_FIRST && id<=CM_PLUGINCOMMAND_LAST) {
			m_App.PluginManager.OnPluginCommand(m_App.CommandList.GetCommandText(m_App.CommandList.IDToIndex(id)));
			return;
		}

		if (id>=CM_PANANDSCAN_PRESET_FIRST && id<=CM_PANANDSCAN_PRESET_LAST) {
			SetPanAndScan(id);
			return;
		}
	}
}


void CMainWindow::OnTimer(HWND hwnd,UINT id)
{
	switch (id) {
	case TIMER_ID_UPDATE:
		// ���X�V
		{
			static unsigned int TimerCount=0;
			const CChannelInfo *pChInfo=m_App.ChannelManager.GetCurrentChannelInfo();

			DWORD UpdateStatus=m_App.CoreEngine.UpdateAsyncStatus();
			DWORD UpdateStatistics=m_App.CoreEngine.UpdateStatistics();

			// �f���T�C�Y�̕ω�
			if ((UpdateStatus&CCoreEngine::STATUS_VIDEOSIZE)!=0) {
				m_App.StatusView.UpdateItem(STATUS_ITEM_VIDEOSIZE);
				m_App.Panel.InfoPanel.SetVideoSize(
					m_App.CoreEngine.GetOriginalVideoWidth(),
					m_App.CoreEngine.GetOriginalVideoHeight(),
					m_App.CoreEngine.GetDisplayVideoWidth(),
					m_App.CoreEngine.GetDisplayVideoHeight());
				m_App.Panel.ControlPanel.UpdateItem(CONTROLPANEL_ITEM_VIDEO);
			}

			// �����`���̕ω�
			if ((UpdateStatus&(CCoreEngine::STATUS_AUDIOCHANNELS
							 | CCoreEngine::STATUS_AUDIOSTREAMS
							 | CCoreEngine::STATUS_AUDIOCOMPONENTTYPE
							 | CCoreEngine::STATUS_SPDIFPASSTHROUGH))!=0) {
				TRACE(TEXT("Audio status changed.\n"));
				if ((UpdateStatus&CCoreEngine::STATUS_SPDIFPASSTHROUGH)==0)
					AutoSelectStereoMode();
				m_App.StatusView.UpdateItem(STATUS_ITEM_AUDIOCHANNEL);
				m_App.Panel.ControlPanel.UpdateItem(CONTROLPANEL_ITEM_AUDIO);
				m_App.SideBar.CheckItem(CM_SPDIF_TOGGLE,
					m_App.CoreEngine.m_DtvEngine.m_MediaViewer.IsSpdifPassthrough());
			}

			// �ԑg�̐؂�ւ��
			if ((UpdateStatus&CCoreEngine::STATUS_EVENTID)!=0) {
				// �ԑg�̍Ō�܂Ř^��
				if (m_App.RecordManager.GetStopOnEventEnd())
					m_App.Core.StopRecord();

				m_pCore->UpdateTitle();

				if (m_App.OSDOptions.IsNotifyEnabled(COSDOptions::NOTIFY_EVENTNAME)
						&& !m_App.Core.IsChannelScanning()) {
					TCHAR szEventName[256];

					if (m_App.CoreEngine.m_DtvEngine.GetEventName(szEventName,lengthof(szEventName))>0) {
						TCHAR szBarText[EpgUtil::MAX_EVENT_TIME_LENGTH+lengthof(szEventName)];
						int Length=0;
						SYSTEMTIME StartTime;
						DWORD Duration;

						if (m_App.CoreEngine.m_DtvEngine.GetEventTime(&StartTime,&Duration)) {
							Length=EpgUtil::FormatEventTime(StartTime,Duration,
															szBarText,EpgUtil::MAX_EVENT_TIME_LENGTH);
							if (Length>0)
								szBarText[Length++]=_T(' ');
						}
						::lstrcpy(szBarText+Length,szEventName);
						ShowNotificationBar(szBarText,CNotificationBar::MESSAGE_INFO,0,true);
					}
				}

				if (m_App.Panel.fShowPanelWindow && m_App.Panel.Form.GetVisible()
						&& m_App.Panel.Form.GetCurPageID()==PANEL_ID_INFORMATION)
					UpdateProgramInfo();

				m_App.Panel.ProgramListPanel.SetCurrentEventID(m_App.CoreEngine.m_DtvEngine.GetEventID());

				CProgramInfoStatusItem *pProgramInfoItem=
					dynamic_cast<CProgramInfoStatusItem*>(m_App.StatusView.GetItemByID(STATUS_ITEM_PROGRAMINFO));
				if (pProgramInfoItem!=nullptr) {
					pProgramInfoItem->UpdateContent();
					pProgramInfoItem->Update();
				}

				if (m_AspectRatioType!=ASPECTRATIO_DEFAULT
						&& (m_fForceResetPanAndScan
						|| (m_App.ViewOptions.GetResetPanScanEventChange()
							&& m_AspectRatioType<ASPECTRATIO_CUSTOM))) {
					m_App.CoreEngine.m_DtvEngine.m_MediaViewer.SetPanAndScan(0,0);
					if (!m_pCore->GetFullscreen()
							&& IsViewerEnabled()) {
						AutoFitWindowToVideo();
						// ���̎��_�ł܂��V�����f���T�C�Y���擾�ł��Ȃ��ꍇ�����邽�߁A
						// WM_APP_VIDEOSIZECHANGED ���������ɒ�������悤�ɂ���
						m_AspectRatioResetTime=::GetTickCount();
					}
					m_AspectRatioType=ASPECTRATIO_DEFAULT;
					m_fForceResetPanAndScan=false;
					m_App.StatusView.UpdateItem(STATUS_ITEM_VIDEOSIZE);
					m_App.Panel.ControlPanel.UpdateItem(CONTROLPANEL_ITEM_VIDEO);
					/*
					m_App.MainMenu.CheckRadioItem(CM_ASPECTRATIO_FIRST,CM_ASPECTRATIO_3D_LAST,
											CM_ASPECTRATIO_DEFAULT);
					*/
					m_App.AspectRatioIconMenu.CheckRadioItem(
						CM_ASPECTRATIO_FIRST,CM_ASPECTRATIO_3D_LAST,
						CM_ASPECTRATIO_DEFAULT);
					m_App.SideBar.CheckRadioItem(CM_ASPECTRATIO_FIRST,CM_ASPECTRATIO_LAST,
										   CM_ASPECTRATIO_DEFAULT);
				}

				m_CurEventStereoMode=-1;
				AutoSelectStereoMode();
			}

			// ���ԕύX�Ȃǂ𔽉f�����邽�߂ɔԑg�����X�V
			if (TimerCount%(10000/UPDATE_TIMER_INTERVAL)==0) {
				m_pCore->UpdateTitle();

				CProgramInfoStatusItem *pProgramInfoItem=
					dynamic_cast<CProgramInfoStatusItem*>(m_App.StatusView.GetItemByID(STATUS_ITEM_PROGRAMINFO));
				if (pProgramInfoItem!=nullptr) {
					if (pProgramInfoItem->UpdateContent())
						pProgramInfoItem->Update();
				}
			}

			if (m_App.RecordManager.IsRecording()) {
				if (m_App.RecordManager.QueryStop()) {
					m_App.Core.StopRecord();
				} else if (!m_App.RecordManager.IsPaused()) {
					m_App.StatusView.UpdateItem(STATUS_ITEM_RECORD);
				}
			} else {
				if (m_App.RecordManager.QueryStart())
					m_App.Core.StartReservedRecord();
			}

			if ((UpdateStatistics&(CCoreEngine::STATISTIC_ERRORPACKETCOUNT
								 | CCoreEngine::STATISTIC_CONTINUITYERRORPACKETCOUNT
								 | CCoreEngine::STATISTIC_SCRAMBLEPACKETCOUNT))!=0) {
				m_App.StatusView.UpdateItem(STATUS_ITEM_ERROR);
			}

			if ((UpdateStatistics&(CCoreEngine::STATISTIC_SIGNALLEVEL
								 | CCoreEngine::STATISTIC_BITRATE))!=0)
				m_App.StatusView.UpdateItem(STATUS_ITEM_SIGNALLEVEL);

			if ((UpdateStatistics&(CCoreEngine::STATISTIC_STREAMREMAIN
								 | CCoreEngine::STATISTIC_PACKETBUFFERRATE))!=0)
				m_App.StatusView.UpdateItem(STATUS_ITEM_BUFFERING);

			m_App.StatusView.UpdateItem(STATUS_ITEM_CLOCK);
			m_App.TotTimeAdjuster.AdjustTime();

			m_App.StatusView.UpdateItem(STATUS_ITEM_MEDIABITRATE);

			if (m_App.Panel.fShowPanelWindow && m_App.Panel.Form.GetVisible()) {
				// �p�l���̍X�V
				if (m_App.Panel.Form.GetCurPageID()==PANEL_ID_INFORMATION) {
					// ���^�u�X�V
					BYTE AspectX,AspectY;
					if (m_App.CoreEngine.m_DtvEngine.m_MediaViewer.GetEffectiveAspectRatio(&AspectX,&AspectY))
						m_App.Panel.InfoPanel.SetAspectRatio(AspectX,AspectY);

					if ((UpdateStatistics&(CCoreEngine::STATISTIC_SIGNALLEVEL
										 | CCoreEngine::STATISTIC_BITRATE))!=0) {
						m_App.Panel.InfoPanel.UpdateItem(CInformationPanel::ITEM_SIGNALLEVEL);
					}

					m_App.Panel.InfoPanel.SetMediaBitRate(
						m_App.CoreEngine.m_DtvEngine.m_MediaViewer.GetVideoBitRate(),
						m_App.CoreEngine.m_DtvEngine.m_MediaViewer.GetAudioBitRate());

					if ((UpdateStatistics&(CCoreEngine::STATISTIC_ERRORPACKETCOUNT
										 | CCoreEngine::STATISTIC_CONTINUITYERRORPACKETCOUNT
										 | CCoreEngine::STATISTIC_SCRAMBLEPACKETCOUNT))!=0) {
						m_App.Panel.InfoPanel.UpdateItem(CInformationPanel::ITEM_ERROR);
					}

					if (m_App.RecordManager.IsRecording()) {
						const CRecordTask *pRecordTask=m_App.RecordManager.GetRecordTask();
						const LONGLONG FreeSpace=pRecordTask->GetFreeSpace();

						m_App.Panel.InfoPanel.SetRecordStatus(true,pRecordTask->GetFileName(),
							pRecordTask->GetWroteSize(),pRecordTask->GetRecordTime(),
							FreeSpace<0?0:FreeSpace);
					}

					if (TimerCount%(10000/UPDATE_TIMER_INTERVAL)==0)
						UpdateProgramInfo();
				} else if (m_App.Panel.Form.GetCurPageID()==PANEL_ID_CHANNEL) {
					// �`�����l���^�u�X�V
					if (!m_App.EpgOptions.IsEpgFileLoading()
							&& m_App.Panel.ChannelPanel.QueryUpdate())
						m_App.Panel.ChannelPanel.UpdateAllChannels(false);
				}
			}

			// �󂫗e�ʂ����Ȃ��ꍇ�̒��ӕ\��
			if (m_App.RecordOptions.GetAlertLowFreeSpace()
					&& !m_fAlertedLowFreeSpace
					&& m_App.RecordManager.IsRecording()) {
				LONGLONG FreeSpace=m_App.RecordManager.GetRecordTask()->GetFreeSpace();

				if (FreeSpace>=0
						&& (ULONGLONG)FreeSpace<=m_App.RecordOptions.GetLowFreeSpaceThresholdBytes()) {
					m_App.NotifyBalloonTip.Show(
						APP_NAME TEXT("�̘^��t�@�C���̕ۑ���̋󂫗e�ʂ����Ȃ��Ȃ��Ă��܂��B"),
						TEXT("�󂫗e�ʂ����Ȃ��Ȃ��Ă��܂��B"),
						nullptr,CBalloonTip::ICON_WARNING);
					::SetTimer(m_hwnd,TIMER_ID_HIDETOOLTIP,10000,nullptr);
					ShowNotificationBar(
						TEXT("�^��t�@�C���̕ۑ���̋󂫗e�ʂ����Ȃ��Ȃ��Ă��܂�"),
						CNotificationBar::MESSAGE_WARNING,6000);
					m_fAlertedLowFreeSpace=true;
				}
			}

			TimerCount++;
		}
		break;

	case TIMER_ID_OSD:
		// OSD ������
		m_App.OSDManager.ClearOSD();
		::KillTimer(hwnd,TIMER_ID_OSD);
		break;

	case TIMER_ID_DISPLAY:
		// ���j�^���I�t�ɂȂ�Ȃ��悤�ɂ���
		::SetThreadExecutionState(ES_DISPLAY_REQUIRED);
		break;

	case TIMER_ID_WHEELCHANNELCHANGE:
		// �z�C�[���ł̃`�����l���ύX
		{
			const int Channel=m_App.ChannelManager.GetChangingChannel();

			SetWheelChannelChanging(false);
			m_App.ChannelManager.SetChangingChannel(-1);
			if (Channel>=0)
				m_App.Core.SwitchChannel(Channel);
		}
		break;

	case TIMER_ID_PROGRAMLISTUPDATE:
		if (m_ProgramListUpdateTimerCount==0) {
			// �T�[�r�X�ƃ��S���֘A�t����
			CTsAnalyzer *pAnalyzer=&m_App.CoreEngine.m_DtvEngine.m_TsAnalyzer;
			const WORD NetworkID=pAnalyzer->GetNetworkID();
			if (NetworkID!=0) {
				CTsAnalyzer::ServiceList ServiceList;
				if (pAnalyzer->GetServiceList(&ServiceList)) {
					for (size_t i=0;i<ServiceList.size();i++) {
						const CTsAnalyzer::ServiceInfo *pServiceInfo=&ServiceList[i];
						const WORD LogoID=pServiceInfo->LogoID;
						if (LogoID!=0xFFFF)
							m_App.LogoManager.AssociateLogoID(NetworkID,pServiceInfo->ServiceID,LogoID);
					}
				}
			}
		}

		// EPG���̓���
		if (!m_App.EpgOptions.IsEpgFileLoading()
				&& !m_App.EpgOptions.IsEDCBDataLoading()) {
			CChannelInfo ChInfo;

			if (m_App.Panel.fShowPanelWindow
					&& m_App.Core.GetCurrentStreamChannelInfo(&ChInfo)) {
				if (m_App.Panel.Form.GetCurPageID()==PANEL_ID_PROGRAMLIST) {
					if (ChInfo.GetServiceID()!=0) {
						const HANDLE hThread=::GetCurrentThread();
						const int OldPriority=::GetThreadPriority(hThread);
						::SetThreadPriority(hThread,THREAD_PRIORITY_BELOW_NORMAL);

						m_App.EpgProgramList.UpdateService(
							ChInfo.GetNetworkID(),
							ChInfo.GetTransportStreamID(),
							ChInfo.GetServiceID());
						m_App.Panel.ProgramListPanel.UpdateProgramList(&ChInfo);

						::SetThreadPriority(hThread,OldPriority);
					}
				} else if (m_App.Panel.Form.GetCurPageID()==PANEL_ID_CHANNEL) {
					m_App.Panel.ChannelPanel.UpdateChannels(
						ChInfo.GetNetworkID(),
						ChInfo.GetTransportStreamID());
				}
			}

			m_ProgramListUpdateTimerCount++;
			// �X�V�p�x��������
			if (m_ProgramListUpdateTimerCount>=6 && m_ProgramListUpdateTimerCount<=10)
				::SetTimer(hwnd,TIMER_ID_PROGRAMLISTUPDATE,(m_ProgramListUpdateTimerCount-5)*(60*1000),nullptr);
		}
		break;

	case TIMER_ID_PROGRAMGUIDEUPDATE:
		// �ԑg�\�̎擾
		if (m_fProgramGuideUpdating) {
			CEventManager *pEventManager=&m_App.CoreEngine.m_DtvEngine.m_EventManager;
			const CChannelList *pChannelList=m_App.ChannelManager.GetCurrentRealChannelList();
			const CChannelInfo *pCurChannelInfo=m_App.ChannelManager.GetCurrentChannelInfo();
			if (pChannelList==nullptr || pCurChannelInfo==nullptr) {
				EndProgramGuideUpdate();
				break;
			}

			bool fComplete=true,fBasic=false,fNoBasic=false;
			EpgChannelGroup &CurChGroup=m_EpgUpdateChannelList[m_EpgUpdateCurChannel];
			for (int i=0;i<CurChGroup.ChannelList.NumChannels();i++) {
				const CChannelInfo *pChannelInfo=CurChGroup.ChannelList.GetChannelInfo(i);
				const WORD NetworkID=pChannelInfo->GetNetworkID();
				const WORD TSID=pChannelInfo->GetTransportStreamID();
				const WORD ServiceID=pChannelInfo->GetServiceID();
				const NetworkType Network=GetNetworkType(NetworkID);

				if (pEventManager->HasSchedule(NetworkID,TSID,ServiceID,false)) {
					fBasic=true;
					if (!pEventManager->IsScheduleComplete(NetworkID,TSID,ServiceID,false)) {
						fComplete=false;
						break;
					}
					if (Network==NETWORK_TERRESTRIAL
							|| (Network==NETWORK_BS && m_App.EpgOptions.GetUpdateBSExtended())
							|| (Network==NETWORK_CS && m_App.EpgOptions.GetUpdateCSExtended())) {
						if (pEventManager->HasSchedule(NetworkID,TSID,ServiceID,true)
								&& !pEventManager->IsScheduleComplete(NetworkID,TSID,ServiceID,true)) {
							fComplete=false;
							break;
						}
					}
				} else {
					fNoBasic=true;
				}
			}

			if (fComplete && fBasic && fNoBasic
					&& m_EpgAccumulateClock.GetSpan()<60000)
				fComplete=false;

			if (fComplete) {
				TRACE(TEXT("EPG schedule complete\n"));
				if (!m_pCore->GetStandby())
					m_App.Epg.ProgramGuide.SendMessage(WM_COMMAND,CM_PROGRAMGUIDE_REFRESH,0);
			} else {
				WORD NetworkID=m_App.CoreEngine.m_DtvEngine.m_TsAnalyzer.GetNetworkID();
				DWORD Timeout;

				// �^�ʖڂɔ��肷��ꍇBIT�������������Ă���K�v������
				if (IsBSNetworkID(NetworkID) || IsCSNetworkID(NetworkID))
					Timeout=360000;
				else
					Timeout=120000;
				if (m_EpgAccumulateClock.GetSpan()>=Timeout) {
					TRACE(TEXT("EPG schedule timeout\n"));
					fComplete=true;
				} else {
					::SetTimer(m_hwnd,TIMER_ID_PROGRAMGUIDEUPDATE,5000,nullptr);
				}
			}

			if (fComplete) {
				SetEpgUpdateNextChannel();
			}
		}
		break;

	case TIMER_ID_VIDEOSIZECHANGED:
		// �f���T�C�Y�̕ω��ɍ��킹��

		if (m_App.ViewOptions.GetRemember1SegWindowSize()) {
			int Width,Height;

			if (m_App.CoreEngine.GetVideoViewSize(&Width,&Height)
					&& Width>0 && Height>0) {
				WindowSizeMode Mode=
					Height<=240 ? WINDOW_SIZE_1SEG : WINDOW_SIZE_HD;

				if (m_WindowSizeMode!=Mode) {
					RECT rc;
					GetPosition(&rc);
					if (m_WindowSizeMode==WINDOW_SIZE_1SEG)
						m_1SegWindowSize=rc;
					else
						m_HDWindowSize=rc;
					m_WindowSizeMode=Mode;
					const WindowSize *pSize;
					if (m_WindowSizeMode==WINDOW_SIZE_1SEG)
						pSize=&m_1SegWindowSize;
					else
						pSize=&m_HDWindowSize;
					if (pSize->IsValid())
						AdjustWindowSize(pSize->Width,pSize->Height,false);
				}
			}
		}

		m_Viewer.GetVideoContainer().SendSizeMessage();
		m_App.StatusView.UpdateItem(STATUS_ITEM_VIDEOSIZE);
		m_App.Panel.ControlPanel.UpdateItem(CONTROLPANEL_ITEM_VIDEO);
		if (m_VideoSizeChangedTimerCount==3)
			::KillTimer(hwnd,TIMER_ID_VIDEOSIZECHANGED);
		else
			m_VideoSizeChangedTimerCount++;
		break;

	case TIMER_ID_RESETERRORCOUNT:
		// �G���[�J�E���g�����Z�b�g����
		// (���ɃT�[�r�X�̏�񂪎擾����Ă���ꍇ�̂�)
		if (m_App.CoreEngine.m_DtvEngine.m_TsAnalyzer.GetServiceNum()>0) {
			SendCommand(CM_RESETERRORCOUNT);
			m_ResetErrorCountTimer.End();
		}
		break;

	case TIMER_ID_HIDETOOLTIP:
		// �c�[���`�b�v���\���ɂ���
		m_App.NotifyBalloonTip.Hide();
		::KillTimer(hwnd,TIMER_ID_HIDETOOLTIP);
		break;

	case TIMER_ID_CHANNELNO:
		// �`�����l���ԍ����͂̎��Ԑ؂�
		EndChannelNoInput();
		return;

	case TIMER_ID_HIDECURSOR:
		if (m_App.OperationOptions.GetHideCursor()) {
			if (!m_fNoHideCursor) {
				POINT pt;
				RECT rc;
				::GetCursorPos(&pt);
				m_Viewer.GetViewWindow().GetScreenPosition(&rc);
				if (::PtInRect(&rc,pt)) {
					ShowCursor(false);
					::SetCursor(nullptr);
				}
			}
		}
		::KillTimer(hwnd,TIMER_ID_HIDECURSOR);
		break;
	}
}


bool CMainWindow::UpdateProgramInfo()
{
	const bool fNext=m_App.Panel.InfoPanel.GetProgramInfoNext();
	TCHAR szText[4096],szTemp[2048];
	CStaticStringFormatter Formatter(szText,lengthof(szText));

	if (fNext)
		Formatter.Append(TEXT("�� : "));

	SYSTEMTIME StartTime;
	DWORD Duration;
	if (m_App.CoreEngine.m_DtvEngine.GetEventTime(&StartTime,&Duration,fNext)
			&& EpgUtil::FormatEventTime(StartTime,Duration,szTemp,lengthof(szTemp),
				EpgUtil::EVENT_TIME_DATE | EpgUtil::EVENT_TIME_YEAR | EpgUtil::EVENT_TIME_UNDECIDED_TEXT)>0) {
		Formatter.Append(szTemp);
		Formatter.Append(TEXT("\r\n"));
	}
	if (m_App.CoreEngine.m_DtvEngine.GetEventName(szTemp,lengthof(szTemp),fNext)>0) {
		Formatter.Append(szTemp);
		Formatter.Append(TEXT("\r\n\r\n"));
	}
	if (m_App.CoreEngine.m_DtvEngine.GetEventText(szTemp,lengthof(szTemp),fNext)>0) {
		Formatter.Append(szTemp);
		Formatter.Append(TEXT("\r\n\r\n"));
	}
	if (m_App.CoreEngine.m_DtvEngine.GetEventExtendedText(szTemp,lengthof(szTemp),fNext)>0) {
		Formatter.Append(szTemp);
	}

	CTsAnalyzer::EventSeriesInfo SeriesInfo;
	if (m_App.CoreEngine.m_DtvEngine.GetEventSeriesInfo(&SeriesInfo,fNext)
			&& SeriesInfo.EpisodeNumber!=0 && SeriesInfo.LastEpisodeNumber!=0) {
		Formatter.Append(TEXT("\r\n\r\n(�V���[�Y"));
		if (SeriesInfo.RepeatLabel!=0)
			Formatter.Append(TEXT(" [��]"));
		if (SeriesInfo.EpisodeNumber!=0 && SeriesInfo.LastEpisodeNumber!=0)
			Formatter.AppendFormat(TEXT(" ��%d�� / �S%d��"),
								   SeriesInfo.EpisodeNumber,SeriesInfo.LastEpisodeNumber);
		// expire_date �͎��ۂ̍ŏI��̓����łȂ��̂ŁA����킵�����ߕ\�����Ȃ�
		/*
		if (SeriesInfo.bIsExpireDateValid)
			Formatter.AppendFormat(TEXT(" �I���\��%d/%d/%d"),
								   SeriesInfo.ExpireDate.wYear,
								   SeriesInfo.ExpireDate.wMonth,
								   SeriesInfo.ExpireDate.wDay);
		*/
		Formatter.Append(TEXT(")"));
	}

	m_App.Panel.InfoPanel.SetProgramInfo(Formatter.GetString());
	return true;
}


bool CMainWindow::OnInitMenuPopup(HMENU hmenu)
{
	if (m_App.MainMenu.IsMainMenu(hmenu)) {
		bool fFullscreen=m_pCore->GetFullscreen();
		bool fView=IsViewerEnabled();

		m_App.MainMenu.EnableItem(CM_COPY,fView);
		m_App.MainMenu.EnableItem(CM_SAVEIMAGE,fView);
		m_App.MainMenu.CheckItem(CM_PANEL,
			fFullscreen?m_Fullscreen.IsPanelVisible():m_App.Panel.fShowPanelWindow);
	} else if (hmenu==m_App.MainMenu.GetSubMenu(CMainMenu::SUBMENU_ZOOM)) {
		CZoomOptions::ZoomInfo Zoom;

		if (!GetZoomRate(&Zoom.Rate.Rate,&Zoom.Rate.Factor)) {
			Zoom.Rate.Rate=0;
			Zoom.Rate.Factor=0;
		}

		SIZE sz;
		if (m_Viewer.GetVideoContainer().GetClientSize(&sz)) {
			Zoom.Size.Width=sz.cx;
			Zoom.Size.Height=sz.cy;
		} else {
			Zoom.Size.Width=0;
			Zoom.Size.Height=0;
		}

		m_App.ZoomOptions.SetMenu(hmenu,&Zoom);
		m_App.Accelerator.SetMenuAccel(hmenu);
	} else if (hmenu==m_App.MainMenu.GetSubMenu(CMainMenu::SUBMENU_SPACE)) {
		m_pCore->InitTunerMenu(hmenu);
	} else if (hmenu==m_App.MainMenu.GetSubMenu(CMainMenu::SUBMENU_PLUGIN)) {
		m_App.PluginManager.SetMenu(hmenu);
		m_App.Accelerator.SetMenuAccel(hmenu);
	} else if (hmenu==m_App.MainMenu.GetSubMenu(CMainMenu::SUBMENU_FAVORITES)) {
		//m_App.FavoritesManager.SetMenu(hmenu);
		m_App.FavoritesMenu.Create(&m_App.FavoritesManager.GetRootFolder(),
			CM_FAVORITECHANNEL_FIRST,hmenu,m_hwnd,
			CFavoritesMenu::FLAG_SHOWEVENTINFO | CFavoritesMenu::FLAG_SHOWLOGO);
		::EnableMenuItem(hmenu,CM_ADDTOFAVORITES,
			MF_BYCOMMAND | (m_App.ChannelManager.GetCurrentRealChannelInfo()!=nullptr?MF_ENABLED:MF_GRAYED));
	} else if (hmenu==m_App.MainMenu.GetSubMenu(CMainMenu::SUBMENU_CHANNELHISTORY)) {
		m_App.RecentChannelList.SetMenu(hmenu);
	} else if (hmenu==m_App.MainMenu.GetSubMenu(CMainMenu::SUBMENU_ASPECTRATIO)) {
		int ItemCount=::GetMenuItemCount(hmenu);

		if (ItemCount>m_DefaultAspectRatioMenuItemCount) {
			for (;ItemCount>m_DefaultAspectRatioMenuItemCount;ItemCount--) {
				::DeleteMenu(hmenu,ItemCount-3,MF_BYPOSITION);
			}
		}

		size_t PresetCount=m_App.PanAndScanOptions.GetPresetCount();
		if (PresetCount>0) {
			::InsertMenu(hmenu,ItemCount-2,MF_BYPOSITION | MF_SEPARATOR,0,nullptr);
			for (size_t i=0;i<PresetCount;i++) {
				CPanAndScanOptions::PanAndScanInfo Info;
				TCHAR szText[CPanAndScanOptions::MAX_NAME*2];

				m_App.PanAndScanOptions.GetPreset(i,&Info);
				CopyToMenuText(Info.szName,szText,lengthof(szText));
				::InsertMenu(hmenu,ItemCount-2+(UINT)i,
							 MF_BYPOSITION | MF_STRING | MF_ENABLED
							 | (m_AspectRatioType==ASPECTRATIO_CUSTOM+(int)i?MF_CHECKED:MF_UNCHECKED),
							 CM_PANANDSCAN_PRESET_FIRST+i,szText);
			}
		}

		m_App.AspectRatioIconMenu.CheckItem(CM_FRAMECUT,
			m_App.CoreEngine.m_DtvEngine.m_MediaViewer.GetViewStretchMode()==CMediaViewer::STRETCH_CUTFRAME);

		m_App.Accelerator.SetMenuAccel(hmenu);
		if (!m_App.AspectRatioIconMenu.OnInitMenuPopup(m_hwnd,hmenu))
			return false;
	} else if (hmenu==m_App.MainMenu.GetSubMenu(CMainMenu::SUBMENU_CHANNEL)) {
		m_pCore->InitChannelMenu(hmenu);
	} else if (hmenu==m_App.MainMenu.GetSubMenu(CMainMenu::SUBMENU_SERVICE)) {
		m_App.ChannelMenu.Destroy();
		ClearMenu(hmenu);

		CAppCore::StreamIDInfo StreamID;

		if (m_App.Core.GetCurrentStreamIDInfo(&StreamID)) {
			CTsAnalyzer::ServiceList ServiceList;
			CChannelList ChList;
			int CurService=-1;

			m_App.CoreEngine.m_DtvEngine.m_TsAnalyzer.GetViewableServiceList(&ServiceList);

			for (int i=0;i<static_cast<int>(ServiceList.size());i++) {
				const CTsAnalyzer::ServiceInfo &ServiceInfo=ServiceList[i];
				CChannelInfo *pChInfo=new CChannelInfo;

				pChInfo->SetChannelNo(i+1);
				if (ServiceInfo.szServiceName[0]!='\0') {
					pChInfo->SetName(ServiceInfo.szServiceName);
				} else {
					TCHAR szName[32];
					StdUtil::snprintf(szName,lengthof(szName),TEXT("�T�[�r�X%d"),i+1);
					pChInfo->SetName(szName);
				}
				pChInfo->SetServiceID(ServiceInfo.ServiceID);
				pChInfo->SetNetworkID(StreamID.NetworkID);
				pChInfo->SetTransportStreamID(StreamID.TransportStreamID);
				ChList.AddChannel(pChInfo);
				if (ServiceInfo.ServiceID==StreamID.ServiceID)
					CurService=i;
			}

			m_App.ChannelMenu.Create(&ChList,CurService,CM_SERVICE_FIRST,hmenu,m_hwnd,
									 CChannelMenu::FLAG_SHOWLOGO |
									 CChannelMenu::FLAG_SHOWEVENTINFO |
									 CChannelMenu::FLAG_CURSERVICES);
		}
	} else if (hmenu==m_App.MainMenu.GetSubMenu(CMainMenu::SUBMENU_AUDIO)) {
		CPopupMenu Menu(hmenu);
		Menu.Clear();

		CTsAnalyzer::EventAudioInfo AudioInfo;
		const bool fDualMono=m_App.CoreEngine.m_DtvEngine.GetEventAudioInfo(&AudioInfo)
								&& AudioInfo.ComponentType==0x02;
		if (fDualMono) {
			// Dual mono
			TCHAR szText[80],szAudio1[64],szAudio2[64];

			szAudio1[0]='\0';
			szAudio2[0]='\0';
			if (AudioInfo.szText[0]!='\0') {
				LPTSTR pszDelimiter=::StrChr(AudioInfo.szText,_T('\r'));
				if (pszDelimiter!=nullptr) {
					*pszDelimiter='\0';
					CopyToMenuText(AudioInfo.szText,szAudio1,lengthof(szAudio1));
					CopyToMenuText(pszDelimiter+1,szAudio2,lengthof(szAudio2));
				}
			}
			// ES multilingual flag �������Ă���̂ɗ������{��̏ꍇ������
			if (AudioInfo.bESMultiLingualFlag
					&& AudioInfo.LanguageCode!=AudioInfo.LanguageCode2) {
				// ��J����
				if (szAudio1[0]=='\0')
					EpgUtil::GetLanguageText(AudioInfo.LanguageCode,szAudio1,lengthof(szAudio1));
				if (szAudio2[0]=='\0')
					EpgUtil::GetLanguageText(AudioInfo.LanguageCode2,szAudio2,lengthof(szAudio2));
				StdUtil::snprintf(szText,lengthof(szText),TEXT("%s+%s(&S)"),szAudio1,szAudio2);
				Menu.Append(CM_STEREO_THROUGH,szText);
				::wsprintf(szText,TEXT("%s(&L)"),szAudio1);
				Menu.Append(CM_STEREO_LEFT,szText);
				::wsprintf(szText,TEXT("%s(&R)"),szAudio2);
				Menu.Append(CM_STEREO_RIGHT,szText);
			} else {
				Menu.Append(CM_STEREO_THROUGH,TEXT("��+������(&S)"));
				if (szAudio1[0]!='\0')
					StdUtil::snprintf(szText,lengthof(szText),TEXT("�剹��(%s)(&L)"),szAudio1);
				else
					::lstrcpy(szText,TEXT("�剹��(&L)"));
				Menu.Append(CM_STEREO_LEFT,szText);
				if (szAudio2[0]!='\0')
					StdUtil::snprintf(szText,lengthof(szText),TEXT("������(%s)(&R)"),szAudio2);
				else
					::lstrcpy(szText,TEXT("������(&R)"));
				Menu.Append(CM_STEREO_RIGHT,szText);
			}
			Menu.AppendSeparator();
		}

		const int NumAudioStreams=m_pCore->GetNumAudioStreams();
		if (NumAudioStreams>0) {
			for (int i=0;i<NumAudioStreams;i++) {
				TCHAR szText[64];
				int Length;

				Length=::wsprintf(szText,TEXT("&%d: ����%d"),i+1,i+1);
				if (NumAudioStreams>1
						&& m_App.CoreEngine.m_DtvEngine.GetEventAudioInfo(&AudioInfo,i)) {
					if (AudioInfo.szText[0]!='\0') {
						LPTSTR p=::StrChr(AudioInfo.szText,_T('\r'));
						if (p!=nullptr) {
							*p++=_T('/');
							if (*p==_T('\n'))
								::MoveMemory(p,p+1,::lstrlen(p)*sizeof(*p));
						}
						StdUtil::snprintf(szText+Length,lengthof(szText)-Length,
							TEXT(" (%s)"),AudioInfo.szText);
					} else {
						TCHAR szLang[EpgUtil::MAX_LANGUAGE_TEXT_LENGTH];
						EpgUtil::GetLanguageText(AudioInfo.LanguageCode,szLang,lengthof(szLang));
						StdUtil::snprintf(szText+Length,lengthof(szText)-Length,
							TEXT(" (%s)"),szLang);
					}
				}
				Menu.Append(CM_AUDIOSTREAM_FIRST+i,szText);
			}
			Menu.CheckRadioItem(CM_AUDIOSTREAM_FIRST,
								CM_AUDIOSTREAM_FIRST+NumAudioStreams-1,
								CM_AUDIOSTREAM_FIRST+m_pCore->GetAudioStream());
		}

		if (!fDualMono) {
			if (NumAudioStreams>0)
				Menu.AppendSeparator();
			Menu.Append(CM_STEREO_THROUGH,TEXT("�X�e���I/�X���[(&S)"));
			Menu.Append(CM_STEREO_LEFT,TEXT("��(�剹��)(&L)"));
			Menu.Append(CM_STEREO_RIGHT,TEXT("�E(������)(&R)"));
		}
		Menu.CheckRadioItem(CM_STEREO_THROUGH,CM_STEREO_RIGHT,
							CM_STEREO_THROUGH+m_pCore->GetStereoMode());

		Menu.AppendSeparator();
		Menu.Append(CM_SPDIF_DISABLED,TEXT("S/PDIF�p�X�X���[ : ����"));
		Menu.Append(CM_SPDIF_PASSTHROUGH,TEXT("S/PDIF�p�X�X���[ : �L��"));
		Menu.Append(CM_SPDIF_AUTO,TEXT("S/PDIF�p�X�X���[ : �����ؑ�"));
		CAudioDecFilter::SpdifOptions SpdifOptions;
		m_App.CoreEngine.GetSpdifOptions(&SpdifOptions);
		Menu.CheckRadioItem(CM_SPDIF_DISABLED,CM_SPDIF_AUTO,
							CM_SPDIF_DISABLED+(int)SpdifOptions.Mode);
	} else if (hmenu==m_App.MainMenu.GetSubMenu(CMainMenu::SUBMENU_FILTERPROPERTY)) {
		for (int i=0;i<lengthof(m_DirectShowFilterPropertyList);i++) {
			m_App.MainMenu.EnableItem(m_DirectShowFilterPropertyList[i].Command,
				m_App.CoreEngine.m_DtvEngine.m_MediaViewer.FilterHasProperty(
					m_DirectShowFilterPropertyList[i].Filter));
		}
	} else if (hmenu==m_App.MainMenu.GetSubMenu(CMainMenu::SUBMENU_BAR)) {
		m_App.MainMenu.CheckItem(CM_TITLEBAR,m_fShowTitleBar);
		m_App.MainMenu.CheckItem(CM_STATUSBAR,m_fShowStatusBar);
		m_App.MainMenu.CheckItem(CM_SIDEBAR,m_fShowSideBar);
		m_App.MainMenu.CheckRadioItem(CM_WINDOWFRAME_NORMAL,CM_WINDOWFRAME_NONE,
			!m_fCustomFrame?CM_WINDOWFRAME_NORMAL:
			(m_CustomFrameWidth==0?CM_WINDOWFRAME_NONE:CM_WINDOWFRAME_CUSTOM));
		m_App.MainMenu.CheckItem(CM_CUSTOMTITLEBAR,m_fCustomTitleBar);
		m_App.MainMenu.EnableItem(CM_CUSTOMTITLEBAR,!m_fCustomFrame);
		m_App.MainMenu.CheckItem(CM_SPLITTITLEBAR,m_fSplitTitleBar);
		m_App.MainMenu.EnableItem(CM_SPLITTITLEBAR,!m_fCustomFrame && m_fCustomTitleBar);
		m_App.MainMenu.CheckItem(CM_VIDEOEDGE,m_fViewWindowEdge);
	} else {
		if (m_App.ChannelMenuManager.InitPopup(m_App.MainMenu.GetSubMenu(CMainMenu::SUBMENU_SPACE),hmenu))
			return true;

		if (m_App.TunerSelectMenu.OnInitMenuPopup(hmenu))
			return true;

		return false;
	}

	return true;
}


void CMainWindow::OnTunerChanged()
{
	SetWheelChannelChanging(false);

	if (m_fProgramGuideUpdating)
		EndProgramGuideUpdate(0);

	m_App.Panel.ProgramListPanel.ClearProgramList();
	m_App.Panel.InfoPanel.ResetStatistics();
	bool fNoSignalLevel=m_App.DriverOptions.IsNoSignalLevel(m_App.CoreEngine.GetDriverFileName());
	m_App.Panel.InfoPanel.ShowSignalLevel(!fNoSignalLevel);
	CSignalLevelStatusItem *pItem=dynamic_cast<CSignalLevelStatusItem*>(
		m_App.StatusView.GetItemByID(STATUS_ITEM_SIGNALLEVEL));
	if (pItem!=nullptr)
		pItem->ShowSignalLevel(!fNoSignalLevel);
	/*
	if (m_App.Panel.fShowPanelWindow && m_App.Panel.Form.GetCurPageID()==PANEL_ID_CHANNEL) {
		m_App.Panel.ChannelPanel.SetChannelList(
			m_App.ChannelManager.GetCurrentChannelList(),
			!m_App.EpgOptions.IsEpgFileLoading());
	} else {
		m_App.Panel.ChannelPanel.ClearChannelList();
	}
	*/
	m_App.Panel.CaptionPanel.Clear();
	m_App.Epg.ProgramGuide.ClearCurrentService();
	ClearMenu(m_App.MainMenu.GetSubMenu(CMainMenu::SUBMENU_SERVICE));
	m_ResetErrorCountTimer.End();
	m_App.StatusView.UpdateItem(STATUS_ITEM_TUNER);
	m_App.Panel.ControlPanel.UpdateItem(CONTROLPANEL_ITEM_TUNER);
	if (m_App.SideBarOptions.GetShowChannelLogo())
		m_App.SideBar.Invalidate();
	m_fForceResetPanAndScan=true;
}


void CMainWindow::OnTunerOpened()
{
	if (m_fProgramGuideUpdating)
		EndProgramGuideUpdate(0);
}


void CMainWindow::OnTunerClosed()
{
}


void CMainWindow::OnChannelListChanged()
{
	if (m_App.Panel.fShowPanelWindow && m_App.Panel.Form.GetCurPageID()==PANEL_ID_CHANNEL) {
		m_App.Panel.ChannelPanel.SetChannelList(m_App.ChannelManager.GetCurrentChannelList());
		m_App.Panel.ChannelPanel.SetCurrentChannel(m_App.ChannelManager.GetCurrentChannel());
	} else {
		m_App.Panel.ChannelPanel.ClearChannelList();
	}
	if (m_App.SideBarOptions.GetShowChannelLogo())
		m_App.SideBar.Invalidate();
}


void CMainWindow::OnChannelChanged(unsigned int Status)
{
	const bool fSpaceChanged=(Status & CUICore::CHANNEL_CHANGED_STATUS_SPACE_CHANGED)!=0;
	const int CurSpace=m_App.ChannelManager.GetCurrentSpace();
	const CChannelInfo *pCurChannel=m_App.ChannelManager.GetCurrentChannelInfo();

	SetWheelChannelChanging(false);

	if (m_fProgramGuideUpdating && !m_fEpgUpdateChannelChange
			&& (Status & CUICore::CHANNEL_CHANGED_STATUS_DETECTED)==0)
		EndProgramGuideUpdate(0);

	if (CurSpace>CChannelManager::SPACE_INVALID)
		m_App.MainMenu.CheckRadioItem(CM_SPACE_ALL,CM_SPACE_ALL+m_App.ChannelManager.NumSpaces(),
									  CM_SPACE_FIRST+CurSpace);
	ClearMenu(m_App.MainMenu.GetSubMenu(CMainMenu::SUBMENU_SERVICE));
	m_App.StatusView.UpdateItem(STATUS_ITEM_CHANNEL);
	m_App.StatusView.UpdateItem(STATUS_ITEM_TUNER);
	m_App.Panel.ControlPanel.UpdateItem(CONTROLPANEL_ITEM_CHANNEL);
	m_App.Panel.ControlPanel.UpdateItem(CONTROLPANEL_ITEM_TUNER);
	if (pCurChannel!=nullptr && m_App.OSDOptions.IsOSDEnabled(COSDOptions::OSD_CHANNEL))
		ShowChannelOSD();
	m_App.Panel.ProgramListPanel.ClearProgramList();
	::SetTimer(m_hwnd,TIMER_ID_PROGRAMLISTUPDATE,10000,nullptr);
	m_ProgramListUpdateTimerCount=0;
	m_App.Panel.InfoPanel.ResetStatistics();
	m_App.Panel.ProgramListPanel.ShowRetrievingMessage(true);
	if (fSpaceChanged) {
		if (m_App.Panel.fShowPanelWindow && m_App.Panel.Form.GetCurPageID()==PANEL_ID_CHANNEL) {
			m_App.Panel.ChannelPanel.SetChannelList(
				m_App.ChannelManager.GetCurrentChannelList(),
				!m_App.EpgOptions.IsEpgFileLoading());
			m_App.Panel.ChannelPanel.SetCurrentChannel(
				m_App.ChannelManager.GetCurrentChannel());
		} else {
			m_App.Panel.ChannelPanel.ClearChannelList();
		}
	} else {
		if (m_App.Panel.fShowPanelWindow && m_App.Panel.Form.GetCurPageID()==PANEL_ID_CHANNEL)
			m_App.Panel.ChannelPanel.SetCurrentChannel(m_App.ChannelManager.GetCurrentChannel());
	}
	if (pCurChannel!=nullptr) {
		m_App.Epg.ProgramGuide.SetCurrentService(
			pCurChannel->GetNetworkID(),
			pCurChannel->GetTransportStreamID(),
			pCurChannel->GetServiceID());
	} else {
		m_App.Epg.ProgramGuide.ClearCurrentService();
	}
	int ChannelNo;
	if (pCurChannel!=nullptr)
		ChannelNo=pCurChannel->GetChannelNo();
	if (fSpaceChanged && m_App.SideBarOptions.GetShowChannelLogo())
		m_App.SideBar.Invalidate();
	m_App.SideBar.CheckRadioItem(CM_CHANNELNO_1,CM_CHANNELNO_12,
								 pCurChannel!=nullptr && ChannelNo>=1 && ChannelNo<=12?
								 CM_CHANNELNO_1+ChannelNo-1:0);
	m_App.Panel.CaptionPanel.Clear();
	UpdateControlPanel();

	LPCTSTR pszDriverFileName=m_App.CoreEngine.GetDriverFileName();
	pCurChannel=m_App.ChannelManager.GetCurrentRealChannelInfo();
	if (pCurChannel!=nullptr) {
		m_App.RecentChannelList.Add(pszDriverFileName,pCurChannel);
		m_App.ChannelHistory.SetCurrentChannel(pszDriverFileName,pCurChannel);
	}
	if (m_App.DriverOptions.IsResetChannelChangeErrorCount(pszDriverFileName))
		m_ResetErrorCountTimer.Begin(m_hwnd,5000);
	else
		m_ResetErrorCountTimer.End();
	/*
	m_pCore->SetStereoMode(0);
	m_CurEventStereoMode=-1;
	*/
	m_fForceResetPanAndScan=true;
}


void CMainWindow::LockLayout()
{
	if (!::IsIconic(m_hwnd) && !::IsZoomed(m_hwnd)) {
		m_fLockLayout=true;
		m_LayoutBase.LockLayout();
	}
}


void CMainWindow::UpdateLayout()
{
	if (m_fLockLayout) {
		m_fLockLayout=false;

		SIZE sz;
		GetClientSize(&sz);
		OnSizeChanged(SIZE_RESTORED,sz.cx,sz.cy);
		m_LayoutBase.UnlockLayout();
	}
}


void CMainWindow::ShowCursor(bool fShow)
{
	m_App.CoreEngine.m_DtvEngine.m_MediaViewer.HideCursor(!fShow);
	m_Viewer.GetViewWindow().ShowCursor(fShow);
	m_fShowCursor=fShow;
}


void CMainWindow::ShowChannelOSD()
{
	if (GetVisible() && !::IsIconic(m_hwnd)) {
		const CChannelInfo *pInfo;

		if (m_fWheelChannelChanging)
			pInfo=m_App.ChannelManager.GetChangingChannelInfo();
		else
			pInfo=m_App.ChannelManager.GetCurrentChannelInfo();
		if (pInfo!=nullptr)
			m_App.OSDManager.ShowChannelOSD(pInfo,m_fWheelChannelChanging);
	}
}


void CMainWindow::OnServiceChanged()
{
	int CurService=0;
	WORD ServiceID;
	if (m_App.CoreEngine.m_DtvEngine.GetServiceID(&ServiceID))
		CurService=m_App.CoreEngine.m_DtvEngine.m_TsAnalyzer.GetViewableServiceIndexByID(ServiceID);
	m_App.MainMenu.CheckRadioItem(CM_SERVICE_FIRST,
		CM_SERVICE_FIRST+m_App.CoreEngine.m_DtvEngine.m_TsAnalyzer.GetViewableServiceNum()-1,
		CM_SERVICE_FIRST+CurService);

	m_App.StatusView.UpdateItem(STATUS_ITEM_CHANNEL);

	if (m_App.Panel.Form.GetCurPageID()==PANEL_ID_INFORMATION)
		UpdateProgramInfo();
}


void CMainWindow::OnRecordingStarted()
{
	EndProgramGuideUpdate(0);

	m_App.StatusView.UpdateItem(STATUS_ITEM_RECORD);
	m_App.StatusView.UpdateItem(STATUS_ITEM_ERROR);
	//m_App.MainMenu.EnableItem(CM_RECORDOPTION,false);
	//m_App.MainMenu.EnableItem(CM_RECORDSTOPTIME,true);
	m_App.TaskbarManager.SetRecordingStatus(true);

	m_ResetErrorCountTimer.End();
	m_fAlertedLowFreeSpace=false;
	if (m_App.OSDOptions.IsOSDEnabled(COSDOptions::OSD_RECORDING))
		m_App.OSDManager.ShowOSD(TEXT("���^��"));
}


void CMainWindow::OnRecordingStopped()
{
	m_App.StatusView.UpdateItem(STATUS_ITEM_RECORD);
	m_App.Panel.InfoPanel.SetRecordStatus(false);
	//m_App.MainMenu.EnableItem(CM_RECORDOPTION,true);
	//m_App.MainMenu.EnableItem(CM_RECORDSTOPTIME,false);
	m_App.TaskbarManager.SetRecordingStatus(false);
	m_App.RecordManager.SetStopOnEventEnd(false);
	if (m_App.OSDOptions.IsOSDEnabled(COSDOptions::OSD_RECORDING))
		m_App.OSDManager.ShowOSD(TEXT("���^���~"));
	if (m_pCore->GetStandby())
		m_App.Core.CloseTuner();
}


void CMainWindow::On1SegModeChanged(bool f1SegMode)
{
	m_App.MainMenu.CheckItem(CM_1SEGMODE,f1SegMode);
	m_App.SideBar.CheckItem(CM_1SEGMODE,f1SegMode);
}


void CMainWindow::OnMouseWheel(WPARAM wParam,LPARAM lParam,bool fHorz)
{
	POINT pt;
	pt.x=GET_X_LPARAM(lParam);
	pt.y=GET_Y_LPARAM(lParam);

	if (m_Viewer.GetDisplayBase().IsVisible()) {
		CDisplayView *pDisplayView=m_Viewer.GetDisplayBase().GetDisplayView();

		if (pDisplayView!=nullptr) {
			RECT rc;

			m_Viewer.GetDisplayBase().GetParent()->GetScreenPosition(&rc);
			if (::PtInRect(&rc,pt)) {
				if (pDisplayView->OnMouseWheel(fHorz?WM_MOUSEHWHEEL:WM_MOUSEWHEEL,wParam,lParam))
					return;
			}
		}
	}

	COperationOptions::WheelMode Mode;

	if (fHorz) {
		Mode=m_App.OperationOptions.GetWheelTiltMode();
	} else {
		if ((wParam&MK_SHIFT)!=0)
			Mode=m_App.OperationOptions.GetWheelShiftMode();
		else if ((wParam&MK_CONTROL)!=0)
			Mode=m_App.OperationOptions.GetWheelCtrlMode();
		else
			Mode=m_App.OperationOptions.GetWheelMode();
	}

	if (m_App.OperationOptions.IsStatusBarWheelEnabled() && m_App.StatusView.GetVisible()) {
		RECT rc;

		m_App.StatusView.GetScreenPosition(&rc);
		if (::PtInRect(&rc,pt)) {
			switch (m_App.StatusView.GetCurItem()) {
			case STATUS_ITEM_CHANNEL:
				Mode=COperationOptions::WHEEL_MODE_CHANNEL;
				break;
#if 0
			// �{�����ς��ƃE�B���h�E�T�C�Y���ς��̂Ŏg���Â炢
			case STATUS_ITEM_VIDEOSIZE:
				Mode=COperationOptions::WHEEL_MODE_ZOOM;
				break;
#endif
			case STATUS_ITEM_VOLUME:
				Mode=COperationOptions::WHEEL_MODE_VOLUME;
				break;
			case STATUS_ITEM_AUDIOCHANNEL:
				Mode=COperationOptions::WHEEL_MODE_AUDIO;
				break;
			}
		}
	}

	int Delta=m_WheelHandler.OnMouseWheel(wParam,1);
	if (Delta==0)
		return;
	if (m_App.OperationOptions.IsWheelModeReverse(Mode))
		Delta=-Delta;
	const DWORD CurTime=::GetTickCount();
	bool fProcessed=false;

	if (Mode!=m_PrevWheelMode)
		m_WheelCount=0;
	else
		m_WheelCount++;

	switch (Mode) {
	case COperationOptions::WHEEL_MODE_VOLUME:
		SendCommand(Delta>0?CM_VOLUME_UP:CM_VOLUME_DOWN);
		fProcessed=true;
		break;

	case COperationOptions::WHEEL_MODE_CHANNEL:
		{
			bool fUp;

			if (fHorz)
				fUp=Delta>0;
			else
				fUp=Delta<0;
			int Channel=m_App.ChannelManager.GetNextChannel(fUp);
			if (Channel>=0) {
				if (m_fWheelChannelChanging
						&& m_WheelCount<5
						&& TickTimeSpan(m_PrevWheelTime,CurTime)<(5UL-m_WheelCount)*100UL) {
					break;
				}
				SetWheelChannelChanging(true,m_App.OperationOptions.GetWheelChannelDelay());
				m_App.ChannelManager.SetChangingChannel(Channel);
				m_App.StatusView.UpdateItem(STATUS_ITEM_CHANNEL);
				if (m_App.OSDOptions.IsOSDEnabled(COSDOptions::OSD_CHANNEL))
					ShowChannelOSD();
			}
			fProcessed=true;
		}
		break;

	case COperationOptions::WHEEL_MODE_AUDIO:
		if (Mode!=m_PrevWheelMode || TickTimeSpan(m_PrevWheelTime,CurTime)>=300) {
			SendCommand(CM_SWITCHAUDIO);
			fProcessed=true;
		}
		break;

	case COperationOptions::WHEEL_MODE_ZOOM:
		if (Mode!=m_PrevWheelMode || TickTimeSpan(m_PrevWheelTime,CurTime)>=500) {
			if (!IsZoomed(m_hwnd) && !m_pCore->GetFullscreen()) {
				int Zoom;

				Zoom=GetZoomPercentage();
				if (Delta>0)
					Zoom+=m_App.OperationOptions.GetWheelZoomStep();
				else
					Zoom-=m_App.OperationOptions.GetWheelZoomStep();
				SetZoomRate(Zoom,100);
			}
			fProcessed=true;
		}
		break;

	case COperationOptions::WHEEL_MODE_ASPECTRATIO:
		if (Mode!=m_PrevWheelMode || TickTimeSpan(m_PrevWheelTime,CurTime)>=300) {
			SendCommand(CM_ASPECTRATIO);
			fProcessed=true;
		}
		break;
	}

	m_PrevWheelMode=Mode;
	if (fProcessed)
		m_PrevWheelTime=CurTime;
}


bool CMainWindow::EnableViewer(bool fEnable)
{
	CMediaViewer &MediaViewer=m_App.CoreEngine.m_DtvEngine.m_MediaViewer;

	if (fEnable) {
		bool fInit;

		if (!MediaViewer.IsOpen()) {
			fInit=true;
		} else {
			BYTE VideoStreamType=m_App.CoreEngine.m_DtvEngine.GetVideoStreamType();
			fInit=
				VideoStreamType!=STREAM_TYPE_UNINITIALIZED &&
				VideoStreamType!=MediaViewer.GetVideoStreamType();
		}

		if (fInit) {
			if (!InitializeViewer())
				return false;
		}
	}

	if (MediaViewer.IsOpen()) {
		if (!m_Viewer.EnableViewer(fEnable))
			return false;

		m_pCore->PreventDisplaySave(fEnable);
	}

	m_fEnablePlayback=fEnable;

	m_App.MainMenu.CheckItem(CM_DISABLEVIEWER,!fEnable);
	m_App.SideBar.CheckItem(CM_DISABLEVIEWER,!fEnable);

	return true;
}


bool CMainWindow::IsViewerEnabled() const
{
	return m_Viewer.IsViewerEnabled();
}


HWND CMainWindow::GetViewerWindow() const
{
	return m_Viewer.GetVideoContainer().GetHandle();
}


void CMainWindow::OnVolumeChanged(bool fOSD)
{
	const int Volume=m_pCore->GetVolume();

	m_App.StatusView.UpdateItem(STATUS_ITEM_VOLUME);
	m_App.Panel.ControlPanel.UpdateItem(CONTROLPANEL_ITEM_VOLUME);
	m_App.MainMenu.CheckItem(CM_VOLUME_MUTE,false);
	if (fOSD && m_App.OSDOptions.IsOSDEnabled(COSDOptions::OSD_VOLUME)
			&& GetVisible() && !::IsIconic(m_hwnd))
		m_App.OSDManager.ShowVolumeOSD(Volume);
}


void CMainWindow::OnMuteChanged()
{
	const bool fMute=m_pCore->GetMute();

	m_App.StatusView.UpdateItem(STATUS_ITEM_VOLUME);
	m_App.Panel.ControlPanel.UpdateItem(CONTROLPANEL_ITEM_VOLUME);
	m_App.MainMenu.CheckItem(CM_VOLUME_MUTE,fMute);
}


void CMainWindow::OnStereoModeChanged()
{
	const int StereoMode=m_pCore->GetStereoMode();

	m_CurEventStereoMode=StereoMode;
	/*
	m_App.MainMenu.CheckRadioItem(CM_STEREO_THROUGH,CM_STEREO_RIGHT,
								  CM_STEREO_THROUGH+StereoMode);
	*/
	m_App.StatusView.UpdateItem(STATUS_ITEM_AUDIOCHANNEL);
	m_App.Panel.ControlPanel.UpdateItem(CONTROLPANEL_ITEM_AUDIO);
}


void CMainWindow::OnAudioStreamChanged()
{
	const int Stream=m_pCore->GetAudioStream();

	m_App.MainMenu.CheckRadioItem(CM_AUDIOSTREAM_FIRST,
								  CM_AUDIOSTREAM_FIRST+m_pCore->GetNumAudioStreams()-1,
								  CM_AUDIOSTREAM_FIRST+Stream);
	m_App.StatusView.UpdateItem(STATUS_ITEM_AUDIOCHANNEL);
	m_App.Panel.ControlPanel.UpdateItem(CONTROLPANEL_ITEM_AUDIO);
}


void CMainWindow::AutoSelectStereoMode()
{
	/*
		Dual Mono ���ɉ����������őI������
		��̔ԑg�Ŗ{��Dual Mono/CM�X�e���I�̂悤�ȏꍇ
		  A�p�[�g -> CM       -> B�p�[�g
		  ������  -> �X�e���I -> ������
		�̂悤�ɁA���[�U�[�̑I�����L�����Ă����K�v������
	*/
	const bool fDualMono=m_App.CoreEngine.m_DtvEngine.GetAudioChannelNum()==
										CMediaViewer::AUDIO_CHANNEL_DUALMONO
					/*|| m_App.CoreEngine.m_DtvEngine.GetAudioComponentType()==0x02*/;

	if (m_CurEventStereoMode<0) {
		m_pCore->SetStereoMode(fDualMono?m_App.CoreEngine.GetAutoStereoMode():CCoreEngine::STEREOMODE_STEREO);
		m_CurEventStereoMode=-1;
	} else {
		int OldStereoMode=m_CurEventStereoMode;
		m_pCore->SetStereoMode(fDualMono?m_CurEventStereoMode:CCoreEngine::STEREOMODE_STEREO);
		m_CurEventStereoMode=OldStereoMode;
	}
}


void CMainWindow::ShowAudioOSD()
{
	if (m_App.OSDOptions.IsOSDEnabled(COSDOptions::OSD_AUDIO)) {
		CTsAnalyzer::EventAudioInfo AudioInfo;
		TCHAR szText[128];

		if (m_App.CoreEngine.m_DtvEngine.GetEventAudioInfo(&AudioInfo)) {
			if (AudioInfo.ComponentType==0x02) {
				// Dual mono
				TCHAR szAudio1[64],szAudio2[64];

				szAudio1[0]=_T('\0');
				szAudio2[0]=_T('\0');
				if (AudioInfo.szText[0]!=_T('\0')) {
					LPTSTR pszDelimiter=::StrChr(AudioInfo.szText,_T('\r'));
					if (pszDelimiter!=nullptr) {
						*pszDelimiter=_T('\0');
						if (*(pszDelimiter+1)==_T('\n'))
							pszDelimiter++;
						::lstrcpyn(szAudio1,AudioInfo.szText,lengthof(szAudio1));
						::lstrcpyn(szAudio2,pszDelimiter+1,lengthof(szAudio2));
					}
				}
				if (AudioInfo.bESMultiLingualFlag
						&& AudioInfo.LanguageCode!=AudioInfo.LanguageCode2) {
					// ��J����
					if (szAudio1[0]==_T('\0'))
						EpgUtil::GetLanguageText(AudioInfo.LanguageCode,szAudio1,lengthof(szAudio1));
					if (szAudio2[0]==_T('\0'))
						EpgUtil::GetLanguageText(AudioInfo.LanguageCode2,szAudio2,lengthof(szAudio2));
				} else {
					if (szAudio1[0]==_T('\0'))
						::lstrcpy(szAudio1,TEXT("�剹��"));
					if (szAudio2[0]==_T('\0'))
						::lstrcpy(szAudio2,TEXT("������"));
				}
				switch (m_pCore->GetStereoMode()) {
				case 0:
					StdUtil::snprintf(szText,lengthof(szText),TEXT("%s+%s"),szAudio1,szAudio2);
					break;
				case 1:
					::lstrcpyn(szText,szAudio1,lengthof(szText));
					break;
				case 2:
					::lstrcpyn(szText,szAudio2,lengthof(szText));
					break;
				default:
					return;
				}
			} else {
				if (AudioInfo.szText[0]==_T('\0')) {
					EpgUtil::GetLanguageText(AudioInfo.LanguageCode,
											 AudioInfo.szText,lengthof(AudioInfo.szText));
				}
				StdUtil::snprintf(szText,lengthof(szText),TEXT("����%d (%s)"),
					m_pCore->GetAudioStream()+1,AudioInfo.szText);
			}
		} else {
			StdUtil::snprintf(szText,lengthof(szText),TEXT("����%d"),m_pCore->GetAudioStream()+1);
		}

		m_App.OSDManager.ShowOSD(szText);
	}
}


bool CMainWindow::SetZoomRate(int Rate,int Factor)
{
	if (Rate<1 || Factor<1)
		return false;

	int Width,Height;

	if (m_App.CoreEngine.GetVideoViewSize(&Width,&Height) && Width>0 && Height>0) {
		int ZoomWidth,ZoomHeight;

		ZoomWidth=CalcZoomSize(Width,Rate,Factor);
		ZoomHeight=CalcZoomSize(Height,Rate,Factor);

		if (m_App.ViewOptions.GetZoomKeepAspectRatio()) {
			SIZE ScreenSize;

			m_Viewer.GetVideoContainer().GetClientSize(&ScreenSize);
			if (ScreenSize.cx>0 && ScreenSize.cy>0) {
				if ((double)ZoomWidth/(double)ScreenSize.cx<=(double)ZoomHeight/(double)ScreenSize.cy) {
					ZoomWidth=CalcZoomSize(ScreenSize.cx,ZoomHeight,ScreenSize.cy);
				} else {
					ZoomHeight=CalcZoomSize(ScreenSize.cy,ZoomWidth,ScreenSize.cx);
				}
			}
		}

		AdjustWindowSize(ZoomWidth,ZoomHeight);
	}

	return true;
}


bool CMainWindow::GetZoomRate(int *pRate,int *pFactor)
{
	bool fOK=false;
	int Width,Height;
	int Rate=0,Factor=1;

	if (m_App.CoreEngine.GetVideoViewSize(&Width,&Height) && Width>0 && Height>0) {
		/*
		SIZE sz;

		m_Viewer.GetVideoContainer().GetClientSize(&sz);
		Rate=sz.cy;
		Factor=Height;
		*/
		WORD DstWidth,DstHeight;
		if (m_App.CoreEngine.m_DtvEngine.m_MediaViewer.GetDestSize(&DstWidth,&DstHeight)) {
			Rate=DstHeight;
			Factor=Height;
		}
		fOK=true;
	}
	if (pRate)
		*pRate=Rate;
	if (pFactor)
		*pFactor=Factor;
	return fOK;
}


int CMainWindow::GetZoomPercentage()
{
	int Rate,Factor;

	if (!GetZoomRate(&Rate,&Factor) || Factor==0)
		return 0;
	return (Rate*100+Factor/2)/Factor;
}


bool CMainWindow::AutoFitWindowToVideo()
{
	int Width,Height;
	SIZE sz;

	if (!m_App.CoreEngine.GetVideoViewSize(&Width,&Height)
			|| Width<=0 || Height<=0)
		return false;
	m_Viewer.GetVideoContainer().GetClientSize(&sz);
	Width=CalcZoomSize(Width,sz.cy,Height);
	if (sz.cx<Width)
		AdjustWindowSize(Width,sz.cy);

	return true;
}


bool CMainWindow::SetPanAndScan(const PanAndScanInfo &Info)
{
	CMediaViewer::ClippingInfo Clipping;

	Clipping.Left=Info.XPos;
	Clipping.Right=Info.XFactor-(Info.XPos+Info.Width);
	Clipping.HorzFactor=Info.XFactor;
	Clipping.Top=Info.YPos;
	Clipping.Bottom=Info.YFactor-(Info.YPos+Info.Height);
	Clipping.VertFactor=Info.YFactor;

	m_App.CoreEngine.m_DtvEngine.m_MediaViewer.SetPanAndScan(Info.XAspect,Info.YAspect,&Clipping);

	if (!m_pCore->GetFullscreen()) {
		switch (m_App.ViewOptions.GetPanScanAdjustWindowMode()) {
		case CViewOptions::ADJUSTWINDOW_FIT:
			{
				int ZoomRate,ZoomFactor;
				int Width,Height;

				if (GetZoomRate(&ZoomRate,&ZoomFactor)
						&& m_App.CoreEngine.GetVideoViewSize(&Width,&Height)) {
					AdjustWindowSize(CalcZoomSize(Width,ZoomRate,ZoomFactor),
									 CalcZoomSize(Height,ZoomRate,ZoomFactor));
				} else {
					WORD DstWidth,DstHeight;

					if (m_App.CoreEngine.m_DtvEngine.m_MediaViewer.GetDestSize(&DstWidth,&DstHeight))
						AdjustWindowSize(DstWidth,DstHeight);
				}
			}
			break;

		case CViewOptions::ADJUSTWINDOW_WIDTH:
			{
				SIZE sz;
				int Width,Height;

				m_Viewer.GetVideoContainer().GetClientSize(&sz);
				if (m_App.CoreEngine.GetVideoViewSize(&Width,&Height))
					AdjustWindowSize(CalcZoomSize(Width,sz.cy,Height),sz.cy);
			}
			break;
		}
	}

	m_App.StatusView.UpdateItem(STATUS_ITEM_VIDEOSIZE);
	m_App.Panel.ControlPanel.UpdateItem(CONTROLPANEL_ITEM_VIDEO);

	return true;
}


bool CMainWindow::GetPanAndScan(PanAndScanInfo *pInfo) const
{
	if (pInfo==nullptr)
		return false;

	const CMediaViewer &MediaViewer=m_App.CoreEngine.m_DtvEngine.m_MediaViewer;
	CMediaViewer::ClippingInfo Clipping;

	MediaViewer.GetForceAspectRatio(&pInfo->XAspect,&pInfo->YAspect);
	MediaViewer.GetClippingInfo(&Clipping);

	pInfo->XPos=Clipping.Left;
	pInfo->YPos=Clipping.Top;
	pInfo->Width=Clipping.HorzFactor-(Clipping.Left+Clipping.Right);
	pInfo->Height=Clipping.VertFactor-(Clipping.Top+Clipping.Bottom);
	pInfo->XFactor=Clipping.HorzFactor;
	pInfo->YFactor=Clipping.VertFactor;

	return true;
}


bool CMainWindow::SetPanAndScan(int Command)
{
	PanAndScanInfo Info;
	int Type;

	if (Command>=CM_ASPECTRATIO_FIRST && Command<=CM_ASPECTRATIO_3D_LAST) {
		static const CUICore::PanAndScanInfo PanAndScanList[] = {
			{0, 0,  0,  0,  0,  0,  0,  0},	// �f�t�H���g
			{0, 0,  1,  1,  1,  1, 16,  9},	// 16:9
			{0, 3,  1, 18,  1, 24, 16,  9},	// 16:9 ���^�[�{�b�N�X
			{2, 3, 12, 18, 16, 24, 16,  9},	// 16:9 ���z��
			{2, 0, 12,  1, 16,  1,  4,  3},	// 4:3 �T�C�h�J�b�g
			{0, 0,  1,  1,  1,  1,  4,  3},	// 4:3
			{0, 0,  1,  1,  1,  1, 32,  9},	// 32:9
			{0, 0,  1,  1,  2,  1, 16,  9},	// 16:9 ��
			{1, 0,  1,  1,  2,  1, 16,  9},	// 16:9 �E
		};

		Type=Command-CM_ASPECTRATIO_FIRST;
		Info=PanAndScanList[Type];
	} else if (Command>=CM_PANANDSCAN_PRESET_FIRST && Command<=CM_PANANDSCAN_PRESET_LAST) {
		CPanAndScanOptions::PanAndScanInfo PanScan;
		if (!m_App.PanAndScanOptions.GetPreset(Command-CM_PANANDSCAN_PRESET_FIRST,&PanScan))
			return false;
		Info=PanScan.Info;
		Type=ASPECTRATIO_CUSTOM+(Command-CM_PANANDSCAN_PRESET_FIRST);
	} else {
		return false;
	}

	SetPanAndScan(Info);

	m_AspectRatioType=Type;
	m_AspectRatioResetTime=0;

	m_App.AspectRatioIconMenu.CheckRadioItem(
		CM_ASPECTRATIO_FIRST,CM_ASPECTRATIO_3D_LAST,
		m_AspectRatioType<ASPECTRATIO_CUSTOM?CM_ASPECTRATIO_FIRST+m_AspectRatioType:0);
	m_App.SideBar.CheckRadioItem(CM_ASPECTRATIO_FIRST,CM_ASPECTRATIO_LAST,
		m_AspectRatioType<ASPECTRATIO_CUSTOM?CM_ASPECTRATIO_FIRST+m_AspectRatioType:0);

	return true;
}


bool CMainWindow::SetLogo(HBITMAP hbm)
{
	return m_Viewer.GetViewWindow().SetLogo(hbm);
}


bool CMainWindow::ShowProgramGuide(bool fShow,unsigned int Flags,const ProgramGuideSpaceInfo *pSpaceInfo)
{
	if (m_App.Epg.fShowProgramGuide==fShow)
		return true;

	if (fShow) {
		const bool fOnScreen=
			(Flags & PROGRAMGUIDE_SHOW_POPUP)==0
			&& ((Flags & PROGRAMGUIDE_SHOW_ONSCREEN)!=0
				|| m_App.ProgramGuideOptions.GetOnScreen()
				|| (m_pCore->GetFullscreen() && ::GetSystemMetrics(SM_CMONITORS)==1));

		Util::CWaitCursor WaitCursor;

		if (fOnScreen) {
			m_App.Epg.ProgramGuideDisplay.Create(m_Viewer.GetDisplayBase().GetParent()->GetHandle(),
				WS_CHILD | WS_CLIPCHILDREN);
			m_Viewer.GetDisplayBase().SetDisplayView(&m_App.Epg.ProgramGuideDisplay);
			if (m_fCustomFrame)
				HookWindows(m_App.Epg.ProgramGuideDisplay.GetHandle());
		} else {
			m_App.Epg.ProgramGuideFrame.Create(nullptr,
				WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
					WS_THICKFRAME | WS_CLIPCHILDREN);
		}

		SYSTEMTIME stFirst,stLast;
		m_App.ProgramGuideOptions.GetTimeRange(&stFirst,&stLast);
		m_App.Epg.ProgramGuide.SetTimeRange(&stFirst,&stLast);
		m_App.Epg.ProgramGuide.SetViewDay(CProgramGuide::DAY_TODAY);

		if (fOnScreen) {
			m_Viewer.GetDisplayBase().SetVisible(true);
		} else {
			m_App.Epg.ProgramGuideFrame.Show();
			m_App.Epg.ProgramGuideFrame.Update();
		}

		if (m_App.EpgOptions.IsEpgFileLoading()
				|| m_App.EpgOptions.IsEDCBDataLoading()) {
			m_App.Epg.ProgramGuide.SetMessage(TEXT("EPG�t�@�C���̓ǂݍ��ݒ�..."));
			m_App.EpgOptions.WaitEpgFileLoad();
			m_App.EpgOptions.WaitEDCBDataLoad();
			m_App.Epg.ProgramGuide.SetMessage(nullptr);
		}

		CEpg::CChannelProviderManager *pChannelProviderManager=
			m_App.Epg.CreateChannelProviderManager(pSpaceInfo!=nullptr?pSpaceInfo->pszTuner:nullptr);
		int Provider=pChannelProviderManager->GetCurChannelProvider();
		int Space;
		if (Provider>=0) {
			CProgramGuideChannelProvider *pChannelProvider=
				pChannelProviderManager->GetChannelProvider(Provider);
			bool fGroupID=false;
			if (pSpaceInfo!=nullptr && pSpaceInfo->pszSpace!=nullptr) {
				if (StringIsDigit(pSpaceInfo->pszSpace)) {
					Space=::StrToInt(pSpaceInfo->pszSpace);
				} else {
					Space=pChannelProvider->ParseGroupID(pSpaceInfo->pszSpace);
					fGroupID=true;
				}
			} else {
				Space=m_App.ChannelManager.GetCurrentSpace();
			}
			if (Space<0) {
				Space=0;
			} else if (!fGroupID) {
				CProgramGuideBaseChannelProvider *pBaseChannelProvider=
					dynamic_cast<CProgramGuideBaseChannelProvider*>(pChannelProvider);
				if (pBaseChannelProvider!=nullptr) {
					if (pBaseChannelProvider->HasAllChannelGroup())
						Space++;
					if ((size_t)Space>=pBaseChannelProvider->GetGroupCount())
						Space=0;
				}
			}
		} else {
			Space=-1;
		}
		m_App.Epg.ProgramGuide.SetCurrentChannelProvider(Provider,Space);
		m_App.Epg.ProgramGuide.UpdateProgramGuide(true);
		if (m_App.ProgramGuideOptions.ScrollToCurChannel())
			m_App.Epg.ProgramGuide.ScrollToCurrentService();
	} else {
		if (m_App.Epg.ProgramGuideFrame.IsCreated()) {
			m_App.Epg.ProgramGuideFrame.Destroy();
		} else {
			m_Viewer.GetDisplayBase().SetVisible(false);
		}
	}

	m_App.Epg.fShowProgramGuide=fShow;

	m_App.MainMenu.CheckItem(CM_PROGRAMGUIDE,m_App.Epg.fShowProgramGuide);
	m_App.SideBar.CheckItem(CM_PROGRAMGUIDE,m_App.Epg.fShowProgramGuide);

	return true;
}


LRESULT CALLBACK CMainWindow::WndProc(HWND hwnd,UINT uMsg,WPARAM wParam,LPARAM lParam)
{
	CMainWindow *pThis;

	if (uMsg==WM_NCCREATE) {
		pThis=static_cast<CMainWindow*>(CBasicWindow::OnCreate(hwnd,lParam));
	} else {
		pThis=static_cast<CMainWindow*>(GetBasicWindow(hwnd));
	}

	if (pThis==nullptr) {
		return ::DefWindowProc(hwnd,uMsg,wParam,lParam);
	}

	if (uMsg==WM_CREATE) {
		if (!pThis->OnCreate(reinterpret_cast<LPCREATESTRUCT>(lParam)))
			return -1;
		return 0;
	}

	LRESULT Result=0;
	if (pThis->m_App.PluginManager.OnMessage(hwnd,uMsg,wParam,lParam,&Result))
		return Result;

	if (uMsg==WM_DESTROY) {
		pThis->OnMessage(hwnd,uMsg,wParam,lParam);
		::PostQuitMessage(0);
		return 0;
	}

	return pThis->OnMessage(hwnd,uMsg,wParam,lParam);
}


void CMainWindow::HookWindows(HWND hwnd)
{
	if (hwnd!=nullptr) {
		HookChildWindow(hwnd);
		for (hwnd=::GetWindow(hwnd,GW_CHILD);hwnd!=nullptr;hwnd=::GetWindow(hwnd,GW_HWNDNEXT))
			HookWindows(hwnd);
	}
}


#define CHILD_PROP_THIS	APP_NAME TEXT("ChildThis")

void CMainWindow::HookChildWindow(HWND hwnd)
{
	if (hwnd==nullptr)
		return;

	if (m_atomChildOldWndProcProp==0) {
		m_atomChildOldWndProcProp=::GlobalAddAtom(APP_NAME TEXT("ChildOldWndProc"));
		if (m_atomChildOldWndProcProp==0)
			return;
	}

	if (::GetProp(hwnd,MAKEINTATOM(m_atomChildOldWndProcProp))==nullptr) {
#ifdef _DEBUG
		TCHAR szClass[256];
		::GetClassName(hwnd,szClass,lengthof(szClass));
		TRACE(TEXT("Hook window %p \"%s\"\n"),hwnd,szClass);
#endif
		WNDPROC pOldWndProc=SubclassWindow(hwnd,ChildHookProc);
		::SetProp(hwnd,MAKEINTATOM(m_atomChildOldWndProcProp),pOldWndProc);
		::SetProp(hwnd,CHILD_PROP_THIS,this);
	}
}


LRESULT CALLBACK CMainWindow::ChildHookProc(HWND hwnd,UINT uMsg,WPARAM wParam,LPARAM lParam)
{
	WNDPROC pOldWndProc=static_cast<WNDPROC>(::GetProp(hwnd,MAKEINTATOM(m_atomChildOldWndProcProp)));

	if (pOldWndProc==nullptr)
		return ::DefWindowProc(hwnd,uMsg,wParam,lParam);

	switch (uMsg) {
	case WM_NCHITTEST:
		{
			CMainWindow *pThis=static_cast<CMainWindow*>(::GetProp(hwnd,CHILD_PROP_THIS));

			if (pThis!=nullptr && pThis->m_fCustomFrame && ::GetAncestor(hwnd,GA_ROOT)==pThis->m_hwnd) {
				POINT pt={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
				RECT rc;

				pThis->GetScreenPosition(&rc);
				if (::PtInRect(&rc,pt)) {
					int FrameWidth=max(::GetSystemMetrics(SM_CXSIZEFRAME),pThis->m_CustomFrameWidth);
					int FrameHeight=max(::GetSystemMetrics(SM_CYSIZEFRAME),pThis->m_CustomFrameWidth);
					int Code=HTNOWHERE;

					if (pt.x<rc.left+FrameWidth) {
						if (pt.y<rc.top+FrameHeight)
							Code=HTTOPLEFT;
						else if (pt.y>=rc.bottom-FrameHeight)
							Code=HTBOTTOMLEFT;
						else
							Code=HTLEFT;
					} else if (pt.x>=rc.right-FrameWidth) {
						if (pt.y<rc.top+FrameHeight)
							Code=HTTOPRIGHT;
						else if (pt.y>=rc.bottom-FrameHeight)
							Code=HTBOTTOMRIGHT;
						else
							Code=HTRIGHT;
					} else if (pt.y<rc.top+FrameHeight) {
						Code=HTTOP;
					} else if (pt.y>=rc.bottom-FrameHeight) {
						Code=HTBOTTOM;
					}
					if (Code!=HTNOWHERE) {
						return Code;
					}
				}
			}
		}
		break;

	case WM_NCLBUTTONDOWN:
		{
			CMainWindow *pThis=static_cast<CMainWindow*>(::GetProp(hwnd,CHILD_PROP_THIS));

			if (pThis!=nullptr && pThis->m_fCustomFrame && ::GetAncestor(hwnd,GA_ROOT)==pThis->m_hwnd) {
				BYTE Flag=0;

				switch (wParam) {
				case HTTOP:			Flag=WMSZ_TOP;			break;
				case HTTOPLEFT:		Flag=WMSZ_TOPLEFT;		break;
				case HTTOPRIGHT:	Flag=WMSZ_TOPRIGHT;		break;
				case HTLEFT:		Flag=WMSZ_LEFT;			break;
				case HTRIGHT:		Flag=WMSZ_RIGHT;		break;
				case HTBOTTOM:		Flag=WMSZ_BOTTOM;		break;
				case HTBOTTOMLEFT:	Flag=WMSZ_BOTTOMLEFT;	break;
				case HTBOTTOMRIGHT:	Flag=WMSZ_BOTTOMRIGHT;	break;
				}

				if (Flag!=0) {
					::SendMessage(pThis->m_hwnd,WM_SYSCOMMAND,SC_SIZE | Flag,lParam);
					return 0;
				}
			}
		}
		break;

	case WM_DESTROY:
#ifdef _DEBUG
		{
			TCHAR szClass[256];
			::GetClassName(hwnd,szClass,lengthof(szClass));
			TRACE(TEXT("Unhook window %p \"%s\"\n"),hwnd,szClass);
		}
#endif
		::SetWindowLongPtr(hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(pOldWndProc));
		::RemoveProp(hwnd,MAKEINTATOM(m_atomChildOldWndProcProp));
		::RemoveProp(hwnd,CHILD_PROP_THIS);
		break;
	}

	return ::CallWindowProc(pOldWndProc,hwnd,uMsg,wParam,lParam);
}


void CMainWindow::SetWindowVisible()
{
	bool fRestore=false,fShow=false;

	if ((m_App.ResidentManager.GetStatus()&CResidentManager::STATUS_MINIMIZED)!=0) {
		m_App.ResidentManager.SetStatus(0,CResidentManager::STATUS_MINIMIZED);
		m_App.ResidentManager.SetMinimizeToTray(m_App.ViewOptions.GetMinimizeToTray());
		fRestore=true;
	}
	if (!GetVisible()) {
		SetVisible(true);
		ForegroundWindow(m_hwnd);
		Update();
		fShow=true;
	}
	if (::IsIconic(m_hwnd)) {
		::ShowWindow(m_hwnd,SW_RESTORE);
		Update();
		fRestore=true;
	} else if (!fShow) {
		ForegroundWindow(m_hwnd);
	}
	if (m_fMinimizeInit) {
		// �ŏ�����Ԃł̋N����ŏ��̕\��
		ShowFloatingWindows(true);
		m_fMinimizeInit=false;
	}
	if (fRestore) {
		ResumeViewer(ResumeInfo::VIEWERSUSPEND_MINIMIZE);
	}
}


void CMainWindow::ShowFloatingWindows(bool fShow)
{
	if (m_App.Panel.fShowPanelWindow && m_App.Panel.IsFloating()) {
		m_App.Panel.Frame.SetPanelVisible(fShow);
		if (fShow)
			m_App.Panel.Frame.Update();
	}
	if (m_App.Epg.fShowProgramGuide)
		m_App.Epg.ProgramGuideFrame.SetVisible(fShow);
	if (m_App.CaptureWindow.IsCreated())
		m_App.CaptureWindow.SetVisible(fShow);
	if (m_App.StreamInfo.IsCreated())
		m_App.StreamInfo.SetVisible(fShow);
}


bool CMainWindow::OnStandbyChange(bool fStandby)
{
	if (fStandby) {
		if (m_fStandbyInit)
			return true;
		m_App.Logger.AddLog(TEXT("�ҋ@��ԂɈڍs���܂��B"));
		SuspendViewer(ResumeInfo::VIEWERSUSPEND_STANDBY);
		//FinalizeViewer();
		m_Resume.fFullscreen=m_pCore->GetFullscreen();
		if (m_Resume.fFullscreen)
			m_pCore->SetFullscreen(false);
		ShowFloatingWindows(false);
		SetVisible(false);
		m_App.PluginManager.SendStandbyEvent(true);

		if (!m_fProgramGuideUpdating) {
			StoreTunerResumeInfo();

			if (m_App.EpgOptions.GetUpdateWhenStandby()
					&& m_App.CoreEngine.IsTunerOpen()
					&& !m_App.RecordManager.IsRecording()
					&& !m_App.CoreEngine.IsNetworkDriver()
					&& !m_App.CmdLineOptions.m_fNoEpg)
				BeginProgramGuideUpdate(nullptr,nullptr,true);

			if (!m_App.RecordManager.IsRecording() && !m_fProgramGuideUpdating)
				m_App.Core.CloseTuner();
		}
	} else {
		m_App.Logger.AddLog(TEXT("�ҋ@��Ԃ��畜�A���܂��B"));
		SetWindowVisible();
		Util::CWaitCursor WaitCursor;
		if (m_fStandbyInit) {
			bool fSetChannel=m_Resume.fSetChannel;
			m_Resume.fSetChannel=false;
			ResumeTuner();
			m_Resume.fSetChannel=fSetChannel;
			m_App.Core.InitializeChannel();
			InitializeViewer();
			if (!m_App.GeneralOptions.GetResident())
				m_App.ResidentManager.SetResident(false);
			m_fStandbyInit=false;
		}
		if (m_Resume.fFullscreen)
			m_pCore->SetFullscreen(true);
		ShowFloatingWindows(true);
		ForegroundWindow(m_hwnd);
		m_App.PluginManager.SendStandbyEvent(false);
		ResumeTuner();
		ResumeViewer(ResumeInfo::VIEWERSUSPEND_STANDBY);
	}

	return true;
}


bool CMainWindow::EnablePlayback(bool fEnable)
{
	m_fEnablePlayback=fEnable;

	return true;
}


bool CMainWindow::InitStandby()
{
	if (!m_App.CmdLineOptions.m_fNoDirectShow && !m_App.CmdLineOptions.m_fNoView
			&& (!m_App.PlaybackOptions.GetRestorePlayStatus() || m_App.GetEnablePlaybackOnStart())) {
		m_Resume.fEnableViewer=true;
		m_Resume.ViewerSuspendFlags=ResumeInfo::VIEWERSUSPEND_STANDBY;
	}
	m_Resume.fFullscreen=m_App.CmdLineOptions.m_fFullscreen;

	if (m_App.CoreEngine.IsDriverSpecified())
		m_Resume.fOpenTuner=true;

	if (m_App.RestoreChannelInfo.Space>=0 && m_App.RestoreChannelInfo.Channel>=0) {
		int Space=m_App.RestoreChannelInfo.fAllChannels?CChannelManager::SPACE_ALL:m_App.RestoreChannelInfo.Space;
		const CChannelList *pList=m_App.ChannelManager.GetChannelList(Space);
		if (pList!=nullptr) {
			int Index=pList->FindByIndex(m_App.RestoreChannelInfo.Space,
										 m_App.RestoreChannelInfo.Channel,
										 m_App.RestoreChannelInfo.ServiceID);
			if (Index>=0) {
				m_Resume.Channel.SetSpace(Space);
				m_Resume.Channel.SetChannel(Index);
				m_Resume.Channel.SetServiceID(m_App.RestoreChannelInfo.ServiceID);
				m_Resume.fSetChannel=true;
			}
		}
	}

	m_App.ResidentManager.SetResident(true);
	m_fStandbyInit=true;
	m_pCore->SetStandby(true);

	return true;
}


bool CMainWindow::InitMinimize()
{
	if (!m_App.CmdLineOptions.m_fNoDirectShow && !m_App.CmdLineOptions.m_fNoView
			&& (!m_App.PlaybackOptions.GetRestorePlayStatus() || m_App.GetEnablePlaybackOnStart())) {
		m_Resume.fEnableViewer=true;
		m_Resume.ViewerSuspendFlags=ResumeInfo::VIEWERSUSPEND_MINIMIZE;
	}

	m_App.ResidentManager.SetStatus(CResidentManager::STATUS_MINIMIZED,
									CResidentManager::STATUS_MINIMIZED);
	if (!m_App.ResidentManager.GetMinimizeToTray())
		::ShowWindow(m_hwnd,SW_SHOWMINNOACTIVE);

	m_fMinimizeInit=true;

	return true;
}


bool CMainWindow::IsMinimizeToTray() const
{
	return m_App.ResidentManager.GetMinimizeToTray()
		&& (m_App.ResidentManager.GetStatus()&CResidentManager::STATUS_MINIMIZED)!=0;
}


void CMainWindow::StoreTunerResumeInfo()
{
	m_Resume.Channel.Store(&m_App.ChannelManager);
	m_Resume.fSetChannel=m_Resume.Channel.IsValid();
	m_Resume.fOpenTuner=m_App.CoreEngine.IsTunerOpen();
}


bool CMainWindow::ResumeTuner()
{
	if (m_fProgramGuideUpdating)
		EndProgramGuideUpdate(0);

	if (m_Resume.fOpenTuner) {
		m_Resume.fOpenTuner=false;
		if (!m_App.Core.OpenTuner()) {
			m_Resume.fSetChannel=false;
			return false;
		}
	}

	ResumeChannel();

	return true;
}


void CMainWindow::ResumeChannel()
{
	if (m_Resume.fSetChannel) {
		if (m_App.CoreEngine.IsTunerOpen()
				&& !m_App.RecordManager.IsRecording()) {
			m_App.Core.SetChannel(m_Resume.Channel.GetSpace(),
								  m_Resume.Channel.GetChannel(),
								  m_Resume.Channel.GetServiceID());
		}
		m_Resume.fSetChannel=false;
	}
}


void CMainWindow::SuspendViewer(unsigned int Flags)
{
	if (IsViewerEnabled()) {
		TRACE(TEXT("Suspend viewer\n"));
		m_pCore->EnableViewer(false);
		m_Resume.fEnableViewer=true;
	}
	m_Resume.ViewerSuspendFlags|=Flags;
}


void CMainWindow::ResumeViewer(unsigned int Flags)
{
	if ((m_Resume.ViewerSuspendFlags & Flags)!=0) {
		m_Resume.ViewerSuspendFlags&=~Flags;
		if (m_Resume.ViewerSuspendFlags==0) {
			if (m_Resume.fEnableViewer) {
				TRACE(TEXT("Resume viewer\n"));
				m_pCore->EnableViewer(true);
				m_Resume.fEnableViewer=false;
			}
		}
	}
}


bool CMainWindow::ConfirmExit()
{
	return m_App.RecordOptions.ConfirmExit(GetVideoHostWindow(),&m_App.RecordManager);
}


bool CMainWindow::OnExecute(LPCTSTR pszCmdLine)
{
	CCommandLineOptions CmdLine;

	m_App.PluginManager.SendExecuteEvent(pszCmdLine);

	CmdLine.Parse(pszCmdLine);

	if (!CmdLine.m_fMinimize && !CmdLine.m_fTray) {
		SendCommand(CM_SHOW);

		if (CmdLine.m_fFullscreen)
			m_pCore->SetFullscreen(true);
		else if (CmdLine.m_fMaximize)
			SetMaximize(true);
	}

	if (CmdLine.m_fSilent || CmdLine.m_TvRockDID>=0)
		m_App.Core.SetSilent(true);
	if (CmdLine.m_fSaveLog)
		m_App.CmdLineOptions.m_fSaveLog=true;

	if (!CmdLine.m_DriverName.IsEmpty()) {
		if (m_App.Core.OpenTuner(CmdLine.m_DriverName.Get())) {
			if (CmdLine.IsChannelSpecified())
				m_App.Core.SetCommandLineChannel(&CmdLine);
			else
				m_App.Core.RestoreChannel();
		}
	} else {
		if (CmdLine.IsChannelSpecified())
			m_App.Core.SetCommandLineChannel(&CmdLine);
	}

	if (CmdLine.m_fRecord) {
		if (CmdLine.m_fRecordCurServiceOnly)
			m_App.CmdLineOptions.m_fRecordCurServiceOnly=true;
		m_App.Core.CommandLineRecord(&CmdLine);
	} else if (CmdLine.m_fRecordStop) {
		m_App.Core.StopRecord();
	}

	if (CmdLine.m_Volume>=0)
		m_pCore->SetVolume(min(CmdLine.m_Volume,CCoreEngine::MAX_VOLUME),false);
	if (CmdLine.m_fMute)
		m_pCore->SetMute(true);

	if (CmdLine.m_fShowProgramGuide)
		ShowProgramGuide(true);
	if (CmdLine.m_fHomeDisplay)
		PostCommand(CM_HOMEDISPLAY);
	else if (CmdLine.m_fChannelDisplay)
		PostCommand(CM_CHANNELDISPLAY);

	return true;
}


bool CMainWindow::BeginChannelNoInput(int Digits)
{
	if (m_ChannelNoInput.fInputting)
		EndChannelNoInput();

	if (Digits<1 || Digits>5)
		return false;

	TRACE(TEXT("�`�����l���ԍ�%d�����͊J�n\n"),Digits);

	m_ChannelNoInput.fInputting=true;
	m_ChannelNoInput.Digits=Digits;
	m_ChannelNoInput.CurDigit=0;
	m_ChannelNoInput.Number=0;

	m_ChannelNoInputTimer.Begin(m_hwnd,m_ChannelNoInputTimeout);

	if (m_App.OSDOptions.IsOSDEnabled(COSDOptions::OSD_CHANNELNOINPUT)) {
		m_App.OSDManager.ShowOSD(TEXT("---"),COSDManager::SHOW_NO_FADE);
	}

	return true;
}


void CMainWindow::EndChannelNoInput()
{
	if (m_ChannelNoInput.fInputting) {
		TRACE(TEXT("�`�����l���ԍ����͏I��\n"));
		m_ChannelNoInput.fInputting=false;
		m_ChannelNoInputTimer.End();
		m_App.OSDManager.ClearOSD();
	}
}


bool CMainWindow::OnChannelNoInput(int Number)
{
	if (!m_ChannelNoInput.fInputting
			|| Number<0 || Number>9)
		return false;

	m_ChannelNoInput.Number=m_ChannelNoInput.Number*10+Number;
	m_ChannelNoInput.CurDigit++;

	if (m_App.OSDOptions.IsOSDEnabled(COSDOptions::OSD_CHANNELNOINPUT)) {
		TCHAR szText[8];
		int Number=m_ChannelNoInput.Number;
		for (int i=m_ChannelNoInput.Digits-1;i>=0;i--) {
			if (i<m_ChannelNoInput.CurDigit) {
				szText[i]=Number%10+_T('0');
				Number/=10;
			} else {
				szText[i]=_T('-');
			}
		}
		szText[m_ChannelNoInput.Digits]=_T('\0');
		m_App.OSDManager.ShowOSD(szText,COSDManager::SHOW_NO_FADE);
	}

	if (m_ChannelNoInput.CurDigit<m_ChannelNoInput.Digits) {
		m_ChannelNoInputTimer.Begin(m_hwnd,m_ChannelNoInputTimeout);
	} else {
		EndChannelNoInput();

		if (m_ChannelNoInput.Number>0) {
			const CChannelList *pChannelList=m_App.ChannelManager.GetCurrentChannelList();
			if (pChannelList!=nullptr) {
				int Index=pChannelList->FindChannelNo(m_ChannelNoInput.Number);
				if (Index<0 && m_ChannelNoInput.Number<=0xFFFF)
					Index=pChannelList->FindServiceID((WORD)m_ChannelNoInput.Number);
				if (Index>=0)
					SendCommand(CM_CHANNEL_FIRST+Index);
			}
		}
	}

	return true;
}


bool CMainWindow::BeginProgramGuideUpdate(LPCTSTR pszBonDriver,const CChannelList *pChannelList,bool fStandby)
{
	if (!m_fProgramGuideUpdating) {
		if (m_App.CmdLineOptions.m_fNoEpg) {
			if (!fStandby)
				ShowMessage(TEXT("�R�}���h���C���I�v�V������EPG�����擾���Ȃ��悤�Ɏw�肳��Ă��邽�߁A\n�ԑg�\�̎擾���ł��܂���B"),
							TEXT("���m�点"),MB_OK | MB_ICONINFORMATION);
			return false;
		}
		if (m_App.RecordManager.IsRecording()) {
			if (!fStandby)
				ShowMessage(TEXT("�^�撆�͔ԑg�\�̎擾���s���܂���B"),
							TEXT("���m�点"),MB_OK | MB_ICONINFORMATION);
			return false;
		}

		const bool fTunerOpen=m_App.CoreEngine.IsTunerOpen();

		if (!IsStringEmpty(pszBonDriver)) {
			if (!m_App.Core.OpenTuner(pszBonDriver))
				return false;
		}

		if (pChannelList==nullptr) {
			pChannelList=m_App.ChannelManager.GetCurrentRealChannelList();
			if (pChannelList==nullptr) {
				if (!fTunerOpen)
					m_App.Core.CloseTuner();
				return false;
			}
		}
		m_EpgUpdateChannelList.clear();
		for (int i=0;i<pChannelList->NumChannels();i++) {
			const CChannelInfo *pChInfo=pChannelList->GetChannelInfo(i);

			if (pChInfo->IsEnabled()) {
				const NetworkType Network=GetNetworkType(pChInfo->GetNetworkID());
				std::vector<EpgChannelGroup>::iterator itr;

				for (itr=m_EpgUpdateChannelList.begin();itr!=m_EpgUpdateChannelList.end();++itr) {
					if (pChInfo->GetSpace()==itr->Space && pChInfo->GetChannelIndex()==itr->Channel)
						break;
					if (pChInfo->GetNetworkID()==itr->ChannelList.GetChannelInfo(0)->GetNetworkID()
							&& ((Network==NETWORK_BS && !m_App.EpgOptions.GetUpdateBSExtended())
							 || (Network==NETWORK_CS && !m_App.EpgOptions.GetUpdateCSExtended())))
						break;
				}
				if (itr==m_EpgUpdateChannelList.end()) {
					m_EpgUpdateChannelList.push_back(EpgChannelGroup());
					itr=m_EpgUpdateChannelList.end();
					--itr;
					itr->Space=pChInfo->GetSpace();
					itr->Channel=pChInfo->GetChannelIndex();
					itr->Time=0;
				}
				itr->ChannelList.AddChannel(*pChInfo);
			}
		}
		if (m_EpgUpdateChannelList.empty()) {
			if (!fTunerOpen)
				m_App.Core.CloseTuner();
			return false;
		}

		if (m_pCore->GetStandby()) {
			if (!m_App.Core.OpenTuner())
				return false;
		} else {
			if (!m_App.CoreEngine.IsTunerOpen())
				return false;
			if (!fStandby) {
				StoreTunerResumeInfo();
				m_Resume.fOpenTuner=fTunerOpen;
			}
		}

		m_App.Logger.AddLog(TEXT("�ԑg�\�̎擾���J�n���܂��B"));
		m_fProgramGuideUpdating=true;
		SuspendViewer(ResumeInfo::VIEWERSUSPEND_EPGUPDATE);
		m_EpgUpdateCurChannel=-1;
		SetEpgUpdateNextChannel();
	}

	return true;
}


void CMainWindow::OnProgramGuideUpdateEnd(unsigned int Flags)
{
	if (m_fProgramGuideUpdating) {
		HANDLE hThread;
		int OldPriority;

		m_App.Logger.AddLog(TEXT("�ԑg�\�̎擾���I�����܂��B"));
		::KillTimer(m_hwnd,TIMER_ID_PROGRAMGUIDEUPDATE);
		m_fProgramGuideUpdating=false;
		m_EpgUpdateChannelList.clear();
		if (m_pCore->GetStandby()) {
			hThread=::GetCurrentThread();
			OldPriority=::GetThreadPriority(hThread);
			::SetThreadPriority(hThread,THREAD_PRIORITY_LOWEST);
		} else {
			::SetCursor(::LoadCursor(nullptr,IDC_WAIT));
		}
		m_App.EpgProgramList.UpdateProgramList();
		m_App.EpgOptions.SaveEpgFile(&m_App.EpgProgramList);
		if (m_pCore->GetStandby()) {
			m_App.Epg.ProgramGuide.SendMessage(WM_COMMAND,CM_PROGRAMGUIDE_REFRESH,0);
			::SetThreadPriority(hThread,OldPriority);
			if ((Flags&EPG_UPDATE_END_CLOSE_TUNER)!=0)
				m_App.Core.CloseTuner();
		} else {
			::SetCursor(::LoadCursor(nullptr,IDC_ARROW));
			if ((Flags&EPG_UPDATE_END_RESUME)!=0)
				ResumeChannel();
			if (m_App.Panel.fShowPanelWindow && m_App.Panel.Form.GetCurPageID()==PANEL_ID_CHANNEL)
				m_App.Panel.ChannelPanel.UpdateAllChannels(false);
		}
		ResumeViewer(ResumeInfo::VIEWERSUSPEND_EPGUPDATE);
	}
}


void CMainWindow::EndProgramGuideUpdate(unsigned int Flags)
{
	OnProgramGuideUpdateEnd(Flags);

	if (m_App.Epg.ProgramGuide.IsCreated())
		m_App.Epg.ProgramGuide.SendMessage(WM_COMMAND,CM_PROGRAMGUIDE_ENDUPDATE,0);
}


bool CMainWindow::SetEpgUpdateNextChannel()
{
	size_t i;

	for (i=m_EpgUpdateCurChannel+1;i<m_EpgUpdateChannelList.size();i++) {
		const EpgChannelGroup &ChGroup=m_EpgUpdateChannelList[i];

		m_fEpgUpdateChannelChange=true;
		bool fOK=m_App.Core.SetChannelByIndex(ChGroup.Space,ChGroup.Channel);
		m_fEpgUpdateChannelChange=false;
		if (fOK) {
			::SetTimer(m_hwnd,TIMER_ID_PROGRAMGUIDEUPDATE,30000,nullptr);
			m_EpgUpdateCurChannel=(int)i;
			m_EpgAccumulateClock.Start();

			// TODO: �c�莞�Ԃ������ƎZ�o����
			DWORD Time=0;
			for (size_t j=i;j<m_EpgUpdateChannelList.size();j++) {
				WORD NetworkID=m_EpgUpdateChannelList[j].ChannelList.GetChannelInfo(0)->GetNetworkID();
				if (IsBSNetworkID(NetworkID) || IsCSNetworkID(NetworkID))
					Time+=180000;
				else
					Time+=60000;
			}
			m_App.Epg.ProgramGuide.SetEpgUpdateProgress(
				m_EpgUpdateCurChannel,(int)m_EpgUpdateChannelList.size(),Time);
			return true;
		}
	}

	EndProgramGuideUpdate();
	return false;
}


void CMainWindow::UpdatePanel()
{
	switch (m_App.Panel.Form.GetCurPageID()) {
	case PANEL_ID_INFORMATION:
		{
			BYTE AspectX,AspectY;
			if (m_App.CoreEngine.m_DtvEngine.m_MediaViewer.GetEffectiveAspectRatio(&AspectX,&AspectY))
				m_App.Panel.InfoPanel.SetAspectRatio(AspectX,AspectY);
			if (m_App.RecordManager.IsRecording()) {
				const CRecordTask *pRecordTask=m_App.RecordManager.GetRecordTask();
				m_App.Panel.InfoPanel.SetRecordStatus(true,pRecordTask->GetFileName(),
					pRecordTask->GetWroteSize(),pRecordTask->GetRecordTime());
			}
			UpdateProgramInfo();
		}
		break;

	case PANEL_ID_PROGRAMLIST:
		if (m_ProgramListUpdateTimerCount>0) {
			CChannelInfo ChInfo;
			if (m_App.Core.GetCurrentStreamChannelInfo(&ChInfo)
					&& ChInfo.GetServiceID()!=0) {
				m_App.EpgProgramList.UpdateService(
					ChInfo.GetNetworkID(),
					ChInfo.GetTransportStreamID(),
					ChInfo.GetServiceID());
				m_App.Panel.ProgramListPanel.UpdateProgramList(&ChInfo);
			}
		}
		break;

	case PANEL_ID_CHANNEL:
		RefreshChannelPanel();
		break;
	}
}


void CMainWindow::RefreshChannelPanel()
{
	Util::CWaitCursor WaitCursor;

	if (m_App.Panel.ChannelPanel.IsChannelListEmpty()) {
		m_App.Panel.ChannelPanel.SetChannelList(
			m_App.ChannelManager.GetCurrentChannelList(),
			!m_App.EpgOptions.IsEpgFileLoading());
	} else {
		if (!m_App.EpgOptions.IsEpgFileLoading())
			m_App.Panel.ChannelPanel.UpdateAllChannels(false);
	}
	m_App.Panel.ChannelPanel.SetCurrentChannel(m_App.ChannelManager.GetCurrentChannel());
}


// ����p�l���̃A�C�e����ݒ肷��
void CMainWindow::InitControlPanel()
{
	m_App.Panel.ControlPanel.AddItem(new CTunerControlItem);
	m_App.Panel.ControlPanel.AddItem(new CChannelControlItem);

	const CChannelList *pList=m_App.ChannelManager.GetCurrentChannelList();
	for (int i=0;i<12;i++) {
		TCHAR szText[4];
		CControlPanelButton *pItem;

		StdUtil::snprintf(szText,lengthof(szText),TEXT("%d"),i+1);
		pItem=new CControlPanelButton(CM_CHANNELNO_FIRST+i,szText,i%6==0,1);
		if (pList==nullptr || pList->FindChannelNo(i+1)<0)
			pItem->SetEnable(false);
		m_App.Panel.ControlPanel.AddItem(pItem);
	}

	m_App.Panel.ControlPanel.AddItem(new CVideoControlItem);
	m_App.Panel.ControlPanel.AddItem(new CVolumeControlItem);
	m_App.Panel.ControlPanel.AddItem(new CAudioControlItem);
}


void CMainWindow::UpdateControlPanel()
{
	const CChannelList *pList=m_App.ChannelManager.GetCurrentChannelList();
	const CChannelInfo *pCurChannel=m_App.ChannelManager.GetCurrentChannelInfo();

	for (int i=0;i<12;i++) {
		CControlPanelItem *pItem=m_App.Panel.ControlPanel.GetItem(CONTROLPANEL_ITEM_CHANNEL_1+i);
		if (pItem!=nullptr) {
			pItem->SetEnable(pList!=nullptr && pList->FindChannelNo(i+1)>=0);
			pItem->SetCheck(false);
		}
	}
	if (pCurChannel!=nullptr) {
		if (pCurChannel->GetChannelNo()>=1 && pCurChannel->GetChannelNo()<=12) {
			m_App.Panel.ControlPanel.CheckRadioItem(
				CM_CHANNELNO_FIRST,CM_CHANNELNO_LAST,
				CM_CHANNELNO_FIRST+pCurChannel->GetChannelNo()-1);
		}
	}
}


void CMainWindow::ApplyColorScheme(const CColorScheme *pColorScheme)
{
	Theme::BorderInfo Border;

	m_LayoutBase.SetBackColor(pColorScheme->GetColor(CColorScheme::COLOR_SPLITTER));
	pColorScheme->GetBorderInfo(CColorScheme::BORDER_SCREEN,&Border);
	if (!m_fViewWindowEdge)
		Border.Type=Theme::BORDER_NONE;
	m_Viewer.GetViewWindow().SetBorder(&Border);

	CTitleBar::ThemeInfo TitleBarTheme;
	pColorScheme->GetStyle(CColorScheme::STYLE_TITLEBARCAPTION,
						   &TitleBarTheme.CaptionStyle);
	pColorScheme->GetStyle(CColorScheme::STYLE_TITLEBARICON,
						   &TitleBarTheme.IconStyle);
	pColorScheme->GetStyle(CColorScheme::STYLE_TITLEBARHIGHLIGHTITEM,
						   &TitleBarTheme.HighlightIconStyle);
	pColorScheme->GetBorderInfo(CColorScheme::BORDER_TITLEBAR,
								&TitleBarTheme.Border);
	m_TitleBar.SetTheme(&TitleBarTheme);

	Theme::GradientInfo Gradient;
	pColorScheme->GetGradientInfo(CColorScheme::GRADIENT_NOTIFICATIONBARBACK,&Gradient);
	m_NotificationBar.SetColors(
		&Gradient,
		pColorScheme->GetColor(CColorScheme::COLOR_NOTIFICATIONBARTEXT),
		pColorScheme->GetColor(CColorScheme::COLOR_NOTIFICATIONBARWARNINGTEXT),
		pColorScheme->GetColor(CColorScheme::COLOR_NOTIFICATIONBARERRORTEXT));
}


bool CMainWindow::SetViewWindowEdge(bool fEdge)
{
	if (m_fViewWindowEdge!=fEdge) {
		const CColorScheme *pColorScheme=m_App.ColorSchemeOptions.GetColorScheme();
		Theme::BorderInfo Border;

		pColorScheme->GetBorderInfo(CColorScheme::BORDER_SCREEN,&Border);
		if (!fEdge)
			Border.Type=Theme::BORDER_NONE;
		m_Viewer.GetViewWindow().SetBorder(&Border);
		m_fViewWindowEdge=fEdge;
	}
	return true;
}


bool CMainWindow::GetOSDWindow(HWND *phwndParent,RECT *pRect,bool *pfForcePseudoOSD)
{
	if (!GetVisible() || ::IsIconic(m_hwnd))
		return false;
	if (m_Viewer.GetVideoContainer().GetVisible()) {
		*phwndParent=m_Viewer.GetVideoContainer().GetHandle();
	} else {
		*phwndParent=m_Viewer.GetVideoContainer().GetParent();
		*pfForcePseudoOSD=true;
	}
	::GetClientRect(*phwndParent,pRect);
	pRect->top+=m_NotificationBar.GetBarHeight();
	if (!m_fShowStatusBar && m_fPopupStatusBar)
		pRect->bottom-=m_App.StatusView.GetHeight();
	return true;
}


bool CMainWindow::SetOSDHideTimer(DWORD Delay)
{
	return ::SetTimer(m_hwnd,TIMER_ID_OSD,Delay,nullptr)!=0;
}


CStatusView &CMainWindow::GetStatusView()
{
	return m_App.StatusView;
}


CSideBar &CMainWindow::GetSideBar()
{
	return m_App.SideBar;
}




bool CMainWindow::CFullscreen::Initialize(HINSTANCE hinst)
{
	WNDCLASS wc;

	wc.style=CS_DBLCLKS;
	wc.lpfnWndProc=WndProc;
	wc.cbClsExtra=0;
	wc.cbWndExtra=0;
	wc.hInstance=hinst;
	wc.hIcon=nullptr;
	wc.hCursor=nullptr;
	wc.hbrBackground=::CreateSolidBrush(RGB(0,0,0));
	wc.lpszMenuName=nullptr;
	wc.lpszClassName=FULLSCREEN_WINDOW_CLASS;
	return ::RegisterClass(&wc)!=0;
}


CMainWindow::CFullscreen::CFullscreen(CMainWindow &MainWindow)
	: m_MainWindow(MainWindow)
	, m_App(MainWindow.m_App)
	, m_pViewer(nullptr)
	, m_TitleBarManager(&MainWindow,false)
	, m_PanelEventHandler(&MainWindow)
	, m_PanelWidth(-1)
{
}


CMainWindow::CFullscreen::~CFullscreen()
{
	Destroy();
}


LRESULT CMainWindow::CFullscreen::OnMessage(HWND hwnd,UINT uMsg,WPARAM wParam,LPARAM lParam)
{
	switch (uMsg) {
	case WM_CREATE:
		return OnCreate()?0:-1;

	case WM_SIZE:
		m_LayoutBase.SetPosition(0,0,LOWORD(lParam),HIWORD(lParam));
		return 0;

	case WM_RBUTTONDOWN:
		OnRButtonDown();
		return 0;

	case WM_MBUTTONDOWN:
		OnMButtonDown();
		return 0;

	case WM_LBUTTONDBLCLK:
		OnLButtonDoubleClick();
		return 0;

	case WM_MOUSEMOVE:
		OnMouseMove();
		return 0;

	case WM_TIMER:
		if (wParam==TIMER_ID_HIDECURSOR) {
			if (!m_fMenu) {
				POINT pt;
				RECT rc;
				::GetCursorPos(&pt);
				m_ViewWindow.GetScreenPosition(&rc);
				if (::PtInRect(&rc,pt)) {
					ShowCursor(false);
					::SetCursor(nullptr);
				}
			}
			::KillTimer(hwnd,TIMER_ID_HIDECURSOR);
		}
		return 0;

	case WM_SETCURSOR:
		if (LOWORD(lParam)==HTCLIENT) {
			HWND hwndCursor=reinterpret_cast<HWND>(wParam);

			if (hwndCursor==hwnd
					|| hwndCursor==m_pViewer->GetVideoContainer().GetHandle()
					|| hwndCursor==m_ViewWindow.GetHandle()
					|| CPseudoOSD::IsPseudoOSD(hwndCursor)) {
				::SetCursor(m_fShowCursor?::LoadCursor(nullptr,IDC_ARROW):nullptr);
				return TRUE;
			}
		}
		break;

	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
		{
			bool fHorz=uMsg==WM_MOUSEHWHEEL;

			m_MainWindow.OnMouseWheel(wParam,lParam,fHorz);
			return fHorz;
		}

#if 0
	case WM_WINDOWPOSCHANGING:
		{
			WINDOWPOS *pwp=reinterpret_cast<WINDOWPOS*>(lParam);

			pwp->hwndInsertAfter=HWND_TOPMOST;
		}
		return 0;
#endif

	case WM_SYSKEYDOWN:
		if (wParam!=VK_F10)
			break;
	case WM_KEYDOWN:
		if (wParam==VK_ESCAPE) {
			m_App.UICore.SetFullscreen(false);
			return 0;
		}
	case WM_COMMAND:
		return m_MainWindow.SendMessage(uMsg,wParam,lParam);

	case WM_SYSCOMMAND:
		switch (wParam&0xFFFFFFF0) {
		case SC_MONITORPOWER:
			if (m_App.ViewOptions.GetNoMonitorLowPower()
					&& m_App.UICore.IsViewerEnabled())
				return 0;
			break;

		case SC_SCREENSAVE:
			if (m_App.ViewOptions.GetNoScreenSaver()
					&& m_App.UICore.IsViewerEnabled())
				return 0;
			break;
		}
		break;

	case WM_APPCOMMAND:
		{
			int Command=m_App.Accelerator.TranslateAppCommand(wParam,lParam);

			if (Command!=0) {
				m_MainWindow.SendCommand(Command);
				return TRUE;
			}
		}
		break;

	case WM_SETFOCUS:
		if (m_pViewer->GetDisplayBase().IsVisible())
			m_pViewer->GetDisplayBase().SetFocus();
		return 0;

	case WM_SETTEXT:
		m_TitleBar.SetLabel(reinterpret_cast<LPCTSTR>(lParam));
		break;

	case WM_SETICON:
		if (wParam==ICON_SMALL)
			m_TitleBar.SetIcon(reinterpret_cast<HICON>(lParam));
		break;

	case WM_DESTROY:
		m_pViewer->GetVideoContainer().SetParent(&m_pViewer->GetViewWindow());
		m_pViewer->GetViewWindow().SendSizeMessage();
		ShowCursor(true);
		m_pViewer->GetDisplayBase().AdjustPosition();
		m_TitleBar.Destroy();
		m_App.OSDManager.Reset();
		ShowStatusView(false);
		ShowSideBar(false);
		ShowPanel(false);
		return 0;
	}

	return ::DefWindowProc(hwnd,uMsg,wParam,lParam);
}


bool CMainWindow::CFullscreen::Create(HWND hwndParent,DWORD Style,DWORD ExStyle,int ID)
{
	return CreateBasicWindow(hwndParent,Style,ExStyle,ID,
							 FULLSCREEN_WINDOW_CLASS,nullptr,m_App.GetInstance());
}


bool CMainWindow::CFullscreen::Create(HWND hwndOwner,CBasicViewer *pViewer)
{
	HMONITOR hMonitor;
	int x,y,Width,Height;

	hMonitor=::MonitorFromWindow(m_MainWindow.GetHandle(),MONITOR_DEFAULTTONEAREST);
	if (hMonitor!=nullptr) {
		MONITORINFO mi;

		mi.cbSize=sizeof(MONITORINFO);
		::GetMonitorInfo(hMonitor,&mi);
		x=mi.rcMonitor.left;
		y=mi.rcMonitor.top;
		Width=mi.rcMonitor.right-mi.rcMonitor.left;
		Height=mi.rcMonitor.bottom-mi.rcMonitor.top;
	} else {
		x=y=0;
		Width=::GetSystemMetrics(SM_CXSCREEN);
		Height=::GetSystemMetrics(SM_CYSCREEN);
	}
#ifdef _DEBUG
	// �f�o�b�O���Ղ��悤�ɏ������\��
	/*
	Width/=2;
	Height/=2;
	*/
#endif
	SetPosition(x,y,Width,Height);
	m_pViewer=pViewer;
	return Create(hwndOwner,WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,WS_EX_TOPMOST);
}


bool CMainWindow::CFullscreen::OnCreate()
{
	m_LayoutBase.Create(m_hwnd,WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);

	m_ViewWindow.Create(m_LayoutBase.GetHandle(),
		WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN  | WS_CLIPSIBLINGS,0,IDC_VIEW);
	m_ViewWindow.SetMessageWindow(m_hwnd);
	m_pViewer->GetVideoContainer().SetParent(m_ViewWindow.GetHandle());
	m_ViewWindow.SetVideoContainer(&m_pViewer->GetVideoContainer());

	m_Panel.Create(m_LayoutBase.GetHandle(),
				   WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Panel.ShowTitle(true);
	m_Panel.EnableFloating(false);
	m_Panel.SetEventHandler(&m_PanelEventHandler);
	CPanel::ThemeInfo PanelTheme;
	m_App.Panel.Frame.GetTheme(&PanelTheme);
	m_Panel.SetTheme(&PanelTheme);

	Layout::CSplitter *pSplitter=new Layout::CSplitter(CONTAINER_ID_PANELSPLITTER);
	pSplitter->SetVisible(true);
	int PanelPaneIndex=m_MainWindow.GetPanelPaneIndex();
	Layout::CWindowContainer *pViewContainer=new Layout::CWindowContainer(CONTAINER_ID_VIEW);
	pViewContainer->SetWindow(&m_ViewWindow);
	pViewContainer->SetMinSize(32,32);
	pViewContainer->SetVisible(true);
	pSplitter->SetPane(1-PanelPaneIndex,pViewContainer);
	Layout::CWindowContainer *pPanelContainer=new Layout::CWindowContainer(CONTAINER_ID_PANEL);
	pPanelContainer->SetWindow(&m_Panel);
	pPanelContainer->SetMinSize(64,32);
	pSplitter->SetPane(PanelPaneIndex,pPanelContainer);
	pSplitter->SetAdjustPane(CONTAINER_ID_VIEW);
	m_LayoutBase.SetTopContainer(pSplitter);

	RECT rc;
	m_pViewer->GetDisplayBase().GetParent()->GetClientRect(&rc);
	m_pViewer->GetDisplayBase().SetPosition(&rc);

	m_TitleBar.Create(m_ViewWindow.GetHandle(),
					  WS_CHILD | WS_CLIPSIBLINGS,0,IDC_TITLEBAR);
	m_TitleBar.SetEventHandler(&m_TitleBarManager);
	HICON hico=reinterpret_cast<HICON>(m_MainWindow.SendMessage(WM_GETICON,ICON_SMALL,0));
	if (hico==nullptr)
		hico=::LoadIcon(m_App.GetInstance(),MAKEINTRESOURCE(IDI_ICON));
	m_TitleBar.SetIcon(hico);
	m_TitleBar.SetMaximizeMode(m_MainWindow.GetMaximize());
	m_TitleBar.SetFullscreenMode(true);

	m_App.OSDManager.Reset();

	m_App.CoreEngine.m_DtvEngine.m_MediaViewer.SetViewStretchMode(
		m_App.ViewOptions.GetFullscreenStretchMode());

	m_fShowCursor=true;
	m_fMenu=false;
	m_fShowStatusView=false;
	m_fShowTitleBar=false;
	m_fShowSideBar=false;
	m_fShowPanel=false;

	m_LastCursorMovePos.x=LONG_MAX/2;
	m_LastCursorMovePos.y=LONG_MAX/2;
	::SetTimer(m_hwnd,TIMER_ID_HIDECURSOR,HIDE_CURSOR_DELAY,nullptr);

	return true;
}


void CMainWindow::CFullscreen::ShowCursor(bool fShow)
{
	m_App.CoreEngine.m_DtvEngine.m_MediaViewer.HideCursor(!fShow);
	m_ViewWindow.ShowCursor(fShow);
	m_fShowCursor=fShow;
}


void CMainWindow::CFullscreen::ShowPanel(bool fShow)
{
	if (m_fShowPanel!=fShow) {
		Layout::CSplitter *pSplitter=
			dynamic_cast<Layout::CSplitter*>(m_LayoutBase.GetContainerByID(CONTAINER_ID_PANELSPLITTER));

		if (fShow) {
			if (m_Panel.GetWindow()==nullptr) {
				if (m_PanelWidth<0)
					m_PanelWidth=m_App.Panel.Frame.GetDockingWidth();
				m_App.Panel.Frame.SetPanelVisible(false);
				m_App.Panel.Frame.GetPanel()->SetWindow(nullptr,nullptr);
				m_Panel.SetWindow(&m_App.Panel.Form,TEXT("�p�l��"));
				pSplitter->SetPaneSize(CONTAINER_ID_PANEL,m_PanelWidth);
			}
			m_Panel.SendSizeMessage();
			m_LayoutBase.SetContainerVisible(CONTAINER_ID_PANEL,true);
			m_MainWindow.UpdatePanel();
		} else {
			m_PanelWidth=m_Panel.GetWidth();
			m_LayoutBase.SetContainerVisible(CONTAINER_ID_PANEL,false);
			m_Panel.SetWindow(nullptr,nullptr);
			CPanel *pPanel=m_App.Panel.Frame.GetPanel();
			pPanel->SetWindow(&m_App.Panel.Form,TEXT("�p�l��"));
			pPanel->SendSizeMessage();
			if (m_App.Panel.fShowPanelWindow) {
				m_App.Panel.Frame.SetPanelVisible(true);
			}
		}
		m_fShowPanel=fShow;
	}
}


bool CMainWindow::CFullscreen::SetPanelWidth(int Width)
{
	m_PanelWidth=Width;
	return true;
}


void CMainWindow::CFullscreen::HideAllBars()
{
	ShowTitleBar(false);
	ShowSideBar(false);
	ShowStatusView(false);
}


void CMainWindow::CFullscreen::OnMouseCommand(int Command)
{
	if (Command==0)
		return;
	// ���j���[�\�����̓J�[�\���������Ȃ�
	::KillTimer(m_hwnd,TIMER_ID_HIDECURSOR);
	ShowCursor(true);
	::SetCursor(LoadCursor(nullptr,IDC_ARROW));
	m_fMenu=true;
	m_MainWindow.SendMessage(WM_COMMAND,MAKEWPARAM(Command,CMainWindow::COMMAND_FROM_MOUSE),0);
	m_fMenu=false;
	if (m_hwnd!=nullptr)
		::SetTimer(m_hwnd,TIMER_ID_HIDECURSOR,HIDE_CURSOR_DELAY,nullptr);
}


void CMainWindow::CFullscreen::OnRButtonDown()
{
	OnMouseCommand(m_App.OperationOptions.GetRightClickCommand());
}


void CMainWindow::CFullscreen::OnMButtonDown()
{
	OnMouseCommand(m_App.OperationOptions.GetMiddleClickCommand());
}


void CMainWindow::CFullscreen::OnLButtonDoubleClick()
{
	OnMouseCommand(m_App.OperationOptions.GetLeftDoubleClickCommand());
}


void CMainWindow::CFullscreen::OnMouseMove()
{
	if (m_fMenu)
		return;

	POINT pt;
	RECT rcClient,rcStatus,rcTitle,rc;
	bool fShowStatusView=false,fShowTitleBar=false,fShowSideBar=false;

	::GetCursorPos(&pt);
	::ScreenToClient(m_hwnd,&pt);
	m_ViewWindow.GetClientRect(&rcClient);

	rcStatus=rcClient;
	rcStatus.top=rcStatus.bottom-m_App.StatusView.CalcHeight(rcClient.right-rcClient.left);
	if (::PtInRect(&rcStatus,pt))
		fShowStatusView=true;
	rc=rcClient;
	m_TitleBarManager.Layout(&rc,&rcTitle);
	if (::PtInRect(&rcTitle,pt))
		fShowTitleBar=true;

	if (m_App.SideBarOptions.ShowPopup()) {
		RECT rcSideBar;
		switch (m_App.SideBarOptions.GetPlace()) {
		case CSideBarOptions::PLACE_LEFT:
		case CSideBarOptions::PLACE_RIGHT:
			if (!fShowStatusView && !fShowTitleBar) {
				m_MainWindow.m_SideBarManager.Layout(&rcClient,&rcSideBar);
				if (::PtInRect(&rcSideBar,pt))
					fShowSideBar=true;
			}
			break;
		case CSideBarOptions::PLACE_TOP:
			rcClient.top=rcTitle.bottom;
			m_MainWindow.m_SideBarManager.Layout(&rcClient,&rcSideBar);
			if (::PtInRect(&rcSideBar,pt)) {
				fShowSideBar=true;
				fShowTitleBar=true;
			}
			break;
		case CSideBarOptions::PLACE_BOTTOM:
			rcClient.bottom=rcStatus.top;
			m_MainWindow.m_SideBarManager.Layout(&rcClient,&rcSideBar);
			if (::PtInRect(&rcSideBar,pt)) {
				fShowSideBar=true;
				fShowStatusView=true;
			}
			break;
		}
	}

	ShowStatusView(fShowStatusView);
	ShowTitleBar(fShowTitleBar);
	ShowSideBar(fShowSideBar);

	if (fShowStatusView || fShowTitleBar || fShowSideBar) {
		::KillTimer(m_hwnd,TIMER_ID_HIDECURSOR);
		return;
	}

	if (abs(m_LastCursorMovePos.x-pt.x)>=4 || abs(m_LastCursorMovePos.y-pt.y)>=4) {
		m_LastCursorMovePos=pt;
		if (!m_fShowCursor) {
			::SetCursor(::LoadCursor(nullptr,IDC_ARROW));
			ShowCursor(true);
		}
	}

	::SetTimer(m_hwnd,TIMER_ID_HIDECURSOR,HIDE_CURSOR_DELAY,nullptr);
}


void CMainWindow::CFullscreen::ShowStatusView(bool fShow)
{
	if (fShow==m_fShowStatusView)
		return;

	Layout::CLayoutBase &LayoutBase=m_MainWindow.GetLayoutBase();

	if (fShow) {
		RECT rc;

		ShowSideBar(false);
		m_ViewWindow.GetClientRect(&rc);
		rc.top=rc.bottom-m_App.StatusView.CalcHeight(rc.right-rc.left);
		m_App.StatusView.SetVisible(false);
		LayoutBase.SetContainerVisible(CONTAINER_ID_STATUS,false);
		m_App.StatusView.SetParent(&m_ViewWindow);
		m_App.StatusView.SetPosition(&rc);
		m_App.StatusView.SetVisible(true);
		::BringWindowToTop(m_App.StatusView.GetHandle());
	} else {
		m_App.StatusView.SetVisible(false);
		m_App.StatusView.SetParent(&LayoutBase);
		if (m_MainWindow.GetStatusBarVisible()) {
			/*
			LayoutBase.Adjust();
			m_App.StatusView.SetVisible(true);
			*/
			LayoutBase.SetContainerVisible(CONTAINER_ID_STATUS,true);
		}
	}

	m_fShowStatusView=fShow;
}


void CMainWindow::CFullscreen::ShowTitleBar(bool fShow)
{
	if (fShow==m_fShowTitleBar)
		return;

	if (fShow) {
		RECT rc,rcBar;
		const CColorScheme *pColorScheme=m_App.ColorSchemeOptions.GetColorScheme();
		Theme::GradientInfo Gradient1,Gradient2;
		Theme::BorderInfo Border;

		ShowSideBar(false);
		m_ViewWindow.GetClientRect(&rc);
		m_TitleBarManager.Layout(&rc,&rcBar);
		m_TitleBar.SetPosition(&rcBar);
		m_TitleBar.SetLabel(m_MainWindow.GetTitleBar().GetLabel());
		m_TitleBar.SetMaximizeMode(m_MainWindow.GetMaximize());
		CTitleBar::ThemeInfo TitleBarTheme;
		m_MainWindow.GetTitleBar().GetTheme(&TitleBarTheme);
		m_TitleBar.SetTheme(&TitleBarTheme);
		m_TitleBar.SetVisible(true);
		::BringWindowToTop(m_TitleBar.GetHandle());
	} else {
		m_TitleBar.SetVisible(false);
	}

	m_fShowTitleBar=fShow;
}


void CMainWindow::CFullscreen::ShowSideBar(bool fShow)
{
	if (fShow==m_fShowSideBar)
		return;

	Layout::CLayoutBase &LayoutBase=m_MainWindow.GetLayoutBase();

	if (fShow) {
		RECT rcClient,rcBar;

		m_ViewWindow.GetClientRect(&rcClient);
		if (m_fShowStatusView)
			rcClient.bottom-=m_App.StatusView.GetHeight();
		if (m_fShowTitleBar)
			rcClient.top+=m_TitleBar.GetHeight();
		m_MainWindow.m_SideBarManager.Layout(&rcClient,&rcBar);
		m_App.SideBar.SetVisible(false);
		LayoutBase.SetContainerVisible(CONTAINER_ID_SIDEBAR,false);
		m_App.SideBar.SetParent(&m_ViewWindow);
		m_App.SideBar.SetPosition(&rcBar);
		m_App.SideBar.SetVisible(true);
		::BringWindowToTop(m_App.SideBar.GetHandle());
	} else {
		m_App.SideBar.SetVisible(false);
		m_App.SideBar.SetParent(&LayoutBase);
		if (m_MainWindow.GetSideBarVisible()) {
			/*
			LayoutBase.Adjust();
			m_App.SideBar.SetVisible(true);
			*/
			LayoutBase.SetContainerVisible(CONTAINER_ID_SIDEBAR,true);
		}
	}

	m_fShowSideBar=fShow;
}


CMainWindow::CFullscreen::CPanelEventHandler::CPanelEventHandler(CMainWindow *pMainWindow)
	: m_pMainWindow(pMainWindow)
{
}

bool CMainWindow::CFullscreen::CPanelEventHandler::OnClose()
{
	m_pMainWindow->SendCommand(CM_PANEL);
	return true;
}




bool CMainWindow::CBarLayout::IsSpot(const RECT *pArea,const POINT *pPos)
{
	RECT rcArea=*pArea,rcBar;

	Layout(&rcArea,&rcBar);
	return ::PtInRect(&rcBar,*pPos)!=FALSE;
}

void CMainWindow::CBarLayout::AdjustArea(RECT *pArea)
{
	RECT rcBar;
	Layout(pArea,&rcBar);
}

void CMainWindow::CBarLayout::ReserveArea(RECT *pArea,bool fNoMove)
{
	RECT rc;

	rc=*pArea;
	AdjustArea(&rc);
	if (fNoMove) {
		pArea->right+=(pArea->right-pArea->left)-(rc.right-rc.left);
		pArea->bottom+=(pArea->bottom-pArea->top)-(rc.bottom-rc.top);
	} else {
		pArea->left-=rc.left-pArea->left;
		pArea->top-=rc.top-pArea->top;
		pArea->right+=pArea->right-rc.right;
		pArea->bottom+=pArea->bottom-rc.bottom;
	}
}


CMainWindow::CTitleBarManager::CTitleBarManager(CMainWindow *pMainWindow,bool fMainWindow)
	: m_pMainWindow(pMainWindow)
	, m_fMainWindow(fMainWindow)
	, m_fFixed(false)
{
}

bool CMainWindow::CTitleBarManager::OnClose()
{
	m_pMainWindow->PostCommand(CM_CLOSE);
	return true;
}

bool CMainWindow::CTitleBarManager::OnMinimize()
{
	m_pMainWindow->SendMessage(WM_SYSCOMMAND,SC_MINIMIZE,0);
	return true;
}

bool CMainWindow::CTitleBarManager::OnMaximize()
{
	m_pMainWindow->SendMessage(WM_SYSCOMMAND,
		m_pMainWindow->GetMaximize()?SC_RESTORE:SC_MAXIMIZE,0);
	return true;
}

bool CMainWindow::CTitleBarManager::OnFullscreen()
{
	m_pMainWindow->m_pCore->ToggleFullscreen();
	return true;
}

void CMainWindow::CTitleBarManager::OnMouseLeave()
{
	if (!m_fFixed)
		m_pMainWindow->OnBarMouseLeave(m_pTitleBar->GetHandle());
}

void CMainWindow::CTitleBarManager::OnLabelLButtonDown(int x,int y)
{
	if (m_fMainWindow) {
		POINT pt;

		pt.x=x;
		pt.y=y;
		::ClientToScreen(m_pTitleBar->GetHandle(),&pt);
		m_pMainWindow->SendMessage(WM_NCLBUTTONDOWN,HTCAPTION,MAKELPARAM(pt.x,pt.y));
		m_fFixed=true;
	}
}

void CMainWindow::CTitleBarManager::OnLabelLButtonDoubleClick(int x,int y)
{
	if (m_fMainWindow)
		OnMaximize();
	else
		OnFullscreen();
}

void CMainWindow::CTitleBarManager::OnLabelRButtonDown(int x,int y)
{
	POINT pt;

	pt.x=x;
	pt.y=y;
	::ClientToScreen(m_pTitleBar->GetHandle(),&pt);
	ShowSystemMenu(pt.x,pt.y);
}

void CMainWindow::CTitleBarManager::OnIconLButtonDown(int x,int y)
{
	RECT rc;

	m_pTitleBar->GetScreenPosition(&rc);
	ShowSystemMenu(rc.left,rc.bottom);
}

void CMainWindow::CTitleBarManager::OnIconLButtonDoubleClick(int x,int y)
{
	m_pMainWindow->PostCommand(CM_CLOSE);
}

void CMainWindow::CTitleBarManager::Layout(RECT *pArea,RECT *pBarRect)
{
	pBarRect->left=pArea->left;
	pBarRect->top=pArea->top;
	pBarRect->right=pArea->right;
	pBarRect->bottom=pArea->top+m_pTitleBar->GetHeight();
	pArea->top+=m_pTitleBar->GetHeight();
}

void CMainWindow::CTitleBarManager::EndDrag()
{
	m_fFixed=false;
}

void CMainWindow::CTitleBarManager::ShowSystemMenu(int x,int y)
{
	m_fFixed=true;
	m_pMainWindow->SendMessage(0x0313,0,MAKELPARAM(x,y));
	m_fFixed=false;

	RECT rc;
	POINT pt;
	m_pTitleBar->GetScreenPosition(&rc);
	::GetCursorPos(&pt);
	if (!::PtInRect(&rc,pt))
		OnMouseLeave();
}


CMainWindow::CSideBarManager::CSideBarManager(CMainWindow *pMainWindow)
	: m_pMainWindow(pMainWindow)
	, m_fFixed(false)
{
}

void CMainWindow::CSideBarManager::Layout(RECT *pArea,RECT *pBarRect)
{
	const int BarWidth=m_pSideBar->GetBarWidth();

	switch (m_pMainWindow->m_App.SideBarOptions.GetPlace()) {
	case CSideBarOptions::PLACE_LEFT:
		pBarRect->left=pArea->left;
		pBarRect->top=pArea->top;
		pBarRect->right=pBarRect->left+BarWidth;
		pBarRect->bottom=pArea->bottom;
		pArea->left+=BarWidth;
		break;

	case CSideBarOptions::PLACE_RIGHT:
		pBarRect->left=pArea->right-BarWidth;
		pBarRect->top=pArea->top;
		pBarRect->right=pArea->right;
		pBarRect->bottom=pArea->bottom;
		pArea->right-=BarWidth;
		break;

	case CSideBarOptions::PLACE_TOP:
		pBarRect->left=pArea->left;
		pBarRect->top=pArea->top;
		pBarRect->right=pArea->right;
		pBarRect->bottom=pArea->top+BarWidth;
		pArea->top+=BarWidth;
		break;

	case CSideBarOptions::PLACE_BOTTOM:
		pBarRect->left=pArea->left;
		pBarRect->top=pArea->bottom-BarWidth;
		pBarRect->right=pArea->right;
		pBarRect->bottom=pArea->bottom;
		pArea->bottom-=BarWidth;
		break;
	}
}

const CChannelInfo *CMainWindow::CSideBarManager::GetChannelInfoByCommand(int Command)
{
	const CChannelList *pList=m_pMainWindow->m_App.ChannelManager.GetCurrentChannelList();
	if (pList!=nullptr) {
		int No=Command-CM_CHANNELNO_FIRST;
		int Index;

		if (pList->HasRemoteControlKeyID()) {
			Index=pList->FindChannelNo(No+1);
			if (Index<0)
				return nullptr;
		} else {
			Index=No;
		}
		return pList->GetChannelInfo(Index);
	}
	return nullptr;
}

void CMainWindow::CSideBarManager::OnCommand(int Command)
{
	m_pMainWindow->SendCommand(Command);
}

void CMainWindow::CSideBarManager::OnRButtonDown(int x,int y)
{
	CPopupMenu Menu(m_pMainWindow->m_App.GetResourceInstance(),IDM_SIDEBAR);
	POINT pt;

	Menu.CheckItem(CM_SIDEBAR,m_pMainWindow->GetSideBarVisible());
	Menu.EnableItem(CM_SIDEBAR,!m_pMainWindow->m_pCore->GetFullscreen());
	Menu.CheckRadioItem(CM_SIDEBAR_PLACE_FIRST,CM_SIDEBAR_PLACE_LAST,
						CM_SIDEBAR_PLACE_FIRST+(int)m_pMainWindow->m_App.SideBarOptions.GetPlace());
	pt.x=x;
	pt.y=y;
	::ClientToScreen(m_pSideBar->GetHandle(),&pt);
	m_fFixed=true;
	Menu.Show(m_pMainWindow->GetHandle(),&pt);
	m_fFixed=false;

	RECT rc;
	m_pSideBar->GetScreenPosition(&rc);
	::GetCursorPos(&pt);
	if (!::PtInRect(&rc,pt))
		OnMouseLeave();
}

void CMainWindow::CSideBarManager::OnMouseLeave()
{
	if (!m_fFixed)
		m_pMainWindow->OnBarMouseLeave(m_pSideBar->GetHandle());
}

bool CMainWindow::CSideBarManager::GetTooltipText(int Command,LPTSTR pszText,int MaxText)
{
	if (Command>=CM_CHANNELNO_FIRST && Command<=CM_CHANNELNO_LAST
#ifdef NETWORK_REMOCON_SUPPORT
			&& m_pMainWindow->m_App.pNetworkRemocon==nullptr)
#endif
	{
		const CChannelInfo *pChInfo=GetChannelInfoByCommand(Command);
		if (pChInfo!=nullptr) {
			StdUtil::snprintf(pszText,MaxText,TEXT("%d: %s"),
							  (Command-CM_CHANNELNO_FIRST)+1,pChInfo->GetName());
			return true;
		}
	}
	return false;
}

bool CMainWindow::CSideBarManager::DrawIcon(
	int Command,HDC hdc,const RECT &ItemRect,COLORREF ForeColor,HDC hdcBuffer)
{
	if (Command>=CM_CHANNELNO_FIRST && Command<=CM_CHANNELNO_LAST
			&& m_pMainWindow->m_App.SideBarOptions.GetShowChannelLogo()
#ifdef NETWORK_REMOCON_SUPPORT
			&& m_pMainWindow->m_App.pNetworkRemocon==nullptr)
#endif
	{
		// �A�C�R���ɋǃ��S��\��
		// TODO: �V�������S���擾���ꂽ���ɍĕ`�悷��
		const CChannelInfo *pChannel=GetChannelInfoByCommand(Command);
		if (pChannel!=nullptr) {
			HBITMAP hbmLogo=m_pMainWindow->m_App.LogoManager.GetAssociatedLogoBitmap(
				pChannel->GetNetworkID(),pChannel->GetServiceID(),
				CLogoManager::LOGOTYPE_SMALL);
			if (hbmLogo!=nullptr) {
				const int Width=ItemRect.right-ItemRect.left;
				const int Height=ItemRect.bottom-ItemRect.top;
				const int IconHeight=Height*10/16;	// �{���̔䗦���c���ɂ��Ă���(���h���̂���)
				HBITMAP hbmOld=SelectBitmap(hdcBuffer,hbmLogo);
				int OldStretchMode=::SetStretchBltMode(hdc,STRETCH_HALFTONE);
				BITMAP bm;
				::GetObject(hbmLogo,sizeof(bm),&bm);
				::StretchBlt(hdc,ItemRect.left,ItemRect.top+(Height-IconHeight)/2,Width,IconHeight,
							 hdcBuffer,0,0,bm.bmWidth,bm.bmHeight,SRCCOPY);
				::SetStretchBltMode(hdc,OldStretchMode);
				::SelectObject(hdcBuffer,hbmOld);
				return true;
			}
		}
	}
	return false;
}


CMainWindow::CStatusViewEventHandler::CStatusViewEventHandler(CMainWindow *pMainWindow)
	: m_pMainWindow(pMainWindow)
{
}

void CMainWindow::CStatusViewEventHandler::OnMouseLeave()
{
	m_pMainWindow->OnBarMouseLeave(m_pStatusView->GetHandle());
}

void CMainWindow::CStatusViewEventHandler::OnHeightChanged(int Height)
{
	Layout::CWindowContainer *pContainer=
		dynamic_cast<Layout::CWindowContainer*>(m_pMainWindow->GetLayoutBase().GetContainerByID(CONTAINER_ID_STATUS));
	Layout::CSplitter *pSplitter=
		dynamic_cast<Layout::CSplitter*>(m_pMainWindow->GetLayoutBase().GetContainerByID(CONTAINER_ID_STATUSSPLITTER));

	if (pContainer!=nullptr)
		pContainer->SetMinSize(0,Height);
	if (pSplitter!=nullptr)
		pSplitter->SetPaneSize(CONTAINER_ID_STATUS,Height);
}


CMainWindow::CVideoContainerEventHandler::CVideoContainerEventHandler(CMainWindow *pMainWindow)
	: m_pMainWindow(pMainWindow)
{
}

void CMainWindow::CVideoContainerEventHandler::OnSizeChanged(int Width,int Height)
{
	CNotificationBar &NotificationBar=m_pMainWindow->m_NotificationBar;
	if (NotificationBar.GetVisible()) {
		RECT rc,rcView;

		NotificationBar.GetPosition(&rc);
		::GetClientRect(NotificationBar.GetParent(),&rcView);
		rc.left=rcView.left;
		rc.right=rcView.right;
		NotificationBar.SetPosition(&rc);
	}

	m_pMainWindow->m_App.OSDManager.HideVolumeOSD();
}


CMainWindow::CViewWindowEventHandler::CViewWindowEventHandler(CMainWindow *pMainWindow)
	: m_pMainWindow(pMainWindow)
{
}

void CMainWindow::CViewWindowEventHandler::OnSizeChanged(int Width,int Height)
{
	// �ꎞ�I�ɕ\������Ă���o�[�̃T�C�Y�����킹��
	RECT rcView,rc;

	m_pView->GetPosition(&rcView);
	if (!m_pMainWindow->GetTitleBarVisible()
			&& m_pMainWindow->m_TitleBar.GetVisible()) {
		m_pMainWindow->m_TitleBarManager.Layout(&rcView,&rc);
		m_pMainWindow->m_TitleBar.SetPosition(&rc);
	}
	CStatusView &StatusView=m_pMainWindow->GetStatusView();
	if (!m_pMainWindow->GetStatusBarVisible()
			&& StatusView.GetVisible()
			&& StatusView.GetParent()==m_pView->GetParent()) {
		rc=rcView;
		rc.top=rc.bottom-StatusView.GetHeight();
		rcView.bottom-=StatusView.GetHeight();
		StatusView.SetPosition(&rc);
	}
	CSideBar &SideBar=m_pMainWindow->GetSideBar();
	if (!m_pMainWindow->GetSideBarVisible()
			&& SideBar.GetVisible()) {
		m_pMainWindow->m_SideBarManager.Layout(&rcView,&rc);
		SideBar.SetPosition(&rc);
	}
}


CMainWindow::CDisplayBaseEventHandler::CDisplayBaseEventHandler(CMainWindow *pMainWindow)
	: m_pMainWindow(pMainWindow)
{
}

bool CMainWindow::CDisplayBaseEventHandler::OnVisibleChange(bool fVisible)
{
	if (!m_pMainWindow->IsViewerEnabled()) {
		m_pMainWindow->m_Viewer.GetVideoContainer().SetVisible(fVisible);
	}
	if (fVisible && m_pMainWindow->m_pCore->GetFullscreen()) {
		m_pMainWindow->m_Fullscreen.HideAllBars();
	}
	return true;
}