#include "stdafx.h"
#include "Regext.h"
#include "Service.h"
#include "ImeIndicationService.h"

CRITICAL_SECTION csRepaint;
HMODULE hAppService = NULL;
HWND sipButtonWindow = NULL;
HICON hIcons[4];
HREGNOTIFY hNotifies[2];
DWORD currentImeMode = 0;

DWORD GetAssociatedIcon(DWORD imeMode)
{
	if (imeMode == MODE_FN)
		return 1;
	else if (imeMode == MODE_FNLOCK)
		return 3;
	else if (imeMode == MODE_SHIFT)
		return 0;
	else if (imeMode == MODE_CAPSLOCK)
		return 2;
	return NULL;
}

DWORD GetImeMode()
{
	return currentImeMode;
}

DWORD GetSlideStatus()
{
	DWORD slideStatus = 0;
	RegistryGetDWORD(HKEY_LOCAL_MACHINE, L"Software\\OEM\\Keyboard", L"SlidingOut", &slideStatus);
	return slideStatus;
}

// And, again, dirty code. But who cares when sip icon is so slow to response?
void Repaint()
{
	if (TryEnterCriticalSection(&csRepaint) == TRUE)
	{
		HWND wnd = FindWindow(L"MS_SIPBUTTON", NULL);
		HWND wnd2 = GetWindow(wnd, GW_CHILD | GW_HWNDFIRST);

		RECT rect;
		GetWindowRect(wnd2, &rect);
		SetWindowPos(wnd2, NULL, rect.left, rect.top, rect.right - rect.left - 1, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOMOVE);
		Sleep(0);
		SetWindowPos(wnd2, NULL, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOMOVE);
		Sleep(0);
		UpdateWindow(wnd2);
		InvalidateRect(wnd2, NULL, TRUE);
		RedrawWindow(wnd2, NULL, NULL, RDW_ALLCHILDREN | RDW_ERASE | RDW_ERASENOW | RDW_INVALIDATE | RDW_UPDATENOW);

		Sleep(0);

		GetWindowRect(wnd, &rect);
		SetWindowPos(wnd, NULL, rect.left, rect.top, rect.right - rect.left - 1, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOMOVE);
		Sleep(0);
		SetWindowPos(wnd, NULL, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOMOVE);
		Sleep(0);
		UpdateWindow(wnd);
		InvalidateRect(wnd, NULL, TRUE);
		RedrawWindow(wnd, NULL, NULL, RDW_ALLCHILDREN | RDW_ERASE | RDW_ERASENOW | RDW_INVALIDATE | RDW_UPDATENOW);

		LeaveCriticalSection(&csRepaint);
	}
}

ULONG RepaintThread(LPVOID pParam)
{
	Sleep(20);
	Repaint();
	return 0;
}

void Notify(HREGNOTIFY hNotify, DWORD dwUserData, const PBYTE pData, const UINT cbData)
{
	DWORD *newImeMode = (DWORD*)pData;
	currentImeMode = *newImeMode;
	RETAILMSG(1, (L"Got ime mode change (%d now)\n", GetImeMode()));
	Repaint();
	CloseHandle(CreateThread(NULL, 0, RepaintThread, NULL, 0, NULL));
}

void Notify2(HREGNOTIFY hNotify, DWORD dwUserData, const PBYTE pData, const UINT cbData)
{
	RETAILMSG(1, (L"Got sliding status change (%d now)\n", GetSlideStatus()));
	Repaint();
	CloseHandle(CreateThread(NULL, 0, RepaintThread, NULL, 0, NULL));
}

