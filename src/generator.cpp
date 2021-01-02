#include "generator.h"

Generator::Generator(string _id, GeneratorConfig _config, Logger _logger) : 
	id(_id), config(_config), logger(_logger)
{
}

void Generator::validate(Items& items) const
{
	auto& bindings = config.getBindings();

	for (auto& itemPair : items)
		if (itemPair.second.getOwnerId() == id && bindings.find(itemPair.first) == bindings.end())
			throw std::runtime_error("Item " + itemPair.first + " has no binding for link " + id);

	for (auto& bindingPair : bindings)
	{
		Item& item = items.validate(bindingPair.first);
		if (item.getOwnerId() == id)
		{
			item.validateReadable(false);
			item.validateWritable(false);
		}
	}
}

Events Generator::receive(const Items& items)
{
	std::time_t now = std::time(0);

	Events events;

	for (auto& bindingPair : config.getBindings())
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
	return Events();
}
