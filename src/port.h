#ifndef PORT_H
#define PORT_H

#include <regex>
#include <ctime>
#include <termios.h>  

#include <regex.h>

#include "link.h"
#include "logger.h"

class PortConfig
{
public:
	enum Parity { NONE, ODD, EVEN };
	struct Binding
	{
		string itemId;
		regex_t pattern;
		Binding(string _itemId, regex_t _pattern) : 
			itemId(_itemId), pattern(_pattern) {};
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
	int reopenInterval;
//	std::regex msgPattern;
	regex_t msgPattern;
	bool logRawData;
	bool logRawDataInHex;
	Bindings bindings;
	
public:
	PortConfig(string _name, int _baudRate, int _dataBits, int _stopBits, Parity _parity, int _reopenInterval,
		regex_t _msgPattern, bool _logRawData, bool _logRawDataInHex, Bindings _bindings) :
		name(_name), baudRate(_baudRate), dataBits(_dataBits), stopBits(_stopBits), parity(_parity), 
		reopenInterval(_reopenInterval), msgPattern(_msgPattern), 
		logRawData(_logRawData), logRawDataInHex(_logRawDataInHex), bindings(_bindings)
	{}
	string getName() const { return name; }
	int getBaudRate() const { return baudRate; }
	int getDataBits() const { return dataBits; }
	int getStopBits() const { return stopBits; }
	Parity getParity() const { return parity; }
	int getReopenInterval() const { return reopenInterval; }
	const regex_t& getMsgPattern() const { return msgPattern; }
//	const std::regex& getMsgPattern() const { return msgPattern; }
	bool getLogRawData() const { return logRawData; }
	bool getLogRawDataInHex() const { return logRawDataInHex; }
	const Bindings& getBindings() const { return bindings; }
	
	static bool isValidBaudRate(int baudRate);
	static bool isValidDataBits(int dataBits);
	static bool isValidStopBits(int stopBits);
	static bool isValidParity(string parityStr, Parity& parity);
};

class PortHandler: public Handler
{
private:
	string id;
	PortConfig config;
	Logger logger;
	string msgData;
	int fd;
	std::time_t lastOpenTry;
	struct termios oldSettings;

public:
	PortHandler(string _id, PortConfig _config, Logger _logger);
	virtual ~PortHandler();
	virtual bool supports(EventType eventType) const override { return eventType == EventType::STATE_IND; }
	virtual int getReadDescriptor() override { return fd; }
	virtual int getWriteDescriptor() override { return -1; }
	virtual Events receive(const Items& items) override;
	virtual Events send(const Items& items, const Events& events) override { return Events(); }

private:
	bool open();
	void close();	
	Events receiveX();
	void receiveData();
};

#endif