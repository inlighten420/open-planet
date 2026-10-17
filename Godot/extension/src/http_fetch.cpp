// Geodrive Godot client - GPL (see Core/README.md)

#include "http_fetch.h"

#include <godot_cpp/classes/http_client.hpp>
#include <godot_cpp/classes/tls_options.hpp>

#include <chrono>
#include <thread>

using namespace godot;

namespace
{
	double Now()
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}
}

HttpResult HttpGet(const std::string& Url, int64_t From, int64_t To, int Attempts)
{
	HttpResult Out;
	const size_t HostStart = Url.find("://") == std::string::npos ? 0 : Url.find("://") + 3;
	const size_t PathStart = Url.find('/', HostStart);
	const std::string Host = Url.substr(HostStart, PathStart == std::string::npos ? std::string::npos : PathStart - HostStart);
	const std::string Path = PathStart == std::string::npos ? "/" : Url.substr(PathStart);
	for (int Attempt = 1; Attempt <= Attempts; ++Attempt)
	{
		Ref<HTTPClient> Client;
		Client.instantiate();
		if (Client->connect_to_host(String::utf8(Host.c_str()), 443, TLSOptions::client()) != OK)
		{
			continue;
		}
		const double Deadline = Now() + 30.0 * Attempt;
		while ((Client->get_status() == HTTPClient::STATUS_CONNECTING || Client->get_status() == HTTPClient::STATUS_RESOLVING) && Now() < Deadline)
		{
			Client->poll();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		if (Client->get_status() != HTTPClient::STATUS_CONNECTED)
		{
			continue;
		}
		PackedStringArray Headers;
		Headers.push_back("User-Agent: Geodrive (Godot)");
		if (From >= 0 && To >= From)
		{
			Headers.push_back(String::utf8(("Range: bytes=" + std::to_string(From) + "-" + std::to_string(To)).c_str()));
		}
		if (Client->request(HTTPClient::METHOD_GET, String::utf8(Path.c_str()), Headers) != OK)
		{
			continue;
		}
		while (Client->get_status() == HTTPClient::STATUS_REQUESTING && Now() < Deadline)
		{
			Client->poll();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		if (!Client->has_response())
		{
			continue;
		}
		Out.Code = Client->get_response_code();
		if (Out.Code != 200 && Out.Code != 206)
		{
			if (Out.Code >= 400 && Out.Code < 500)
			{
				return Out;	// (refused or not there: asking again won't change it)
			}
			continue;
		}
		Out.Body.clear();
		while (Client->get_status() == HTTPClient::STATUS_BODY && Now() < Deadline)
		{
			Client->poll();
			const PackedByteArray Chunk = Client->read_response_body_chunk();
			if (Chunk.is_empty())
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
				continue;
			}
			Out.Body.insert(Out.Body.end(), Chunk.ptr(), Chunk.ptr() + Chunk.size());
		}
		if (!Out.Body.empty())
		{
			return Out;
		}
	}
	Out.Body.clear();
	return Out;
}
