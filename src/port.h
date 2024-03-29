#ifndef PORT_H
#define PORT_H

#include <regex>
#include <ctime>

#include <termios.h>

#include "link.h"
#include "logger.h"

class PortConfig
{
public:
	enum Parity { NONE, ODD, EVEN };
	struct Binding
	{
		string itemId;
		std::regex pattern;
		bool binMatching;
		Binding(string itemId, std::regex pattern, bool binMatching) :
			itemId(itemId), pattern(pattern), binMatching(binMatching) {};
	};
	class Bindings: public std::map<string, Binding>
	{
	public:
		void add(Binding binding) { insert(value_type(binding.itemId, binding)); }
	};

private:
	string name;
	int baudRate;
	int dataBits;
	int stopBits;
	Parity parity;
	int timeoutInterval;
	int reopenInterval;
	std::regex msgPattern;
	int maxMsgSize;
	bool logRawData;
	bool logRawDataInHex;
	Bindings bindings;

public:
	PortConfig(string _name, int _baudRate, int _dataBits, int _stopBits, Parity _parity, int _timeoutInterval,
		int _reopenInterval, std::regex _msgPattern, int _maxMsgSize, bool _logRawData, bool _logRawDataInHex, Bindings _bindings) :
		name(_name), baudRate(_baudRate), dataBits(_dataBits), stopBits(_stopBits), parity(_parity), 
		timeoutInterval(_timeoutInterval), reopenInterval(_reopenInterval), msgPattern(_msgPattern),
		maxMsgSize(_maxMsgSize), logRawData(_logRawData), logRawDataInHex(_logRawDataInHex), bindings(_bindings)
	{}
	string getName() const { return name; }
	int getBaudRate() const { return baudRate; }
	int getDataBits() const { return dataBits; }
	int getStopBits() const { return stopBits; }
	Parity getParity() const { return parity; }
	int getTimeoutInterval() const { return timeoutInterval; }
	int getReopenInterval() const { return reopenInterval; }
	const std::regex& getMsgPattern() const { return msgPattern; }
	int getMaxMsgSize() const { return maxMsgSize; }
	bool getLogRawData() const { return logRawData; }
	bool getLogRawDataInHex() const { return logRawDataInHex; }
	const Bindings& getBindings() const { return bindings; }

	static bool isValidBaudRate(int baudRate);
	static bool isValidDataBits(int dataBits);
	static bool isValidStopBits(int stopBits);
	static bool isValidParity(string parityStr, Parity& parity);
};

class PortHandler: public HandlerIf
{
private:
	string id;
	PortConfig config;
	Logger logger;
	string msgData;
	int fd;
	std::time_t lastOpenTry;
	std::time_t lastDataReceipt;
	struct termios oldSettings;
	HandlerState handlerState;

public:
	PortHandler(string _id, PortConfig _config, Logger _logger);
	virtual ~PortHandler();
	virtual void validate(Items& items) override;
	virtual HandlerState getState() const override { return handlerState; }
	virtual long collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd) override;
	virtual Events receive(const Items& items) override;
	virtual Events send(const Items& items, const Events& events) override { return Events(); }

private:
	bool open();
	void close();	
	Events receiveX();
	void receiveData();
};

#endif
