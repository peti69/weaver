#ifndef GENERATOR_H
#define GENERATOR_H

#include <ctime>

#include "link.h"
#include "logger.h"

class GeneratorConfig
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
	GeneratorConfig(Bindings _bindings) : bindings(_bindings) {}
	const Bindings& getBindings() const { return bindings; }
};

class Generator: public HandlerIf
{
private:
	string id;
	GeneratorConfig config;
	Logger logger;
	std::map<string, std::time_t> lastGeneration;

public:
	Generator(string _id, GeneratorConfig _config, Logger _logger);
	virtual void validate(Items& items) const override;
	virtual HandlerState getState() const override { return HandlerState(); }
	virtual long collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd) override { return -1; };
	virtual Events receive(const Items& items) override;
	virtual Events send(const Items& items, const Events& events) override;
};

#endif
