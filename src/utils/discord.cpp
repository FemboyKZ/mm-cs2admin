#include "discord.h"
#include "../common.h"
#include "../config/config.h"
#include "../player/player_manager.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <curl/curl.h>
#endif

CS2ADiscord g_CS2ADiscord;

CS2ADiscord::CS2ADiscord() {}

CS2ADiscord::~CS2ADiscord()
{
	Shutdown();
}

void CS2ADiscord::Init()
{
	if (m_running.load())
		return;

	m_running.store(true);
	m_worker = std::thread(&CS2ADiscord::WorkerThread, this);
	META_CONPRINTF("[ADMIN] Discord: Worker thread started.\n");
}

void CS2ADiscord::Shutdown()
{
	if (!m_running.load())
		return;

	m_running.store(false);
	m_cv.notify_all();

	if (m_worker.joinable())
		m_worker.join();

	// Drain any remaining items
	std::lock_guard<std::mutex> lock(m_mutex);
	while (!m_queue.empty())
		m_queue.pop();
}

void CS2ADiscord::WorkerThread()
{
	while (m_running.load())
	{
		std::string payload;
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_cv.wait(lock, [this]() { return !m_queue.empty() || !m_running.load(); });

			if (!m_running.load() && m_queue.empty())
				break;

			if (m_queue.empty())
				continue;

			payload = std::move(m_queue.front());
			m_queue.pop();
		}

		const std::string &url = g_CS2AConfig.discordWebhookUrl;
		size_t hostStart = url.find("://") + 3;
		size_t pathStart = url.find('/', hostStart);
		if (pathStart == std::string::npos)
			continue;

		std::string host = url.substr(hostStart, pathStart - hostStart);
		std::string path = url.substr(pathStart);

		if (!HttpsPost(host, path, payload))
			META_CONPRINTF("[ADMIN] Discord: Failed to send webhook.\n");
	}
}

bool CS2ADiscord::IsEnabled() const
{
	return !g_CS2AConfig.discordWebhookUrl.empty();
}

static std::string JsonEscape(const std::string &input)
{
	std::string output;
	output.reserve(input.size() + 16);
	for (char c : input)
	{
		switch (c)
		{
			case '"': output += "\\\""; break;
			case '\\': output += "\\\\"; break;
			case '\n': output += "\\n"; break;
			case '\r': output += "\\r"; break;
			case '\t': output += "\\t"; break;
			default:
				if (static_cast<unsigned char>(c) < 0x20)
				{
					char buf[8];
					snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
					output += buf;
				}
				else
				{
					output += c;
				}
				break;
		}
	}
	return output;
}

void CS2ADiscord::SendTextMessage(const char *content)
{
	if (!IsEnabled() || !content || !*content)
		return;

	std::string json = "{\"content\":\"" + JsonEscape(content) + "\"}";
	SendPayload(json);
}

void CS2ADiscord::SendEmbedMessage(const char *title, const char *description, int color, const char *footer)
{
	if (!IsEnabled())
		return;

	std::string json = "{\"embeds\":[{";
	json += "\"title\":\"" + JsonEscape(title ? title : "") + "\",";
	json += "\"description\":\"" + JsonEscape(description ? description : "") + "\",";
	json += "\"color\":" + std::to_string(color);
	if (footer && *footer)
		json += ",\"footer\":{\"text\":\"" + JsonEscape(footer) + "\"}";
	json += "}]}";

	SendPayload(json);
}

void CS2ADiscord::NotifyAdminAction(const char *adminName, const char *action, const char *targetName,
	const char *reason, int durationMinutes)
{
	if (!IsEnabled())
		return;

	std::string desc;
	desc += "**Admin:** " + "``" + JsonEscape(adminName ? adminName : "Console") + "``\n";
	desc += "**Action:** " + "``" + JsonEscape(action ? action : "") + "``\n";
	desc += "**Target:** " + "``" + JsonEscape(targetName ? targetName : "") + "``\n";

	if (durationMinutes >= 0)
	{
		std::string dur = (durationMinutes == 0) ? "Permanent" : ADMIN_FormatDuration(durationMinutes);
		desc += "**Duration:** " + "``" + JsonEscape(dur) + "``\n";
	}

	if (reason && *reason)
		desc += "**Reason:** " + "``" + JsonEscape(reason) + "``\n";

	int color = 0xE74C3C; // red default

	if (action)
	{
		std::string act(action);
		if (act.find("Mute") != std::string::npos || act.find("Gag") != std::string::npos ||
			act.find("Silence") != std::string::npos)
			color = 0xE67E22; // orange
		else if (act.find("Unmute") != std::string::npos || act.find("Ungag") != std::string::npos ||
			act.find("Unsilence") != std::string::npos || act.find("Unban") != std::string::npos)
			color = 0x2ECC71; // green
	}

	SendEmbedMessage("Admin Action", desc.c_str(), color, g_CS2AConfig.discordFooterText.c_str());
}

