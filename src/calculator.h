#ifndef CALCULATOR_H
#define CALCULATOR_H

#include "link.h"
#include "logger.h"

namespace calculator
{

enum class Function { MAXIMUM, MINIMUM };

struct Binding
{
	// Item for which the binding applies.
	ItemId itemId;

	// Defines how the item is computed.
	Function function;

	// Refers to the item those values are taken as main input for the calculation.
	ItemId sourceItemId;

	// Refers to the item which defines the length of the considered period in seconds.
	ItemId periodItemId;

	Binding(ItemId itemId, Function function, ItemId sourceItemId, ItemId periodItemId) :
		itemId(itemId), function(function), sourceItemId(sourceItemId), periodItemId(periodItemId) {}
};

struct Bindings: public std::unordered_map<ItemId, Binding>
{
	void add(Binding binding) { insert({binding.itemId, binding}); }
};

class Config
{
private:
	Bindings bindings;

public:
	Config(Bindings bindings) : bindings(bindings) {}
	const Bindings& getBindings() const { return bindings; }
};

class Handler: public HandlerIf
{
private:
	string id;
	Config config;
	Logger logger;

	// Items which need to be recalculated in case an item changes.
	std::unordered_map<ItemId, ItemIds> dependants;

	// Time then the last global recalculation had happened.
	TimePoint lastCalculation = TimePoint::min();

public:
	Handler(string id, Config config, Logger logger);
	virtual void validate(Items& items) override;
	virtual HandlerState getState() const override { return HandlerState(); }
	virtual long collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd) override { return 1000; };
	virtual Events receive(const Items& items) override;
	virtual Events send(const Items& items, const Events& events) override;
};

}

#endif
