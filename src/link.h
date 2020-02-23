#ifndef LINK_H
#define LINK_H

#include <sys/select.h>

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

// State of an interface to an external system.
struct HandlerState
{
	int errorCounter;

	HandlerState() : errorCounter(-1) {}
};

// Interface for exchanging events with an external system.
class HandlerIf
{
public:
	virtual ~HandlerIf() {}

	// Indicates whether the handler generates (STATE_IND) or accepts (READ_REQ, WRITE_REQ)
	// events of the passed type for items it owns.
	virtual bool supports(EventType eventType) const = 0;

	// Returns the current state of the handler.
	virtual HandlerState getState() const = 0;

	// Fetches all data from the handler for feeding the select() system call. The return value
	// is the time duration in milliseconds until when the handler has to be called at latest.
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

	// Only in case the link is enabled events are transmitted over the link.
	bool enabled;

	// Id of item on which the number of errors on the link will be reported.
	string errorCounter;

	// Indicates that values for number items are transmitted over the link as strings
	// and that an automatic conversion is required.
	bool numberAsString;

	// Indicates that values for boolean items are transmitted over the link as strings
	// and that an automatic conversion is required.
	bool booleanAsString;

	// In case booleanAsString = true: String to be used for false and a writable item.
	string falseValue;

	// In case booleanAsString = true: String to be used for true and a writable item.
	string trueValue;

	// In case booleanAsString = true: String to be used for false and an unwritable item.
	string unwritableFalseValue;

	// In case booleanAsString = true: String to be used for true and an unwritable item.
	string unwritableTrueValue;

	// Indicates that void items are transmitted over the link as strings
	// and that an automatic conversion is required.
	bool voidAsString;

	// In case voidAsString = true: String to be used for a writable item.
	string voidValue;

	// In case voidAsString = true: String to be used for an  unwritable item.
	string unwritableVoidValue;

	// Alteration rules for events and their values which are transmitted over the link.
	Modifiers modifiers;

	// Handler embedded into the link.
	std::shared_ptr<HandlerIf> handler;

	// Logger for any kind of logging in the context of the link.
	Logger logger;

	// Last retrieved handler state
	HandlerState oldHandlerState;

	// Events generated in send() and waiting to be returned by receive().
	Events pendingEvents;

public:
	Link(string id, bool enabled, string errorCounter, bool numberAsString,
		bool booleanAsString, string falseValue, string trueValue,
		string unwritableFalseValue, string unwritableTrueValue,
		bool voidAsString, string voidValue, string unwritableVoidValue,
		Modifiers modifiers, std::shared_ptr<HandlerIf> handler, Logger logger) :
		id(id), enabled(enabled), errorCounter(errorCounter), numberAsString(numberAsString),
		booleanAsString(booleanAsString), falseValue(falseValue), trueValue(trueValue),
		unwritableFalseValue(unwritableFalseValue), unwritableTrueValue(unwritableTrueValue),
		voidAsString(voidAsString), voidValue(voidValue), unwritableVoidValue(unwritableVoidValue),
		modifiers(modifiers), handler(handler), logger(logger) {}
	string getId() const { return id; }
	bool isEnabled() const { return enabled; }
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
