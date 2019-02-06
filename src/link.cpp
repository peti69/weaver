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

Events Link::receive(const Items& items)
{
	Events events = handler->receive(items);
	for (auto& event : events)
	{
		if (event.getType() != Event::READ_REQ)
		{
			// convert event value to item type
			auto itemPos = items.find(event.getItemId());
			if (itemPos != items.end())
			{
				Value newValue = itemPos->second.getType().convert(event.getValue());
				if (newValue.isNull())
					logger.error() << "Unable to convert " << event.getValue().getType().toStr() << " value '" << event.getValue().toStr() 
					               << "' to " << itemPos->second.getType().toStr()  << " value for item " << itemPos->first << endOfMsg();
				else
					event.setValue(newValue);
			}

			// apply modifier
			auto modifierPos = modifiers.find(event.getItemId());
			if (modifierPos != modifiers.end())
				event.setValue(modifierPos->second.importValue(event.getValue()));
		}
		else
			event.setValue(Value());
	}
	return events;
}

void Link::send(const Items& items, const Events& events)
{
	Events newEvents = events;
	for (auto& event : newEvents)
	{
		// apply modifier
		auto modifierPos = modifiers.find(event.getItemId());
		if (modifierPos != modifiers.end())
			event.setValue(modifierPos->second.exportValue(event.getValue()));
	}
	handler->send(items, newEvents);
}