LRESULT CALLBACK SipWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
case WM_PAINT:
		{
			DWORD imeMode = GetImeMode();
			DWORD slideStatus = GetSlideStatus();
			if (imeMode && slideStatus)
			{
				SIPINFO si;
				si.cbSize = sizeof(SIPINFO);
				if (SipGetInfo(&si) == TRUE)
				{
					if (si.fdwFlags & SIPF_ON)
					{
						break;
					}
				}
				PAINTSTRUCT ps;
				HDC hdc = BeginPaint(hWnd, &ps);
				HPEN oldPen = (HPEN)SelectObject(hdc, GetStockObject(WHITE_PEN));
				HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(BLACK_BRUSH));
				Rectangle(hdc, 0, 0, 68, 68);
				SelectObject(hdc, oldBrush);
				SelectObject(hdc, oldPen);
				if (imeMode == MODE_CAPSLOCK_FN)
				{
					DWORD iconId1 = GetAssociatedIcon(MODE_CAPSLOCK);
					DWORD iconId2 = GetAssociatedIcon(MODE_FN);

					if (iconId1 != 0xFFFF && iconId2 != 0xFFFF)
					{
						DrawIconEx(hdc, 18, 2, hIcons[iconId1], 32, 32, NULL, NULL, DI_NORMAL);
						DrawIconEx(hdc, 18, 36, hIcons[iconId2], 32, 32, NULL, NULL, DI_NORMAL);
					}
				}
				else
				{
					DWORD iconId = GetAssociatedIcon(imeMode);
					if (iconId != 0xFFFF)
					{
						DrawIconEx(hdc, 18, 18, hIcons[iconId], 32, 32, NULL, NULL, DI_NORMAL);
					}
				}
				EndPaint(hWnd, &ps);
				return 0;
			}
			else
			{
				break;
			}
		}
	}
	LONG oldWndProc = (LONG)GetProp(hWnd, SIPHOOK_WINDOW_PROPERTY);
	if (oldWndProc)
		return CallWindowProc((WNDPROC)oldWndProc, hWnd, uMsg, wParam, lParam);
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

DWORD IMI_Close(DWORD dwData)
{
    return 0;
}

DWORD IMI_Deinit(DWORD dwData)
{
	if (sipButtonWindow)
	{
		LONG oldWndProc = (LONG) RemoveProp(sipButtonWindow, SIPHOOK_WINDOW_PROPERTY);
		if (oldWndProc != NULL)
		{
			SetWindowLong(sipButtonWindow, GWL_WNDPROC, oldWndProc);
		}
	}
	DeleteCriticalSection(&csRepaint);
	if (hAppService)
	{
		FreeLibrary(hAppService);
		for (int x = 0; x < 4; x++)
		{
			DestroyIcon(hIcons[x]);
		}
	}  
	RegistryCloseNotification(hNotifies[0]);
	RegistryCloseNotification(hNotifies[1]);
	return 1;
}


