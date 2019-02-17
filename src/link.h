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

	// Shall an unsolicited STATE_IND event be suppressed in case the associated new item value is equal or almost equal to the
	// value seen beforehand?
	bool suppressDups;
	
	// (current value) * (100 - (variation percentage)) <= new value <= (current value) * (100 + (variation percentage)) => ignore event
	float relVariation;
	
	// New item values are suppressed in case they are inside the interval defined by this delta value.
	// (current value) - (variation value) <= new value <= (current value) + (variation value) => ignore event
	float absVariation;

	// New item values are suppressed if they are greater than or equeal to this one.
	float minimum;
	
	// New item values are suppressed if they are smaller than or equeal to this one.
	float maximum;
	
	// New item values are suppressed in case they are inside the interval defined by this delta percentage.
public:
	Modifier(string _itemId);
	string getItemId() const { return itemId; }
	void setFactor(float _factor) { factor = _factor; }
	void setSuppressDups(bool _suppressDups) { suppressDups = _suppressDups; }
	void setRelVariation(float _relVariation) { relVariation = _relVariation; }
	void setAbsVariation(float _absVariation) { absVariation = _absVariation; }
	void setMinimum(float _minimum) { minimum = _minimum; }
	void setMaximum(float _maximum) { maximum = _maximum; }

	Value exportValue(const Value& value) const;
	Value importValue(const Value& value) const;
	bool suppressValue(const Value& oldValue, const Value& newValue) const;
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
	virtual bool supports(EventType eventType) const = 0;
	virtual int getReadDescriptor() = 0;
	virtual int getWriteDescriptor() = 0;
	virtual Events receive(const Items& items) = 0;
	virtual Events send(const Items& items, const Events& events) = 0;
};

class Link
{
private:
	// id assigned to the link.
	string id;
	
	// Alterations which will be performed on received events and events which will be sent.
	Modifiers modifiers;

	// Actual interface for the exchange of events with the outside world. 
	std::shared_ptr<Handler> handler;
	
	// Logger for any kind of logging in the context of the link.
	Logger logger;
	
	// Events generated in send() and waiting to be returned by receive().
	Events pendingEvents;

public:
	Link(string _id, Modifiers _modifiers, std::shared_ptr<Handler> _handler, Logger _logger) : 
		id(_id), modifiers(_modifiers), handler(_handler), logger(_logger) {}
	string getId() const { return id; }
	int getReadDescriptor() const { return handler->getReadDescriptor(); }
	int getWriteDescriptor() const { return handler->getWriteDescriptor(); }
	long getTimeout() const { return pendingEvents.size() ? 0 : 1000; }
	Events receive(Items& items);
	void send(const Items& items, const Events& events);
};

class Links: public std::map<string, Link>
{
public:
	void add(Link link) { insert(value_type(link.getId(), link)); }
	bool exists(string id) { return find(id) != end(); }
};

#endif