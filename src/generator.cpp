#include "generator.h"

Generator::Generator(string _id, GeneratorConfig _config, Logger _logger) : 
	id(_id), config(_config), logger(_logger)
{
}

Events Generator::receive(const Items& items)
{
	std::time_t now = std::time(0);

	Events events;

	for (auto bindingPair : config.getBindings())
	{
		auto& binding = bindingPair.second;
		string itemId = binding.itemId;
		bool owner = items.getOwnerId(itemId) == id;

		if (lastGeneration[itemId] + binding.interval <= now)
		{
			lastGeneration[itemId] = now;
			
			if (binding.eventType == EventType::READ_REQ && !owner)
				events.add(Event(id, itemId, EventType::READ_REQ, Value()));
			else if (binding.eventType == EventType::WRITE_REQ && !owner)
				events.add(Event(id, itemId, EventType::WRITE_REQ, Value(binding.value)));
			else if (binding.eventType == EventType::STATE_IND && owner)
				events.add(Event(id, itemId, EventType::STATE_IND, Value(binding.value)));
		}
	}

	return events;
}

Events Generator::send(const Items& items, const Events& events)
{
	auto& bindings = config.getBindings();
	Events newEvents;

	for (auto& event : events)
	{
		string itemId = event.getItemId();

		auto bindingPos = bindings.find(itemId);
		if (bindingPos != bindings.end())
		{
			auto& binding = bindingPos->second;

			if (event.getType() == EventType::READ_REQ)
				newEvents.add(Event(id, itemId, EventType::STATE_IND, Value(binding.value)));
		}
	}

	return newEvents;
}
