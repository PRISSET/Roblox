//============ Copyright ImMagic, All rights reserved ============//
//
// Purpose: 
//
//================================================================//

#pragma once

#include <D3D11.h>
#include <string>

class Menu
{
public:
	static bool Initialize(HWND hWnd, ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext);
	static void Shutdown();

public:
	static void Render();
	static bool HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

public:
	// Internal helper, when using hooked directx
	static void InvalidateDeviceObjects();
	static void CreateDeviceObjects();

public:
	static void	DrawMenu();
	static void DrawWatermark();
	static void DrawKeybindsWindow();
	static void DrawNotificationsWindow();
	static void PushNotification(const std::string& text);
	static void PushHitLog(const std::string& player, float damage, const std::string& part);
	static void RequestOverlayPositionSync();

private:
	inline static bool m_bInitialized = false;
};
