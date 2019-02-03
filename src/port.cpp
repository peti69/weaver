#include <cassert>
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
	id(_id), config(_config), logger(_logger), port(-1), lastOpenTry(0)
{
}

PortHandler::~PortHandler() 
{ 
	close(); 
}

bool PortHandler::open()
{
	// port already open?
	if (port >= 0)
		return true;

	// shell we perform another attempt to open the port?
	std::time_t now = std::time(0);
	if (lastOpenTry + config.getReopenInterval() > now)
		return false;
	lastOpenTry = now;
	
	// open port
	port = ::open(config.getName().c_str(), O_RDONLY | O_NONBLOCK | O_NOCTTY);
	if (port < 0)
		logger.errorX() << unixError("open") << endOfMsg();
	auto autoClose = finally([this] { ::close(port); port = -1; });

	// get current port settings
	memset(&oldSettings, 0, sizeof(oldSettings));
	if (tcgetattr(port, &oldSettings) != 0)
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

	// enable new settings
	if (tcsetattr(port, TCSANOW, &settings) != 0)
		logger.errorX() << unixError("tcsetattr") << endOfMsg();
	
	autoClose.disable();
	
	logger.info() << "Port open" << endOfMsg();
	
	return true;
}

void PortHandler::close()
{
	if (port < 0)
		return;
	
	tcsetattr(port, TCSANOW, &oldSettings);
	::close(port);
	port = -1;
	//lastOpenTry = 0;

	logger.info() << "Port closed" << endOfMsg();
}

void PortHandler::receiveData()
{
	// receive data
	char buffer[256];
	int rc = ::read(port, buffer, sizeof(buffer));
	if (rc < 0)
		if (errno == EWOULDBLOCK || errno == EAGAIN)
			return;
		else
			logger.errorX() << unixError("read") << endOfMsg();
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

Events PortHandler::receive()
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
}

Events PortHandler::receiveX()
{
	Events events;

	// try to open the port
	if (!open())
		return events;

	// read all available data
	receiveData();

//	std::smatch match;
//	while (std::regex_search(msgData, match, config.getMsgPattern()))
	regmatch_t match;
	while (!regexec(&config.getMsgPattern(), reinterpret_cast<const char*>(msgData.c_str()), 1, &match, 0))
	{
//		string msg = match.str(0);
//		msgData = match.suffix();
		if (match.rm_so == match.rm_eo)
		{
			msgData.clear();
			logger.warn() << "Message pattern produced an empty match" << endOfMsg();
			return events;
		}
		string msg = msgData.substr(match.rm_so, match.rm_eo - match.rm_so);
		msgData = msgData.substr(match.rm_eo);
		
		for (auto& pair : config.getBindings())
		{
			auto& binding = pair.second;

			regmatch_t match[2];
			if (!regexec(&binding.pattern, reinterpret_cast<const char*>(msg.c_str()), 2, match, 0))
				events.add(Event(id, binding.itemId, Event::STATE_IND, msg.substr(match[1].rm_so, match[1].rm_eo - match[1].rm_so)));
		}
	}

	return events;
}

