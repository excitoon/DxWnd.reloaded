#define _WIN32_WINNT 0x0400
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <malloc.h>
#include <ddraw.h>
#include "dxwnd.h"
#include "dxhook.h"
#include "glhook.h"
#include "syslibs.h"
#include "dxhelper.h"
#include "hddraw.h"
#include "hddproxy.h"

#define WINDOWDC 0xFFFFFFFF

extern GDIGetDC_Type pGDIGetDC;
extern GDIGetDC_Type pGDIGetWindowDC;
extern GDIReleaseDC_Type pGDIReleaseDC;
extern DWORD PaletteEntries[256];
extern HDC PrimSurfaceHDC;
extern LPDIRECTDRAWSURFACE lpDDSPrimHDC;
extern LPDIRECTDRAW lpDD;
extern POINT FixCursorPos(HWND, POINT);
extern Unlock4_Type pUnlockMethod(LPDIRECTDRAWSURFACE);

extern GetDC_Type pGetDC;
extern ReleaseDC_Type pReleaseDC;
extern SetCursor_Type pSetCursor;

DEVMODE SetDevMode;
DEVMODE *pSetDevMode=NULL;

extern short iPosX, iPosY, iSizX, iSizY;
extern void do_slow(int);
extern LPDIRECTDRAWSURFACE GetPrimarySurface(void);
extern HRESULT WINAPI extBlt(LPDIRECTDRAWSURFACE, LPRECT, LPDIRECTDRAWSURFACE, LPRECT, DWORD, LPDDBLTFX);
extern HRESULT WINAPI sBlt(char *, LPDIRECTDRAWSURFACE, LPRECT, LPDIRECTDRAWSURFACE, LPRECT, DWORD, LPDDBLTFX, BOOL);
extern BOOL isFullScreen;

static SetLayeredWindowAttributes_Type pSetLayeredWindowAttributes = NULL;
#ifndef WS_EX_LAYERED
#define WS_EX_LAYERED           0x00080000    
#define LWA_COLORKEY            0x00000001
#define LWA_ALPHA               0x00000002
#endif
// Diablo: end
//extern char *ExplainShowCmd(int);

extern DirectDrawEnumerate_Type pDirectDrawEnumerate;
extern DirectDrawEnumerateEx_Type pDirectDrawEnumerateEx;

extern LRESULT CALLBACK extChildWindowProc(HWND, UINT, WPARAM, LPARAM);
extern INT_PTR CALLBACK extDialogWindowProc(HWND, UINT, WPARAM, LPARAM);

GDIChoosePixelFormat_Type pGDIChoosePixelFormat;
GDIGetPixelFormat_Type pGDIGetPixelFormat;
GDISetPixelFormat_Type pGDISetPixelFormat;
CreateDC_Type pCreateDC;
StretchBlt_Type pStretchBlt;
SaveDC_Type pGDISaveDC;
RestoreDC_Type pGDIRestoreDC;
CreateDialogParam_Type pCreateDialogParam;
CreateDialogIndirectParam_Type pCreateDialogIndirectParam;
BeginPaint_Type pBeginPaint;
EndPaint_Type pEndPaint;
InvalidateRect_Type pInvalidateRect;
InvalidateRgn_Type pInvalidateRgn;
GDICreatePalette_Type pGDICreatePalette;
SelectPalette_Type pSelectPalette;
RealizePalette_Type pRealizePalette;
GetSystemPaletteEntries_Type pGetSystemPaletteEntries;
MoveWindow_Type pMoveWindow;
SetUnhandledExceptionFilter_Type pSetUnhandledExceptionFilter;
GetDiskFreeSpaceA_Type pGetDiskFreeSpaceA;

/* ------------------------------------------------------------------ */

static POINT FixMessagePt(HWND hwnd, POINT point)
{
	RECT rect;
	static POINT curr;
	curr=point;

	if(!(*pScreenToClient)(hwnd, &curr)){
		OutTraceE("ScreenToClient ERROR=%d hwnd=%x at %d\n", GetLastError(), hwnd, __LINE__);
		curr.x = curr.y = 0;
	}

	if (!(*pGetClientRect)(hwnd, &rect)) {
		OutTraceE("GetClientRect ERROR=%d hwnd=%x at %d\n", GetLastError(), hwnd, __LINE__);
		curr.x = curr.y = 0;
	}

#ifdef ISDEBUG
	if(IsDebug) OutTrace("FixMessagePt point=(%d,%d) hwnd=%x win pos=(%d,%d) size=(%d,%d)\n",
		point.x, point.y, hwnd, point.x-curr.x, point.y-curr.y, rect.right, rect.bottom);
#endif

	if (curr.x < 0) curr.x=0;
	if (curr.y < 0) curr.y=0;
	if (curr.x > rect.right) curr.x=rect.right;
	if (curr.y > rect.bottom) curr.y=rect.bottom;
	if (rect.right)  curr.x = (curr.x * VirtualScr.dwWidth) / rect.right;
	if (rect.bottom) curr.y = (curr.y * VirtualScr.dwHeight) / rect.bottom;

	return curr;
}

/* ------------------------------------------------------------------ */

static COLORREF GetMatchingColor(COLORREF crColor)
{
	int iDistance, iMinDistance;
	int iColorIndex, iMinColorIndex;
	COLORREF PalColor;

	iMinDistance=0xFFFFFF;
	iMinColorIndex=0;

	for(iColorIndex=0; iColorIndex<256; iColorIndex++){
		int iDist;
		iDistance=0;

		PalColor=PaletteEntries[iColorIndex];
		switch(ActualScr.PixelFormat.dwRGBBitCount){
		case 32:
			PalColor = ((PalColor & 0x00FF0000) >> 16) | (PalColor & 0x0000FF00) | ((PalColor & 0x000000FF) << 16);
			break;
		case 16:
			if(ActualScr.PixelFormat.dwGBitMask==0x03E0){
				// RGB555 screen settings
				PalColor = ((PalColor & 0x7C00) >> 7) | ((PalColor & 0x03E0) << 6) | ((PalColor & 0x001F) << 19);
			}
			else {
				// RGB565 screen settings
				PalColor = ((PalColor & 0xF800) >> 8) | ((PalColor & 0x07E0) << 5) | ((PalColor & 0x001F) << 19);
			}
			break;
		}

		iDist = (crColor & 0x00FF0000) - (PalColor & 0x00FF0000);
		iDist >>= 16;
		if (iDist<0) iDist=-iDist;
		iDist *= iDist;
		iDistance += iDist;

		iDist = (crColor & 0x0000FF00) - (PalColor & 0x0000FF00);
		iDist >>= 8;
		if (iDist<0) iDist=-iDist;
		iDist *= iDist;
		iDistance += iDist;

		iDist = (crColor & 0x000000FF) - (PalColor & 0x000000FF);
		// iDist >>= 0;
		if (iDist<0) iDist=-iDist;
		iDist *= iDist;
		iDistance += iDist;

		if (iDistance < iMinDistance) {
			iMinDistance = iDistance;
			iMinColorIndex = iColorIndex;
		}

		if (iMinDistance==0) break; // got the perfect match!
	}
	OutTraceD("GetMatchingColor: color=%x matched with palette[%d]=%x dist=%d\n", 
		crColor, iMinColorIndex, PaletteEntries[iMinColorIndex], iDistance);
	PalColor=PaletteEntries[iMinColorIndex];
	switch(ActualScr.PixelFormat.dwRGBBitCount){
	case 32:
		crColor = ((PalColor & 0x00FF0000) >> 16) | (PalColor & 0x0000FF00) | ((PalColor & 0x000000FF) << 16); 
		break;
	case 16:
		if(ActualScr.PixelFormat.dwGBitMask==0x03E0){
			// RGB555 screen settings
			crColor = ((PalColor & 0x7C00) >> 7) | ((PalColor & 0x03E0) << 6) | ((PalColor & 0x001F) << 19);
		}
		else {
			// RGB565 screen settings
			crColor = ((PalColor & 0xF800) >> 8) | ((PalColor & 0x07E0) << 5) | ((PalColor & 0x001F) << 19);
		}
		break;
	}
	return crColor;
}

extern void FixWindowFrame(HWND);

// GHO: pro Diablo
HWND WINAPI extCreateWindowExA(
  DWORD dwExStyle,
  LPCTSTR lpClassName,
  LPCTSTR lpWindowName,
  DWORD dwStyle,
  int x,
  int y,
  int nWidth,
  int nHeight,
  HWND hWndParent,
  HMENU hMenu,
  HINSTANCE hInstance,
  LPVOID lpParam) 
{
	HWND wndh;
	WNDPROC pWindowProc;
	BOOL isValidHandle=TRUE;
	extern BOOL isFullScreen;

	OutTraceD("CreateWindowEx: class=\"%s\" wname=\"%s\" pos=(%d,%d) size=(%d,%d) Style=%x(%s) ExStyle=%x(%s)\n",
		lpClassName, lpWindowName, x, y, nWidth, nHeight, 
		dwStyle, ExplainStyle(dwStyle), dwExStyle, ExplainExStyle(dwExStyle));
	if(IsDebug) OutTrace("CreateWindowEx: DEBUG screen=(%d,%d)\n", VirtualScr.dwWidth, VirtualScr.dwHeight);

	// no maximized windows in any case
	if (dwFlags & PREVENTMAXIMIZE){
		OutTraceD("CreateWindowEx: handling PREVENTMAXIMIZE mode\n");
		dwStyle &= ~(WS_MAXIMIZE | WS_POPUP);
		dwExStyle &= ~WS_EX_TOPMOST;
	}

	// v2.1.92: fixes size & position for auxiliary big window, often used
	// for intro movies etc. : needed for ......
	// evidently, this was supposed to be a fullscreen window....
	// v2.1.100: fixes for "The Grinch": this game creates a new main window for OpenGL
	// rendering using CW_USEDEFAULT placement and 800x600 size while the previous
	// main win was 640x480 only!
	if	(
			(
				((x==0)&&(y==0)) || ((x==CW_USEDEFAULT)&&(y==CW_USEDEFAULT))
			)
		&&
			(((DWORD)nWidth>=VirtualScr.dwWidth)&&((DWORD)nHeight>=VirtualScr.dwHeight))
		){
		RECT screen;
		POINT upleft = {0,0};
		// update virtual screen size if it has grown 
		VirtualScr.dwWidth=nWidth;
		VirtualScr.dwHeight=nHeight;
		// inserted some checks here, since the main window could be destroyed
		// or minimized (see "Jedi Outcast") so that you may get a dangerous 
		// zero size. In this case, better renew the hWnd assignement and its coordinates.
		do { // fake loop
			isValidHandle = FALSE;
			if (!(*pGetClientRect)(hWnd,&screen)) break;
			if (!(*pClientToScreen)(hWnd,&upleft)) break;
			if (screen.right==0 || screen.bottom==0) break;
			isValidHandle = TRUE;
		} while(FALSE);
		if (isValidHandle){
			x=upleft.x;
			y=upleft.y;
			nWidth=screen.right;
			nHeight=screen.bottom;
			OutTraceD("CreateWindowEx: fixed BIG win pos=(%d,%d) size=(%d,%d)\n", x, y, nWidth, nHeight);
		}
		else {
			// invalid parent coordinates: use initial placement, but leave the size.
			// should also fix the window style and compensate for borders here?
			x=iPosX;
			y=iPosY;
			nWidth=iSizX;
			nHeight=iSizY;
			OutTraceD("CreateWindowEx: renewed BIG win pos=(%d,%d) size=(%d,%d)\n", x, y, nWidth, nHeight);
		}
		isFullScreen = TRUE;
	}

	if(!isFullScreen){ // v2.1.63: needed for "Monster Truck Madness"
		wndh= (*pCreateWindowExA)(dwExStyle, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight, 
			hWndParent, hMenu, hInstance, lpParam);
		OutTraceD("CreateWindowEx: windowed mode ret=%x\n", wndh);
		return wndh;
	}

	// tested on Gangsters: coordinates must be window-relative!!!
	// Age of Empires....
	if (dwStyle & WS_CHILD){ 
		RECT screen;
		(*pGetClientRect)(hWnd,&screen);
		x=x*screen.right/VirtualScr.dwWidth;
		y=y*screen.bottom/VirtualScr.dwHeight;
		nWidth=nWidth*screen.right/VirtualScr.dwWidth;
		nHeight=nHeight*screen.bottom/VirtualScr.dwHeight;
		OutTraceD("CreateWindowEx: fixed WS_CHILD pos=(%d,%d) size=(%d,%d)\n",
			x, y, nWidth, nHeight);
	}
	// needed for Diablo, that creates a new control parent window that must be
	// overlapped to the directdraw surface.
	else if (dwExStyle & WS_EX_CONTROLPARENT){
		RECT screen;
		POINT upleft = {0,0};
		(*pGetClientRect)(hWnd,&screen);
		(*pClientToScreen)(hWnd,&upleft);
		x=upleft.x;
		y=upleft.y;
		nWidth=screen.right;
		nHeight=screen.bottom;
		OutTraceD("CreateWindowEx: fixed WS_EX_CONTROLPARENT win=(%d,%d)-(%d,%d)\n",
			x, y, x+nWidth, y+nHeight);
	}

	if(IsDebug) 
		OutTrace("CreateWindowEx: fixed pos=(%d,%d) size=(%d,%d) Style=%x(%s) ExStyle=%x(%s)\n",
		x, y, nWidth, nHeight, dwStyle, ExplainStyle(dwStyle), dwExStyle, ExplainExStyle(dwExStyle));

	wndh= (*pCreateWindowExA)(dwExStyle, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight, 
		hWndParent, hMenu, hInstance, lpParam);
	if (wndh==(HWND)NULL){
		OutTraceE("CreateWindowEx: ERROR err=%d Style=%x(%s) ExStyle=%x\n",
			GetLastError(), dwStyle, ExplainStyle(dwStyle), dwExStyle);
		return wndh;
	}

	if ((!isValidHandle) && isFullScreen) {
		hWnd = wndh;
		extern void AdjustWindowPos(HWND, DWORD, DWORD);
		(*pSetWindowLong)(wndh, GWL_STYLE, (dwFlags2 & MODALSTYLE) ? 0 : WS_OVERLAPPEDWINDOW);
		(*pSetWindowLong)(wndh, GWL_EXSTYLE, 0); 
		//(*pShowWindow)(wndh, SW_SHOWNORMAL);
		OutTraceD("CreateWindow: hWnd=%x, set style=WS_OVERLAPPEDWINDOW extstyle=0\n", wndh); 
		AdjustWindowPos(wndh, nWidth, nHeight);
		(*pShowWindow)(wndh, SW_SHOWNORMAL);
	}

	if ((dwFlags & FIXWINFRAME) && !(dwStyle & WS_CHILD))
		FixWindowFrame(wndh);

	// to do: handle inner child, and leave dialogue & modal child alone!!!
	if (dwStyle & WS_CHILD){
		long res;
		pWindowProc = (WNDPROC)(*pGetWindowLong)(wndh, GWL_WNDPROC);
		OutTraceD("Hooking CHILD wndh=%x WindowProc %x->%x\n", wndh, pWindowProc, extChildWindowProc);
		res=(*pSetWindowLong)(wndh, GWL_WNDPROC, (LONG)extChildWindowProc);
		WhndStackPush(wndh, pWindowProc);
		if(!res) OutTraceE("CreateWindowExA: SetWindowLong ERROR %x\n", GetLastError());
	}

	OutTraceD("CreateWindowEx: ret=%x\n", wndh);
	return wndh;
}

COLORREF WINAPI extSetTextColor(HDC hdc, COLORREF crColor)
{
	COLORREF res;

	if ((dwFlags & EMULATESURFACE) && (dwFlags & HANDLEDC) && (VirtualScr.PixelFormat.dwRGBBitCount==8))
		crColor=GetMatchingColor(crColor);

	res=(*pSetTextColor)(hdc, crColor);
	OutTraceD("SetTextColor: color=%x res=%x%s\n", crColor, res, (res==CLR_INVALID)?"(CLR_INVALID)":"");
	return res;
}

COLORREF WINAPI extSetBkColor(HDC hdc, COLORREF crColor)
{
	COLORREF res;

	if ((dwFlags & EMULATESURFACE) && (dwFlags & HANDLEDC) && (VirtualScr.PixelFormat.dwRGBBitCount==8))
		crColor=GetMatchingColor(crColor);

	res=(*pSetBkColor)(hdc, crColor);
	OutTraceD("SetBkColor: color=%x res=%x%s\n", crColor, res, (res==CLR_INVALID)?"(CLR_INVALID)":"");
	return res;
}

