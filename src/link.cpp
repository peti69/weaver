#include "link.h"

Value Modifier::exportValue(const Value& value) const
{
	if (value.isNumber())
		return Value(value.getNumber() / factor);
	else
		return value;
}

Value Modifier::importValue(const Value& value) const
{
	if (value.isNumber())
		return Value(value.getNumber() * factor);
	else
		return value;
}

bool Link::supports(EventType eventType) const
{
	return handler->supports(eventType);
}

long Link::collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd)
{
	return pendingEvents.size() ? 0 : handler->collectFds(readFds, writeFds, excpFds, maxFd);
}

Events Link::receive(Items& items)
{
	Events events;

	if (pendingEvents.size())
	{
		events = pendingEvents;
		pendingEvents.clear();
	}
	else
		events = handler->receive(items);

	for (auto eventPos = events.begin(); eventPos != events.end();)
	{
		// provide event
		auto& event = *eventPos;

		// provide item
		auto itemPos = items.find(event.getItemId());
		if (itemPos == items.end())
		{
			eventPos++;
			continue;
		}
		auto& item = itemPos->second;

		// provide modifier
		auto modifierPos = modifiers.find(event.getItemId());
		auto modifier = modifierPos != modifiers.end() ? &modifierPos->second : 0;

		// remove READ_REQ and WRITE_REQ in case the link is the owner of the item
		if (event.getType() != EventType::STATE_IND && item.getOwnerId() == id)
		{
			eventPos = events.erase(eventPos);
			continue;
		}

		// remove STATE_IND in case the link is not the owner of the item
		if (event.getType() == EventType::STATE_IND && item.getOwnerId() != id)
		{
			eventPos = events.erase(eventPos);
			continue;
		}

		if (event.getType() != EventType::READ_REQ)
		{
			// convert event value to item type
			Value newValue = item.getType().convert(event.getValue());
			if (newValue.isNull())
				logger.error() << "Unable to convert " << event.getValue().getType().toStr() << " value '" << event.getValue().toStr() 
				               << "' to " << item.getType().toStr()  << " value for item " << item.getId() << endOfMsg();
			else
				event.setValue(newValue);
		}
		else
			event.setValue(Value());

		// convert event value from external to internal representation
		if (event.getType() != EventType::READ_REQ && modifier)
			event.setValue(modifier->importValue(event.getValue()));

		eventPos++;
	}

	return events;
}

void Link::send(Items& items, const Events& events)
{
	Events modifiedEvents = events;

	for (auto eventPos = modifiedEvents.begin(); eventPos != modifiedEvents.end();)
	{
		// provide event
		auto& event = *eventPos;

		// provide item
		auto itemPos = items.find(event.getItemId());
		if (itemPos == items.end())
		{
			eventPos++;
			continue;
		}
		auto& item = itemPos->second;

		// provide modifier
		auto modifierPos = modifiers.find(event.getItemId());
		auto modifier = modifierPos != modifiers.end() ? &modifierPos->second : 0;

		// remove READ_REQ and WRITE_REQ in case the link is not the owner of the item
		if (event.getType() != EventType::STATE_IND && item.getOwnerId() != id)
		{
			eventPos = modifiedEvents.erase(eventPos);
			continue;
		}

		// remove STATE_IND in case the link is the owner of the item
		if (event.getType() == EventType::STATE_IND && item.getOwnerId() == id)
		{
			eventPos = modifiedEvents.erase(eventPos);
			continue;
		}

		// convert event value from internal to external representation
		if (event.getType() != EventType::READ_REQ && modifier)
			event.setValue(modifier->exportValue(event.getValue()));

		eventPos++;
	}

	pendingEvents = handler->send(items, modifiedEvents);
}


