#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <ctime>
#include <cstring>

#include "tr064.h"
#include "finally.h"

class UdpSocket
{
private:
	int fd;

public:
	UdpSocket(int _fd) : fd(_fd) {}
	static UdpSocket create();
	void sendMsg(IpAddr addr, IpPort port, ByteString msg) const;
	bool receiveMsg(ByteString& msg, IpAddr& addr, IpPort& port) const;
	bool waitForMsg(long timeoutMs) const;
};

UdpSocket UdpSocket::create()
{
	int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	if (fd == -1)
		Error() << unixError("socket") << endOfMsg();

	sockaddr_in sockAddr;
	sockAddr.sin_family = AF_INET;
	sockAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	sockAddr.sin_port = 0;
	int rc = ::bind(fd, reinterpret_cast<sockaddr*>(&sockAddr), sizeof(sockAddr));
	if (rc == -1)
		Error() << unixError("bind") << endOfMsg();

	return UdpSocket(fd);
}

void UdpSocket::sendMsg(IpAddr addr, IpPort port, ByteString msg) const
{
	sockaddr_in sockAddr;
	sockAddr.sin_family = AF_INET;
	sockAddr.sin_addr.s_addr = htonl(addr);
	sockAddr.sin_port = htons(port);
	int rc = ::sendto(fd, msg.data(), msg.length(), 0, reinterpret_cast<sockaddr*>(&sockAddr), sizeof(sockAddr));
	if (rc == -1)
		Error() << unixError("sendto") << endOfMsg();
}

bool UdpSocket::receiveMsg(ByteString& msg, IpAddr& addr, IpPort& port) const
{
	Byte buffer[1024];
	sockaddr_in sockAddr;
	socklen_t sockAddrLen = sizeof(sockAddr);
	int rc = ::recvfrom(fd, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&sockAddr), &sockAddrLen);
	if (rc == -1)
		if (errno == EWOULDBLOCK || errno == EAGAIN)
			return false;
		else
			Error() << unixError("recvFrom") << endOfMsg();

	if (sockAddrLen != sizeof(sockAddr))
		Error() << "Address returned by recvFrom() has unexpected size" << endOfMsg();
	if (sockAddr.sin_family != AF_INET)
		Error() << "Address returned by recvFrom() does not belong to family AF_INET" << endOfMsg();
	addr = IpAddr(ntohl(sockAddr.sin_addr.s_addr));
	port = ntohs(sockAddr.sin_port);
	msg = ByteString(buffer, rc);

	return true;
}

bool UdpSocket::waitForMsg(long timeoutMs) const
{
	fd_set readFds;
	FD_ZERO(&readFds);
	int fdMax = 0;
	FD_SET(fd, &readFds);

	timespec timeout;
	timeout.tv_sec = timeoutMs / 1000;
	timeout.tv_nsec = (timeoutMs % 1000) * 1000000;
	
	int rc = pselect(fd + 1, &readFds, 0, 0, &timeout, 0);
	if (rc == -1)
		Error() << unixError("pselect") << endOfMsg();
	
	return rc > 0;
}

Tr064::Tr064(string _id, Tr064Config _config, Logger _logger) : 
	id(_id), config(_config), logger(_logger), socket(-1)
{
}

long Tr064::collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd)
{
	if (socket != -1)
	{
		FD_SET(socket, readFds);
		*maxFd = std::max(*maxFd, socket);
	}

	return -1;
}

void Tr064::sendMSearch()
{
	ByteString msg = cnvFromAsciiStr(
		"M-SEARCH * HTTP/1.1\r\n"
		"HOST: 239.255.255.250:1900\r\n"
		"MAN: \"ssdp:discover\"\r\n"
		"MX: 3\r\n"
		"ST: urn:all\r\n"
		"\r\n");

	auto socket = UdpSocket::create();
	socket.sendMsg(ssdpAddr, ssdpPort, msg);

	logger.debug() << "Message sent:\n" << cnvToAsciiStr(msg) << endOfMsg();

	while (socket.waitForMsg(3000))
	{
		ByteString msg;
		IpAddr senderIpAddr;
		IpPort senderIpPort;
		receiveMsg(msg, senderIpAddr, senderIpPort);
		
		logger.debug() << "Message received from " << senderIpAddr.toStr() << ":" << senderIpPort << "\n" << cnvToAsciiStr(msg) << endOfMsg();
		
		return;
	}
}

