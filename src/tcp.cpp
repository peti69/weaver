#include <cstring>

#include <unistd.h>
#include <errno.h> 
#include <netdb.h>
#include <fcntl.h>

#include "finally.h"
#include "tcp.h"

TcpHandler::TcpHandler(string _id, TcpConfig _config, Logger _logger) :
	id(_id), config(_config), logger(_logger), socket(-1), lastConnectTry(0)
{
}

TcpHandler::~TcpHandler()
{ 
	close(); 
}

void TcpHandler::validate(Items& items) const
{
	auto& bindings = config.getBindings();

	for (auto& itemPair : items)
		if (itemPair.second.getOwnerId() == id && bindings.find(itemPair.first) == bindings.end())
			throw std::runtime_error("Item " + itemPair.first + " has no binding for link " + id);

	for (auto& bindingPair : config.getBindings())
	{
		Item& item = items.validate(bindingPair.first);
		item.validateOwnerId(id);
//		item.validateReadable(false);
//		item.validateWritable(false);
		item.setReadable(false);
		item.setWritable(false);
	}
}

bool TcpHandler::open()
{
	// connection already established?
	if (socket >= 0)
		return true;

	// shell we perform another attempt to connect to the remote site?
	std::time_t now = std::time(0);
	if (lastConnectTry + config.getReconnectInterval() > now)
		return false;
	lastConnectTry = now;

	addrinfo hints;
	addrinfo* addrs;
//	hints.ai_family = AF_UNSPEC;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_NUMERICSERV | AI_ADDRCONFIG;
	int rc = getaddrinfo(config.getHostname().c_str(), cnvToStr(config.getPort()).c_str(), &hints, &addrs);
	if (rc != 0)
		logger.errorX() << "Error " << rc << " (" << gai_strerror(rc) << ") occurred in function getaddrinfo" << endOfMsg();
	auto autoFree = finally([addrs] { freeaddrinfo(addrs); });

	socket = ::socket(AF_INET, SOCK_STREAM /*| SOCK_NONBLOCK*/, 0);
	if (socket == -1)
		logger.errorX() << unixError("socket") << endOfMsg();
	auto autoClose = finally([this] { ::close(socket); socket = -1; });

	if (connect(socket, addrs->ai_addr, addrs->ai_addrlen) == -1)
		logger.errorX() << unixError("connect") << endOfMsg();

	int flags = fcntl(socket, F_GETFL);
	if (flags == -1)
		logger.errorX() << unixError("fcntl") << endOfMsg();
	if (fcntl(socket, F_SETFL, flags | O_NONBLOCK) == -1)
		logger.errorX() << unixError("fcntl") << endOfMsg();

	autoClose.disable();
	logger.info() << "Connected to " << config.getHostname() << ":" << config.getPort() << endOfMsg();

	return true;
}

void TcpHandler::close()
{
	if (socket < 0)
		return;

	::close(socket);
	socket = -1;
	lastConnectTry = 0;

	logger.info() << "Disconnected from " << config.getHostname() << ":" << config.getPort() << endOfMsg();
}

void TcpHandler::receiveData()
{
	// receive data
	char buffer[256];
	int rc = ::read(socket, buffer, sizeof(buffer));
	if (rc < 0)
		if (errno == EWOULDBLOCK || errno == EAGAIN)
			return;
		else
			logger.errorX() << unixError("read") << endOfMsg();
	if (rc == 0)
		logger.errorX() << "Disconnect by remote party" << endOfMsg();
	string receivedData = string(buffer, rc);

	// remove 0x00 bytes from received data
	string::size_type pos;
	while ((pos = receivedData.find('\x00')) != string::npos)
		receivedData.erase(pos);

	// trace received data
	if (config.getLogRawData())
		if (config.getLogRawDataInHex())
			logger.debug() << "R " << cnvToHexStr(receivedData) << endOfMsg();
		else
			logger.debug() << "R " << receivedData << endOfMsg();

	// append received data to overall data
	msgData += receivedData;
}

long TcpHandler::collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd)
{
	if (socket != -1)
	{
		FD_SET(socket, readFds);
		*maxFd = std::max(*maxFd, socket);
	}

	return -1;
}

Events TcpHandler::receive(const Items& items)
{
	try
	{
		return receiveX();
	}
	catch (const std::exception& ex)
	{
		logger.error() << ex.what() << endOfMsg();
	}

	close();
	return Events();
}

Events TcpHandler::receiveX()
{
	Events events;

	// try to connect to the remote site
	if (!open())
		return events;

	// read all available data
	receiveData();

	// break received data into messages
	std::smatch match;
	while (std::regex_search(msgData, match, config.getMsgPattern()))
	{
		string msg = match[0];
		msgData = match.suffix();

		for (auto& bindingPair : config.getBindings())
		{
			auto& binding = bindingPair.second;

			if (std::regex_search(msg, match, binding.pattern))
				if (match.size() == 2)
					events.add(Event(id, binding.itemId, EventType::STATE_IND, string(match[1])));
				else
					events.add(Event(id, binding.itemId, EventType::STATE_IND, Value::newVoid()));
		}
	}

	return events;
}

