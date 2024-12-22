#ifndef TCP_H
#define TCP_H

#include <regex>
#include <ctime>

#include "link.h"
#include "logger.h"

class TcpConfig
{
public:
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
	string hostname;
	int port;
	int timeoutInterval;
	int reconnectInterval;
	bool convertToHex;
	std::regex msgPattern;
	int maxMsgSize;
	bool logRawData;
	Bindings bindings;

public:
	TcpConfig(string hostname, int port, int timeoutInterval, int reconnectInterval, bool convertToHex,
		std::regex msgPattern, int maxMsgSize, bool logRawData, Bindings bindings) :
		hostname(hostname), port(port), timeoutInterval(timeoutInterval),
		reconnectInterval(reconnectInterval), convertToHex(convertToHex), msgPattern(msgPattern),
		maxMsgSize(maxMsgSize), logRawData(logRawData), bindings(bindings)
	{}
	string getHostname() const { return hostname; }
	int getPort() const { return port; }
	int getTimeoutInterval() const { return timeoutInterval; }
	int getReconnectInterval() const { return reconnectInterval; }
	bool getConvertToHex() const { return convertToHex; }
	const std::regex& getMsgPattern() const { return msgPattern; }
	int getMaxMsgSize() const { return maxMsgSize; }
	bool getLogRawData() const { return logRawData; }
	const Bindings& getBindings() const { return bindings; }
};

class TcpHandler: public HandlerIf
{
private:
	string id;
	TcpConfig config;
	Logger logger;
	string msgData;
	int socket;
	std::time_t lastConnectTry;
	std::time_t lastDataReceipt;
	HandlerState handlerState;

public:
	TcpHandler(string id, TcpConfig config, Logger logger);
	virtual ~TcpHandler();
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
