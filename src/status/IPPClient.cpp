/*
 * ============================================================================
 * File:        IPPClient.cpp
 * Project:     haiku-printer-monitor
 * Component:   status
 * Description: Implementation of the minimal IPP client. Encodes a
 *              Get-Printer-Attributes request, sends it as an HTTP/1.1 POST
 *              (non-blocking connect with timeout), decodes the HTTP reply
 *              (Content-Length or chunked), then parses the IPP attribute
 *              groups with bounds checking.
 * License:     MIT (see LICENSE)
 * Author:      Tony (DrJuice), with Claude (Anthropic)
 * ----------------------------------------------------------------------------
 * Revision History
 *   Rev  Date        Author        Changes
 *   0.1  2026-11-01  Tony/Claude   Initial creation (Phase 1, milestone 2).
 * ============================================================================
 */


#include "IPPClient.h"

#include <OS.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>


namespace {


const uint16	kOpGetPrinterAttributes = 0x000B;
const int32		kRequestID = 1;

// IPP delimiter and value tags (RFC 8010 section 3.5)
const uint8		kTagOperationGroup = 0x01;
const uint8		kTagEndOfAttributes = 0x03;
const uint8		kTagFirstValue = 0x10;	// below this: group delimiters
const uint8		kTagUri = 0x45;
const uint8		kTagCharset = 0x47;
const uint8		kTagNaturalLanguage = 0x48;
const uint8		kTagKeyword = 0x44;

const size_t	kMaxResponseSize = 1024 * 1024;

// Only what the monitor needs, to keep the reply small.
const char* kRequestedAttributes[] = {
	"printer-make-and-model",
	"printer-state",
	"printer-state-reasons",
	"marker-names",
	"marker-colors",
	"marker-types",
	"marker-levels",
	"marker-low-levels",
	"marker-high-levels"
};


struct IPPValue {
	uint8		tag;
	std::string	data;
};

typedef std::map<std::string, std::vector<IPPValue> > AttributeMap;


struct SocketCloser {
	int fd;
	~SocketCloser() { if (fd >= 0) close(fd); }
};


void
AppendU16(std::vector<uint8>& buffer, uint16 value)
{
	buffer.push_back((uint8)(value >> 8));
	buffer.push_back((uint8)(value & 0xFF));
}


void
AppendU32(std::vector<uint8>& buffer, uint32 value)
{
	AppendU16(buffer, (uint16)(value >> 16));
	AppendU16(buffer, (uint16)(value & 0xFFFF));
}


void
AppendString(std::vector<uint8>& buffer, const std::string& text)
{
	AppendU16(buffer, (uint16)text.size());
	buffer.insert(buffer.end(), text.begin(), text.end());
}


// An empty name marks an additional value of the previous attribute.
void
AppendAttribute(std::vector<uint8>& buffer, uint8 tag, const std::string& name,
	const std::string& value)
{
	buffer.push_back(tag);
	AppendString(buffer, name);
	AppendString(buffer, value);
}


void
BuildRequest(const std::string& printerUri, std::vector<uint8>& body)
{
	body.clear();
	body.push_back(1);		// IPP version 1.1: the widest compatibility
	body.push_back(1);
	AppendU16(body, kOpGetPrinterAttributes);
	AppendU32(body, (uint32)kRequestID);

	body.push_back(kTagOperationGroup);
	AppendAttribute(body, kTagCharset, "attributes-charset", "utf-8");
	AppendAttribute(body, kTagNaturalLanguage, "attributes-natural-language",
		"en");
	AppendAttribute(body, kTagUri, "printer-uri", printerUri);

	size_t count = sizeof(kRequestedAttributes) / sizeof(kRequestedAttributes[0]);
	for (size_t i = 0; i < count; i++) {
		AppendAttribute(body, kTagKeyword, i == 0 ? "requested-attributes" : "",
			kRequestedAttributes[i]);
	}

	body.push_back(kTagEndOfAttributes);
}


std::string
ToLower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
		[](unsigned char c) { return (char)std::tolower(c); });
	return text;
}


bool
DecodeChunked(const std::string& input, std::string& output)
{
	output.clear();
	size_t pos = 0;
	while (true) {
		size_t lineEnd = input.find("\r\n", pos);
		if (lineEnd == std::string::npos)
			return false;
		unsigned long size = strtoul(input.substr(pos, lineEnd - pos).c_str(),
			NULL, 16);	// strtoul stops at any ";extension"
		pos = lineEnd + 2;
		if (size == 0)
			return true;
		if (pos + size > input.size())
			return false;
		output.append(input, pos, size);
		pos += size + 2;	// skip the chunk's trailing CRLF
	}
}