ULONG  ImeIndicationThreadProc( LPVOID pParam )
{
	InitializeCriticalSection(&csRepaint);
	static ApiState state = APISTATE_UNKNOWN;
	HANDLE hEvent;
	if (state == APISTATE_UNKNOWN)
	{
		hEvent = OpenEvent(EVENT_ALL_ACCESS, 0, EVENT_NAME);
		if (hEvent)
		{
			WaitForSingleObject(hEvent, INFINITE);
			CloseHandle(hEvent);
			state = APISTATE_READY;
		}
		else
		{
			state = APISTATE_NEVER;
		}
	}
	if (state == APISTATE_READY)
	{
		sipButtonWindow = NULL;
		while (sipButtonWindow == NULL)
		{
			sipButtonWindow = FindWindow(MS_SIPBUTTON_CLASS, NULL);
		}
		if (sipButtonWindow)
		{
			sipButtonWindow = GetWindow(sipButtonWindow, GW_HWNDFIRST | GW_CHILD);
			LONG oldWindowProc = GetWindowLong(sipButtonWindow, GWL_WNDPROC);

			SetProp(sipButtonWindow, SIPHOOK_WINDOW_PROPERTY, (HANDLE)oldWindowProc);
			SetWindowLong(sipButtonWindow, GWL_WNDPROC, (LONG)SipWndProc);
		}
		hAppService = LoadLibrary(L"\\Windows\\App_Service.dll");
		if (hAppService)
		{
			hIcons[1] = LoadIcon(hAppService, MAKEINTRESOURCE(ICON_FN));
			hIcons[3] = LoadIcon(hAppService, MAKEINTRESOURCE(ICON_FNLOCK));

			hIcons[0] = LoadIcon(hAppService, MAKEINTRESOURCE(ICON_SHIFT));
			hIcons[2] = LoadIcon(hAppService, MAKEINTRESOURCE(ICON_CAPSLOCK));
		}
		RegistryNotifyCallback(HKEY_LOCAL_MACHINE,
			L"Software\\XT9 Input Method", 
			L"IMEMode",
			Notify,
			0,
			0,
			&hNotifies[0]);

		RegistryNotifyCallback(HKEY_LOCAL_MACHINE,
			L"Software\\OEM\\Keyboard", 
			L"SlidingOut",
			Notify2,
			0,
			0,
			&hNotifies[1]);


	}
	MSG msg;
	while (GetMessage(&msg, 0, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return 0;
}


DWORD IMI_Init(DWORD dwData)
{
	CloseHandle(CreateThread( 0, 0, ImeIndicationThreadProc, 0, 0, 0));
    return 1;
}

DWORD IMI_IOControl(DWORD AData, DWORD ACode, void *ABufIn, 
				   DWORD ASzIn, void *ABufOut, DWORD ASzOut, 
				   DWORD *ARealSzOut) 
{
   	switch (ACode) 
	{
	case IOCTL_SERVICE_START:
		return TRUE;
	case IOCTL_SERVICE_STOP:
		return TRUE;
	case IOCTL_SERVICE_STARTED:
		return TRUE;
	case IOCTL_SERVICE_INSTALL: 
		{
			// Registering our service in the OS
			HKEY hKey;
			DWORD dwValue;

			if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"Services\\ImeIndication", 0, 
								NULL, 0, 0, NULL, &hKey, &dwValue)) 
				return FALSE;

			// DLL name
			WCHAR dllname[] = L"ImeIndicationService.dll";
			RegSetValueExW(hKey, L"Dll", 0, REG_SZ, 
				(const BYTE *)dllname, wcslen(dllname) << 1);

			// Setting prefix used to control our service
			RegSetValueExW(hKey, L"Prefix", 0, REG_SZ, (const BYTE *)L"IMI",6);

			// Flags, Index, Context
			dwValue = 0;
			RegSetValueExW(hKey, L"Flags", 0, REG_DWORD, (const BYTE *) &dwValue, 4);
			RegSetValueExW(hKey, L"Index", 0, REG_DWORD, (const BYTE *) &dwValue, 4);
			RegSetValueExW(hKey, L"Context", 0, REG_DWORD, (const BYTE *) &dwValue, 4);

			// Should system keep service alive after initialization?
			dwValue = 1;
			RegSetValueExW(hKey, L"Keep", 0, REG_DWORD, (const BYTE *) &dwValue, 4);

			// Setting load order
			dwValue = 99;
			RegSetValueExW(hKey, L"Order", 0, REG_DWORD, (const BYTE *) &dwValue, 4);

			RegCloseKey(hKey);
			return TRUE;
		}
	case IOCTL_SERVICE_UNINSTALL: 
		{
			// Uninstalling service from the OS
			HKEY rk;
			if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Services", 0, NULL, &rk)) 
				return FALSE;

			RegDeleteKeyW(rk, L"ImeIndicationSrv");
			RegCloseKey(rk);

			return TRUE;
		}

	case IOCTL_SERVICE_QUERY_CAN_DEINIT:
		{
			memset(ABufOut, 1, ASzOut);
			return TRUE;
		}
	case IOCTL_SERVICE_CONTROL:
		{
			return TRUE;
		}
	default:
		// Unknown control code received
		return FALSE;
	}
	return TRUE;
};

DWORD IMI_Open(DWORD dwData,
			DWORD dwAccess,
			DWORD dwShareMode)
{
   return 0;
}

DWORD IMI_Read(
  DWORD dwData,
  LPVOID pBuf,
  DWORD dwLen)
{
   
   return 0;
}

DWORD IMI_Seek(
  DWORD dwData,
  long pos,
  DWORD type)
{
   
   return 0;
}

DWORD IMI_Write(
  DWORD dwData,
  LPCVOID pInBuf,
  DWORD dwInLen)
{
   
   return 0;
}

BOOL APIENTRY DllMain( HANDLE hModule, 
                       DWORD  ul_reason_for_call, 
                       LPVOID lpReserved
					 )
{
    return TRUE;
}