LPRECT lpClipRegion=NULL;
RECT ClipRegion;

BOOL WINAPI extClipCursor(RECT *lpRectArg)
{
	// reference: hooking and setting ClipCursor is mandatori in "Emergency: Fighters for Life"
	// where the application expects the cursor to be moved just in a inner rect within the 
	// main window surface.

	BOOL res;
	RECT *lpRect;
	RECT Rect;

	if(IsTraceC){
		if (lpRectArg)
			OutTrace("ClipCursor: rect=(%d,%d)-(%d,%d)\n", 
				lpRectArg->left,lpRectArg->top,lpRectArg->right,lpRectArg->bottom);
		else 
			OutTrace("ClipCursor: rect=(NULL)\n");
	}

 	if (!(dwFlags & ENABLECLIPPING)) return 1;

	if(lpRectArg){
		Rect=*lpRectArg;
		lpRect=&Rect;
	}
	else
		lpRect=NULL;

	if(dwFlags & MODIFYMOUSE){
		RECT rect;
		POINT corner;
		RECT ClientArea;

		// beware: computations for RECT coordinates: the range is 1 pixel wider
		// than accettable coordinates for cursor positions ???

		// find window metrics
		if (!(*pGetClientRect)(hWnd, &rect)){
			OutTraceE("GetClientRect ERROR %d at %d\n", GetLastError(), __LINE__);
			return 0;
		}
		corner.x=corner.y=0;
		if (!(*pClientToScreen)(hWnd, &corner)){
			OutTraceE("ClientToScreen ERROR %d at %d\n", GetLastError(), __LINE__);
			return 0;
		}		
		if (lpRect) {
			// save desired clip region
			ClipRegion=*lpRectArg;
			lpClipRegion=&ClipRegion;
			// need no scaling for upper left, since they're set to 0.
			lpRect->left = corner.x + (lpRect->left * rect.right) / VirtualScr.dwWidth;
			lpRect->right = corner.x + (lpRect->right * rect.right) / VirtualScr.dwWidth;
			lpRect->top = corner.y + (lpRect->top * rect.bottom) / VirtualScr.dwHeight;
			lpRect->bottom = corner.y + (lpRect->bottom * rect.bottom) / VirtualScr.dwHeight;
		}
		else {
			// NULL -> whole screen -> whole client area!!!
			// save desired clip region
			lpClipRegion=NULL;
			// map space for null ptr
			lpRect=&ClientArea;
			lpRect->left = corner.x;
			lpRect->right = corner.x + rect.right;
			lpRect->top = corner.y;
			lpRect->bottom = corner.y + rect.bottom;
		}
	}

	if (pClipCursor) res=(*pClipCursor)(lpRect);
	OutTraceD("ClipCursor: rect=(%d,%d)-(%d,%d) res=%x\n", 
		lpRect->left,lpRect->top,lpRect->right,lpRect->bottom, res);

	return TRUE;
}

BOOL WINAPI extGetClipCursor(LPRECT lpRect)
{
	// v2.1.93: if ENABLECLIPPING, return the saved clip rect coordinates

	BOOL ret;

	// proxy....
	if (!(dwFlags & ENABLECLIPPING)) {
		ret=(*pGetClipCursor)(lpRect);
		if(IsTraceD){
			if (lpRect)
				OutTrace("ClipCursor: PROXED rect=(%d,%d)-(%d,%d) ret=%d\n", 
					lpRect->left,lpRect->top,lpRect->right,lpRect->bottom, ret);
			else 
				OutTrace("ClipCursor: PROXED rect=(NULL) ret=%d\n", ret);
		}		
		return ret;
	}

	if(lpRect){
		if(lpClipRegion)
			*lpRect=ClipRegion;
		else{
			lpRect->top = lpRect->left = 0;
			lpRect->right = VirtualScr.dwWidth;
			lpRect->bottom = VirtualScr.dwHeight;
		}
		OutTraceD("ClipCursor: rect=(%d,%d)-(%d,%d) ret=%d\n", 
			lpRect->left,lpRect->top,lpRect->right,lpRect->bottom, TRUE);
	}

	return TRUE;
}

int LastCurPosX, LastCurPosY;

BOOL WINAPI extGetCursorPos(LPPOINT lppoint)
{
	HRESULT res;
	static int PrevX, PrevY;

	if(dwFlags & SLOWDOWN) do_slow(2);

	//if(dwFlags2 & KEEPCURSORFIXED) {
	//	lppoint->x=LastCurPosX;
	//	lppoint->y=LastCurPosY;
	//	return 1;
	//}

	if (pGetCursorPos) {
		res=(*pGetCursorPos)(lppoint);
	}
	else {
		lppoint->x =0; lppoint->y=0;
		res=1;
	}

	if(dwFlags2 & DIFFERENTIALMOUSE){
		int NewX, NewY;
		RECT client;
		POINT corner={0,0};

		// get win placement
		(*pGetClientRect)(hWnd, &client);
		(*pClientToScreen)(hWnd, &corner);
		NewX = lppoint->x - PrevX + LastCurPosX;
		NewY = lppoint->y - PrevY + LastCurPosY;
		// handle virtual clipping
		if(lppoint->x <= corner.x) NewX--;
		if(lppoint->x >= corner.x+client.right-1) NewX++;
		if(lppoint->y <= corner.y) NewY--;
		if(lppoint->y >= corner.y+client.bottom-1) NewY++;

		// swap coordinates...
		PrevX=lppoint->x;
		PrevY=lppoint->y;
		lppoint->x = NewX;
		lppoint->y = NewY;
		OutTraceC("GetCursorPos: DIFF pos=(%d,%d) delta=(%d,%d)->(%d,%d)\n", 
			PrevX, PrevY, LastCurPosX, LastCurPosY, NewX, NewY);
		return TRUE;
	}


	PrevX=lppoint->x;
	PrevY=lppoint->y;

	if(dwFlags & MODIFYMOUSE){
		RECT rect;

		// find window metrics
		if (!(*pGetClientRect)(hWnd, &rect)){
			OutTraceE("GetClientRect(%x) ERROR %d at %d\n", hWnd, GetLastError(), __LINE__);
			lppoint->x =0; lppoint->y=0;
			return TRUE;
		}

		// convert absolute screen coordinates to frame relative
		if (!(*pScreenToClient)(hWnd, lppoint)) {
			OutTraceE("ScreenToClient(%x) ERROR %d at %d\n", hWnd, GetLastError(), __LINE__);
			lppoint->x =0; lppoint->y=0;
			return TRUE;
		}

		// divide by zero check
		if (rect.right==0 || rect.bottom==0){
			OutTraceC("avoiding divide by zero for (x,y)=(%d,%d) at %d\n", rect.right, rect.bottom, __LINE__);
			lppoint->x=0; lppoint->y=0;
			return TRUE;
		}

		// ensure you stay within borders		
		// and avoid trusting arithmetic operations on negative integers!!!
		if (lppoint->x < 0) lppoint->x=0;
		if (lppoint->y < 0) lppoint->y=0;
		if (lppoint->x > rect.right) lppoint->x=rect.right;
		if (lppoint->y > rect.bottom) lppoint->y=rect.bottom;

		lppoint->x = (lppoint->x * VirtualScr.dwWidth) / rect.right;
		lppoint->y = (lppoint->y * VirtualScr.dwHeight) / rect.bottom;

		OutTraceC("GetCursorPos: hwnd=%x res=%x XY=(%d,%d)->(%d,%d)\n", 
			hWnd, res, PrevX, PrevY, lppoint->x, lppoint->y);
	}
	
	return res;
}

BOOL WINAPI extSetCursorPos(int x, int y)
{
	BOOL res;
	int PrevX, PrevY;

	PrevX=x;
	PrevY=y;

	if(dwFlags2 & KEEPCURSORFIXED) {
		OutTraceC("SetCursorPos: FIXED pos=(%d,%d)\n", x, y);
		LastCurPosX=x;
		LastCurPosY=y;
		return 1;
	}

	if(dwFlags & SLOWDOWN) do_slow(2);

	if(dwFlags & KEEPCURSORWITHIN){
		// Intercept SetCursorPos outside screen boundaries (used as Cursor OFF in some games)
		if ((y<0)||(y>=(int)VirtualScr.dwHeight)||(x<0)||(x>=(int)VirtualScr.dwWidth)) return 1;
	}

	if(dwFlags & MODIFYMOUSE){
		POINT cur;
		RECT rect;

		// find window metrics
		if (!(*pGetClientRect)(hWnd, &rect)) {
			// report error and ignore ...
			OutTraceE("GetClientRect(%x) ERROR %d at %d\n", hWnd, GetLastError(), __LINE__);
			return 0;
		}

		x= x * rect.right / VirtualScr.dwWidth;
		y= y * rect.bottom / VirtualScr.dwHeight;

		// check for boundaries (???)
		if (x >= rect.right) x=rect.right-1;
		if (x<0) x=0;
		if (y >= rect.bottom) y=rect.bottom-1;
		if (y<0) y=0;

		// make it screen absolute
		cur.x = x;
		cur.y = y;
		if (!(*pClientToScreen)(hWnd, &cur)) {
			OutTraceE("ClientToScreen(%x) ERROR %d at %d\n", hWnd, GetLastError(), __LINE__);
			return 0;
		}
		x = cur.x;
		y = cur.y;
	}

	res=0;
	if (pSetCursorPos) res=(*pSetCursorPos)(x,y);

	OutTraceC("SetCursorPos: res=%x XY=(%d,%d)->(%d,%d)\n",res, PrevX, PrevY, x, y);
	return res;
}

BOOL WINAPI extTextOutA(HDC hdc, int nXStart, int nYStart, LPCTSTR lpString, int cchString)
{
	OutTraceD("TextOut: hdc=%x xy=(%d,%d) str=(%d)\"%s\"\n", hdc, nXStart, nYStart, cchString, lpString);
	if (dwFlags & FIXTEXTOUT) {
		POINT anchor;
		anchor.x=nXStart;
		anchor.y=nYStart;
		(*pClientToScreen)(hWnd, &anchor);
		nXStart=anchor.x;
		nYStart=anchor.y;
	}
	return (*pTextOutA)(hdc, nXStart, nYStart, lpString, cchString);
}

BOOL WINAPI extRectangle(HDC hdc, int nLeftRect, int nTopRect, int nRightRect, int nBottomRect)
{
	OutTraceD("Rectangle: hdc=%x xy=(%d,%d)-(%d,%d)\n", hdc, nLeftRect, nTopRect, nRightRect, nBottomRect);
	if (dwFlags & FIXTEXTOUT) {
		POINT anchor;
		anchor.x=nLeftRect;
		anchor.y=nTopRect;
		(*pClientToScreen)(hWnd, &anchor);
		nLeftRect=anchor.x;
		nTopRect=anchor.y;
		anchor.x=nRightRect;
		anchor.y=nBottomRect;
		(*pClientToScreen)(hWnd, &anchor);
		nRightRect=anchor.x;
		nBottomRect=anchor.y;
	}
	return (*pRectangle)(hdc, nLeftRect, nTopRect, nRightRect, nBottomRect);
}

int WINAPI extFillRect(HDC hdc, const RECT *lprc, HBRUSH hbr)
{
	RECT rc;
	OutTraceD("FillRect: hdc=%x xy=(%d,%d)-(%d,%d)\n", hdc, lprc->left, lprc->top, lprc->right, lprc->bottom);
	memcpy(&rc, lprc, sizeof(rc));
	if (dwFlags & FIXTEXTOUT) {
		POINT anchor;
		anchor.x=rc.left;
		anchor.y=rc.top;
		(*pClientToScreen)(hWnd, &anchor);
		rc.left=anchor.x;
		rc.top=anchor.y;
		anchor.x=rc.right;
		anchor.y=rc.bottom;
		(*pClientToScreen)(hWnd, &anchor);
		rc.right=anchor.x;
		rc.bottom=anchor.y;
	}
	return (*pFillRect)(hdc, &rc, hbr);
}

int WINAPI extDrawFocusRect(HDC hdc, const RECT *lprc)
{
	RECT rc;
	OutTraceD("DrawFocusRect: hdc=%x xy=(%d,%d)-(%d,%d)\n", hdc, lprc->left, lprc->top, lprc->right, lprc->bottom);
	memcpy(&rc, lprc, sizeof(rc));
	if (dwFlags & FIXTEXTOUT) {
		POINT anchor;
		anchor.x=rc.left;
		anchor.y=rc.top;
		(*pClientToScreen)(hWnd, &anchor);
		rc.left=anchor.x;
		rc.top=anchor.y;
		anchor.x=rc.right;
		anchor.y=rc.bottom;
		(*pClientToScreen)(hWnd, &anchor);
		rc.right=anchor.x;
		rc.bottom=anchor.y;
	}
	return (*pDrawFocusRect)(hdc, &rc);
}

HFONT WINAPI extCreateFont(int nHeight, int nWidth, int nEscapement, int nOrientation, int fnWeight,
				 DWORD fdwItalic, DWORD fdwUnderline, DWORD fdwStrikeOut, DWORD fdwCharSet,
				 DWORD fdwOutputPrecision, DWORD fdwClipPrecision, DWORD fdwQuality,
				 DWORD fdwPitchAndFamily, LPCTSTR lpszFace)
{
	OutTraceD("CreateFont: h=%d w=%d face=\"%s\"\n", nHeight, nWidth, lpszFace);
	return (*pCreateFont)(nHeight, nWidth, nEscapement, nOrientation, fnWeight,
				 fdwItalic, fdwUnderline, fdwStrikeOut, fdwCharSet,
				 fdwOutputPrecision, fdwClipPrecision, NONANTIALIASED_QUALITY,
				 fdwPitchAndFamily, lpszFace);
}

// CreateFontIndirect hook routine to avoid font aliasing that prevents reverse blitting working on palettized surfaces

HFONT WINAPI extCreateFontIndirect(const LOGFONT* lplf)
{
	LOGFONT lf;
	HFONT retHFont;
	OutTraceD("CreateFontIndirect: h=%d w=%d face=\"%s\"\n", lplf->lfHeight, lplf->lfWidth, lplf->lfFaceName);
	memcpy((char *)&lf, (char *)lplf, sizeof(LOGFONT));
	lf.lfQuality=NONANTIALIASED_QUALITY;
	retHFont=((*pCreateFontIndirect)(&lf));
	if(retHFont)
		OutTraceD("CreateFontIndirect: hfont=%x\n", retHFont);
	else
		OutTraceD("CreateFontIndirect: error=%d at %d\n", GetLastError(), __LINE__);
	return retHFont;
}

BOOL WINAPI extShowWindow(HWND hwnd, int nCmdShow)
{
	BOOL res;

	OutTraceD("ShowWindow: hwnd=%x, CmdShow=%x(%s)\n", hwnd, nCmdShow, ExplainShowCmd(nCmdShow));
	if (dwFlags & PREVENTMAXIMIZE){
		if(nCmdShow==SW_MAXIMIZE){
			OutTraceD("ShowWindow: suppress maximize\n");
			nCmdShow=SW_SHOWNORMAL;
		}
	}

	res=pShowWindow(hwnd, nCmdShow);

	return res;
}

LONG WINAPI extGetWindowLong(HWND hwnd, int nIndex)
{
	LONG res;

	res=(*pGetWindowLong)(hwnd, nIndex);

	OutTraceD("GetWindowLong: hwnd=%x, Index=%x(%s) res=%x\n", hwnd, nIndex, ExplainSetWindowIndex(nIndex), res);

	if(nIndex==GWL_WNDPROC){
		WNDPROC wp;
		wp=WhndGetWindowProc(hwnd);
		OutTraceD("GetWindowLong: remapping WindowProc res=%x -> %x\n", res, (LONG)wp);
		if(wp) res=(LONG)wp; // if not found, don't alter the value.
	}

	return res;
}