bool Tr064::receiveMsg(ByteString& msg, IpAddr& addr, IpPort& port)
{
	Byte buffer[1024];
	sockaddr_in sockAddr;
	socklen_t sockAddrLen = sizeof(sockAddr);
	int rc = ::recvfrom(socket, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&sockAddr), &sockAddrLen);
	if (rc == -1)
		if (errno == EWOULDBLOCK || errno == EAGAIN)
			return false;
		else
			logger.errorX() << unixError("recvFrom") << endOfMsg();

	if (sockAddrLen != sizeof(sockAddr))
		logger.errorX() << "Address returned by recvFrom() has unexpected size" << endOfMsg();
	if (sockAddr.sin_family != AF_INET)
		logger.errorX() << "Address returned by recvFrom() does not belong to family AF_INET" << endOfMsg();
	addr = IpAddr(ntohl(sockAddr.sin_addr.s_addr));
	port = ntohs(sockAddr.sin_port);
	msg = ByteString(buffer, rc);

	//logMsg(msg, true);

	return true;
}

Events Tr064::receive(const Items& items)
{
	return Events();

	std::time_t now = std::time(0);

	if (socket == -1)
	{
		socket = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
		if (socket == -1)
			logger.errorX() << unixError("socket") << endOfMsg();
		auto autoClose = finally([this] { ::close(socket); socket = -1; });
	
		int loop = 1;
		int rc = setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &loop, sizeof (loop));
		if (rc == -1)
			logger.errorX() << unixError("setsockopt(SO_REUSEADDR)") << endOfMsg();
					   
		sockaddr_in localAddr;
		localAddr.sin_family = AF_INET;
		localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
		localAddr.sin_port = htons(ssdpPort);
		rc = bind(socket, reinterpret_cast<sockaddr*>(&localAddr), sizeof(localAddr));
		if (rc == -1)
			logger.errorX() << unixError("bind") << endOfMsg();

		rc = setsockopt(socket, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
		if (rc == -1)
			logger.errorX() << unixError("setsockopt(IP_MULTICAST_LOOP)") << endOfMsg();

		ip_mreq mreq;
		std::memset(&mreq, 0, sizeof(mreq));
		mreq.imr_multiaddr.s_addr = htonl(ssdpAddr);
		mreq.imr_interface.s_addr = INADDR_ANY;
		rc = setsockopt(socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof (mreq));
		if (rc == -1)
			logger.errorX() << unixError("setsockopt(IP_ADD_MEMBERSHIP)") << endOfMsg();

		sendMSearch();

		autoClose.disable();
	}
	
	Events events;

	ByteString msg;
	IpAddr senderIpAddr;
	IpPort senderIpPort;
	if (!receiveMsg(msg, senderIpAddr, senderIpPort))
		return events;
	
	logger.debug() << "Message received:\n" << cnvToAsciiStr(msg) << endOfMsg();
	
	for (auto& bindingPair : config.getBindings())
	{
		auto& binding = bindingPair.second;
		string itemId = binding.itemId;
		bool owner = items.getOwnerId(itemId) == id;

		// TODO
	}

	return events;
}

Events Tr064::send(const Items& items, const Events& events)
{
	Events newEvents;

	auto& bindings = config.getBindings();
	for (auto& event : events)
	{
		string itemId = event.getItemId();

		auto bindingPos = bindings.find(itemId);
		if (bindingPos != bindings.end())
		{
			auto& binding = bindingPos->second;
			bool owner = items.getOwnerId(itemId) == id;

			// TODO
		}
	}

	return newEvents;
}
