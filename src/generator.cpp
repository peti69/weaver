#include "generator.h"

Generator::Generator(string id, GeneratorConfig config, Logger logger) :
	id(id), config(config), logger(logger)
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
		item.validateValueType(binding.value.getType());
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
				events.add(Event(id, itemId, EventType::WRITE_REQ, binding.value));
			else if (binding.eventType == EventType::STATE_IND && owner)
				events.add(Event(id, itemId, EventType::STATE_IND, binding.value));
		}
	}

	return events;
}

Events Generator::send(const Items& items, const Events& events)
{
	return Events();
}