LONG WINAPI extSetWindowLong(HWND hwnd, int nIndex, LONG dwNewLong)
{
	LONG res;

	OutTraceD("SetWindowLong: hwnd=%x, Index=%x(%s) Val=%x\n", 
		hwnd, nIndex, ExplainSetWindowIndex(nIndex), dwNewLong);

	//if(!hwnd) hwnd=hWnd;

	if (dwFlags & LOCKWINSTYLE){
		if(nIndex==GWL_STYLE){
			OutTraceD("SetWindowLong: Lock GWL_STYLE=%x\n", dwNewLong);
			//return 1;
			return (*pGetWindowLong)(hwnd, nIndex);
		}
		if(nIndex==GWL_EXSTYLE){
			OutTraceD("SetWindowLong: Lock GWL_EXSTYLE=%x\n", dwNewLong);
			//return 1;
			return (*pGetWindowLong)(hwnd, nIndex);
		}
	}

	if (dwFlags & PREVENTMAXIMIZE){
		if(nIndex==GWL_STYLE){
			OutTraceD("SetWindowLong: GWL_STYLE %x suppress MAXIMIZE\n", dwNewLong);
			dwNewLong |= WS_OVERLAPPEDWINDOW; 
			dwNewLong &= ~(WS_DLGFRAME|WS_MAXIMIZE|WS_POPUP|WS_VSCROLL|WS_HSCROLL|WS_CLIPSIBLINGS); 
		}
		if(nIndex==GWL_EXSTYLE){
			OutTraceD("SetWindowLong: GWL_EXSTYLE %x suppress TOPMOST\n", dwNewLong);
			dwNewLong = dwNewLong & ~(WS_EX_TOPMOST); 
		}
	}

	if (dwFlags & FIXWINFRAME){
		if((nIndex==GWL_STYLE) && !(dwNewLong & WS_CHILD)){
			OutTraceD("SetWindowLong: GWL_STYLE %x force OVERLAPPEDWINDOW\n", dwNewLong);
			dwNewLong |= WS_OVERLAPPEDWINDOW; 
			dwNewLong &= ~WS_CLIPSIBLINGS; 
		}
	}

	if (nIndex==GWL_WNDPROC){
		long lres;
		res=(LONG)WhndGetWindowProc(hwnd);		
		WhndStackPush(hwnd, (WNDPROC)dwNewLong);
		SetLastError(0);
		lres=(*pSetWindowLong)(hwnd, GWL_WNDPROC, (LONG)extWindowProc);
		if(!lres && GetLastError())OutTraceE("SetWindowLong: ERROR err=%d at %d\n", GetLastError(), __LINE__);
	}
	else {
		res=(*pSetWindowLong)(hwnd, nIndex, dwNewLong);
	}

	OutTraceD("SetWindowLong: hwnd=%x, nIndex=%x, Val=%x, res=%x\n", hwnd, nIndex, dwNewLong, res);
	return res;
}

