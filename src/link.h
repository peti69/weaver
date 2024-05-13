#ifndef LINK_H
#define LINK_H

#include <sys/select.h>
#include <regex>

#include "basic.h"
#include "logger.h"
#include "item.h"
#include "event.h"

struct Modifier
{
	// Modifier only applies to events for this item.
	ItemId itemId;

	// Unit of values received from the handler or values which will be sent to the
	// handler.
	Unit unit;

	// Factor applied to values received from the handler. Acts as divisor for values
	// which will be sent to the handler.
	Number factor = 1.0;

	// Summand applied to values received from the handler. Acts as subtrahend for values
	// which will be sent to the handler.
	Number summand = 0.0;

	// Indicates if passed values are rounded or.
	bool round = false;

	// Inbound strings are assumed to be SML files transporting a smart meter property
	// which is identified by its hexadecimal OBIS code.
	string inObisCode;

	// JSON pointer which is applied on inbound values to extract normalized values.
	string inJsonPointer;

	// Regular expression which is applied on inbound values to extract normalized values.
	std::regex inPattern;

	// Maps inbound values to normalized values.
	std::map<string, string> inMappings;

	// printf() format to convert normalized values to outbound values.
	string outPattern;

	// Maps normalized values to outbound values.
	std::map<string, string> outMappings;

	Modifier() : factor(1.0), summand(0.0) {}

	void addInMapping(string from, string to) { inMappings[from] = to; }
	void addOutMapping(string from, string to) { outMappings[from] = to; }

	string mapInbound(string value) const;
	string mapOutbound(string value) const;

	Value convertOutbound(const Value& value) const;
	Value convertInbound(const Value& value) const;
};

class Modifiers: public std::map<ItemId, Modifier>
{
public:
	void add(Modifier modifier) { insert(value_type(modifier.itemId, modifier)); }
	bool exists(ItemId itemId) const { return find(itemId) != end(); }
};

// State of an interface to an external system.
struct HandlerState
{
	int errorCounter = 0;
	bool operational = false;
};

// Interface for exchanging events with an external system.
class HandlerIf
{
public:
	virtual ~HandlerIf() {}

	// Enables the handler to validate but also to adapt the definition of the items it owns
	// and itself.
	virtual void validate(Items& items) = 0;

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
	LinkId id;

	// Only in case the link is enabled events are transmitted over the link.
	bool enabled;

	// Discard READ_REQ events to be sent or received ones?
	bool suppressReadEvents;

	// Id of item on which the operational status of the link will be reported.
	ItemId operationalItemId;

	// Id of item on which the number of occurred errors on the link will be reported.
	ItemId errorCounterItemId;

	// A warning message is generated in case event receiving over the link requires
	// more time than defined here in milliseconds.
	int maxReceiveDuration;

	// A warning message is generated in case event sending over the link requires
	// more time than defined here in milliseconds.
	int maxSendDuration;

	// Indicates that number values are transmitted over the link as string values
	// and that an automatic conversion is required.
	bool numberAsString;

	// Indicates that boolean values are transmitted over the link as string values
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

	// Indicates that time point values are transmitted over the link as string values
	// and that an automatic conversion is required.
	bool timePointAsString;

	// In case timePointAsString = true: Conversion format according to
	// std::get_time() and std::put_time().
	string timePointFormat;

	// Indicates that void values are transmitted over the link as string values
	// and that an automatic conversion is required.
	bool voidAsString;

	// In case voidAsString = true: String to be used for a writable item.
	string voidValue;

	// In case voidAsString = true: String to be used for an unwritable item.
	string unwritableVoidValue;

	// Indicates that void values are transmitted over the link as boolean values
	// and that an automatic conversion is required.
	bool voidAsBoolean;

	// Indicates that undefined values are transmitted over the link as string values
	// and that an automatic conversion is required.
	bool undefinedAsString;

	// In case undefinedAsString = true: String to be used.
	string undefinedValue;

	// Discard undefined values to be sent or received ones?
	bool suppressUndefined;

	// Alteration rules for events and their values which are transmitted over the link.
	Modifiers modifiers;

	// Handler embedded into the link.
	std::shared_ptr<HandlerIf> handler;

	// Logger for any kind of logging in the context of the link.
	Logger logger;

	// Last retrieved handler state.
	HandlerState oldHandlerState;

	// Events generated in send() and waiting to be returned by receive().
	Events pendingEvents;

public:
	Link(LinkId id, bool enabled, bool suppressReadEvents,
		ItemId operationalItemId, ItemId errorCounterItemId,
		int maxReceiveDuration, int maxSendDuration,
		bool numberAsString, bool booleanAsString,
		string falseValue, string trueValue,
		string unwritableFalseValue, string unwritableTrueValue,
		bool timePointAsString, string timePointFormat,
		bool voidAsString, string voidValue, string unwritableVoidValue,
		bool voidAsBoolean, bool undefinedAsString, string undefinedValue,
		bool suppressUndefined,
		Modifiers modifiers, std::shared_ptr<HandlerIf> handler, Logger logger);
	LinkId getId() const { return id; }
	bool isEnabled() const { return enabled; }
	void validate(Items& items) const;
	long collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd);
	void send(Items& items, const Events& events);
	Events receive(Items& items);
};

class Links: public std::map<LinkId, Link>
{
public:
	void add(Link link) { insert({link.getId(), link}); }
	bool exists(LinkId id) { return find(id) != end(); }
	const Link& get(LinkId id) const { auto pos = find(id); assert(pos != end()); return pos->second; }
};

#endif
