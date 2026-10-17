// Geodrive Godot client - GPL (see Core/README.md)

#pragma once

#include <cstdint>
#include <string>
#include <vector>

/** An HTTPS GET's answer: its status (0: no answer at all) and its body */
struct HttpResult
{
	int Code = 0;
	std::vector<uint8_t> Body;
};

/** Fetches a URL over HTTPS, blocking (call it on a worker thread): all of it, or the bytes From..To (inclusive) when
 *  given. Tries again on no answer or a server error, not on a refusal (403, 404: there's nothing there). */
HttpResult HttpGet(const std::string& Url, int64_t From = -1, int64_t To = -1, int Attempts = 4);
