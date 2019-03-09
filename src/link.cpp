#include <limits>

#include "link.h"

Modifier::Modifier(string _itemId) : 
	itemId(_itemId), factor(1.0), suppressDups(false), relVariation(0.0), absVariation(0.0),
	minimum(std::numeric_limits<float>::lowest()), maximum(std::numeric_limits<float>::max())	 
{
}

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

bool Modifier::suppressValue(const Value& oldValue, const Value& newValue) const
{
	if (!suppressDups)
		return false;

	if (oldValue == newValue)
		return true;

	if (newValue.isNumber() && oldValue.isNumber())
	{
		double oldNum = oldValue.getNumber();
		double newNum = newValue.getNumber();
		if (  newNum >= minimum && newNum <= maximum
		   && newNum >= oldNum * (1.0 - relVariation / 100.0) - absVariation 
		   && newNum <= oldNum * (1.0 + relVariation / 100.0) + absVariation
		   )
			return true;
	}

	return false;
}

Events Link::receive(Items& items)
{
	Events events = pendingEvents;
	pendingEvents.clear();
	events.splice(events.begin(), handler->receive(items));

	for (auto eventPos = events.begin(); eventPos != events.end();)
	{	
		auto& event = *eventPos;

		if (event.getType() != EventType::READ_REQ)
		{
			// provide item
			auto itemPos = items.find(event.getItemId());
			if (itemPos == items.end())
			{
				eventPos++;
				continue;
			}
			auto& item = itemPos->second;

			// convert event value to item type
			Value newValue = item.getType().convert(event.getValue());
			if (newValue.isNull())
				logger.error() << "Unable to convert " << event.getValue().getType().toStr() << " value '" << event.getValue().toStr() 
				               << "' to " << item.getType().toStr()  << " value for item " << item.getId() << endOfMsg();
			else
				event.setValue(newValue);

			// apply modifier
			auto modifierPos = modifiers.find(item.getId());
			if (modifierPos != modifiers.end())
			{
				// convert the value from external to internal representation
				event.setValue(modifierPos->second.importValue(event.getValue()));

				// suppress STATE_IND in case the item value did not change (much) and the handler 
				// only supports STATE_IND
				if (  event.getType() == EventType::STATE_IND 
				   && !handler->supports(EventType::READ_REQ)
				   && !handler->supports(EventType::WRITE_REQ)
				   && modifierPos->second.suppressValue(item.getValue(), event.getValue())
				   )
				{
					// old and new value are equal or equal within tolerances
					eventPos = events.erase(eventPos);
					continue;
				}
			}

			// store item value
			if (event.getType() == EventType::STATE_IND)
				item.setValue(event.getValue());
		}
		else
			event.setValue(Value());

		eventPos++;
	}

	return events;
}

void Link::send(const Items& items, const Events& events)
{
	Events modifiedEvents = events;
	for (auto& event : modifiedEvents)
		if (event.getType() != EventType::READ_REQ)
		{
			// apply modifier
			auto modifierPos = modifiers.find(event.getItemId());
			if (modifierPos != modifiers.end())
				// convert the value from internal to external representation
				event.setValue(modifierPos->second.exportValue(event.getValue()));
		}
		else
		{
			// provide item
			auto itemPos = items.find(event.getItemId());
			if (itemPos == items.end())
				continue;
			auto& item = itemPos->second;

			// generate STATE_IND in case the handler does not support READ_REQ
			if (item.getOwnerId() == id && !handler->supports(EventType::READ_REQ))
			{
				// make use of item value
				const Value& value = itemPos->second.getValue();
				if (!value.isNull())
					pendingEvents.add(Event("auto", event.getItemId(), EventType::STATE_IND, value));
			}
		}
	
	pendingEvents.splice(pendingEvents.begin(), handler->send(items, modifiedEvents));
}
