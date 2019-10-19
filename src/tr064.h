#ifndef TR064_H
#define TR064_H

#include "link.h"
#include "logger.h"
#include "knx.h"

class Tr064Config
{
public:
	struct Binding
	{
		string itemId;
		EventType eventType;
		string value;
		int interval;
		Binding(string _itemId, EventType _eventType, string _value, int _interval) : 
			itemId(_itemId), eventType(_eventType), value(_value), interval(_interval) {};
	};
	struct Bindings: public std::map<string, Binding>
	{
		void add(Binding binding) { insert(value_type(binding.itemId, binding)); }
	};

private:
	Bindings bindings;

public:
	Tr064Config(Bindings _bindings) : bindings(_bindings) {}
	const Bindings& getBindings() const { return bindings; }
};

static const IpPort ssdpPort(1900);
static const IpAddr ssdpAddr(239, 255, 255, 250);

class Tr064: public Handler
{
private:
	string id;
	Tr064Config config;
	Logger logger;
	int socket;

public:
	Tr064(string _id, Tr064Config _config, Logger _logger);
	virtual bool supports(EventType eventType) const override { return true; }
	virtual long collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd) override;
	virtual Events receive(const Items& items) override;
	virtual Events send(const Items& items, const Events& events) override;

private:
	void sendMSearch();
	bool receiveMsg(ByteString& msg, IpAddr& addr, IpPort& port);
};

#endif
