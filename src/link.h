#ifndef LINK_H
#define LINK_H

#include "basic.h"
#include "logger.h"

class Modifier
{
private:
	// Modifier only applies to events for this item.
	string itemId;

	// Factor applied to values received from the handler. Acts as divisor for values which will be sent to
	// the handler.
	float factor;

public:
	Modifier(string _itemId) : itemId(_itemId), factor(1.0) {}
	string getItemId() const { return itemId; }
	void setFactor(float _factor) { factor = _factor; }

	Value exportValue(const Value& value) const;
	Value importValue(const Value& value) const;
};

class Modifiers: public std::map<string, Modifier> 
{
public:
	void add(Modifier modifier) { insert(value_type(modifier.getItemId(), modifier)); }
	bool exists(string itemId) const { return find(itemId) != end(); }
};

// Interface for exchanging events with external systems.
class Handler
{
public:
	virtual ~Handler() {}

	// Indicates whether the handler generates (STATE_IND) or accepts (READ_REQ, WRITE_REQ)
	// events of the passed type for items it owns.
	virtual bool supports(EventType eventType) const = 0;

	// Fetches all data from the handler for feeding the select() system call. The return value
	// is the time duration in milliseconds until when the handler has be called at latest.
	virtual long collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd) = 0;

	// When the select() system call returns this method is invoked to receive events.
	virtual Events receive(const Items& items) = 0;

	// Events returned by receive() are passed to all handlers via this method.
	virtual Events send(const Items& items, const Events& events) = 0;
};

class Link
{
private:
	// Id assigned to the link.
	string id;

	// Alterations which will be performed on received events and events which will be sent.
	Modifiers modifiers;

	// Actual interface for the exchange of events with the external systems.
	std::shared_ptr<Handler> handler;

	// Logger for any kind of logging in the context of the link.
	Logger logger;

	// Events generated in send() and waiting to be returned by receive().
	Events pendingEvents;

public:
	Link(string _id, Modifiers _modifiers, std::shared_ptr<Handler> _handler, Logger _logger) : 
		id(_id), modifiers(_modifiers), handler(_handler), logger(_logger) {}
	string getId() const { return id; }
	bool supports(EventType eventType) const;
	long collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd);
	void send(Items& items, const Events& events);
	Events receive(Items& items);
};

class Links: public std::map<string, Link>
{
public:
	void add(Link link) { insert(value_type(link.getId(), link)); }
	bool exists(string id) { return find(id) != end(); }
};

#endif
