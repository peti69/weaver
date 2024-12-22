#include <cstring>

#include <unistd.h>
#include <errno.h> 
#include <netdb.h>
#include <fcntl.h>

#include "finally.h"
#include "tcp.h"

TcpHandler::TcpHandler(string id, TcpConfig config, Logger logger) :
	id(id), config(config), logger(logger), socket(-1), lastConnectTry(0), lastDataReceipt(0)
{
	handlerState.errorCounter = 0;
	handlerState.operational = false;
}

TcpHandler::~TcpHandler()
{ 
	close(); 
}

void TcpHandler::validate(Items& items)
{
	auto& bindings = config.getBindings();

	for (auto& [itemId, item] : items)
		if (item.getOwnerId() == id && !bindings.count(itemId))
			throw std::runtime_error("Item " + itemId + " has no binding for link " + id);

	for (auto& [itemId, binding] : bindings)
	{
		auto& item = items.validate(itemId);
		item.validateOwnerId(id);
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
	lastDataReceipt = now;

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
	handlerState.operational = true;

	return true;
}

void TcpHandler::close()
{
	if (socket < 0)
		return;

	::close(socket);
	socket = -1;
	lastConnectTry = 0;
	lastDataReceipt = 0;

	logger.info() << "Disconnected from " << config.getHostname() << ":" << config.getPort() << endOfMsg();
	handlerState.operational = false;
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

	if (receivedData.length())
	{
		// hex handling?
		if (config.getConvertToHex())
			receivedData = cnvToHexStr(receivedData);

		// trace received data
		if (config.getLogRawData())
			logger.debug() << "R " << receivedData << endOfMsg();

		// append received data to overall data
		msgData += receivedData;

		// remember time of data receipt
		lastDataReceipt = std::time(0);
	}

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
		handlerState.errorCounter++;

		logger.error() << ex.what() << endOfMsg();
	}

	close();
	return Events();
}

Events TcpHandler::receiveX()
{
	std::time_t now = std::time(0);

	Events events;

	// try to connect to the remote site
	if (!open())
		return events;

	// detect data timeout
	if (config.getTimeoutInterval() && lastDataReceipt + config.getTimeoutInterval() <= now)
		logger.errorX() << "Data transmission timed out" << endOfMsg();

	// read all available data
	receiveData();

	// analyze available data
	std::smatch match;
	while (std::regex_search(msgData, match, config.getMsgPattern()) && match.size() == 2)
	{
		// complete message available
		string msg = match[1];
		string binMsg = cnvToBinStr(msg);
		msgData = match.suffix();

		// analyze message
		for (auto& [itemId, binding] : config.getBindings())
			if (  (binding.binMatching && std::regex_search(binMsg, match, binding.pattern) && match.size() == 2)
			   || (!binding.binMatching && std::regex_search(msg, match, binding.pattern) && match.size() == 2)
			   )
				events.add(Event(id, itemId, EventType::STATE_IND, Value::newString(match[1])));
	}

	// detect wrong data
	if (msgData.length() > 2 * config.getMaxMsgSize())
		logger.errorX() << "Data " << msgData << " does not match message pattern" << endOfMsg();

	return events;
}

