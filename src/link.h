#ifndef LINK_H
#define LINK_H

#include "basic.h"
#include "logger.h"

class Modifier
{
	private:
	
	// Modifier only applies to events for this item.
	string itemId;
	
	// Factor applied to values received over the link. Acts as divisor for values which will be sent over the link.
	float factor;
	
	public:
	Modifier(string _itemId, float _factor) : itemId(_itemId), factor(_factor) {}
	string getItemId() const { return itemId; }
	Value exportValue(const Value& value) const;
	Value importValue(const Value& value) const;
};

class Modifiers: public std::map<string, Modifier> 
{
	public:
	void add(Modifier modifier) { insert(value_type(modifier.getItemId(), modifier)); }
	bool exists(string itemId) const { return find(itemId) != end(); }
};

class Handler
{
	public:
	virtual bool supports(Event::Type eventType) const = 0;
	virtual int getReadDescriptor() = 0;
	virtual int getWriteDescriptor() = 0;
	virtual Events receive(const Items& items) = 0;
	virtual void send(const Items& items, const Events& events) = 0;
};

class Link
{
	private:
	string id;
	Modifiers modifiers;
	std::shared_ptr<Handler> handler;
	Logger logger;

	public:
	Link(string _id, Modifiers _modifiers, std::shared_ptr<Handler> _handler, Logger _logger) : 
		id(_id), modifiers(_modifiers), handler(_handler), logger(_logger) {}
	string getId() const { return id; }
	bool supports(Event::Type eventType) const { return handler->supports(eventType); }
	int getReadDescriptor() const { return handler->getReadDescriptor(); }
	int getWriteDescriptor() const { return handler->getWriteDescriptor(); }
	Events receive(Items& items);
	void send(const Items& items, const Events& events);
};

class Links: public std::map<string, Link>
{
	public:
	void add(Link link) { insert(value_type(link.getId(), link)); }
	bool exists(string linkId) const { return find(linkId) != end(); }
};

#endif