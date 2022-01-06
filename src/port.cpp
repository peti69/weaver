#include <cstring>
#include <stdexcept>

#include <unistd.h>  
#include <fcntl.h>  
#include <errno.h> 

#include "finally.h"
#include "port.h"

bool PortConfig::isValidBaudRate(int baudRate) 
{
	return (  baudRate == 1200 || baudRate == 1800 || baudRate == 2400 
	       || baudRate == 4800  || baudRate == 9600 || baudRate == 19200
	       || baudRate == 38400 || baudRate == 76800 || baudRate == 115200
	       );
}

bool PortConfig::isValidDataBits(int dataBits) 
{
	return dataBits == 5 || dataBits == 6 || dataBits == 7 || dataBits == 8;
}

bool PortConfig::isValidStopBits(int stopBits) 
{
	return stopBits == 1 || stopBits == 2;
}

bool PortConfig::isValidParity(string parityStr, Parity& parity)
{
	if (parityStr == "even")
		parity = EVEN;
	else if (parityStr == "odd")
		parity = ODD;
	else if (parityStr == "none")
		parity = NONE;
	else
		return false;
	return true;
}

PortHandler::PortHandler(string _id, PortConfig _config, Logger _logger) : 
	id(_id), config(_config), logger(_logger), fd(-1), lastOpenTry(0), lastDataReceipt(0)
{
	handlerState.errorCounter = 0;
}

PortHandler::~PortHandler() 
{ 
	close(); 
}

void PortHandler::validate(Items& items) const
{
	auto& bindings = config.getBindings();

	for (auto& itemPair : items)
		if (itemPair.second.getOwnerId() == id && bindings.find(itemPair.first) == bindings.end())
			throw std::runtime_error("Item " + itemPair.first + " has no binding for link " + id);

	for (auto& bindingPair : bindings)
	{
		Item& item = items.validate(bindingPair.first);
		item.validateOwnerId(id);
//		item.validateReadable(false);
//		item.validateWritable(false);
		item.setReadable(false);
		item.setWritable(false);
	}
}

bool PortHandler::open()
{
	// port already open?
	if (fd >= 0)
		return true;

	// shell we perform another attempt to open the port?
	std::time_t now = std::time(0);
	if (lastOpenTry + config.getReopenInterval() > now)
		return false;
	lastOpenTry = now;
	lastDataReceipt = now;

	// open port
	fd = ::open(config.getName().c_str(), O_RDONLY | O_NONBLOCK | O_NDELAY |O_NOCTTY);
	if (fd < 0)
		logger.errorX() << unixError("open") << endOfMsg();
	auto autoClose = finally([this] { ::close(fd); fd = -1; });

	// get current port settings
	memset(&oldSettings, 0, sizeof(oldSettings));
	if (tcgetattr(fd, &oldSettings) != 0)
		logger.errorX() << unixError("tcgetattr") << endOfMsg();

	// create new port settings
	struct termios settings;
	memset(&settings, 0, sizeof(settings));
	
	// set baud rate
	speed_t speed;
	switch (config.getBaudRate())
	{
		case 1200:
			speed = B1200; break;
		case 1800:
			speed = B1800; break;
		case 2400:
			speed = B2400; break;
		case 4800:
			speed = B4800; break;
		case 9600:
			speed = B9600; break;
		case 19200:
			speed = B19200; break;
		case 38400:
			speed = B38400; break;
		case 57600:
			speed = B57600; break;
		case 115200:
			speed = B115200; break;
		default:
			assert(false && "invalid baud rate");
	}
	cfsetospeed(&settings, speed);
	cfsetispeed(&settings, speed);
	
	// set parity
	switch (config.getParity())
	{
		case PortConfig::NONE:
			settings.c_cflag &= ~PARENB;
			break;
		case PortConfig::ODD:
			settings.c_cflag |= PARENB;
			settings.c_cflag |= PARODD;
			break;
		case PortConfig::EVEN:
			settings.c_cflag |= PARENB;
			settings.c_cflag &= ~PARODD;
			break;
		default:
			assert(false && "invalid parity");
	}
	
	// set data bits
	tcflag_t cs;
	switch (config.getDataBits())
	{
		case 5:
			cs = CS5; break;
		case 6:
			cs = CS6; break;
		case 7:
			cs = CS7; break;
		case 8:
			cs = CS8; break;
		default:
			assert(false && "invalid data bits");
	}
	settings.c_cflag &= ~CSIZE;
	settings.c_cflag |= cs;
		
	// set stop bits
	switch (config.getStopBits())
	{
		case 1:
			settings.c_cflag &= ~CSTOPB; break;
		case 2:
			settings.c_cflag |= CSTOPB; break;
		default:
			assert(false && "invalid stop bits");
	}
	
	// enable the receiver and set local mode
	settings.c_cflag |= (CLOCAL | CREAD);

	// ignore parity errors	
	//settings.c_iflag |= IGNPAR;  
	  
	// enable canonical mode
	settings.c_lflag |= ICANON;
	
	// generate signals
	settings.c_lflag |= ISIG;

	// enable new settings
	if (tcsetattr(fd, TCSANOW, &settings) != 0)
		logger.errorX() << unixError("tcsetattr") << endOfMsg();
	
	autoClose.disable();
	logger.info() << "Serial port " << config.getName() << " open" << endOfMsg();
	
	return true;
}