// Returns true when `raw` holds a complete HTTP response (or the connection
// has closed and what we have is usable). Skips interim 1xx responses.
bool
DecodeHTTP(const std::string& raw, bool connectionClosed, int32& statusCode,
	std::string& body)
{
	size_t start = 0;
	while (true) {
		size_t headerEnd = raw.find("\r\n\r\n", start);
		if (headerEnd == std::string::npos)
			return connectionClosed;

		std::string head = ToLower(raw.substr(start, headerEnd - start));
		size_t space = head.find(' ');
		statusCode = space == std::string::npos ? 0
			: (int32)atoi(head.c_str() + space + 1);

		if (statusCode >= 100 && statusCode < 200) {
			start = headerEnd + 4;	// interim reply, e.g. 100 Continue
			continue;
		}

		std::string rest = raw.substr(headerEnd + 4);
		head = "\r\n" + head;

		size_t encoding = head.find("\r\ntransfer-encoding:");
		if (encoding != std::string::npos
			&& head.find("chunked", encoding) != std::string::npos) {
			if (DecodeChunked(rest, body))
				return true;
			if (connectionClosed) {
				statusCode = 0;	// truncated chunked body
				return true;
			}
			return false;
		}

		size_t length = head.find("\r\ncontent-length:");
		if (length != std::string::npos) {
			size_t expected = (size_t)strtoul(head.c_str() + length + 17,
				NULL, 10);
			if (rest.size() >= expected) {
				body = rest.substr(0, expected);
				return true;
			}
			if (connectionClosed) {
				body = rest;
				return true;
			}
			return false;
		}

		if (connectionClosed) {
			body = rest;
			return true;
		}
		return false;
	}
}


bool
ParseIPPResponse(const std::string& body, int32& ippStatus,
	AttributeMap& attributes)
{
	const uint8* data = (const uint8*)body.data();
	size_t size = body.size();
	if (size < 8)
		return false;

	ippStatus = (data[2] << 8) | data[3];

	size_t pos = 8;
	std::string current;
	while (pos < size) {
		uint8 tag = data[pos++];
		if (tag == kTagEndOfAttributes)
			return true;
		if (tag < kTagFirstValue)
			continue;	// start of a new attribute group

		if (pos + 2 > size)
			return false;
		size_t nameLength = (data[pos] << 8) | data[pos + 1];
		pos += 2;
		if (pos + nameLength + 2 > size)
			return false;
		std::string name((const char*)data + pos, nameLength);
		pos += nameLength;

		size_t valueLength = (data[pos] << 8) | data[pos + 1];
		pos += 2;
		if (pos + valueLength > size)
			return false;
		IPPValue value;
		value.tag = tag;
		value.data.assign((const char*)data + pos, valueLength);
		pos += valueLength;

		if (nameLength > 0)
			current = name;
		if (!current.empty())
			attributes[current].push_back(value);
	}

	return true;	// tolerate a missing end-of-attributes tag
}


bool
ValueAsInt(const IPPValue& value, int32& result)
{
	if (value.data.size() != 4)
		return false;
	const uint8* d = (const uint8*)value.data.data();
	result = (int32)(((uint32)d[0] << 24) | ((uint32)d[1] << 16)
		| ((uint32)d[2] << 8) | (uint32)d[3]);
	return true;
}


const std::vector<IPPValue>*
Find(const AttributeMap& attributes, const char* name)
{
	auto found = attributes.find(name);
	return found == attributes.end() ? NULL : &found->second;
}


void
FillStatus(const AttributeMap& attributes, PrinterStatus& status)
{
	const std::vector<IPPValue>* values;

	if ((values = Find(attributes, "printer-make-and-model")) != NULL
		&& !values->empty())
		status.makeAndModel = (*values)[0].data;

	if ((values = Find(attributes, "printer-state")) != NULL
		&& !values->empty())
		ValueAsInt((*values)[0], status.state);

	if ((values = Find(attributes, "printer-state-reasons")) != NULL) {
		for (const IPPValue& value : *values)
			status.stateReasons.push_back(value.data);
	}

	const std::vector<IPPValue>* names = Find(attributes, "marker-names");
	if (names == NULL)
		return;

	const std::vector<IPPValue>* colors = Find(attributes, "marker-colors");
	const std::vector<IPPValue>* types = Find(attributes, "marker-types");
	const std::vector<IPPValue>* levels = Find(attributes, "marker-levels");
	const std::vector<IPPValue>* lows = Find(attributes, "marker-low-levels");
	const std::vector<IPPValue>* highs = Find(attributes, "marker-high-levels");

	for (size_t i = 0; i < names->size(); i++) {
		MarkerInfo marker;
		marker.name = (*names)[i].data;
		if (colors != NULL && i < colors->size())
			marker.color = (*colors)[i].data;
		if (types != NULL && i < types->size())
			marker.type = (*types)[i].data;
		if (levels != NULL && i < levels->size())
			ValueAsInt((*levels)[i], marker.level);
		if (lows != NULL && i < lows->size())
			ValueAsInt((*lows)[i], marker.lowLevel);
		if (highs != NULL && i < highs->size())
			ValueAsInt((*highs)[i], marker.highLevel);
		status.markers.push_back(marker);
	}
}


}	// namespace


IPPClient::IPPClient()
	:
	fErrorContext(""),
	fHTTPStatus(0),
	fIPPStatus(-1)
{
}


