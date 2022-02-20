#include "generator.h"

Generator::Generator(string _id, GeneratorConfig _config, Logger _logger) : 
	id(_id), config(_config), logger(_logger)
{
}

void Generator::validate(Items& items)
{
	auto& bindings = config.getBindings();

	for (auto& [itemId, item] : items)
		if (item.getOwnerId() == id && !bindings.count(itemId))
			throw std::runtime_error("Item " + itemId + " has no binding for link " + id);

	for (auto& [itemId, binding] : bindings)
	{
		auto& item = items.validate(itemId);
		if (item.getOwnerId() == id)
		{
			item.setReadable(false);
			item.setWritable(false);
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
				events.add(Event(id, itemId, EventType::READ_REQ, Value::newVoid()));
			else if (binding.eventType == EventType::WRITE_REQ && !owner)
				events.add(Event(id, itemId, EventType::WRITE_REQ, Value::newString(binding.value)));
			else if (binding.eventType == EventType::STATE_IND && owner)
				events.add(Event(id, itemId, EventType::STATE_IND, Value::newString(binding.value)));
		}
	}

	return events;
}

Events Generator::send(const Items& items, const Events& events)
{
	return Events();
}
