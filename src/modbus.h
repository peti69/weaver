#ifndef MODBUS_H
#define MODBUS_H

#include "link.h"
#include "logger.h"

namespace modbus
{

class Config
{
public:
	struct Binding
	{
		string itemId;
		Byte unitId;
		int firstRegister;
		int lastRegister;
		int factorRegister;
		Binding(string itemId, Byte unitId, int firstRegister, int lastRegister, int factorRegister) :
			itemId(itemId), unitId(unitId), firstRegister(firstRegister), lastRegister(lastRegister), factorRegister(factorRegister) {};
	};
	class Bindings: public std::map<string, Binding>
	{
	public:
		void add(Binding binding) { insert(value_type(binding.itemId, binding)); }
	};

private:
	string hostname;
	int port;
	Seconds reconnectInterval;
	Seconds responseTimeout = 5s;
	bool logRawData;
	bool logMsgs;
	Bindings bindings;

public:
	Config(string hostname, int port,  Seconds reconnectInterval,
		bool logRawData, bool logMsgs, Bindings bindings) :
		hostname(hostname), port(port), reconnectInterval(reconnectInterval),
		logRawData(logRawData), logMsgs(logMsgs), bindings(bindings)
	{}
	string getHostname() const { return hostname; }
	int getPort() const { return port; }
	Seconds getReconnectInterval() const { return reconnectInterval; }
	Seconds getResponseTimeout() const { return responseTimeout; }
	bool getLogRawData() const { return logRawData; }
	bool getLogMsgs() const { return logMsgs; }
	const Bindings& getBindings() const { return bindings; }
};

class Handler: public HandlerIf
{
private:
	string id;
	Config config;
	Logger logger;
	ByteString streamData;
	int socket;
	Byte transactionId;
	TimePoint lastConnectTry;
	TimePoint lastDataReceipt;
	HandlerState handlerState;
	std::map<Byte, std::pair<TimePoint, Config::Bindings::const_iterator>> requests;

public:
	Handler(string id, Config config, Logger logger);
	virtual ~Handler();
	virtual void validate(Items& items) override;
	virtual HandlerState getState() const override { return handlerState; }
	virtual long collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd) override;
	virtual Events receive(const Items& items) override;
	virtual Events send(const Items& items, const Events& events) override;

private:
	bool open();
	void close();	
	Events receiveX(const Items& items);
	void receiveData();
	void sendX(const Items& items, const Events& events);
};

}

#endif
