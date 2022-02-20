#ifndef STORAGE_H
#define STORAGE_H

#include "link.h"
#include "logger.h"

namespace storage
{

struct Binding
{
	// Item to which the binding applies.
	ItemId itemId;

	// Before the first WRITE_REQ arrives the item has the value given here.
	Value initialValue;

	// Indicates whether item value is persisted or not.
	bool persistent;

	Binding(ItemId itemId, Value initialValue, bool persistent) :
		itemId(itemId), initialValue(initialValue), persistent(persistent) {};
};

class Bindings: public std::map<ItemId, Binding>
{
public:
	void add(Binding binding) { insert(value_type(binding.itemId, binding)); }
};

class Config
{
private:
	// Name of file in which the item values are stored.
	string fileName;

	// Item bindings.
	Bindings bindings;

public:
	Config(string fileName, Bindings bindings) : fileName(fileName), bindings(bindings) {}
	string getFileName() const { return fileName; }
	const Bindings& getBindings() const { return bindings; }
};

class Handler: public HandlerIf
{
private:
	LinkId id;
	Config config;
	Logger logger;
	Bindings bindings;

	// Has the value file been read?
	bool fileRead = false;

	// Time when the last attempt was done to read the value file.
	TimePoint lastFileReadTry = TimePoint::min();

	// Time span between successive attempts to read the item values from the file.
	const Seconds rereadInterval = 60s;

public:
	Handler(LinkId id, Config config, Logger logger);
	virtual void validate(Items& items) override;
	virtual HandlerState getState() const override { return HandlerState(); }
	virtual long collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd) override { return -1; }
	virtual Events receive(const Items& items) override;
	virtual Events send(const Items& items, const Events& events) override;

private:
	Events receiveX(const Items& items);
};

}

#endif
