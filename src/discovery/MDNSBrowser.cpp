/*
 * ============================================================================
 * File:        MDNSBrowser.cpp
 * Project:     haiku-printer-monitor
 * Component:   discovery
 * Description: Implementation of the one-shot mDNS / DNS-SD browser.
 *              Builds a PTR query, sends it to 224.0.0.251:5353, collects
 *              responses until the timeout, parses DNS records (with name
 *              compression and bounds checking), and assembles instances.
 * License:     MIT (see LICENSE)
 * Author:      Tony (DrJuice), with Claude (Anthropic)
 * ----------------------------------------------------------------------------
 * Revision History
 *   Rev  Date        Author        Changes
 *   0.1  2026-11-01  Tony/Claude   Initial creation (Phase 1, milestone 1).
 *   0.2  2026-11-01  Tony/Claude   Added kBrowseDirect: unicast query to a
 *                                  given IP's port 5353. QU bit now set for
 *                                  every non-multicast mode.
 * ============================================================================
 */


#include "MDNSBrowser.h"

#include <OS.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <map>
#include <set>


namespace {


const char*		kMDNSGroup = "224.0.0.251";
const uint16	kMDNSPort = 5353;

const uint16	kTypeA = 1;
const uint16	kTypePTR = 12;
const uint16	kTypeTXT = 16;
const uint16	kTypeSRV = 33;
const uint16	kClassIN = 1;
const uint16	kClassMask = 0x7FFF;			// strips mDNS cache-flush bit
const uint16	kQuestionUnicastBit = 0x8000;	// "QU" bit, RFC 6762 5.4
const uint16	kFlagResponse = 0x8000;			// QR bit in header flags

const size_t	kMaxPacketSize = 9000;
const int		kMaxPointerJumps = 32;			// guards against loops
const size_t	kMaxNameLength = 1024;


// Everything gathered from all response packets, keyed by normalized name.
struct RecordStore {
	std::set<std::string>			instances;
	std::map<std::string, std::string>
									displayNames;	// original spelling
	std::map<std::string, std::pair<std::string, uint16> >
									services;		// SRV: host, port
	std::map<std::string, std::vector<std::string> >
									txt;
	std::map<std::string, std::string>
									addresses;		// A: host -> IPv4
	std::map<std::string, std::string>
									sources;		// SRV packet source IP
};


std::string
StripTrailingDot(const std::string& name)
{
	std::string result = name;
	while (!result.empty() && result.back() == '.')
		result.pop_back();
	return result;
}


// DNS names compare case-insensitively (RFC 4343).
std::string
NormalizeName(const std::string& name)
{
	std::string result = StripTrailingDot(name);
	std::transform(result.begin(), result.end(), result.begin(),
		[](unsigned char c) { return (char)std::tolower(c); });
	return result;
}


bool
EndsWith(const std::string& text, const std::string& suffix)
{
	return text.size() >= suffix.size()
		&& text.compare(text.size() - suffix.size(), suffix.size(),
			suffix) == 0;
}


void
AppendU16(std::vector<uint8>& buffer, uint16 value)
{
	buffer.push_back((uint8)(value >> 8));
	buffer.push_back((uint8)(value & 0xFF));
}


bool
ReadU16(const uint8* data, size_t size, size_t pos, uint16& value)
{
	if (pos + 2 > size)
		return false;
	value = (uint16)((data[pos] << 8) | data[pos + 1]);
	return true;
}


bool
ReadU32(const uint8* data, size_t size, size_t pos, uint32& value)
{
	if (pos + 4 > size)
		return false;
	value = ((uint32)data[pos] << 24) | ((uint32)data[pos + 1] << 16)
		| ((uint32)data[pos + 2] << 8) | (uint32)data[pos + 3];
	return true;
}


// Encodes "_ipp._tcp.local" as length-prefixed DNS labels.
bool
EncodeName(const std::string& name, std::vector<uint8>& out)
{
	std::string clean = StripTrailingDot(name);
	size_t start = 0;
	while (true) {
		size_t dot = clean.find('.', start);
		if (dot == std::string::npos)
			dot = clean.size();
		size_t length = dot - start;
		if (length == 0 || length > 63)
			return false;
		out.push_back((uint8)length);
		out.insert(out.end(), clean.begin() + start, clean.begin() + dot);
		if (dot == clean.size())
			break;
		start = dot + 1;
	}
	out.push_back(0);
	return true;
}


// Reads a possibly-compressed DNS name starting at `pos`. On success,
// `pos` is advanced past the name as it appears at that position.
bool
ReadName(const uint8* data, size_t size, size_t& pos, std::string& name)
{
	name.clear();
	size_t cursor = pos;
	bool jumped = false;
	int jumps = 0;

	while (true) {
		if (cursor >= size)
			return false;
		uint8 length = data[cursor];

		if (length == 0) {
			cursor++;
			break;
		}

		if ((length & 0xC0) == 0xC0) {
			// Compression pointer: 14-bit offset into the packet.
			if (cursor + 1 >= size)
				return false;
			size_t target = ((size_t)(length & 0x3F) << 8) | data[cursor + 1];
			if (!jumped)
				pos = cursor + 2;
			jumped = true;
			if (++jumps > kMaxPointerJumps || target >= size)
				return false;
			cursor = target;
			continue;
		}

		if ((length & 0xC0) != 0)
			return false;	// reserved label types
		if (cursor + 1 + length > size)
			return false;

		if (!name.empty())
			name += '.';
		name.append((const char*)data + cursor + 1, length);
		if (name.size() > kMaxNameLength)
			return false;
		cursor += 1 + length;
	}

	if (!jumped)
		pos = cursor;
	return true;
}


bool
BuildQuery(const char* serviceType, bool unicastResponse,
	std::vector<uint8>& packet)
{
	packet.clear();
	AppendU16(packet, 0);	// ID: 0 for mDNS
	AppendU16(packet, 0);	// flags: standard query
	AppendU16(packet, 1);	// QDCOUNT
	AppendU16(packet, 0);	// ANCOUNT
	AppendU16(packet, 0);	// NSCOUNT
	AppendU16(packet, 0);	// ARCOUNT
	if (!EncodeName(serviceType, packet))
		return false;
	AppendU16(packet, kTypePTR);
	AppendU16(packet, kClassIN | (unicastResponse ? kQuestionUnicastBit : 0));
	return true;
}


// TXT RDATA is a sequence of length-prefixed strings.
void
ParseTXT(const uint8* data, size_t start, size_t length,
	std::vector<std::string>& out)
{
	out.clear();
	size_t pos = start;
	size_t end = start + length;
	while (pos < end) {
		uint8 entryLength = data[pos++];
		if (entryLength == 0)
			continue;	// an empty TXT record is a single zero byte
		if (pos + entryLength > end)
			break;
		out.push_back(std::string((const char*)data + pos, entryLength));
		pos += entryLength;
	}
}


// Returns true if the packet was an mDNS response (as opposed to a query
// from another host, or our own query looped back).
bool
ParsePacket(const uint8* data, size_t size, const std::string& sourceAddress,
	const std::string& serviceType, RecordStore& store)
{
	uint16 flags, questions, answers, authority, additional;
	if (!ReadU16(data, size, 2, flags)
		|| !ReadU16(data, size, 4, questions)
		|| !ReadU16(data, size, 6, answers)
		|| !ReadU16(data, size, 8, authority)
		|| !ReadU16(data, size, 10, additional))
		return false;
	if ((flags & kFlagResponse) == 0)
		return false;

	size_t pos = 12;
	std::string name;
	for (uint16 i = 0; i < questions; i++) {
		if (!ReadName(data, size, pos, name) || pos + 4 > size)
			return true;
		pos += 4;	// QTYPE + QCLASS
	}

	std::string instanceSuffix = "." + serviceType;
	uint32 recordCount = (uint32)answers + authority + additional;

	for (uint32 i = 0; i < recordCount; i++) {
		uint16 type, rrClass, rdLength;
		uint32 ttl;
		if (!ReadName(data, size, pos, name)
			|| !ReadU16(data, size, pos, type)
			|| !ReadU16(data, size, pos + 2, rrClass)
			|| !ReadU32(data, size, pos + 4, ttl)
			|| !ReadU16(data, size, pos + 8, rdLength))
			return true;

		size_t rdStart = pos + 10;
		if (rdStart + rdLength > size)
			return true;
		pos = rdStart + rdLength;

		// Skip other classes, and TTL 0 "goodbye" announcements.
		if ((rrClass & kClassMask) != kClassIN || ttl == 0)
			continue;

		std::string key = NormalizeName(name);

		switch (type) {
			case kTypePTR:
			{
				if (key != serviceType)
					break;
				size_t p = rdStart;
				std::string target;
				if (ReadName(data, size, p, target)) {
					std::string targetKey = NormalizeName(target);
					store.instances.insert(targetKey);
					store.displayNames[targetKey] = StripTrailingDot(target);
				}
				break;
			}

			case kTypeSRV:
			{
				// priority(2) weight(2) port(2) target(name)
				uint16 port;
				size_t p = rdStart + 6;
				std::string target;
				if (rdLength < 7 || !ReadU16(data, size, rdStart + 4, port)
					|| !ReadName(data, size, p, target))
					break;
				store.services[key] = std::make_pair(StripTrailingDot(target),
					port);
				store.displayNames[key] = StripTrailingDot(name);
				store.sources[key] = sourceAddress;
				// Count it even if the PTR record didn't arrive.
				if (EndsWith(key, instanceSuffix))
					store.instances.insert(key);
				break;
			}

			case kTypeTXT:
				ParseTXT(data, rdStart, rdLength, store.txt[key]);
				break;

			case kTypeA:
			{
				if (rdLength != 4)
					break;
				char text[INET_ADDRSTRLEN];
				if (inet_ntop(AF_INET, data + rdStart, text, sizeof(text))
						!= NULL)
					store.addresses[key] = text;
				break;
			}

			default:
				break;	// AAAA, NSEC, etc. are not needed for Phase 1
		}
	}

	return true;
}


void
AssembleResults(const RecordStore& store, const std::string& serviceType,
	std::vector<ServiceInstance>& results)
{
	std::string suffix = "." + serviceType;

	for (const std::string& key : store.instances) {
		ServiceInstance instance;

		auto display = store.displayNames.find(key);
		instance.fullName = display != store.displayNames.end()
			? display->second : key;
		instance.instanceName = instance.fullName;
		if (EndsWith(NormalizeName(instance.fullName), suffix)) {
			instance.instanceName = instance.fullName.substr(0,
				instance.fullName.size() - suffix.size());
		}

		auto service = store.services.find(key);
		if (service != store.services.end()) {
			instance.hostName = service->second.first;
			instance.port = service->second.second;

			auto address = store.addresses.find(
				NormalizeName(instance.hostName));
			if (address != store.addresses.end()) {
				instance.address = address->second;
			} else {
				auto source = store.sources.find(key);
				if (source != store.sources.end()) {
					instance.address = source->second;
					instance.addressFromSource = true;
				}
			}
		}

		auto txt = store.txt.find(key);
		if (txt != store.txt.end()) {
			for (const std::string& entry : txt->second) {
				size_t equals = entry.find('=');
				if (equals == std::string::npos)
					instance.txt.push_back(std::make_pair(entry, std::string()));
				else {
					instance.txt.push_back(std::make_pair(
						entry.substr(0, equals), entry.substr(equals + 1)));
				}
			}
		}

		results.push_back(instance);
	}
}


// Closes the socket on every return path.
struct SocketCloser {
	int fd;
	~SocketCloser() { if (fd >= 0) close(fd); }
};


}	// namespace