IPPClient::~IPPClient()
{
}


status_t
IPPClient::_Fail(const char* context)
{
	status_t error = errno;	// capture before anything can change it
	fErrorContext = context;
	return error;
}


status_t
IPPClient::_Wait(int fd, short events, bigtime_t deadline)
{
	while (true) {
		bigtime_t remaining = deadline - system_time();
		if (remaining <= 0) {
			fErrorContext = "waiting for the printer (timed out)";
			return B_TIMED_OUT;
		}

		pollfd waitFor;
		waitFor.fd = fd;
		waitFor.events = events;
		waitFor.revents = 0;
		int ready = poll(&waitFor, 1, (int)((remaining + 999) / 1000));
		if (ready > 0)
			return B_OK;
		if (ready == 0) {
			fErrorContext = "waiting for the printer (timed out)";
			return B_TIMED_OUT;
		}
		if (errno != EINTR)
			return _Fail("waiting for the printer");
	}
}


status_t
IPPClient::GetStatus(const char* address, uint16 port, const char* resource,
	bigtime_t timeout, PrinterStatus& status)
{
	status = PrinterStatus();
	fErrorContext = "";
	fHTTPStatus = 0;
	fIPPStatus = -1;

	sockaddr_in target;
	memset(&target, 0, sizeof(target));
	target.sin_family = AF_INET;
	target.sin_port = htons(port);
	if (address == NULL || inet_pton(AF_INET, address, &target.sin_addr) != 1) {
		fErrorContext = "parsing the printer address (expected a.b.c.d)";
		return B_BAD_VALUE;
	}

	std::string path = resource != NULL ? resource : "ipp/print";
	while (!path.empty() && path[0] == '/')
		path.erase(0, 1);

	char hostPort[64];
	snprintf(hostPort, sizeof(hostPort), "%s:%u", address, (unsigned)port);
	std::string printerUri = std::string("ipp://") + hostPort + "/" + path;

	std::vector<uint8> body;
	BuildRequest(printerUri, body);

	std::string request = "POST /" + path + " HTTP/1.1\r\n"
		"Host: " + hostPort + "\r\n"
		"Content-Type: application/ipp\r\n"
		"Content-Length: " + std::to_string(body.size()) + "\r\n"
		"Connection: close\r\n\r\n";
	request.append(body.begin(), body.end());

	bigtime_t deadline = system_time() + timeout;

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return _Fail("creating the socket");
	SocketCloser closer = { fd };

	// Non-blocking connect, so an unreachable printer can't hang us.
	int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	if (connect(fd, (sockaddr*)&target, sizeof(target)) < 0) {
		if (errno != EINPROGRESS)
			return _Fail("connecting to the printer");
		status_t result = _Wait(fd, POLLOUT, deadline);
		if (result != B_OK)
			return result;
		int socketError = 0;
		socklen_t length = sizeof(socketError);
		getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &length);
		if (socketError != 0) {
			fErrorContext = "connecting to the printer";
			return socketError;
		}
	}

	size_t sent = 0;
	while (sent < request.size()) {
		ssize_t written = send(fd, request.data() + sent,
			request.size() - sent, 0);
		if (written < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
				status_t result = _Wait(fd, POLLOUT, deadline);
				if (result != B_OK)
					return result;
				continue;
			}
			return _Fail("sending the request");
		}
		sent += (size_t)written;
	}

	std::string raw;
	std::string httpBody;
	int32 httpStatus = 0;
	char chunk[4096];
	bool complete = false;

	while (!complete) {
		status_t result = _Wait(fd, POLLIN, deadline);
		if (result != B_OK)
			return result;

		ssize_t received = recv(fd, chunk, sizeof(chunk), 0);
		if (received < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
				continue;
			return _Fail("receiving the response");
		}

		bool closed = received == 0;
		raw.append(chunk, (size_t)received);
		if (raw.size() > kMaxResponseSize) {
			fErrorContext = "receiving the response (too large)";
			return B_BAD_DATA;
		}

		// Stop as soon as the reply is whole, even if the printer keeps
		// the connection open despite "Connection: close".
		complete = DecodeHTTP(raw, closed, httpStatus, httpBody);
		if (closed && !complete) {
			fErrorContext = "receiving the response (connection closed early)";
			return B_BAD_DATA;
		}
	}

	fHTTPStatus = httpStatus;
	if (httpStatus != 200) {
		fErrorContext = "reading the HTTP reply (status was not 200)";
		return B_BAD_DATA;
	}

	AttributeMap attributes;
	int32 ippStatus = -1;
	if (!ParseIPPResponse(httpBody, ippStatus, attributes)) {
		fErrorContext = "parsing the IPP reply (malformed)";
		return B_BAD_DATA;
	}
	fIPPStatus = ippStatus;

	// 0x0000-0x00FF are the "successful" status codes.
	if (ippStatus > 0x00FF) {
		fErrorContext = "reading the IPP reply (printer reported an error)";
		return B_BAD_DATA;
	}

	FillStatus(attributes, status);
	return B_OK;
}
