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
	bool convertToHex;
	std::regex msgPattern;
	int maxMsgSize;
	bool logRawData;
	ItemId inputItemId;
	Bindings bindings;

public:
	PortConfig(string name, int baudRate, int dataBits, int stopBits, Parity parity, int timeoutInterval,
		int reopenInterval, bool convertToHex, std::regex msgPattern, int maxMsgSize, bool logRawData,
		ItemId inputItemId, Bindings bindings) :
		name(name), baudRate(baudRate), dataBits(dataBits), stopBits(stopBits), parity(parity),
		timeoutInterval(timeoutInterval), reopenInterval(reopenInterval), convertToHex(convertToHex),
		msgPattern(msgPattern), maxMsgSize(maxMsgSize), logRawData(logRawData), inputItemId(inputItemId),
		bindings(bindings)
	{}
	string getName() const { return name; }
	int getBaudRate() const { return baudRate; }
	int getDataBits() const { return dataBits; }
	int getStopBits() const { return stopBits; }
	Parity getParity() const { return parity; }
	int getTimeoutInterval() const { return timeoutInterval; }
	int getReopenInterval() const { return reopenInterval; }
	bool getConvertToHex() const { return convertToHex; }
	const std::regex& getMsgPattern() const { return msgPattern; }
	int getMaxMsgSize() const { return maxMsgSize; }
	bool getLogRawData() const { return logRawData; }
	ItemId getInputItemId() const { return inputItemId; }
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
	string streamData;
	string inputData;
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
	virtual Events send(const Items& items, const Events& events) override;

private:
	bool open();
	void close();	
	Events receiveX();
	void receiveData();
};

#endif