MDNSBrowser::MDNSBrowser()
	:
	fResponsesReceived(0),
	fErrorContext("")
{
}


MDNSBrowser::~MDNSBrowser()
{
}


status_t
MDNSBrowser::_Fail(const char* context)
{
	status_t error = errno;	// capture before anything can change it
	fErrorContext = context;
	return error;
}


status_t
MDNSBrowser::Browse(const char* serviceType, bigtime_t timeout,
	BrowseMode mode, std::vector<ServiceInstance>& results,
	const char* directAddress)
{
	results.clear();
	fResponsesReceived = 0;
	fErrorContext = "";

	std::vector<uint8> query;
	if (serviceType == NULL
		|| !BuildQuery(serviceType, mode != kBrowseMulticast, query)) {
		fErrorContext = "building the query (invalid service type)";
		return B_BAD_VALUE;
	}

	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return _Fail("creating the socket");
	SocketCloser closer = { fd };

	sockaddr_in local;
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_ANY);

	if (mode == kBrowseMulticast) {
		int yes = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
		setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
		local.sin_port = htons(kMDNSPort);
	} else
		local.sin_port = 0;	// any free port

	if (bind(fd, (sockaddr*)&local, sizeof(local)) < 0) {
		return _Fail(mode == kBrowseMulticast
			? "binding UDP port 5353" : "binding a local UDP port");
	}

	// Destination: the multicast group, or one host in direct mode.
	sockaddr_in group;
	memset(&group, 0, sizeof(group));
	group.sin_family = AF_INET;
	group.sin_port = htons(kMDNSPort);
	if (mode == kBrowseDirect) {
		if (directAddress == NULL
			|| inet_pton(AF_INET, directAddress, &group.sin_addr) != 1) {
			fErrorContext = "parsing the direct address (expected a.b.c.d)";
			return B_BAD_VALUE;
		}
	} else
		inet_pton(AF_INET, kMDNSGroup, &group.sin_addr);

	if (mode == kBrowseMulticast) {
		ip_mreq membership;
		memset(&membership, 0, sizeof(membership));
		membership.imr_multiaddr = group.sin_addr;
		membership.imr_interface.s_addr = htonl(INADDR_ANY);
		if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership,
				sizeof(membership)) < 0)
			return _Fail("joining multicast group 224.0.0.251");
	}

	// RFC 6762 section 11 recommends TTL 255. Not fatal if refused: the
	// default TTL of 1 still reaches devices on the local link.
	unsigned char ttl = 255;
	setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

	if (sendto(fd, query.data(), query.size(), 0, (sockaddr*)&group,
			sizeof(group)) < 0)
		return _Fail("sending the query");

	RecordStore store;
	std::string normalizedType = NormalizeName(serviceType);
	std::vector<uint8> buffer(kMaxPacketSize);
	bigtime_t deadline = system_time() + timeout;

	while (true) {
		bigtime_t remaining = deadline - system_time();
		if (remaining <= 0)
			break;

		pollfd waitFor;
		waitFor.fd = fd;
		waitFor.events = POLLIN;
		waitFor.revents = 0;
		int ready = poll(&waitFor, 1, (int)((remaining + 999) / 1000));
		if (ready < 0) {
			if (errno == EINTR)
				continue;
			return _Fail("waiting for responses");
		}
		if (ready == 0)
			break;	// timed out

		sockaddr_in from;
		socklen_t fromLength = sizeof(from);
		ssize_t received = recvfrom(fd, buffer.data(), buffer.size(), 0,
			(sockaddr*)&from, &fromLength);
		if (received < 0) {
			if (errno == EINTR)
				continue;
			return _Fail("receiving a response");
		}

		char fromText[INET_ADDRSTRLEN] = "";
		inet_ntop(AF_INET, &from.sin_addr, fromText, sizeof(fromText));

		if (ParsePacket(buffer.data(), (size_t)received, fromText,
				normalizedType, store))
			fResponsesReceived++;
	}

	AssembleResults(store, normalizedType, results);
	return B_OK;
}