BOOL WINAPI extSetWindowPos(HWND hwnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)
{
	extern BOOL isFullScreen;
	BOOL res;

	OutTraceD("SetWindowPos: hwnd=%x%s pos=(%d,%d) dim=(%d,%d) Flags=%x\n", 
		hwnd, isFullScreen?"(FULLSCREEN)":"", X, Y, cx, cy, uFlags);

	if ((hwnd != hWnd) || !isFullScreen){
		// just proxy
		res=(*pSetWindowPos)(hwnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
		if(!res)OutTraceE("SetWindowPos: ERROR err=%d at %d\n", GetLastError(), __LINE__);
		return res;
	}

	if ((dwFlags & LOCKWINPOS) && isFullScreen){
		// Note: any attempt to change the window position, no matter where and how, through the
		// SetWindowPos API is causing resizing to the default 1:1 pixed size in Commandos. 
		// in such cases, there is incompatibility between LOCKWINPOS and LOCKWINSTYLE.
		return 1;
	}

	if (dwFlags & PREVENTMAXIMIZE){
		int UpdFlag =0;
		int MaxX, MaxY;
		MaxX = iSizX;
		MaxY = iSizY;
		if (!MaxX) MaxX = VirtualScr.dwWidth;
		if (!MaxY) MaxY = VirtualScr.dwHeight;
		if(cx>MaxX) { cx=MaxX; UpdFlag=1; }
		if(cy>MaxY) { cy=MaxY; UpdFlag=1; }
		if (UpdFlag) 
			OutTraceD("SetWindowPos: using max dim=(%d,%d)\n", cx, cy);
	}

	//OutTraceD("SetWindowPos: DEBUG hWnd=%x hwnd=%x isFullScreen=%x\n", hwnd, hWnd, isFullScreen);
	// useful??? to be demonstrated....
	// when altering main window in fullscreen mode, fix the coordinates for borders
	DWORD dwCurStyle;
	RECT rect;
	rect.top=rect.left=0;
	rect.right=cx; rect.bottom=cy;
	dwCurStyle=(*pGetWindowLong)(hwnd, GWL_STYLE);
	AdjustWindowRect(&rect, dwCurStyle, FALSE);
	cx=rect.right; cy=rect.bottom;
	OutTraceD("SetWindowPos: main form hwnd=%x fixed size=(%d,%d)\n", hwnd, cx, cy);

	res=(*pSetWindowPos)(hwnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
	if(!res)OutTraceE("SetWindowPos: ERROR err=%d at %d\n", GetLastError(), __LINE__);
	return res;
}

HDWP WINAPI extDeferWindowPos(HDWP hWinPosInfo, HWND hwnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)
{
	extern BOOL isFullScreen;
	HDWP res;

	OutTraceD("DeferWindowPos: hwnd=%x%s pos=(%d,%d) dim=(%d,%d) Flags=%x\n", 
		hwnd, isFullScreen?"(FULLSCREEN)":"", X, Y, cx, cy, uFlags);

	if ((hwnd != hWnd) || !isFullScreen){
		// just proxy
		res=(*pDeferWindowPos)(hWinPosInfo, hwnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
		if(!res)OutTraceE("SetWindowPos: ERROR err=%d at %d\n", GetLastError(), __LINE__);
		return res;
	}

	if (dwFlags & LOCKWINPOS){
		return hWinPosInfo;
	}

	if (dwFlags & PREVENTMAXIMIZE){
		int UpdFlag =0;
		int MaxX, MaxY;
		MaxX = iSizX;
		MaxY = iSizY;
		if (!MaxX) MaxX = VirtualScr.dwWidth;
		if (!MaxY) MaxY = VirtualScr.dwHeight;
		if(cx>MaxX) { cx=MaxX; UpdFlag=1; }
		if(cy>MaxY) { cy=MaxY; UpdFlag=1; }
		if (UpdFlag) 
			OutTraceD("SetWindowPos: using max dim=(%d,%d)\n", cx, cy);
	}

	// useful??? to be demonstrated....
	// when altering main window in fullscreen mode, fix the coordinates for borders
	DWORD dwCurStyle;
	RECT rect;
	rect.top=rect.left=0;
	rect.right=cx; rect.bottom=cy;
	dwCurStyle=(*pGetWindowLong)(hwnd, GWL_STYLE);
	AdjustWindowRect(&rect, dwCurStyle, FALSE);
	cx=rect.right; cy=rect.bottom;
	OutTraceD("SetWindowPos: main form hwnd=%x fixed size=(%d,%d)\n", hwnd, cx, cy);

	res=(*pDeferWindowPos)(hWinPosInfo, hwnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
	if(!res)OutTraceE("DeferWindowPos: ERROR err=%d at %d\n", GetLastError(), __LINE__);
	return res;
}

void dxwFixWindowPos(char *ApiName, HWND hwnd, LPARAM lParam)
{
	LPWINDOWPOS wp;
	int MaxX, MaxY;
	wp = (LPWINDOWPOS)lParam;
	MaxX = iSizX;
	MaxY = iSizY;
	if (!MaxX) MaxX = VirtualScr.dwWidth;
	if (!MaxY) MaxY = VirtualScr.dwHeight;
	static int iLastCX, iLastCY;
	static int BorderX=-1;
	static int BorderY=-1;
	int cx, cy;

	OutTraceD("%s: GOT hwnd=%x pos=(%d,%d) dim=(%d,%d) Flags=%x\n", 
		ApiName, hwnd, wp->x, wp->y, wp->cx, wp->cy, wp->flags);

	if ((dwFlags & LOCKWINPOS) && isFullScreen && (hwnd==hWnd)){ 
		extern void CalculateWindowPos(HWND, DWORD, DWORD, LPWINDOWPOS);
		CalculateWindowPos(hwnd, MaxX, MaxY, wp);
		OutTraceD("%s: LOCK pos=(%d,%d) dim=(%d,%d)\n", ApiName, wp->x, wp->y, wp->cx, wp->cy);
	}

	if ((dwFlags2 & KEEPASPECTRATIO) && isFullScreen && (hwnd==hWnd)){ 
		// note: while keeping aspect ration, resizing from one corner doesn't tell
		// which coordinate is prevalent to the other. We made an arbitrary choice.
		// note: v2.1.93: compensation must refer to the client area, not the wp
		// window dimensions that include the window borders.
		if(BorderX==-1){
			RECT client, full;
			(*pGetClientRect)(hwnd, &client);
			(*pGetWindowRect)(hwnd, &full);
			BorderX= full.right - full.left - client.right;
			BorderY= full.bottom - full.top - client.bottom;
			OutTraceD("%s: KEEPASPECTRATIO window borders=(%d,%d)\n", ApiName, BorderX, BorderY);
		}
		extern LRESULT LastCursorPos;
		switch (LastCursorPos){
			case HTBOTTOM:
			case HTTOP:
			case HTBOTTOMLEFT:
			case HTBOTTOMRIGHT:
			case HTTOPLEFT:
			case HTTOPRIGHT:
				cx = BorderX + ((wp->cy - BorderY) * VirtualScr.dwWidth) / VirtualScr.dwHeight;
				if(cx!=wp->cx){
					OutTraceD("%s: KEEPASPECTRATIO adjusted cx=%d->%d\n", ApiName, wp->cx, cx);
					wp->cx = cx;
				}
				break;
			case HTLEFT:
			case HTRIGHT:
				cy = BorderY + ((wp->cx - BorderX) * VirtualScr.dwHeight) / VirtualScr.dwWidth;
				if(cy!=wp->cy){
					OutTraceD("%s: KEEPASPECTRATIO adjusted cy=%d->%d\n", ApiName, wp->cy, cy);
					wp->cy = cy;
				}
				break;
		}
	}

	if (dwFlags & PREVENTMAXIMIZE){
		int UpdFlag = 0;

		if(wp->cx>MaxX) { wp->cx=MaxX; UpdFlag=1; }
		if(wp->cy>MaxY) { wp->cy=MaxY; UpdFlag=1; }
		if (UpdFlag) 
			OutTraceD("%s: SET max dim=(%d,%d)\n", ApiName, wp->cx, wp->cy);
	}

	iLastCX= wp->cx;
	iLastCY= wp->cy;
}

void dxwFixMinMaxInfo(char *ApiName, HWND hwnd, LPARAM lParam)
{
	if (dwFlags & PREVENTMAXIMIZE){
		LPMINMAXINFO lpmmi;
		lpmmi=(LPMINMAXINFO)lParam;
		OutTraceD("%s: GOT MaxPosition=(%d,%d) MaxSize=(%d,%d)\n", ApiName, 
			lpmmi->ptMaxPosition.x, lpmmi->ptMaxPosition.y, lpmmi->ptMaxSize.x, lpmmi->ptMaxSize.y);
		lpmmi->ptMaxPosition.x=0;
		lpmmi->ptMaxPosition.y=0;
		if(pSetDevMode){
			lpmmi->ptMaxSize.x = pSetDevMode->dmPelsWidth;
			lpmmi->ptMaxSize.y = pSetDevMode->dmPelsHeight;
		}
		else{
			lpmmi->ptMaxSize.x = VirtualScr.dwWidth;
			lpmmi->ptMaxSize.y = VirtualScr.dwHeight;
		}
		OutTraceD("%s: SET PREVENTMAXIMIZE MaxPosition=(%d,%d) MaxSize=(%d,%d)\n", ApiName, 
			lpmmi->ptMaxPosition.x, lpmmi->ptMaxPosition.y, lpmmi->ptMaxSize.x, lpmmi->ptMaxSize.y);
	}
	// v2.1.75: added logic to fix win coordinates to selected ones. 
	// fixes the problem with "Achtung Spitfire", that can't be managed through PREVENTMAXIMIZE flag.
	if (dwFlags & LOCKWINPOS){
		LPMINMAXINFO lpmmi;
		lpmmi=(LPMINMAXINFO)lParam;
		OutTraceD("%s: GOT MaxPosition=(%d,%d) MaxSize=(%d,%d)\n", ApiName, 
			lpmmi->ptMaxPosition.x, lpmmi->ptMaxPosition.y, lpmmi->ptMaxSize.x, lpmmi->ptMaxSize.y);
		lpmmi->ptMaxPosition.x=iPosX;
		lpmmi->ptMaxPosition.y=iPosY;
		lpmmi->ptMaxSize.x = iSizX ? iSizX : VirtualScr.dwWidth;
		lpmmi->ptMaxSize.y = iSizY ? iSizY : VirtualScr.dwHeight;
		OutTraceD("%s: SET LOCKWINPOS MaxPosition=(%d,%d) MaxSize=(%d,%d)\n", ApiName, 
			lpmmi->ptMaxPosition.x, lpmmi->ptMaxPosition.y, lpmmi->ptMaxSize.x, lpmmi->ptMaxSize.y);
	}
}

void dxwFixStyle(char *ApiName, HWND hwnd, LPARAM lParam)
{
	LPSTYLESTRUCT lpSS;
	lpSS = (LPSTYLESTRUCT) lParam;

	OutTraceD("%s: new Style=%x(%s)\n", 
		ApiName, lpSS->styleNew, ExplainStyle(lpSS->styleNew));

	if (dwFlags & FIXWINFRAME){ // set canonical style
		lpSS->styleNew= WS_OVERLAPPEDWINDOW;
	}
	if (dwFlags & LOCKWINSTYLE){ // set to current value
		lpSS->styleNew= (*pGetWindowLong)(hwnd, GWL_STYLE);
	}
	if (dwFlags & PREVENTMAXIMIZE){ // disable maximize settings
		if (lpSS->styleNew & WS_MAXIMIZE){
			OutTraceD("%s: prevent maximize style\n", ApiName);
			lpSS->styleNew &= ~WS_MAXIMIZE;
		}
	}
}

void dxwFixExStyle(char *ApiName, HWND hwnd, LPARAM lParam)
{
	LPSTYLESTRUCT lpSS;
	lpSS = (LPSTYLESTRUCT) lParam;

	OutTraceD("%s: new ExStyle=%x(%s)\n", 
		ApiName, lpSS->styleNew, ExplainExStyle(lpSS->styleNew));

	if (dwFlags & FIXWINFRAME){ // set canonical style
		lpSS->styleNew= 0;
	}
	if (dwFlags & LOCKWINSTYLE){ // set to current value
			lpSS->styleNew= (*pGetWindowLong)(hwnd, GWL_EXSTYLE);
	}
	if (dwFlags & PREVENTMAXIMIZE){ // disable maximize settings
		if (lpSS->styleNew & WS_EX_TOPMOST){
			OutTraceD("%s: prevent EXSTYLE topmost style\n", ApiName);
			lpSS->styleNew &= ~WS_EX_TOPMOST;
		}
	}
}

static LRESULT WINAPI FixWindowProc(char *ApiName, HWND hwnd, UINT Msg, WPARAM wParam, LPARAM *lpParam)
{
	LPARAM lParam;

	lParam=*lpParam;
	OutTraceW("%s: hwnd=%x msg=[0x%x]%s(%x,%x)\n",
		ApiName, hwnd, Msg, ExplainWinMessage(Msg), wParam, lParam);

	switch(Msg){
	// attempt to fix Sleepwalker
	//case WM_NCCALCSIZE:
	//	if (dwFlags & PREVENTMAXIMIZE)
	//		return 0;
	//	break;
	case WM_ERASEBKGND:
		OutTraceD("%s: prevent erase background\n", ApiName);
		return 1; // 1=erased
		break; // useless
	case WM_GETMINMAXINFO:
		dxwFixMinMaxInfo(ApiName, hwnd, lParam);
		break;
	case WM_WINDOWPOSCHANGING:
	case WM_WINDOWPOSCHANGED:
		dxwFixWindowPos(ApiName, hwnd, lParam);
		break;
	case WM_STYLECHANGING:
	case WM_STYLECHANGED:
		if (wParam==GWL_STYLE) 
			dxwFixStyle(ApiName, hwnd, lParam);
		else
			dxwFixExStyle(ApiName, hwnd, lParam);
		break;
	case WM_DISPLAYCHANGE:
		// too late? to be deleted....
		if ((dwFlags & LOCKWINPOS) && isFullScreen) return 0;
		if (dwFlags & PREVENTMAXIMIZE){
			OutTraceD("%s: WM_DISPLAYCHANGE depth=%d size=(%d,%d)\n",
				ApiName, wParam, HIWORD(lParam), LOWORD(lParam));
			return 0;
		}
		break;
	case WM_SIZE:
		if ((dwFlags & LOCKWINPOS) && isFullScreen) return 0;
		if (dwFlags & PREVENTMAXIMIZE){
			if ((wParam == SIZE_MAXIMIZED)||(wParam == SIZE_MAXSHOW)){
				OutTraceD("%s: prevent screen SIZE to fullscreen wparam=%d(%s) size=(%d,%d)\n", ApiName,
					wParam, ExplainResizing(wParam), HIWORD(lParam), LOWORD(lParam));
				return 0; // checked
				//lParam = MAKELPARAM(VirtualScr.dwWidth, VirtualScr.dwHeight); 
				//OutTraceD("%s: updated SIZE wparam=%d(%s) size=(%d,%d)\n", ApiName,
				//	wParam, ExplainResizing(wParam), HIWORD(lParam), LOWORD(lParam));
			}
		}
		break;	
	default:
		break;
	}
	
	// marker to run hooked function
	return(-1);
}

LRESULT WINAPI extCallWindowProc(WNDPROC lpPrevWndFunc, HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	HRESULT res;

	res=FixWindowProc("CallWindowProc", hwnd, Msg, wParam, &lParam);

	if (res==(HRESULT)-1)
		return (*pCallWindowProc)(lpPrevWndFunc, hwnd, Msg, wParam, lParam);
	else
		return res;
}

LRESULT WINAPI extDefWindowProc(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	HRESULT res;

	res=FixWindowProc("DefWindowProc", hwnd, Msg, wParam, &lParam);

	if (res==(HRESULT)-1)
		return (*pDefWindowProc)(hwnd, Msg, wParam, lParam);
	else
		return res;
}

int WINAPI extGetDeviceCaps(HDC hdc, int nindex)
{
	DWORD res;
	
	res = (*pGetDeviceCaps)(hdc, nindex);
	OutTraceD("GetDeviceCaps: hdc=%x index=%x(%s) res=%x\n",
		hdc, nindex, ExplainDeviceCaps(nindex), res);

	// if you have a bypassed setting, use it first!
	if(pSetDevMode){
		switch(nindex){
		case BITSPIXEL:
		case COLORRES:
			res = pSetDevMode->dmBitsPerPel;
			OutTraceD("GetDeviceCaps: fix BITSPIXEL/COLORRES cap=%x\n",res);
			return res;
		case HORZRES:
			res = pSetDevMode->dmPelsWidth;
			OutTraceD("GetDeviceCaps: fix HORZRES cap=%d\n", res);
			return res;
		case VERTRES:
			res = pSetDevMode->dmPelsHeight;
			OutTraceD("GetDeviceCaps: fix VERTRES cap=%d\n", res);
			return res;
		}
	}

	switch(nindex){
	case VERTRES:
		res= VirtualScr.dwHeight;
		OutTraceD("GetDeviceCaps: fix VERTRES cap=%d\n", res);
		break;
	case HORZRES:
		res= VirtualScr.dwWidth;
		OutTraceD("GetDeviceCaps: fix HORZRES cap=%d\n", res);
		break;
	// WARNING: in no-emu mode, the INIT8BPP and INIT16BPP flags expose capabilities that
	// are NOT implemented and may cause later troubles!
	case RASTERCAPS:
		if(dwFlags2 & INIT8BPP) {
			res = RC_PALETTE;
			OutTraceD("GetDeviceCaps: fix RASTERCAPS setting RC_PALETTE cap=%x\n",res);
		}
		break;
	case BITSPIXEL:
	case COLORRES:
		if(dwFlags2 & INIT8BPP|INIT16BPP){
			if(dwFlags2 & INIT8BPP) res = 8;
			if(dwFlags2 & INIT16BPP) res = 16;
			OutTraceD("GetDeviceCaps: fix BITSPIXEL/COLORRES cap=%d\n",res);
		}
		break;
	}

	if(dwFlags & EMULATESURFACE){
		switch(nindex){
		case RASTERCAPS:
			if((VirtualScr.PixelFormat.dwRGBBitCount==8) || (dwFlags2 & INIT8BPP)){
				res = RC_PALETTE;
				OutTraceD("GetDeviceCaps: fix RASTERCAPS setting RC_PALETTE cap=%x\n",res);
			}
			break;
		case BITSPIXEL:
		case COLORRES:
			int PrevRes;
			PrevRes=res;
			if(VirtualScr.PixelFormat.dwRGBBitCount!=0) res = VirtualScr.PixelFormat.dwRGBBitCount;
			if(dwFlags2 & INIT8BPP) res = 8;
			if(dwFlags2 & INIT16BPP) res = 16;
			if(PrevRes != res) OutTraceD("GetDeviceCaps: fix BITSPIXEL/COLORRES cap=%d\n",res);
			break;
		case SIZEPALETTE:
			res = 256;
			OutTraceD("GetDeviceCaps: fix SIZEPALETTE cap=%x\n",res);
			break;
		case NUMRESERVED:
			res = 0;
			OutTraceD("GetDeviceCaps: fix NUMRESERVED cap=%x\n",res);
			break;
		}
	}
	return res;
}

int WINAPI extGetSystemMetrics(int nindex)
{
	HRESULT res;

	res=(*pGetSystemMetrics)(nindex);
	OutTraceD("GetSystemMetrics: index=%x, res=%d\n", nindex, res);

	// if you have a bypassed setting, use it first!
	if(pSetDevMode){
		switch(nindex){
		case SM_CXFULLSCREEN:
		case SM_CXSCREEN:
			res = pSetDevMode->dmPelsWidth;
			OutTraceD("GetDeviceCaps: fix HORZRES cap=%d\n", res);
			return res;
		case SM_CYFULLSCREEN:
		case SM_CYSCREEN:
			res = pSetDevMode->dmPelsHeight;
			OutTraceD("GetDeviceCaps: fix VERTRES cap=%d\n", res);
			return res;
		}
	}

	switch(nindex){
	case SM_CXFULLSCREEN:
	case SM_CXSCREEN:
		res= VirtualScr.dwWidth;
		OutTraceD("GetSystemMetrics: fix SM_CXSCREEN=%d\n", res);
		break;
	case SM_CYFULLSCREEN:
	case SM_CYSCREEN:
		res= VirtualScr.dwHeight;
		OutTraceD("GetSystemMetrics: fix SM_CYSCREEN=%d\n", res);
		break;
	}

	return res;
}

BOOL WINAPI extScaleWindowExtEx(HDC hdc, int Xnum, int Xdenom, int Ynum, int Ydenom, LPSIZE lpSize)
{
	OutTraceD("ScaleWindowExtEx: hdc=%x num=(%d,%d) denom=(%d,%d) lpSize=%d\n",
		hdc, Xnum, Ynum, Xdenom, Ydenom, lpSize);

	if ((dwFlags & LOCKWINPOS) && isFullScreen) return 1;

	return (*pScaleWindowExtEx)(hdc, Xnum, Xdenom, Ynum, Ydenom, lpSize);
}

/* ---- pixel color management ----*/

int WINAPI extGDIChoosePixelFormat(HDC hdc, const PIXELFORMATDESCRIPTOR *ppfd)
{
	int ret;

	OutTraceD("GDIChoosePixelFormat: hdc=%x, pfd=%x\n", hdc, ppfd);
	OutTraceD("GDIChoosePixelFormat: Version=%x Flags-%x PixelType=%x ColorBits=%x RedBits=%x RedShift=%x "
			 "GreenBits=%x BlueBits=%x BlueShift=%x AlphaBits=%x AlphaShift=%x",
		ppfd->nVersion, ppfd->dwFlags, ppfd->iPixelType, ppfd->cColorBits, ppfd->cRedBits, ppfd->cRedShift,
		ppfd->cGreenBits, ppfd->cGreenShift, ppfd->cBlueBits, ppfd->cBlueShift, ppfd->cAlphaBits, ppfd->cAlphaShift);
		/*
		  ppfd->cAccumBits, ppfd->cAccumRedBits, ppfd->cAccumGreenBits, ppfd->cAccumBlueBits, ppfd->cAccumAlphaBits,
		  ppfd->cDepthBits, ppfd->cStencilBits, ppfd->cAuxBuffers, ppfd->iLayerType, ppfd->bReserved,
		  ppfd->dwLayerMask, ppfd->dwVisibleMask, ppfd->dwDamageMask
		 */

	ret= (*pGDIChoosePixelFormat)(hdc, ppfd);
	OutTraceD("GDIChoosePixelFormat: ret=%d\n",ret);
	return ret;
}

int WINAPI extGDIGetPixelFormat(HDC hdc)
{
	OutTraceD("GDIGetPixelFormat: hdc=%x\n", hdc);
	return (*pGDIGetPixelFormat)(hdc);
}

BOOL WINAPI extGDISetPixelFormat(HDC hdc, int iPixelFormat, const PIXELFORMATDESCRIPTOR *ppfd)
{
	OutTraceD("GDISetPixelFormat: hdc=%x\n", hdc);

#if 0
	if(dwFlags & EMULATESURFACE){
		// set VirtualScr view
		VirtualScr.dwRGBBitCount=ppfd->cColorBits;
		VirtualScr.dwFlags=ppfd->dwFlags;
		VirtualScr.dwRBitMask=(2^ppfd->cRedBits - 1)<<ppfd->cRedShift;
		VirtualScr.dwGBitMask=(2^ppfd->cGreenBits - 1)<<ppfd->cGreenShift;
		VirtualScr.dwBBitMask=(2^ppfd->cBlueBits - 1)<<ppfd->cBlueShift;
		VirtualScr.dwRGBAlphaBitMask=(2^ppfd->cAlphaBits - 1)<<ppfd->cAlphaShift;
		OutTraceD("GDISetPixelFormat: RGBA colorbits=%d mask=(%x,%x,%x,%x)\n", 
			VirtualScr.dwRGBBitCount, VirtualScr.dwRBitMask, VirtualScr.dwGBitMask, VirtualScr.dwBBitMask, VirtualScr.dwRGBAlphaBitMask);
		return 0;
	}
#endif

	return (*pGDISetPixelFormat)(hdc, iPixelFormat, ppfd);
}

//HRGN WINAPI extCreateRectRgn(int nLeftRect, int nTopRect, int nRightRect, int nBottomRect)
//{
//	OutTraceD("CreateRectRgn: (%d,%d)-(%d,%d)\n", nLeftRect, nTopRect, nRightRect, nBottomRect);
//	return (*pCreateRectRgn)(nLeftRect, nTopRect, nRightRect, nBottomRect);
//}

LONG WINAPI MyChangeDisplaySettings(char *fname, DEVMODE *lpDevMode, DWORD dwflags)
{
	HRESULT res;

	// save desired settings first v.2.1.89
	// v2.1.95 protect when lpDevMode is null (closing game... Jedi Outcast 
	if(lpDevMode){
		VirtualScr.dwWidth = lpDevMode->dmPelsWidth;
		VirtualScr.dwHeight = lpDevMode->dmPelsHeight;
	}

	if (dwFlags & EMULATESURFACE){
		OutTraceD("%s: BYPASS res=DISP_CHANGE_SUCCESSFUL\n", fname);
		return DISP_CHANGE_SUCCESSFUL;
	}
	else{
		if ((dwflags==0 || dwflags==CDS_FULLSCREEN) && lpDevMode){
			DEVMODE NewMode, TryMode;
			int i;
			//EnumDisplaySettings_Type pEnum;

			// find what address call to use
			// pEnum = pEnumDisplaySettings ? pEnumDisplaySettings : EnumDisplaySettings;
			// pEnum = EnumDisplaySettings;

			SetDevMode=*lpDevMode;
			pSetDevMode=&SetDevMode;

			// set the proper mode
			NewMode = *lpDevMode;
			NewMode.dmPelsHeight = (*GetSystemMetrics)(SM_CYSCREEN);
			NewMode.dmPelsWidth = (*GetSystemMetrics)(SM_CXSCREEN);
			TryMode.dmSize = sizeof(TryMode);
			 OutTraceD("ChangeDisplaySettings: DEBUG looking for size=(%d x %d) bpp=%d\n",
				NewMode.dmPelsWidth, NewMode.dmPelsHeight, NewMode.dmBitsPerPel);
			for(i=0; ;i++){
				if (pEnumDisplaySettings)
					res=(*pEnumDisplaySettings)(NULL, i, &TryMode);
				else
					res=EnumDisplaySettings(NULL, i, &TryMode);
				if(res==0) {
					OutTraceE("%s: ERROR unable to find a matching video mode among %d ones\n", fname, i);
					return DISP_CHANGE_FAILED;
				}
				//OutTraceD("ChangeDisplaySettings: DEBUG index=%d size=(%d x %d) bpp=%x\n",
				//	i, TryMode.dmPelsWidth, TryMode.dmPelsHeight, TryMode.dmBitsPerPel);
				if((NewMode.dmBitsPerPel==TryMode.dmBitsPerPel) &&
					(NewMode.dmPelsHeight==TryMode.dmPelsHeight) &&
					(NewMode.dmPelsWidth==TryMode.dmPelsWidth)) break;
			}
			if(dwflags==CDS_FULLSCREEN) dwflags=0; // no FULLSCREEN
			res=(*ChangeDisplaySettings)(&TryMode, dwflags);
			OutTraceD("%s: fixed size=(%d x %d) bpp=%d res=%x(%s)\n",
				fname, NewMode.dmPelsHeight, NewMode.dmPelsWidth, NewMode.dmBitsPerPel, 
				res, ExplainDisplaySettingsRetcode(res));
			return res;
		}
		else
			return (*ChangeDisplaySettings)(lpDevMode, dwflags);
	}
}

LONG WINAPI extChangeDisplaySettings(DEVMODE *lpDevMode, DWORD dwflags)
{
	if(IsTraceD){
		OutTrace("ChangeDisplaySettings: lpDevMode=%x flags=%x", lpDevMode, dwflags);
		if (lpDevMode) OutTrace(" size=(%d x %d) bpp=%x", 
			lpDevMode->dmPelsWidth, lpDevMode->dmPelsHeight, lpDevMode->dmBitsPerPel);
		OutTrace("\n");
	}

	return MyChangeDisplaySettings("ChangeDisplaySettings", lpDevMode, dwflags);
}

LONG WINAPI extChangeDisplaySettingsEx(LPCTSTR lpszDeviceName, DEVMODE *lpDevMode, HWND hwnd, DWORD dwflags, LPVOID lParam)
{
	if(IsTraceD){
		OutTrace("ChangeDisplaySettingsEx: DeviceName=%s lpDevMode=%x flags=%x", lpszDeviceName, lpDevMode, dwflags);
		if (lpDevMode) OutTrace(" size=(%d x %d) bpp=%x", 
			lpDevMode->dmPelsWidth, lpDevMode->dmPelsHeight, lpDevMode->dmBitsPerPel);
		OutTrace("\n");
	}

	return MyChangeDisplaySettings("ChangeDisplaySettingsEx", lpDevMode, dwflags);
}

LONG WINAPI extEnumDisplaySettings(LPCTSTR lpszDeviceName, DWORD iModeNum, DEVMODE *lpDevMode)
{
	OutTraceD("EnumDisplaySettings: Devicename=%s ModeNum=%x\n", lpszDeviceName, iModeNum);
	if(pSetDevMode && iModeNum==ENUM_CURRENT_SETTINGS){
		lpDevMode=pSetDevMode;
		return 1;
	}
	else
		return (*pEnumDisplaySettings)(lpszDeviceName, iModeNum, lpDevMode);
}

BOOL WINAPI extSetWindowPlacement(const WINDOWPLACEMENT*lpwndpl)
{
	OutTraceD("SetWindowPlacement: BYPASS\n");
	return 1;
}

ATOM WINAPI extRegisterClassExA(WNDCLASSEX *lpwcx)
{
	OutTraceD("RegisterClassEx: PROXED ClassName=%s style=%x(%s)\n", 
		lpwcx->lpszClassName, lpwcx->style, ExplainStyle(lpwcx->style));
	return (*pRegisterClassExA)(lpwcx);
}

BOOL WINAPI extClientToScreen(HWND whnd, LPPOINT lppoint)
{
	if (lppoint && isFullScreen && (whnd == hWnd))
		return 1;
	else
		return (*pClientToScreen)(whnd, lppoint);
//	return 1;
}

BOOL WINAPI extScreenToClient(HWND whnd, LPPOINT lppoint)
{
	if (lppoint && isFullScreen && (whnd == hWnd))
		return 1;
	else
		return (*pScreenToClient)(whnd, lppoint);
//	return 1;
}

BOOL WINAPI extGetClientRect(HWND hwnd, LPRECT lpRect)
{
	// do (almost) nothing !!!
	OutTraceD("GetClientRect: whnd=%x isFullScreen=%x\n", hwnd, isFullScreen);
	if (lpRect && isFullScreen && (hwnd == hWnd)){
		lpRect->left=0;
		lpRect->top=0;
		lpRect->right=VirtualScr.dwWidth;
		lpRect->bottom=VirtualScr.dwHeight;
		OutTraceD("GetClientRect: fixed rect=(%d,%d)-(%d,%d)\n", lpRect->left, lpRect->top, lpRect->right, lpRect->bottom);
		return 1;
	}

	// v2.1.75: in PREVENTMAXIMIZE mode, prevent the application to know the actual size of the desktop
	// by calling GetClientRect on it!! Used to windowize "AfterLife".
	// should I do the same with hwnd==0 ??
	if ((hwnd==(*pGetDesktopWindow)()) || (hwnd==0)){
		lpRect->left=0;
		lpRect->top=0;
		lpRect->right=VirtualScr.dwWidth;
		lpRect->bottom=VirtualScr.dwHeight;
		OutTraceD("GetClientRect: fixed rect=(%d,%d)-(%d,%d)\n", lpRect->left, lpRect->top, lpRect->right, lpRect->bottom);
		return 1;
	}
	
	// proxed call
	return (*pGetClientRect)(hwnd, lpRect);

	}

BOOL WINAPI extGetWindowRect(HWND hwnd, LPRECT lpRect)
{
	BOOL ret;

	OutTraceD("GetWindowRect: hwnd=%x hWnd=%x isFullScreen=%x\n", hwnd, hWnd, isFullScreen);
	if (lpRect && isFullScreen && (hwnd == hWnd)){
		// a fullscreen window should have NO BORDERS!
		lpRect->left = 0;
		lpRect->top = 0;
		lpRect->right = VirtualScr.dwWidth;
		lpRect->bottom = VirtualScr.dwHeight;
		OutTraceD("GetWindowRect: fixed rect=(%d,%d)-(%d,%d)\n", lpRect->left, lpRect->top, lpRect->right, lpRect->bottom);
		return 1;
	}

	if (isFullScreen && ((*pGetWindowLong)(hwnd, GWL_STYLE) & WS_CHILD)){
		// a child win should return the original supposed size
		// so you basically revert here the coordinates compensation.
		// Used by "Road Rash" to blit graphic on top of child windows
		POINT upleft={0,0};
		RECT client;
		(*pClientToScreen)(hWnd,&upleft);
		(*pGetClientRect)(hWnd,&client);
#if 1
		// using GetWindowRect and compensate for displacement.....
		ret=(*pGetWindowRect)(hwnd, lpRect);
		if (client.right && client.bottom){ // avoid divide by 0
			lpRect->left = ((lpRect->left - upleft.x) * VirtualScr.dwWidth) / client.right;
			lpRect->top = ((lpRect->top - upleft.y) * VirtualScr.dwHeight) / client.bottom;
			lpRect->right = ((lpRect->right - upleft.x) * VirtualScr.dwWidth) / client.right;
			lpRect->bottom = ((lpRect->bottom - upleft.y) * VirtualScr.dwHeight) / client.bottom;
#else
		// using GetClientRect (simpler....) but not working.....
		ret=(*pGetClientRect)(hwnd, lpRect);
		if (client.right && client.bottom){ // avoid divide by 0
			lpRect->left = (lpRect->left * VirtualScr.dwWidth) / client.right;
			lpRect->top = (lpRect->top * VirtualScr.dwHeight) / client.bottom;
			lpRect->right = (lpRect->right * VirtualScr.dwWidth) / client.right;
			lpRect->bottom = (lpRect->bottom * VirtualScr.dwHeight) / client.bottom;
#endif
			OutTraceD("GetWindowRect: fixed CHILD rect=(%d,%d)-(%d,%d)\n", lpRect->left, lpRect->top, lpRect->right, lpRect->bottom);
		}
		return ret;
	}

	ret=(*pGetWindowRect)(hwnd, lpRect);
	OutTraceD("GetWindowRect: rect=(%d,%d)-(%d,%d)\n", lpRect->left, lpRect->top, lpRect->right, lpRect->bottom);
	return ret;
}

int WINAPI extMapWindowPoints(HWND hWndFrom, HWND hWndTo, LPPOINT lpPoints, UINT cPoints)
{
	// a rarely used API, but responsible for a painful headache: needs hooking for "Commandos 2".

	OutTraceD("MapWindowPoints: hWndFrom=%x hWndTo=%x cPoints=%d isFullScreen=%x\n", 
		hWndFrom, hWndTo, cPoints, isFullScreen);
	if((hWndTo==HWND_DESKTOP) && (hWndFrom==hWnd) && isFullScreen){
		OutTraceD("MapWindowPoints: fullscreen condition\n"); 
		SetLastError(0);
		return 0; // = 0,0 shift
	}
	else{
		// just proxy it
		return (*pMapWindowPoints)(hWndFrom, hWndTo, lpPoints, cPoints);
	}
}

BOOL WINAPI extPeekMessage(LPMSG lpMsg, HWND hwnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
	BOOL res;

	res=(*pPeekMessage)(lpMsg, hwnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
	
	OutTraceW("PeekMessage: lpmsg=%x hwnd=%x filter=(%x-%x) remove=%x msg=%x(%s) wparam=%x, lparam=%x pt=(%d,%d) res=%x\n", 
		lpMsg, lpMsg->hwnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg, 
		lpMsg->message, ExplainWinMessage(lpMsg->message & 0xFFFF), 
		lpMsg->wParam, lpMsg->lParam, lpMsg->pt.x, lpMsg->pt.y, res);

	// v2.1.74: skip message fix for WM_CHAR to avoid double typing bug
	switch(lpMsg->message){
		//case WM_CHAR:
		case WM_KEYUP:
		case WM_KEYDOWN:
			return res;
	}

	// fix to avoid crash in Warhammer Final Liberation, that evidently intercepts mouse position by 
	// peeking & removing messages from window queue and considering the lParam parameter.
	// v2.1.100 - never alter the mlMsg, otherwise the message is duplicated in the queue! Work on a copy of it.
	if(wRemoveMsg){
		static MSG MsgCopy;
		MsgCopy=*lpMsg;
		MsgCopy.pt=FixMessagePt(hWnd, MsgCopy.pt);
		if((MsgCopy.message <= WM_MOUSELAST) && (MsgCopy.message >= WM_MOUSEFIRST)) MsgCopy.lParam = MAKELPARAM(MsgCopy.pt.x, MsgCopy.pt.y); 
		OutTraceC("PeekMessage: fixed lparam/pt=(%d,%d)\n", MsgCopy.pt.x, MsgCopy.pt.y);
		lpMsg=&MsgCopy;
	}

	return res;
}

BOOL WINAPI extGetMessage(LPMSG lpMsg, HWND hwnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
	BOOL res;
	HWND FixedHwnd;

	res=(*pGetMessage)(lpMsg, hwnd, wMsgFilterMin, wMsgFilterMax);

	OutTraceW("GetMessage: lpmsg=%x hwnd=%x filter=(%x-%x) msg=%x(%s) wparam=%x, lparam=%x pt=(%d,%d) res=%x\n", 
		lpMsg, lpMsg->hwnd, wMsgFilterMin, wMsgFilterMax, 
		lpMsg->message, ExplainWinMessage(lpMsg->message & 0xFFFF), 
		lpMsg->wParam, lpMsg->lParam, lpMsg->pt.x, lpMsg->pt.y, res);

	// V2.1.68: processing ALL mouse events, to sync mouse over and mouse click events
	// in "Uprising 2", now perfectly working.
	DWORD Message;
	Message=lpMsg->message & 0xFFFF;
	if((Message <= WM_MOUSELAST) && (Message >= WM_MOUSEFIRST)){
		FixedHwnd=(hwnd)?hwnd:hWnd;
		lpMsg->pt=FixMessagePt(FixedHwnd, lpMsg->pt);
		lpMsg->lParam = MAKELPARAM(lpMsg->pt.x, lpMsg->pt.y); 
		OutTraceC("PeekMessage: fixed lparam/pt=(%d,%d)\n", lpMsg->pt.x, lpMsg->pt.y);
	}
	return res;
}

LRESULT WINAPI extDispatchMessage(LPMSG lpMsg)
{
	BOOL res;
	DWORD Message;
#if WORKONCOPY
	static MSG MsgCopy;
	MsgCopy=*lpMsg;
	MsgCopy.pt=FixMessagePt(hWnd, MsgCopy.pt);
	Message=MsgCopy.message & 0xFFFF;
	if((Message <= WM_MOUSELAST) && (Message >= WM_MOUSEFIRST)){
		MsgCopy.lParam = MAKELPARAM(MsgCopy.pt.x, MsgCopy.pt.y); 
		OutTraceC("PeekMessage: fixed lparam/pt=(%d,%d)\n", MsgCopy.pt.x, MsgCopy.pt.y);
	}
	res=(*pDispatchMessage)(&MsgCopy);
	OutTraceW("DispatchMessage: lpmsg=%x msg=%x(%s) wparam=%x, lparam=%x pt=(%d,%d) res=%x\n", 
		lpMsg, MsgCopy.message, ExplainWinMessage(MsgCopy.message & 0xFFFF), 
		MsgCopy.wParam, MsgCopy.lParam, MsgCopy.pt.x, MsgCopy.pt.y, res);
#else
	lpMsg->pt=FixMessagePt(hWnd, lpMsg->pt);
	Message=lpMsg->message & 0xFFFF;
	if((Message <= WM_MOUSELAST) && (Message >= WM_MOUSEFIRST)){
		lpMsg->lParam = MAKELPARAM(lpMsg->pt.x, lpMsg->pt.y); 
		OutTraceC("PeekMessage: fixed lparam/pt=(%d,%d)\n", lpMsg->pt.x, lpMsg->pt.y);
	}
	res=(*pDispatchMessage)(lpMsg);
	OutTraceW("DispatchMessage: lpmsg=%x msg=%x(%s) wparam=%x, lparam=%x pt=(%d,%d) res=%x\n", 
		lpMsg, lpMsg->message, ExplainWinMessage(lpMsg->message & 0xFFFF), 
		lpMsg->wParam, lpMsg->lParam, lpMsg->pt.x, lpMsg->pt.y, res);
#endif
	return res;
}

// intercept GetProcAddress to initialize DirectDraw hook pointers.
// This is necessary in "The Sims" game, that loads DirectDraw dinamically

#define SYSLIBIDX_KERNEL32		0
#define SYSLIBIDX_USER32		1
#define SYSLIBIDX_GDI32			2
#define SYSLIBIDX_OLE32			3
#define SYSLIBIDX_DIRECTDRAW	4
#define SYSLIBIDX_OPENGL		5
#define SYSLIBIDX_MAX			6 // array size
HMODULE SysLibs[SYSLIBIDX_MAX];
char *SysNames[SYSLIBIDX_MAX]={
	"kernel32.dll",
	"USER32.dll",
	"GDI32.dll",
	"ole32.dll",
	"ddraw.dll",
	"opengl32.dll"
};
char *SysNames2[SYSLIBIDX_MAX]={
	"kernel32",
	"USER32",
	"GDI32",
	"ole32",
	"ddraw",
	"opengl32"
};
extern void HookModule(char *, int);

HMODULE WINAPI extLoadLibraryA(LPCTSTR lpFileName)
{
	HMODULE ret;
	int idx;
	char *lpName, *lpNext;
	ret=(*pLoadLibraryA)(lpFileName);
	OutTraceD("LoadLibraryA: FileName=%s hmodule=%x\n", lpFileName, ret);
	lpName=(char *)lpFileName;
	while (lpNext=strchr(lpName,'\\')) lpName=lpNext+1;
	for(idx=0; idx<SYSLIBIDX_MAX; idx++){
		if(
			(!lstrcmpi(lpName,SysNames[idx])) ||
			(!lstrcmpi(lpName,SysNames2[idx]))
		){
			OutTraceD("LoadLibraryA: registered hmodule=%x->FileName=%s\n", ret, lpFileName);
			SysLibs[idx]=ret;
			break;
		}
	}
	if(idx==SYSLIBIDX_MAX) {
		OutTraceD("LoadLibraryA: hook %s\n", lpFileName);
		HookModule((char *)lpFileName, 0);
	}
	return ret;
}
						
HMODULE WINAPI extLoadLibraryExA(LPCTSTR lpFileName, HANDLE hFile, DWORD dwFlags)
{
	HMODULE ret;
	int idx;
	ret=(*pLoadLibraryExA)(lpFileName, hFile, dwFlags);
	OutTraceD("LoadLibraryExA: FileName=%s hFile=%x Flags=%x hmodule=%x\n", lpFileName, hFile, dwFlags, ret);
	for(idx=0; idx<SYSLIBIDX_MAX; idx++){
		if(
			(!lstrcmpi(lpFileName,SysNames[idx])) ||
			(!lstrcmpi(lpFileName,SysNames2[idx]))
		){
			OutTraceD("LoadLibraryExA: registered hmodule=%x->FileName=%s\n", ret, lpFileName);
			SysLibs[idx]=ret;
			break;
		}
	}
	if(idx==SYSLIBIDX_MAX) {
		OutTraceD("LoadLibraryExA: hook %s\n", lpFileName);
		HookModule((char *)lpFileName, 0);
	}
	return ret;
}

extern DirectDrawCreate_Type pDirectDrawCreate;
extern DirectDrawCreateEx_Type pDirectDrawCreateEx;
extern HRESULT WINAPI extDirectDrawCreate(GUID FAR *, LPDIRECTDRAW FAR *, IUnknown FAR *);
extern HRESULT WINAPI extDirectDrawCreateEx(GUID FAR *, LPDIRECTDRAW FAR *, REFIID, IUnknown FAR *);
extern GetProcAddress_Type pGetProcAddress;
extern HRESULT STDAPICALLTYPE extCoCreateInstance(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID FAR*);

int WINAPI extIsDebuggerPresent(void)
{
	OutTraceD("extIsDebuggerPresent: return FALSE\n");
	return FALSE;
}

FARPROC WINAPI extGetProcAddress(HMODULE hModule, LPCSTR proc)
{
	FARPROC ret;
	int idx;

	// WARNING: seems to be called with bad LPCSTR value....
	// from MSDN:
	// The function or variable name, or the function's ordinal value. 
	// If this parameter is an ordinal value, it must be in the low-order word; 
	// the high-order word must be zero.

	OutTraceD("GetProcAddress: hModule=%x proc=%s\n", hModule, ProcToString(proc));

	for(idx=0; idx<SYSLIBIDX_MAX; idx++){
		if(SysLibs[idx]==hModule) break;
	}

	// to do: the else condition: the program COULD load addresses by ordinal value ... done ??
	// to do: CoCreateInstanceEx
	if((DWORD)proc & 0xFFFF0000){
		FARPROC remap;
		switch(idx){
		case SYSLIBIDX_DIRECTDRAW:
			if (!strcmp(proc,"DirectDrawCreate")){
				pDirectDrawCreate=(DirectDrawCreate_Type)(*pGetProcAddress)(hModule, proc);
				OutTraceD("GetProcAddress: hooking proc=%s at addr=%x\n", ProcToString(proc), pDirectDrawCreate);
				return (FARPROC)extDirectDrawCreate;
			}
			if (!strcmp(proc,"DirectDrawCreateEx")){
				pDirectDrawCreateEx=(DirectDrawCreateEx_Type)(*pGetProcAddress)(hModule, proc);
				OutTraceD("GetProcAddress: hooking proc=%s at addr=%x\n", ProcToString(proc), pDirectDrawCreateEx);
				return (FARPROC)extDirectDrawCreateEx;
			}
			if (!strcmp(proc,"DirectDrawEnumerateA")){
				pDirectDrawEnumerate=(DirectDrawEnumerate_Type)(*pGetProcAddress)(hModule, proc);
				OutTraceP("GetProcAddress: hooking proc=%s at addr=%x\n", proc, pDirectDrawEnumerate);
				return (FARPROC)extDirectDrawEnumerateProxy;
			}
			if (!strcmp(proc,"DirectDrawEnumerateExA")){
				pDirectDrawEnumerateEx=(DirectDrawEnumerateEx_Type)(*pGetProcAddress)(hModule, proc);
				OutTraceP("GetProcAddress: hooking proc=%s at addr=%x\n", proc, pDirectDrawEnumerateEx);
				return (FARPROC)extDirectDrawEnumerateExProxy;
			}
			break;
		case SYSLIBIDX_USER32:
			if (!strcmp(proc,"ChangeDisplaySettingsA")){
				pChangeDisplaySettings=(ChangeDisplaySettings_Type)(*pGetProcAddress)(hModule, proc);
				OutTraceD("GetProcAddress: hooking proc=%s at addr=%x\n", ProcToString(proc), pChangeDisplaySettings);
				return (FARPROC)extChangeDisplaySettings;
			}
			break;
		case SYSLIBIDX_KERNEL32:
			if (!strcmp(proc,"IsDebuggerPresent")){
				OutTraceD("GetProcAddress: hooking proc=%s at addr=%x\n", ProcToString(proc), extIsDebuggerPresent);
				return (FARPROC)extIsDebuggerPresent;
			}
		case SYSLIBIDX_OLE32:
			if (!strcmp(proc,"CoCreateInstance")){
				pCoCreateInstance=(CoCreateInstance_Type)(*pGetProcAddress)(hModule, proc);
				OutTraceD("GetProcAddress: hooking proc=%s at addr=%x\n", ProcToString(proc), pCoCreateInstance);
				return (FARPROC)extCoCreateInstance;
			}
			break;
		case SYSLIBIDX_OPENGL:
		//default:
			if (remap=Remap_gl_ProcAddress(proc, hModule)) return remap;
		}
	}
	else {
		switch(idx){
		case SYSLIBIDX_DIRECTDRAW:
			switch((DWORD)proc){
			case 0x0008: // DirectDrawCreate
				pDirectDrawCreate=(DirectDrawCreate_Type)(*pGetProcAddress)(hModule, proc);
				OutTraceD("GetProcAddress: hooking proc=%s at addr=%x\n", ProcToString(proc), pDirectDrawCreate);
				return (FARPROC)extDirectDrawCreate;
				break;
			case 0x000A: // DirectDrawCreateEx
				pDirectDrawCreateEx=(DirectDrawCreateEx_Type)(*pGetProcAddress)(hModule, proc);
				OutTraceD("GetProcAddress: hooking proc=%s at addr=%x\n", ProcToString(proc), pDirectDrawCreateEx);
				return (FARPROC)extDirectDrawCreateEx;
				break;
			case 0x000B: // DirectDrawEnumerateA
				pDirectDrawEnumerate=(DirectDrawEnumerate_Type)(*pGetProcAddress)(hModule, proc);
				OutTraceP("GetProcAddress: hooking proc=%s at addr=%x\n", proc, pDirectDrawEnumerate);
				return (FARPROC)extDirectDrawEnumerateProxy;
				break;
			case 0x000C: // DirectDrawEnumerateExA
				pDirectDrawEnumerateEx=(DirectDrawEnumerateEx_Type)(*pGetProcAddress)(hModule, proc);
				OutTraceP("GetProcAddress: hooking proc=%s at addr=%x\n", proc, pDirectDrawEnumerateEx);
				return (FARPROC)extDirectDrawEnumerateExProxy;
				break;
			}
			break;
		case SYSLIBIDX_USER32:
			if ((DWORD)proc == 0x0020){ // ChangeDisplaySettingsA
				pChangeDisplaySettings=(ChangeDisplaySettings_Type)(*pGetProcAddress)(hModule, proc);
				OutTraceD("GetProcAddress: hooking proc=%s at addr=%x\n", ProcToString(proc), pChangeDisplaySettings);
				return (FARPROC)extChangeDisplaySettings;
			}
			break;
#ifndef ANTICHEATING
		case SYSLIBIDX_KERNEL32:
			if ((DWORD)proc == 0x022D){ // "IsDebuggerPresent"
				OutTraceD("GetProcAddress: hooking proc=%s at addr=%x\n", ProcToString(proc), extIsDebuggerPresent);
				return (FARPROC)extIsDebuggerPresent;
			}
#endif
		case SYSLIBIDX_OLE32:
			if ((DWORD)proc == 0x0011){ // "CoCreateInstance"
				pCoCreateInstance=(CoCreateInstance_Type)(*pGetProcAddress)(hModule, proc);
				OutTraceD("GetProcAddress: hooking proc=%s at addr=%x\n", ProcToString(proc), pCoCreateInstance);
				return (FARPROC)extCoCreateInstance;
			}
			break;
		}
	}

	ret=(*pGetProcAddress)(hModule, proc);
	OutTraceD("GetProcAddress: ret=%x\n", ret);
	return ret;
}

HDC WINAPI extGDIGetDC(HWND hwnd)
{
	HDC ret;
	HWND lochwnd;
	OutTraceD("GDI.GetDC: hwnd=%x\n", hwnd);
	lochwnd=hwnd;
	if (isFullScreen && ((hwnd==0) || (hwnd==(*pGetDesktopWindow)()))) {
		OutTraceD("GDI.GetDC: desktop remapping hwnd=%x->%x\n", hwnd, hWnd);
		lochwnd=hWnd;
	}
	ret=(*pGDIGetDC)(lochwnd);
	if(ret){
		OutTraceD("GDI.GetDC: hwnd=%x ret=%x\n", lochwnd, ret);
	}
	else{
		int err;
		err=GetLastError();
		OutTraceE("GDI.GetDC ERROR: hwnd=%x err=%d at %d\n", lochwnd, err, __LINE__);
		if((err==ERROR_INVALID_WINDOW_HANDLE) && (lochwnd!=hwnd)){
			ret=(*pGDIGetDC)(hwnd);	
			if(ret)
				OutTraceD("GDI.GetDC: hwnd=%x ret=%x\n", hwnd, ret);
			else
				OutTraceE("GDI.GetDC ERROR: hwnd=%x err=%d at %d\n", hwnd, GetLastError(), __LINE__);
		}
	}
	return ret;
}

HDC WINAPI extGDIGetWindowDC(HWND hwnd)
{
	HDC ret;
	HWND lochwnd;
	OutTraceD("GDI.GetWindowDC: hwnd=%x\n", hwnd);
	lochwnd=hwnd;
	if ((hwnd==0) || (hwnd==(*pGetDesktopWindow)())) {
		OutTraceD("GDI.GetWindowDC: desktop remapping hwnd=%x->%x\n", hwnd, hWnd);
		lochwnd=hWnd;
	}
	ret=(*pGDIGetWindowDC)(lochwnd);
	if(ret){
		OutTraceD("GDI.GetWindowDC: hwnd=%x ret=%x\n", lochwnd, ret);
	}
	else{
		int err;
		err=GetLastError();
		OutTraceE("GDI.GetWindowDC ERROR: hwnd=%x err=%d at %d\n", lochwnd, err, __LINE__);
		if((err==ERROR_INVALID_WINDOW_HANDLE) && (lochwnd!=hwnd)){
			ret=(*pGDIGetWindowDC)(hwnd);
			if(ret)
				OutTraceD("GDI.GetWindowDC: hwnd=%x ret=%x\n", hwnd, ret);
			else
				OutTraceE("GDI.GetWindowDC ERROR: hwnd=%x err=%d at %d\n", hwnd, GetLastError(), __LINE__);
		}
	}
	return ret;
}

int WINAPI extGDIReleaseDC(HWND hwnd, HDC hDC)
{
	int res;
	OutTraceD("GDI.ReleaseDC: hwnd=%x hdc=%x\n", hwnd, hDC);
	if (hwnd==0) hwnd=hWnd;
	res=(*pGDIReleaseDC)(hwnd, hDC);
	if (!res) OutTraceE("GDI.ReleaseDC ERROR: err=%d at %d\n", GetLastError(), __LINE__);
	return(res);
}

HDC WINAPI extGDICreateDC(LPSTR Driver, LPSTR Device, LPSTR Output, CONST DEVMODE *InitData)
{
	HDC WinHDC, RetHDC;
	OutTraceD("GDI.CreateDC: Driver=%s Device=%s Output=%s InitData=%x\n", 
		Driver?Driver:"(NULL)", Device?Device:"(NULL)", Output?Output:"(NULL)", InitData);

	if (!Driver || !strncmp(Driver,"DISPLAY",7)) {
		OutTraceD("GDI.CreateDC: returning window surface DC\n");
		WinHDC=(*pGDIGetDC)(hWnd);
		RetHDC=(*pCreateCompatibleDC)(WinHDC);
		(*pGDIReleaseDC)(hWnd, WinHDC);
	}
	else{
		RetHDC=(*pCreateDC)(Driver, Device, Output, InitData);
	}
	if(RetHDC)
		OutTraceD("GDI.CreateDC: returning HDC=%x\n", RetHDC);
	else
		OutTraceE("GDI.CreateDC ERROR: err=%d at %d\n", GetLastError(), __LINE__);
	return RetHDC;
}

HDC WINAPI extGDICreateCompatibleDC(HDC hdc)
{
	HDC RetHdc, SrcHdc;
	extern HDC PrimSurfaceHDC;
	extern LPDIRECTDRAWSURFACE lpDDSHDC, lpDDSPrimHDC;
	extern GetDC_Type pGetDC;

	OutTraceD("GDI.CreateCompatibleDC: hdc=%x\n", hdc);
	if(hdc==0){
		SrcHdc=(*pGDIGetDC)(hWnd);
		OutTraceD("GDI.CreateCompatibleDC: duplicating win HDC hWnd=%x\n", hWnd); 
	}
	RetHdc=(*pCreateCompatibleDC)(hdc);
	if(RetHdc)
		OutTraceD("GDI.CreateCompatibleDC: returning HDC=%x\n", RetHdc);
	else
		OutTraceE("GDI.CreateCompatibleDC ERROR: err=%d at %d\n", GetLastError(), __LINE__);
	return RetHdc;
}

BOOL WINAPI extGDIBitBlt(HDC hdcDest, int nXDest, int nYDest, int nWidth, int nHeight, HDC hdcSrc, int nXSrc, int nYSrc, DWORD dwRop)
{
	BOOL res;
	extern BOOL isWithinDialog;

	OutTraceD("GDI.BitBlt: HDC=%x nXDest=%d nYDest=%d nWidth=%d nHeight=%d hdcSrc=%x nXSrc=%d nYSrc=%d dwRop=%x(%s)\n", 
		hdcDest, nXDest, nYDest, nWidth, nHeight, hdcSrc, nXSrc, nYSrc, dwRop, ExplainROP(dwRop));

#ifdef UNSTRETCH
	res=(*pBitBlt)(hdcDest, nXDest, nYDest, nWidth, nHeight, hdcSrc, nXSrc, nYSrc, dwRop);
	if(!res) OutTraceE("GDI.BitBlt: ERROR err=%d at %d\n", GetLastError(), __LINE__);
#else
	if (isFullScreen){
		RECT client;
		int nWDest, nHDest;
		(*pGetClientRect)(hWnd, &client);
		nXDest= nXDest * client.right / VirtualScr.dwWidth;
		nYDest= nYDest * client.bottom / VirtualScr.dwHeight;
		nWDest= nWidth * client.right / VirtualScr.dwWidth;
		nHDest= nHeight * client.bottom / VirtualScr.dwHeight;
		res=(*pStretchBlt)(hdcDest, nXDest, nYDest, nWDest, nHDest, hdcSrc, nXSrc, nYSrc, nWidth, nHeight, dwRop);
	}
	else {
		res=(*pBitBlt)(hdcDest, nXDest, nYDest, nWidth, nHeight, hdcSrc, nXSrc, nYSrc, dwRop);
	}
	if(!res) OutTraceE("GDI.BitBlt: ERROR err=%d at %d\n", GetLastError(), __LINE__);

#endif
	return res;
}

BOOL WINAPI extGDIPatBlt(HDC hdcDest, int nXDest, int nYDest, int nWidth, int nHeight, DWORD dwRop)
{
	BOOL res;

	OutTraceD("GDI.PatBlt: HDC=%x nXDest=%d nYDest=%d nWidth=%d nHeight=%d dwRop=%x(%s)\n", 
		hdcDest, nXDest, nYDest, nWidth, nHeight, dwRop, ExplainROP(dwRop));

#ifdef UNSTRETCH
	res=(*pPatBlt)(hdcDest, nXDest, nYDest, nWidth, nHeight, dwRop);
	if(!res) OutTraceE("GDI.PatBlt: ERROR err=%d at %d\n", GetLastError(), __LINE__);
#else
	if (isFullScreen){
		RECT client;
		int nWDest, nHDest;
		(*pGetClientRect)(hWnd, &client);
		nXDest= nXDest * client.right / VirtualScr.dwWidth;
		nYDest= nYDest * client.bottom / VirtualScr.dwHeight;
		nWDest= nWidth * client.right / VirtualScr.dwWidth;
		nHDest= nHeight * client.bottom / VirtualScr.dwHeight;
		res=(*pPatBlt)(hdcDest, nXDest, nYDest, nWDest, nHDest, dwRop);
	}
	else {
		res=(*pPatBlt)(hdcDest, nXDest, nYDest, nWidth, nHeight, dwRop);
	}
	if(!res) OutTraceE("GDI.PatBlt: ERROR err=%d at %d\n", GetLastError(), __LINE__);

#endif
	return res;
}

BOOL WINAPI extGDIStretchBlt(HDC hdcDest, int nXDest, int nYDest, int nWidth, int nHeight, 
							 HDC hdcSrc, int nXSrc, int nYSrc, int nWSrc, int nHSrc, DWORD dwRop)
{
	BOOL res;

	OutTraceD("GDI.StretchBlt: HDC=%x nXDest=%d nYDest=%d nWidth=%d nHeight=%d hdcSrc=%x nXSrc=%d nYSrc=%d nWSrc=%d nHSrc=%d dwRop=%x(%s)\n", 
		hdcDest, nXDest, nYDest, nWidth, nHeight, hdcSrc, nXSrc, nYSrc, nWSrc, nHSrc, dwRop, ExplainROP(dwRop));

	res=(*pStretchBlt)(hdcDest, nXDest, nYDest, nWidth, nHeight, hdcSrc, nXSrc, nYSrc, nWSrc, nHSrc, dwRop);
	if(!res) OutTraceE("GDI.StretchBlt: ERROR err=%d at %d\n", GetLastError(), __LINE__);
	return res;
}

BOOL WINAPI extGDIDeleteDC(HDC hdc)
{
	BOOL res;
	OutTraceD("GDI.DeleteDC: hdc=%x\n", hdc);
	res=(*pDeleteDC)(hdc);
	if(!res) OutTraceE("GDI.DeleteDC: ERROR err=%d at %d\n", GetLastError(), __LINE__);
	return res;
}

static HANDLE AutoRefreshThread;
static DWORD dwThrdId;
void AutoRefresh(HDC hdc)
{
//	HRESULT res;
//	extern void dx_ScreenRefresh();
	while(1){
		Sleep(10);
		(*pInvalidateRect)(hWnd, 0, FALSE);
	}
}

int WINAPI extGDISaveDC(HDC hdc)
{
	int ret;

	ret=(*pGDISaveDC)(hdc);
	//ret=1;
	OutTraceD("GDI.SaveDC: hdc=%x ret=%x\n", hdc, ret);
	//AutoRefreshThread=CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)AutoRefresh, (LPVOID)hdc, 0, &dwThrdId);
	return ret;
}

BOOL WINAPI extGDIRestoreDC(HDC hdc, int nSavedDC)
{
	BOOL ret;

	ret=(*pGDIRestoreDC)(hdc, nSavedDC);
	//ret=1;
	OutTraceD("GDI.RestoreDC: hdc=%x nSavedDC=%x ret=%x\n", hdc, nSavedDC, ret);
	//TerminateThread(AutoRefreshThread, 0);
	return ret;
}

/* -------------------------------------------------------------------- */
// directdraw supported GDI calls
/* -------------------------------------------------------------------- */

// PrimHDC: DC handle of the selected DirectDraw primary surface. NULL when invalid.
static HDC PrimHDC=NULL;

HDC WINAPI extDDCreateCompatibleDC(HDC hdc)
{
	HDC RetHdc, SrcHdc;
	extern LPDIRECTDRAWSURFACE lpDDSPrimHDC;
	extern GetDC_Type pGetDC;

	OutTraceD("GDI.CreateCompatibleDC: hdc=%x\n", hdc);

	if(hdc==0 && pGetDC && isFullScreen){
		if(!lpDDSPrimHDC) lpDDSPrimHDC=GetPrimarySurface();
		(*pGetDC)(lpDDSPrimHDC,&SrcHdc);
		OutTraceD("GDI.CreateCompatibleDC: duplicating screen HDC lpDDSPrimHDC=%x\n", lpDDSPrimHDC); 
		RetHdc=(*pCreateCompatibleDC)(SrcHdc);
		(*pReleaseDC)(lpDDSPrimHDC,SrcHdc);
	}
	else
		RetHdc=(*pCreateCompatibleDC)(hdc);

	if(RetHdc)
		OutTraceD("GDI.CreateCompatibleDC: returning HDC=%x\n", RetHdc);
	else
		OutTraceE("GDI.CreateCompatibleDC ERROR: err=%d at %d\n", GetLastError(), __LINE__);

	return RetHdc;
}

BOOL WINAPI extDDDeleteDC(HDC hdc)
{
	BOOL res;

	OutTraceD("GDI.DeleteDC: hdc=%x\n", hdc);

	res=(*pDeleteDC)(hdc);
	if(!res) OutTraceE("GDI.DeleteDC: ERROR err=%d at %d\n", GetLastError(), __LINE__);
	return res;
}

static HDC WINAPI winDDGetDC(HWND hwnd, char *api)
{
	HDC hdc;
	HRESULT res;
	extern HRESULT WINAPI extGetDC(LPDIRECTDRAWSURFACE, HDC FAR *);

	OutTraceD("%s: hwnd=%x\n", api, hwnd);

	lpDDSPrimHDC=GetPrimarySurface();

	if(lpDDSPrimHDC){ 
		if (PrimHDC){
			OutTraceD("%s: reusing primary hdc\n", api);
			(*pUnlockMethod(lpDDSPrimHDC))(lpDDSPrimHDC, NULL);
			hdc=PrimHDC;
		}
		else{
			OutTraceD("%s: get hdc from PRIMARY surface lpdds=%x\n", api, lpDDSPrimHDC);
			//res=(*pGetDC)(lpDDSPrimHDC,&hdc);
			//res=lpDDSPrimHDC->GetDC(&hdc);
			res=extGetDC(lpDDSPrimHDC,&hdc);
			if(res) {
				OutTraceE("%s: GetDC(%x) ERROR %x(%s) at %d\n", api, lpDDSPrimHDC, res, ExplainDDError(res), __LINE__);
				if(res==DDERR_DCALREADYCREATED){
					// try recovery....
					(*pReleaseDC)(lpDDSPrimHDC,NULL);
					//res=(*pGetDC)(lpDDSPrimHDC,&hdc);
					res=extGetDC(lpDDSPrimHDC,&hdc);
				}
				if(res)return 0;
			}
			PrimHDC=hdc;
		}
	}
	else {
		hdc=(*pGDIGetDC)(hwnd ? hwnd : hWnd);
		OutTraceD("%s: returning window DC handle hwnd=%x hdc=%x\n", api, hwnd, hdc);
		PrimHDC=NULL;
	}

	if(hdc)
		OutTraceD("%s: hwnd=%x hdc=%x\n", api, hwnd, hdc);
	else
		OutTraceE("%s: ERROR err=%d at %d\n", api, GetLastError, __LINE__);
	return(hdc);
}

HDC WINAPI extDDCreateDC(LPSTR Driver, LPSTR Device, LPSTR Output, CONST DEVMODE *InitData)
{
	HDC RetHDC;
	OutTraceD("GDI.CreateDC: Driver=%s Device=%s Output=%s InitData=%x\n", 
		Driver?Driver:"(NULL)", Device?Device:"(NULL)", Output?Output:"(NULL)", InitData);

	if (!Driver || !strncmp(Driver,"DISPLAY",7)) {
		//HDC PrimHDC;
		LPDIRECTDRAWSURFACE lpdds;
		OutTraceD("GDI.CreateDC: returning primary surface DC\n");
		lpdds=GetPrimarySurface();
		(*pGetDC)(lpdds, &PrimHDC);
		RetHDC=(*pCreateCompatibleDC)(PrimHDC);
		(*pReleaseDC)(lpdds, PrimHDC);
	}
	else{
		RetHDC=(*pCreateDC)(Driver, Device, Output, InitData);
	}
	if(RetHDC)
		OutTraceD("GDI.CreateDC: returning HDC=%x\n", RetHDC);
	else
		OutTraceE("GDI.CreateDC ERROR: err=%d at %d\n", GetLastError(), __LINE__);
	return RetHDC;
}

HDC WINAPI extDDGetDC(HWND hwnd)
{
	HDC ret;
	ret=winDDGetDC(hwnd, "GDI.GetDC");
	return ret;
}

HDC WINAPI extDDGetWindowDC(HWND hwnd)
{
	HDC ret;
	ret=winDDGetDC(hwnd, "GDI.GetWindowDC");
	return ret;
}

int WINAPI extDDReleaseDC(HWND hwnd, HDC hDC)
{
	int res;
	extern HRESULT WINAPI extReleaseDC(LPDIRECTDRAWSURFACE, HDC);

	OutTraceD("GDI.ReleaseDC: hwnd=%x hdc=%x\n", hwnd, hDC);
	res=0;
//	if (hDC == PrimHDC /* && IsAScreenHDC(hDC) */){
	if ((hDC == PrimHDC) || (hwnd==0)){
		if(!lpDDSPrimHDC) lpDDSPrimHDC=GetPrimarySurface();
		OutTraceD("GDI.ReleaseDC: refreshing primary surface lpdds=%x\n",lpDDSPrimHDC);
		if(!lpDDSPrimHDC) return 0;
#if 0
		//extBlt(lpDDSPrimHDC, NULL, lpDDSPrimHDC, NULL, 0, NULL);
		sBlt("GDI.ReleaseDC", lpDDSPrimHDC, NULL, lpDDSPrimHDC, NULL, 0, NULL, 0);
		res=(*pUnlockMethod(lpDDSPrimHDC))(lpDDSPrimHDC, NULL);
		//if(res) OutTraceE("Unlock ERROR: res=%x(%s) at %d\n",  res, ExplainDDError(res), __LINE__);
		res=(*pReleaseDC)(lpDDSPrimHDC,hDC);
		if(res) OutTraceE("GDI.ReleaseDC: ReleaseDC ERROR=%x(%s) at %d\n", res, ExplainDDError(res), __LINE__);
#else
		//lpDDSPrimHDC->ReleaseDC(hDC);
		extReleaseDC(lpDDSPrimHDC, hDC);
#endif
		PrimHDC=NULL;
		res=1; // 1 = OK
	}
	else {
		res=(*pGDIReleaseDC)(hwnd, hDC);
		if (!res) OutTraceE("GDI.ReleaseDC: ERROR err=%d at %d\n", GetLastError(), __LINE__);
	}

	return(res);
}

BOOL WINAPI extDDBitBlt(HDC hdcDest, int nXDest, int nYDest, int nWidth, int nHeight, HDC hdcSrc, int nXSrc, int nYSrc, DWORD dwRop)
{
	BOOL ret;
	HRESULT res;
	extern HRESULT WINAPI extGetDC(LPDIRECTDRAWSURFACE, HDC FAR *);

#if 0
	OutTraceD("GDI.BitBlt: HDC=%x nXDest=%d nYDest=%d nWidth=%d nHeight=%d hdcSrc=%x nXSrc=%d nYSrc=%d dwRop=%x\n", 
		hdcDest, nXDest, nYDest, nWidth, nHeight, hdcSrc, nXSrc, nYSrc, dwRop);

	ret=1; // OK

	if(hdcDest==0) extGetDC(lpDDSPrimHDC, &hdcDest);
	if(hdcDest != hdcSrc){
		ret=(*pBitBlt)(hdcDest, nXDest, nYDest, nWidth, nHeight, hdcSrc, nXSrc, nYSrc, dwRop);
		if(!ret) {
			OutTraceE("GDI.BitBlt: ERROR err=%d at %d\n", GetLastError(), __LINE__);
			return ret;
		}
	}

//	if(hdcDest==PrimHDC){
	{
		if(!lpDDSPrimHDC) lpDDSPrimHDC=GetPrimarySurface();
		OutTraceD("GDI.BitBlt: refreshing primary surface lpdds=%x\n",lpDDSPrimHDC);
		//extBlt(lpDDSPrimHDC, NULL, lpDDSPrimHDC, NULL, 0, NULL);
		sBlt("GDI.BitBlt", lpDDSPrimHDC, NULL, lpDDSPrimHDC, NULL, 0, NULL, 0);
		res=(*pUnlockMethod(lpDDSPrimHDC))(lpDDSPrimHDC, NULL);
		//if(res) OutTraceE("Unlock: ERROR err=%x(%s) at %d\n", res, ExplainDDError(res), __LINE__);
	}
#else
	OutTraceD("GDI.BitBlt: HDC=%x nXDest=%d nYDest=%d nWidth=%d nHeight=%d hdcSrc=%x nXSrc=%d nYSrc=%d dwRop=%x(%s)\n", 
		hdcDest, nXDest, nYDest, nWidth, nHeight, hdcSrc, nXSrc, nYSrc, dwRop, ExplainROP(dwRop));

	ret=1; // OK

	if(hdcDest==0) hdcDest=PrimHDC;
	if(hdcDest==0) {
		lpDDSPrimHDC=GetPrimarySurface();
		res=extGetDC(lpDDSPrimHDC, &PrimHDC);
		hdcDest=PrimHDC;
	}

	res=(*pBitBlt)(hdcDest, nXDest, nYDest, nWidth, nHeight, hdcSrc, nXSrc, nYSrc, dwRop);
	if(!res) OutTraceE("GDI.BitBlt: ERROR err=%d at %d\n", GetLastError(), __LINE__);

		res=(*pBitBlt)(NULL, nXDest, nYDest, nWidth, nHeight, hdcSrc, nXSrc, nYSrc, dwRop);

	//if(hdcDest==PrimHDC){
	//	OutTraceD("GDI.BitBlt: refreshing primary surface lpdds=%x\n",lpDDSPrimHDC);
	//	//extBlt(lpDDSPrimHDC, NULL, lpDDSPrimHDC, NULL, 0, NULL);
	//	res=sBlt("GDI.BitBlt", lpDDSPrimHDC, NULL, lpDDSPrimHDC, NULL, 0, NULL, 0);
	//	if(res) OutTraceE("GDI.BitBlt: ERROR err=%x(%s) at %d\n", res, ExplainDDError(res), __LINE__);
	//	res=(*pUnlockMethod(lpDDSPrimHDC))(lpDDSPrimHDC, NULL);
	//	if(res) OutTraceE("Unlock: ERROR err=%x(%s) at %d\n", res, ExplainDDError(res), __LINE__);
	//}
	if(!res) ret=0;
#endif

	return ret;
}

BOOL WINAPI extDDStretchBlt(HDC hdcDest, int nXDest, int nYDest, int nWidth, int nHeight, 
							 HDC hdcSrc, int nXSrc, int nYSrc, int nWSrc, int nHSrc, DWORD dwRop)
{
	BOOL ret;
	HRESULT res;
	RECT ClientRect;

	OutTraceD("GDI.StretchBlt: HDC=%x nXDest=%d nYDest=%d nWidth=%d nHeight=%d hdcSrc=%x nXSrc=%d nYSrc=%d nWSrc=%x nHSrc=%x dwRop=%x\n", 
		hdcDest, nXDest, nYDest, nWidth, nHeight, hdcSrc, nXSrc, nYSrc, nWSrc, nHSrc, dwRop);

	if(hdcDest != hdcSrc){
		(*pGetClientRect)(hWnd,&ClientRect);
		ret=(*pStretchBlt)(hdcDest, nXDest, nYDest, nWidth, nHeight, hdcSrc, nXSrc, nYSrc, nWidth, nHeight, dwRop);
		if(!ret) {
			OutTraceE("GDI.StretchBlt: ERROR err=%d at %d\n", GetLastError(), __LINE__);
			return ret;
		}
	}
//	if(hdcDest==PrimHDC){
	{
		if(!lpDDSPrimHDC) lpDDSPrimHDC=GetPrimarySurface();
		OutTraceD("GDI.StretchBlt: refreshing primary surface lpdds=%x\n",lpDDSPrimHDC);
		//extBlt(lpDDSPrimHDC, NULL, lpDDSPrimHDC, NULL, 0, NULL);
		sBlt("GDI.StretchBlt", lpDDSPrimHDC, NULL, lpDDSPrimHDC, NULL, 0, NULL, 0);
		res=(*pUnlockMethod(lpDDSPrimHDC))(lpDDSPrimHDC, NULL);
		//if(res) OutTraceE("Unlock: ERROR err=%x(%s) at %d\n", res, ExplainDDError(res), __LINE__);
	}
	return ret;
}

HDC WINAPI extBeginPaint(HWND hwnd, LPPAINTSTRUCT lpPaint)
{
	HDC hdc;
	extern HRESULT WINAPI extGetDC(LPDIRECTDRAWSURFACE, HDC FAR *);

	// proxy part ...
	OutTraceD("GDI.BeginPaint: hwnd=%x lpPaint=%x FullScreen=%x\n", hwnd, lpPaint, isFullScreen);
	hdc=(*pBeginPaint)(hwnd, lpPaint);

	// if not in fullscreen mode, that's all!
	if(!isFullScreen) return hdc;

	// on MAPGDITOPRIMARY, return the PrimHDC handle instead of the window DC
	if(dwFlags & MAPGDITOPRIMARY) {
		if(pGetDC && lpDDSPrimHDC){
			//(*pGetDC)(lpDDSPrimHDC,&PrimHDC);
			extGetDC(lpDDSPrimHDC,&PrimHDC);
			OutTraceD("GDI.BeginPaint: redirect hdc=%x -> PrimHDC=%x\n", hdc, PrimHDC);
			hdc=PrimHDC;
		}
		else {
			OutTraceD("GDI.BeginPaint: hdc=%x\n", hdc);
		}
	}

	// on CLIENTREMAPPING, resize the paint area to virtual screen size
	if(dwFlags & CLIENTREMAPPING){
		lpPaint->rcPaint.top=0;
		lpPaint->rcPaint.left=0;
		lpPaint->rcPaint.right=VirtualScr.dwWidth;
		lpPaint->rcPaint.bottom=VirtualScr.dwHeight;
	}

	return hdc;
}

BOOL WINAPI extEndPaint(HWND hwnd, const PAINTSTRUCT *lpPaint)
{
	BOOL ret;

	// proxy part ...
	OutTraceD("GDI.EndPaint: hwnd=%x lpPaint=%x\n", hwnd, lpPaint);
	ret=(*pEndPaint)(hwnd, lpPaint);
	OutTraceD("GDI.EndPaint: hwnd=%x ret=%x\n", hwnd, ret);
	if(!ret) OutTraceE("GDI.EndPaint ERROR: err=%d at %d\n", GetLastError(), __LINE__);

	// if not in fullscreen mode, that's all!
	if(!isFullScreen) return ret;

	return ret;
}

/* --------------------------------------------------------------------------- */
// C&C Tiberian Sun: mixes DirectDraw with GDI Dialogues.
// To make them visible, the lpDialog call had to be hooked to insert a periodic 
// InvalidateRect call to make GDI appear on screen
/* --------------------------------------------------------------------------- */

BOOL __cdecl TraceChildWin(HWND hwnd, LPARAM lParam)
{
	POINT pos={0,0};
	RECT child;
	(*pGetClientRect)(hwnd, &child);
	(*pClientToScreen)(hwnd,&pos);
	OutTraceD("Father hwnd=%x has child=%x pos=(%d,%d) size=(%d,%d)\n",
		HWND(lParam), hwnd, pos.x, pos.y, child.right, child.bottom);
	return TRUE;
}

BOOL isWithinDialog=FALSE;

HWND WINAPI extCreateDialogIndirectParam(HINSTANCE hInstance, LPCDLGTEMPLATE lpTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM lParamInit)
{
	HWND RetHWND;
	isWithinDialog=TRUE;
	OutTraceD("CreateDialogIndirectParam: hInstance=%x lpTemplate=%s hWndParent=%x lpDialogFunc=%x lParamInit=%x\n",
		hInstance, "tbd", hWndParent, lpDialogFunc, lParamInit);
	if(hWndParent==NULL) hWndParent=hWnd;
	RetHWND=(*pCreateDialogIndirectParam)(hInstance, lpTemplate, hWndParent, lpDialogFunc, lParamInit);

	WhndStackPush(RetHWND, (WNDPROC)lpDialogFunc);
	if(!(*pSetWindowLong)(RetHWND, DWL_DLGPROC, (LONG)extDialogWindowProc))
		OutTraceE("SetWindowLong: ERROR err=%d at %d\n", GetLastError(), __LINE__);

	//if(!(*pMoveWindow)(RetHWND, iPosX, iPosY, iSizX, iSizY, FALSE))
	//	OutTraceE("MoveWindow: ERROR err=%d at %d\n", GetLastError(), __LINE__);
	OutTraceD("CreateDialogIndirectParam: hwnd=%x\n", RetHWND);
	isWithinDialog=FALSE;
	//if (IsDebug) EnumChildWindows(RetHWND, (WNDENUMPROC)TraceChildWin, (LPARAM)RetHWND);
	return RetHWND;
}

HWND WINAPI extCreateDialogParam(HINSTANCE hInstance, LPCTSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM lParamInit)
{
	HWND RetHWND;
	isWithinDialog=TRUE;
	OutTraceD("CreateDialogParam: hInstance=%x lpTemplateName=%s hWndParent=%x lpDialogFunc=%x lParamInit=%x\n",
		hInstance, "tbd", hWndParent, lpDialogFunc, lParamInit);
	if(hWndParent==NULL) hWndParent=hWnd;
	RetHWND=(*pCreateDialogParam)(hInstance, lpTemplateName, hWndParent, lpDialogFunc, lParamInit);

	WhndStackPush(RetHWND, (WNDPROC)lpDialogFunc);
	if(!(*pSetWindowLong)(RetHWND, DWL_DLGPROC, (LONG)extDialogWindowProc))
		OutTraceE("SetWindowLong: ERROR err=%d at %d\n", GetLastError(), __LINE__);

	//if(!(*pMoveWindow)(RetHWND, iPosX, iPosY, iSizX, iSizY, FALSE))
	//	OutTraceE("MoveWindow: ERROR err=%d at %d\n", GetLastError(), __LINE__);
	OutTraceD("CreateDialogParam: hwnd=%x\n", RetHWND);
	isWithinDialog=FALSE;
	//if (IsDebug) EnumChildWindows(RetHWND, (WNDENUMPROC)TraceChildWin, (LPARAM)RetHWND);
	return RetHWND;
}

HWND WINAPI extSetCapture(HWND hwnd)
{
	OutTraceD("SetCapture: hwnd=%x\n", hwnd);
	return NULL;
}

BOOL WINAPI extReleaseCapture(void)
{
	OutTraceD("ReleaseCapture\n");
	return 1;
}

BOOL WINAPI extDDInvalidateRect(HWND hwnd, RECT *lpRect, BOOL bErase)
{
	if(lpRect)
		OutTraceD("InvalidateRect: hwnd=%x rect=(%d,%d)-(%d,%d) erase=%x\n",
		hwnd, lpRect->left, lpRect->top, lpRect->right, lpRect->bottom, bErase);
	else
		OutTraceD("InvalidateRect: hwnd=%x rect=NULL erase=%x\n",
		hwnd, bErase);

	return (*pInvalidateRect)(hwnd, NULL, bErase);
}

BOOL WINAPI extInvalidateRect(HWND hwnd, RECT *lpRect, BOOL bErase)
{
	if(lpRect)
		OutTraceD("InvalidateRect: hwnd=%x rect=(%d,%d)-(%d,%d) erase=%x\n",
		hwnd, lpRect->left, lpRect->top, lpRect->right, lpRect->bottom, bErase);
	else
		OutTraceD("InvalidateRect: hwnd=%x rect=NULL erase=%x\n",
		hwnd, bErase);

	return (*pInvalidateRect)(hwnd, NULL, bErase);
}

BOOL WINAPI extInvalidateRgn(HWND hwnd, HRGN hRgn, BOOL bErase)
{
	OutTraceD("InvalidateRgn: hwnd=%x hrgn=%x erase=%x\n",
	hwnd, hRgn, bErase);
	return (*pInvalidateRgn)(hwnd, hRgn, bErase);
}

/* --------------------------------------------------------------------------- */

// v2.1.75: Hooking for GDI32 CreatePalette, SelectPalette, RealizePalette: 
// maps the GDI palette to the buffered DirectDraw one. This fixes the screen 
// output for "Dementia" (a.k.a. "Armed & Delirious").

typedef struct tagDxWndLOGPALETTE
    {
    WORD palVersion;
    WORD palNumEntries;
    PALETTEENTRY palPalEntry[ 256 ];
    } 	DxWndLOGPALETTE;

DxWndLOGPALETTE MyPal;
//BOOL G_bForceBackground;

HPALETTE WINAPI extGDICreatePalette(CONST LOGPALETTE *plpal)
{
	HPALETTE ret;
	int idx;

	OutTraceD("GDI.CreatePalette: plpal=%x version=%x NumEntries=%x\n", plpal, plpal->palVersion, plpal->palNumEntries);
	ret=(*pGDICreatePalette)(plpal);
	if(IsDebug){
		OutTraceD("PalEntry[%x]= ", MyPal.palNumEntries);
		for(idx=0; idx<MyPal.palNumEntries; idx++) OutTraceD("(%x)", plpal->palPalEntry[idx]);
		OutTraceD("\n");
	}
	MyPal.palVersion=plpal->palVersion;
	MyPal.palNumEntries=plpal->palNumEntries;
	if(MyPal.palNumEntries>256) MyPal.palNumEntries=256;
	for(idx=0; idx<MyPal.palNumEntries; idx++) MyPal.palPalEntry[idx]=plpal->palPalEntry[idx];
	OutTraceD("GDI.CreatePalette: hPalette=%x\n", ret);
	return ret;
}

HPALETTE WINAPI extSelectPalette(HDC hdc, HPALETTE hpal, BOOL bForceBackground)
{
	HPALETTE ret;

	ret=(*pSelectPalette)(hdc, hpal, bForceBackground);
	//G_bForceBackground=bForceBackground;
	OutTraceD("GDI.SelectPalette: hdc=%x hpal=%x ForceBackground=%x ret=%x\n", hdc, hpal, bForceBackground, ret);
	return ret;
}

UINT WINAPI extRealizePalette(HDC hdc)
{
	UINT ret;
	extern void mySetPalette(int, int, LPPALETTEENTRY);

	ret=(*pRealizePalette)(hdc);
	OutTraceD("GDI.RealizePalette: hdc=%x ret=%x\n", hdc, ret);
	// quick & dirty implementation through a nasty global:
	// if the SelectPalette didn't force to the background (arg bForceBackground==FALSE)
	// then don't override the current palette set by the DirectDrawPalette class.
	// should be cleaned up a little....
	// maybe not: now both Diable & Dementia colors are working...
	if(dwFlags & EMULATESURFACE)
		mySetPalette(0, MyPal.palNumEntries, MyPal.palPalEntry);
	// DEBUGGING
	if(IsDebug){
		int idx;
		OutTraceD("PaletteEntries[%x]= ", MyPal.palNumEntries);
		for(idx=0; idx<MyPal.palNumEntries; idx++) OutTraceD("(%x)", PaletteEntries[idx]);
		OutTraceD("\n");
	}
//	return 1;
	return ret;
}

// In emulated mode (when color depyth is 8BPP ?) it may happen that the game
// expects to get the requested system palette entries, while the 32BPP screen
// returns 0. "Mission Frce Cyberstorm" is one of these. Returning the same
// value as nEntries, even though lppe is untouched, fixes the problem.

UINT WINAPI extGetSystemPaletteEntries(HDC hdc, UINT iStartIndex, UINT nEntries, LPPALETTEENTRY lppe)
{
	int ret;
	OutTraceD("GetSystemPaletteEntries: hdc=%x start=%d num=%d\n", hdc, iStartIndex, nEntries);
	ret=(*pGetSystemPaletteEntries)(hdc, iStartIndex, nEntries, lppe);
	OutTraceD("GetSystemPaletteEntries: ret=%d\n", ret);
	if((ret == 0) && (dwFlags & EMULATESURFACE)) {
		OutTraceD("GetSystemPaletteEntries: fixing ret=%d\n", nEntries);
		ret = nEntries;
	}
	return ret;
}

HCURSOR WINAPI extSetCursor(HCURSOR hCursor)
{
	HCURSOR ret;

	ret=(*pSetCursor)(hCursor);
	OutTraceD("GDI.SetCursor: Cursor=%x, ret=%x\n", hCursor, ret);
	//MessageBox(0, "SelectPalette", "GDI32.dll", MB_OK | MB_ICONEXCLAMATION);
	return ret;
}

BOOL WINAPI extMoveWindow(HWND hwnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint)
{
	BOOL ret;
	extern HWND hParentWnd;
	OutTraceD("MoveWindow: hwnd=%x xy=(%d,%d) size=(%d,%d) repaint=%x indialog=%x\n",
		hwnd, X, Y, nWidth, nHeight, bRepaint, isWithinDialog);

	if((hwnd==hWnd) || (hwnd==hParentWnd)){
		OutTraceD("MoveWindow: prevent moving main win\n");
		if (nHeight && nWidth){
			OutTraceD("MoveWindow: setting screen size=(%d,%d)\n", nHeight, nWidth);
			VirtualScr.dwHeight=nHeight;
			VirtualScr.dwWidth=nWidth;
//			isFullScreen=TRUE;
		}
		return TRUE;
	}

	if((hwnd==0) || (hwnd==(*pGetDesktopWindow)())){
		// v2.1.93: happens in "Emergency Fighters for Life" ...
		OutTraceD("MoveWindow: prevent moving desktop win\n");
		// v2.1.93: moving the desktop seems a way to change its resolution?
		if (nHeight && nWidth){
			OutTraceD("MoveWindow: setting screen size=(%d,%d)\n", nHeight, nWidth);
			VirtualScr.dwHeight=nHeight;
			VirtualScr.dwWidth=nWidth;
//			isFullScreen=TRUE;
		}
		return TRUE;
	}

	if (isFullScreen){
		POINT upleft={0,0};
		RECT client;
		BOOL isChild;
		(*pClientToScreen)(hWnd,&upleft);
		(*pGetClientRect)(hWnd,&client);
		if ((*pGetWindowLong)(hwnd, GWL_STYLE) & WS_CHILD){
			isChild=TRUE;
			// child coordinate adjustement
			X = (X * client.right) / VirtualScr.dwWidth;
			Y = (Y * client.bottom) / VirtualScr.dwHeight;
			nWidth = (nWidth * client.right) / VirtualScr.dwWidth;
			nHeight = (nHeight * client.bottom) / VirtualScr.dwHeight;
		}
		else {
			isChild=FALSE;
			// regular win coordinate adjustement
			X = upleft.x + (X * client.right) / VirtualScr.dwWidth;
			Y = upleft.y + (Y * client.bottom) / VirtualScr.dwHeight;
			nWidth = (nWidth * client.right) / VirtualScr.dwWidth;
			nHeight = (nHeight * client.bottom) / VirtualScr.dwHeight;
		}
		OutTraceD("MoveWindow: DEBUG client=(%d,%d) screen=(%d,%d)\n",
			client.right, client.bottom, VirtualScr.dwWidth, VirtualScr.dwHeight);
		OutTraceD("MoveWindow: hwnd=%x child=%x relocated to xy=(%d,%d) size=(%d,%d)\n",
			hwnd, isChild, X, Y, nWidth, nHeight);
	}
	else{
		if((X==0)&&(Y==0)&&(nWidth==VirtualScr.dwWidth)&&(nHeight==VirtualScr.dwHeight)){
			// evidently, this was supposed to be a fullscreen window....
			RECT screen;
			POINT upleft = {0,0};
			(*pGetClientRect)(hWnd,&screen);
			(*pClientToScreen)(hWnd,&upleft);
			X=upleft.x;
			Y=upleft.y;
			nWidth=screen.right;
			nHeight=screen.bottom;
			OutTraceD("MoveWindow: fixed BIG win pos=(%d,%d) size=(%d,%d)\n", X, Y, nWidth, nHeight);
		}
	}


	ret=(*pMoveWindow)(hwnd, X, Y, nWidth, nHeight, bRepaint);
	if(!ret) OutTraceE("MoveWindow: ERROR err=%d at %d\n", GetLastError(), __LINE__);
	return ret;
} 

LPTOP_LEVEL_EXCEPTION_FILTER WINAPI extSetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter)
{
	OutTraceD("SetUnhandledExceptionFilter: lpExceptionFilter=%x\n", lpTopLevelExceptionFilter);
	extern LONG WINAPI myUnhandledExceptionFilter(LPEXCEPTION_POINTERS);
	return (*pSetUnhandledExceptionFilter)(myUnhandledExceptionFilter);
}

BOOL WINAPI extGetDiskFreeSpaceA(LPCSTR lpRootPathName, LPDWORD lpSectorsPerCluster, LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters, LPDWORD lpTotalNumberOfClusters)
{
	BOOL ret;
	OutTraceD("GetDiskFreeSpace: RootPathName=\"%s\"\n", lpRootPathName);
	ret=(*pGetDiskFreeSpaceA)(lpRootPathName, lpSectorsPerCluster, lpBytesPerSector, lpNumberOfFreeClusters, lpTotalNumberOfClusters);
	if(!ret) OutTraceE("GetDiskFreeSpace: ERROR err=%d at %d\n", GetLastError(), __LINE__);
	*lpNumberOfFreeClusters = 16000;
	return ret;
}

BOOL WINAPI extSetDeviceGammaRamp(HDC hDC, LPVOID lpRamp)
{
	BOOL ret;
	OutTraceD("SetDeviceGammaRamp: hdc=%x\n", hDC);
	if(dwFlags2 & DISABLEGAMMARAMP) {
		OutTraceD("SetDeviceGammaRamp: SUPPRESSED\n");
		return TRUE;
	}
	ret=(*pSetDeviceGammaRamp)(hDC, lpRamp);
	if(!ret) OutTraceE("SetDeviceGammaRamp: ERROR err=%d\n", GetLastError());
	return ret;
}

BOOL WINAPI extGetDeviceGammaRamp(HDC hDC, LPVOID lpRamp)
{
	BOOL ret;
	OutTraceD("GetDeviceGammaRamp: hdc=%x\n", hDC);
	ret=(*pGetDeviceGammaRamp)(hDC, lpRamp);
	if(!ret) OutTraceE("GetDeviceGammaRamp: ERROR err=%d\n", GetLastError());
	return ret;
}

LRESULT WINAPI extSendMessage(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	LRESULT ret;
	OutTraceW("SendMessage: hwnd=%x WinMsg=[0x%x]%s(%x,%x)\n", 
		hwnd, Msg, ExplainWinMessage(Msg), wParam, lParam);
	if(dwFlags & MODIFYMOUSE){
		switch (Msg){
		case WM_MOUSEMOVE:
		case WM_MOUSEWHEEL:
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_LBUTTONDBLCLK:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_RBUTTONDBLCLK:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MBUTTONDBLCLK:
			// revert here the WindowProc mouse correction
			POINT prev, curr;
			RECT rect;
			prev.x = LOWORD(lParam);
			prev.y = HIWORD(lParam);
			(*pGetClientRect)(hWnd, &rect);
			if (VirtualScr.dwWidth)  curr.x = (prev.x * rect.right) / VirtualScr.dwWidth;
			if (VirtualScr.dwHeight) curr.y = (prev.y * rect.bottom) / VirtualScr.dwHeight;
			lParam = MAKELPARAM(curr.x, curr.y); 
			OutTraceC("SendMessage: hwnd=%x pos XY=(%d,%d)->(%d,%d)\n", hwnd, prev.x, prev.y, curr.x, curr.y);
			break;
		default:
			break;
		}
	}
	ret=(*pSendMessage)(hwnd, Msg, wParam, lParam);
	OutTraceW("SendMessage: lresult=%x\n", ret); 
	return ret;
}