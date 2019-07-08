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
		string itemId;
		Binding(string _itemId) : itemId(_itemId) {}
	};
	struct Bindings: public std::map<string, Binding>
	{
		void add(Binding binding) { insert(value_type(binding.itemId, binding)); }
	};

private:
	string fileName;
	Bindings bindings;

public:
	StorageConfig(string _fileName, Bindings _bindings) : fileName(_fileName), bindings(_bindings) {}
	string getFileName() const { return fileName; }
	const Bindings& getBindings() const { return bindings; }
};

class Storage: public Handler
{
private:
	string id;
	StorageConfig config;
	Logger logger;
	bool fileRead;
//	std::map<string, std::time_t> lastFileRead;

public:
	Storage(string _id, StorageConfig _config, Logger _logger);
	virtual bool supports(EventType eventType) const override { return eventType != EventType::READ_REQ; }
	virtual int getReadDescriptor() override { return -1; }
	virtual int getWriteDescriptor() override { return -1; }
	virtual Events receive(const Items& items) override;
	virtual Events send(const Items& items, const Events& events) override;
};

#endif
