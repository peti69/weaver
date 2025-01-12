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
		Value value;
		int interval;
		Binding(string itemId, EventType eventType, Value value, int interval) :
			itemId(itemId), eventType(eventType), value(value), interval(interval) {};
	};
	struct Bindings: public std::map<string, Binding>
	{
		void add(Binding binding) { insert(value_type(binding.itemId, binding)); }
	};

private:
	Bindings bindings;

public:
	GeneratorConfig(Bindings bindings) : bindings(bindings) {}
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
	Generator(string id, GeneratorConfig config, Logger logger);
	virtual void validate(Items& items) override;
	virtual HandlerState getState() const override { return HandlerState(); }
	virtual long collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd) override { return -1; };
	virtual Events receive(const Items& items) override;
	virtual Events send(const Items& items, const Events& events) override;
};

#endif
