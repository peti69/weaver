#ifndef STORAGE_H
#define STORAGE_H

#include <ctime>

#include "link.h"
#include "logger.h"

class StorageConfig
{
private:
	// Name of file in which the item values are stored.
	string fileName;

public:
	StorageConfig(string _fileName) : fileName(_fileName) {}
	string getFileName() const { return fileName; }
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

public:
	Storage(string _id, StorageConfig _config, Logger _logger);
	virtual bool supports(EventType eventType) const override { return eventType != EventType::READ_REQ; }
	virtual long collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd) override { return -1; }
	virtual Events receive(const Items& items) override;
	virtual Events send(const Items& items, const Events& events) override;

private:
	Events receiveX(const Items& items);
};

#endif
