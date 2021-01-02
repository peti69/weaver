#ifndef STORAGE_H
#define STORAGE_H

#include <ctime>

#include "link.h"
#include "logger.h"

class StorageConfig
{
public:
	struct Binding
	{
		// Item to which the binding applies.
		string itemId;

		// Before the first WRITE_REQ arrives the item has the value given here.
		Value initialValue;

		Binding(string _itemId, Value _initialValue) :
			itemId(_itemId), initialValue(_initialValue) {};
	};
	class Bindings: public std::map<string, Binding>
	{
	public:
		void add(Binding binding) { insert(value_type(binding.itemId, binding)); }
	};

private:
	// Name of file in which the item values are stored.
	string fileName;

	// Item bindings.
	Bindings bindings;

public:
	StorageConfig(string _fileName, Bindings _bindings) : fileName(_fileName), bindings(_bindings) {}
	string getFileName() const { return fileName; }
	const Bindings& getBindings() const { return bindings; }
};

class Storage: public HandlerIf
{
private:
	string id;
	StorageConfig config;
	Logger logger;

	// Has the value file been read?
	bool fileRead;

	// Time when the last attempt was done to read the value file.
	std::time_t lastFileReadTry;

	// Time span between successive attempts to read the item values from the file.
	const int rereadInterval = 60;

public:
	Storage(string _id, StorageConfig _config, Logger _logger);
	virtual void validate(Items& items) const override;
	virtual HandlerState getState() const override { return HandlerState(); }
	virtual long collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd) override { return -1; }
	virtual Events receive(const Items& items) override;
	virtual Events send(const Items& items, const Events& events) override;

private:
	Events receiveX(const Items& items);
};

#endif
