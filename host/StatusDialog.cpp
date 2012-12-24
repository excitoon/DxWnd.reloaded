// StatusDialog.cpp : implementation file
//

#include "stdafx.h"
#include "dxwndhost.h"
#include "StatusDialog.h"

// CStatusDialog dialog

IMPLEMENT_DYNAMIC(CStatusDialog, CDialog)

CStatusDialog::CStatusDialog(CWnd* pParent /*=NULL*/)
	: CDialog(CStatusDialog::IDD, pParent)
{
}

CStatusDialog::~CStatusDialog()
{
}

void CStatusDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}


BEGIN_MESSAGE_MAP(CStatusDialog, CDialog)
	ON_WM_TIMER()
END_MESSAGE_MAP()

#define IDTIMER 1

// CStatusDialog message handlers

void CStatusDialog::OnTimer(UINT_PTR nIDEvent)
{
	int DxStatus;
	int IconId;
	LPCSTR Status;
	char sMsg[1024];
	char DllVersion[21];
	DXWNDSTATUS DxWndStatus;
	extern PRIVATEMAP *pTitles; 

	CDialog::OnTimer(nIDEvent);
	DxStatus=GetHookStatus(NULL);
	switch (DxStatus){
		case DXW_IDLE: IconId=IDI_DXIDLE; Status="DISABLED"; break;
		case DXW_ACTIVE: IconId=IDI_DXWAIT; Status="READY"; break;
		case DXW_RUNNING: IconId=IDI_DXRUN; Status="RUNNING"; break;
	}

	GetDllVersion(DllVersion);
	DxWndStatus.Status=DxStatus;
	if(DxStatus==DXW_RUNNING){
		GetHookStatus(&DxWndStatus);
		sprintf_s(sMsg, 1024, 
			"DxWnd %s\nHook status: %s\n"
			"Running \"%s\"\nScreen = (%dx%d) %dBPP\n"
			"FullScreen = %s\nDX version = %d\n"
			"Logging = %s",
		DllVersion, Status,
		pTitles[DxWndStatus.TaskIdx].title,
		DxWndStatus.Width, DxWndStatus.Height, DxWndStatus.ColorDepth, 
		DxWndStatus.IsFullScreen ? "Yes":"No", DxWndStatus.DXVersion,
		DxWndStatus.isLogging?"ON":"OFF");   
	}
	else
		sprintf_s(sMsg, 1024, "DxWnd %s\nHook status: %s", DllVersion, Status);

	this->SetDlgItemTextA(IDC_STATUSINFO, sMsg);
}

BOOL CStatusDialog::OnInitDialog()
{
	CDialog::OnInitDialog();

	SetTimer(IDTIMER, 1000, NULL);

	// TODO:  Add extra initialization here

	return TRUE;  // return TRUE unless you set the focus to a control
	// EXCEPTION: OCX Property Pages should return FALSE
}

void CStatusDialog::OnOK()
{
	// TODO: Add your specialized code here and/or call the base class

	KillTimer(IDTIMER);
	// stop timer

	CDialog::OnOK();
}