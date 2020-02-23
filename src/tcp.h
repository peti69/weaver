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
		Binding(string _itemId, std::regex _pattern) :
			itemId(_itemId), pattern(_pattern) {};
	};
	class Bindings: public std::map<string, Binding>
	{
	public:
		void add(Binding binding) { insert(value_type(binding.itemId, binding)); }
	};

private:
	string hostname;
	int port;
	int reconnectInterval;
	std::regex msgPattern;
	bool logRawData;
	bool logRawDataInHex;
	Bindings bindings;

public:
	TcpConfig(string _hostname, int _port, int _reconnectInterval,
		std::regex _msgPattern, bool _logRawData, bool _logRawDataInHex, Bindings _bindings) :
		hostname(_hostname), port(_port), reconnectInterval(_reconnectInterval), msgPattern(_msgPattern),
		logRawData(_logRawData), logRawDataInHex(_logRawDataInHex), bindings(_bindings)
	{}
	string getHostname() const { return hostname; }
	int getPort() const { return port; }
	int getReconnectInterval() const { return reconnectInterval; }
	const std::regex& getMsgPattern() const { return msgPattern; }
	bool getLogRawData() const { return logRawData; }
	bool getLogRawDataInHex() const { return logRawDataInHex; }
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

public:
	TcpHandler(string _id, TcpConfig _config, Logger _logger);
	virtual ~TcpHandler();
	virtual bool supports(EventType eventType) const override { return eventType == EventType::STATE_IND; }
	virtual HandlerState getState() const override { return HandlerState(); }
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
