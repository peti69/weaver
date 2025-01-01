#include <cmath>

#include <unistd.h>
#include <errno.h> 
#include <netdb.h>
#include <fcntl.h>

#include "finally.h"
#include "modbus.h"

namespace modbus
{

Handler::Handler(string id, Config config, Logger logger) :
	id(id), config(config), logger(logger)
{
	handlerState.errorCounter = 0;
	handlerState.operational = false;
}

Handler::~Handler()
{ 
	close(); 
}

void Handler::validate(Items& items)
{
	auto& bindings = config.getBindings();

	for (auto& [itemId, item] : items)
		if (item.getOwnerId() == id && !bindings.count(itemId))
			throw std::runtime_error("Item " + itemId + " has no binding for link " + id);

	for (auto& [itemId, binding] : bindings)
	{
		auto& item = items.validate(itemId);
		item.validateOwnerId(id);
		item.setReadable(true);
		item.setWritable(false);
	}
}

bool Handler::open()
{
	// connection already established?
	if (socket >= 0)
		return true;

	// shell we perform another attempt to connect to the remote site?
	TimePoint now = Clock::now();
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

void Handler::close()
{
	if (socket < 0)
		return;

	::close(socket);
	socket = -1;
	lastConnectTry.setToNull();
	lastDataReceipt.setToNull();
	requests.clear();
	streamData.clear();

	logger.info() << "Disconnected from " << config.getHostname() << ":" << config.getPort() << endOfMsg();
	handlerState.operational = false;
}

long Handler::collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd)
{
	if (socket != -1)
	{
		FD_SET(socket, readFds);
		*maxFd = std::max(*maxFd, socket);
	}

	return -1;
}

Events Handler::receive(const Items& items)
{
	try
	{
		return receiveX(items);
	}
	catch (const std::exception& ex)
	{
		handlerState.errorCounter++;

		logger.error() << ex.what() << endOfMsg();
	}

	close();
	return Events();
}

Events Handler::receiveX(const Items& items)
{
	Events events;

	// try to connect to remote site
	if (!open())
		return events;

	// read all available data
	receiveData();

	// analyze available data
	while (streamData.length() >= 6)
	{
		int length = streamData[4] << 8 | streamData[5];
		if (streamData.length() >= length + 6)
		{
			// complete response available
			ByteString msg = streamData.substr(0, length + 6);

			// verify response consistency
			if (msg.length() < 9)
				logger.errorX() << "Invalid response " + cnvToHexStr(msg) + " received (1)" << endOfMsg();
			if (msg.length() != msg[8] + 9)
				logger.errorX() << "Invalid response " + cnvToHexStr(msg) + " received (2)" << endOfMsg();

			// extract fields from response
			Byte receivedTransactionId = msg[1];
			ByteString data = msg.substr(9);

			// trace response
			if (config.getLogMsgs())
				logger.debug() << "Response " <<  static_cast<int>(receivedTransactionId) << "," << static_cast<int>(msg[6]) << "," << cnvToHexStr(data) << endOfMsg();

			// process response
			if (auto requestPos = requests.find(receivedTransactionId); requestPos != requests.end())
			{
				// matching request with binding found
				auto& [itemId, binding] = *requestPos->second.second;

				// verify response against binding definition
				if (data.length() != (binding.lastRegister() - binding.firstRegister() + 1) * 2)
					logger.errorX() << "Response " + cnvToHexStr(msg) + " does not match binding definition of item " + itemId << endOfMsg();

				auto convert = [](ByteString data)
				{
					assert(data.length() <= sizeof(uint64_t));
					uint64_t value = 0;
					if (data[0] & 0x80)
					{
						for (int i = 0; i < data.length(); i++)
							value = (value << 8) | (~data[i] & 0xFF);
						return -1.0 * value - 1;
					}
					else
					{
						for (int i = 0; i < data.length(); i++)
							value = (value << 8) | data[i];
						return 1.0 * value;
					}
				};

				auto addEvent = [&](ByteString data, int baseRegister, ItemId itemId, const Config::Binding& binding)
				{
					ByteString registerData = data.substr((binding.valueRegister - baseRegister) * 2, binding.valueRegisterCount * 2);
					if (items.get(itemId).hasValueType(ValueType::NUMBER))
					{
						Number num = convert(registerData);
						if (binding.factorRegister >= 0)
							num *= std::pow(10, convert(data.substr((binding.factorRegister - baseRegister) * 2, 2)));
						events.add(Event(id, itemId, EventType::STATE_IND, Value::newNumber(num)));
					}
					else
						events.add(Event(id, itemId, EventType::STATE_IND, Value::newString(cnvToHexStr(registerData))));
				};

				// generate event for binding
				addEvent(data, binding.firstRegister(), itemId, binding);

				// generate events for other bindings if possible
				for (auto& [otherItemId, otherBinding] : config.getBindings())
					if (itemId != otherItemId && binding.unitId == otherBinding.unitId)
					{
						// verify that register range of other binding is covered
						if (otherBinding.firstRegister() < binding.firstRegister() || otherBinding.lastRegister() > binding.lastRegister())
							continue;

						// generate event for other binding
						addEvent(data, binding.firstRegister(), otherItemId, otherBinding);
					}

				// discard request information
				requests.erase(requestPos);
			}
			else
				logger.errorX() << "No matching pending request for received response " + cnvToHexStr(msg) << endOfMsg();

			// discard processed response
			streamData.erase(0, msg.length());
		}
		else
			break;
	}

	// check for response timeouts
	for (auto requestPos = requests.begin(); requestPos != requests.end();)
	{
		auto& request = requestPos->second;
		if (Clock::now() > request.first + config.getResponseTimeout())
		{
			logger.warn() << "No response within expected time span for " + request.second->first + " query request" << endOfMsg();
			requestPos = requests.erase(requestPos);
		}
		else
			requestPos++;
	}

	return events;
}

void Handler::receiveData()
{
	// receive data
	Byte buffer[256];
	int rc = ::read(socket, buffer, sizeof(buffer));
	if (rc < 0)
		if (errno == EWOULDBLOCK || errno == EAGAIN)
			return;
		else
			logger.errorX() << unixError("read") << endOfMsg();
	if (rc == 0)
		logger.errorX() << "Disconnect by remote party" << endOfMsg();
	ByteString receivedData(buffer, rc);

	if (receivedData.length())
	{
		// trace received data
		if (config.getLogRawData())
			logger.debug() << "R " << cnvToHexStr(receivedData) << endOfMsg();

		// append received data to overall data
		streamData += receivedData;

		// remember time of data receipt
		lastDataReceipt = Clock::now();
	}
}

Events Handler::send(const Items& items, const Events& events)
{
	try
	{
		sendX(items, events);
		return Events();
	}
	catch (const std::exception& ex)
	{
		handlerState.errorCounter++;

		logger.error() << ex.what() << endOfMsg();
	}

	close();
	return Events();
}

void Handler::sendX(const Items& items, const Events& events)
{
	// try to connect to remote site
	if (!open())
		return;

	auto& bindings = config.getBindings();
	for (auto& event : events)
		if (auto bindingPos = bindings.find(event.getItemId()); bindingPos != bindings.end())
		{
			auto& binding = bindingPos->second;

			if (event.getType() == EventType::READ_REQ)
			{
				lastTransactionId++;

				// trace request
				if (config.getLogMsgs())
					logger.debug() << "Request " <<  static_cast<int>(lastTransactionId) << "," << static_cast<int>(binding.unitId) << ","
					               << binding.valueRegister << "," << binding.factorRegister << endOfMsg();

				// build request to be sent
				Byte length = 6;
				Byte mbapHeader[7];
				mbapHeader[0] = (lastTransactionId >> 8) & 0xFF;
				mbapHeader[1] = lastTransactionId & 0xFF;
				mbapHeader[2] = 0x00;
				mbapHeader[3] = 0x00;
				mbapHeader[4] = (length >> 8) & 0xFF;
				mbapHeader[5] = length & 0xFF;
				mbapHeader[6] = binding.unitId;
				int address = binding.firstRegister() - 1;
				Byte data[4];
				data[0] = (address >> 8) & 0xFF;
				data[1] = address & 0xFF;
				data[2] = 0x00;
				data[3] = binding.lastRegister() - binding.firstRegister() + 1;
				ByteString msg = ByteString(mbapHeader, sizeof(mbapHeader)) + ByteString({0x03}) + ByteString(data, sizeof(data));

				// trace request to be sent
				if (config.getLogRawData())
					logger.debug() << "S " << cnvToHexStr(msg) << endOfMsg();

				// send request
				int rc = ::write(socket, msg.data(), msg.length());
				if (rc < 0)
					logger.errorX() << unixError("read") << endOfMsg();
				if (rc == 0)
					logger.errorX() << "Disconnect by remote party" << endOfMsg();

				// remember request
				requests[lastTransactionId] = {Clock::now(), bindingPos};
			}
		}
}

}