void CS2ADiscord::NotifyReport(const char *reporterName, const char *targetName, const char *reason)
{
	if (!IsEnabled())
		return;

	std::string desc;
	desc += "**Reporter:** " + "``" + JsonEscape(reporterName ? reporterName : "") + "``\n";
	desc += "**Target:** " + "``" + JsonEscape(targetName ? targetName : "") + "``\n";
	desc += "**Reason:** " + "``" + JsonEscape(reason ? reason : "") + "``\n";

	SendEmbedMessage("Player Report", desc.c_str(), 0xF39C12, g_CS2AConfig.discordFooterText.c_str());
}

void CS2ADiscord::SendPayload(const std::string &json)
{
	if (!IsEnabled())
		return;

	const std::string &url = g_CS2AConfig.discordWebhookUrl;

	// Validate URL starts with a Discord webhook URL
	if (url.find("https://discord.com/api/webhooks/") != 0 &&
		url.find("https://discordapp.com/api/webhooks/") != 0)
	{
		META_CONPRINTF("[ADMIN] Discord: Invalid webhook URL (must be a Discord webhook URL).\n");
		return;
	}

	size_t hostStart = url.find("://") + 3;
	size_t pathStart = url.find('/', hostStart);
	if (pathStart == std::string::npos)
	{
		META_CONPRINTF("[ADMIN] Discord: Malformed webhook URL.\n");
		return;
	}

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_queue.size() >= MAX_QUEUE_SIZE)
		{
			META_CONPRINTF("[ADMIN] Discord: Queue full (%zu items), dropping message.\n", m_queue.size());
			return;
		}
		m_queue.push(json);
	}
	m_cv.notify_one();
}

// Platform specific HTTPS POST

#ifdef _WIN32

bool CS2ADiscord::HttpsPost(const std::string &host, const std::string &path, const std::string &json)
{
	// Convert host and path to wide strings for WinHTTP
	int hostLen = MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, nullptr, 0);
	int pathLen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
	if (hostLen <= 0 || pathLen <= 0)
		return false;

	std::wstring wHost(hostLen, L'\0');
	std::wstring wPath(pathLen, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, &wHost[0], hostLen);
	MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wPath[0], pathLen);

	HINTERNET hSession = WinHttpOpen(L"CS2Admin/1.0",
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession)
		return false;

	HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hConnect)
	{
		WinHttpCloseHandle(hSession);
		return false;
	}

	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", wPath.c_str(),
		nullptr, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES,
		WINHTTP_FLAG_SECURE);
	if (!hRequest)
	{
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	const wchar_t *contentType = L"Content-Type: application/json";
	BOOL result = WinHttpSendRequest(hRequest,
		contentType, (DWORD)-1,
		(LPVOID)json.c_str(), (DWORD)json.size(),
		(DWORD)json.size(), 0);

	bool success = false;
	if (result)
	{
		result = WinHttpReceiveResponse(hRequest, nullptr);
		if (result)
		{
			DWORD statusCode = 0;
			DWORD statusSize = sizeof(statusCode);
			WinHttpQueryHeaders(hRequest,
				WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				WINHTTP_HEADER_NAME_BY_INDEX,
				&statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

			// 2xx = success, 204 = no content (normal for Discord webhooks)
			success = (statusCode >= 200 && statusCode < 300);

			if (!success)
				META_CONPRINTF("[ADMIN] Discord: Webhook returned HTTP %d.\n", (int)statusCode);
		}
	}

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);

	return success;
}

#else // Linux

bool CS2ADiscord::HttpsPost(const std::string &host, const std::string &path, const std::string &json)
{
	std::string url = "https://" + host + path;

	CURL *curl = curl_easy_init();
	if (!curl)
		return false;

	struct curl_slist *headers = nullptr;
	headers = curl_slist_append(headers, "Content-Type: application/json");

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.c_str());
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)json.size());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

	// Discard response body
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char *, size_t size, size_t nmemb, void *) -> size_t {
		return size * nmemb;
	});

	CURLcode res = curl_easy_perform(curl);
	bool success = false;

	if (res == CURLE_OK)
	{
		long httpCode = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
		success = (httpCode >= 200 && httpCode < 300);

		if (!success)
			META_CONPRINTF("[ADMIN] Discord: Webhook returned HTTP %ld.\n", httpCode);
	}
	else
	{
		META_CONPRINTF("[ADMIN] Discord: curl error: %s\n", curl_easy_strerror(res));
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	return success;
}

#endif