void PortHandler::close()
{
	if (fd < 0)
		return;

	tcsetattr(fd, TCSANOW, &oldSettings);
	::close(fd);
	fd = -1;
	lastOpenTry = 0;
	lastDataReceipt = 0;
	msgData.clear();

	logger.info() << "Serial port " << config.getName() << " closed" << endOfMsg();
}

void PortHandler::receiveData()
{
	// receive data
	char buffer[256];
	int rc = ::read(fd, buffer, sizeof(buffer));
	if (rc < 0)
		if (errno == EWOULDBLOCK || errno == EAGAIN)
			return;
		else
			logger.errorX() << unixError("read") << endOfMsg();
	if (rc == 0)
		logger.errorX() << "Data transmission stopped" << endOfMsg();
	string receivedData = string(buffer, rc);

	// trace received data
	if (config.getLogRawData())
		if (config.getLogRawDataInHex())
			logger.debug() << "R " << cnvToHexStr(receivedData) << endOfMsg();
		else
			logger.debug() << "R " << receivedData << endOfMsg();

	// append received data to overall data
	msgData += receivedData;

	// remember time of data receipt
	lastDataReceipt = std::time(0);
}

long PortHandler::collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd)
{
	if (fd != -1)
	{
		FD_SET(fd, readFds);
		*maxFd = std::max(*maxFd, fd);
	}

	return -1;
}

Events PortHandler::receive(const Items& items)
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

Events PortHandler::receiveX()
{
	std::time_t now = std::time(0);

	Events events;

	// try to open the port
	if (!open())
		return events;

	// detect data timeout
	if (lastDataReceipt + config.getTimeoutInterval() <= now)
		logger.errorX() << "Data transmission timed out" << endOfMsg();

	// read all available data
	receiveData();

	// analyze available data
	std::smatch match;
	while (std::regex_search(msgData, match, config.getMsgPattern()))
	{
		// complete message available
		string msg = match[0];
		string binMsg = cnvToBinStr(msg);
		msgData = match.suffix();

		// analyze message
		for (auto& bindingPair : config.getBindings())
		{
			auto& binding = bindingPair.second;

			if (  (binding.binMatching && std::regex_search(binMsg, match, binding.pattern) && match.size() == 2)
			   || (!binding.binMatching && std::regex_search(msg, match, binding.pattern) && match.size() == 2)
			   )
				events.add(Event(id, binding.itemId, EventType::STATE_IND, string(match[1])));
		}
	}

	// detect wrong data
	if (msgData.length() > 2 * config.getMaxMsgSize())
		logger.errorX() << "Data " << msgData << " does not match message pattern" << endOfMsg();

	return events;
}

