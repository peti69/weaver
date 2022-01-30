#define RAPIDJSON_HAS_STDSTRING 1

#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/error/en.h>
#include "rapidjson/filewritestream.h"
#include <rapidjson/prettywriter.h>

#include <utility>
#include <set>

#include "storage.h"
#include "finally.h"

using namespace std::rel_ops;

Storage::Storage(string id, StorageConfig config, Logger logger) :
	id(id), config(config), logger(logger), fileRead(false), lastFileReadTry(0)
{
}

void Storage::validate(Items& items) const
{
	auto& bindings = config.getBindings();

	for (auto& [itemId, item] : items)
		if (item.getOwnerId() == id && !bindings.count(itemId))
			throw std::runtime_error("Item " + itemId + " has no binding for link " + id);

	for (auto& [itemId, binding] : bindings)
	{
		auto& item = items.validate(itemId);
		item.validateOwnerId(id);
		item.setReadable(false);
		item.setWritable(true);
		item.setResponsive(true);
		item.validateType(binding.initialValue.getType());
	}
}

Events Storage::receiveX(const Items& items)
{
	Events newEvents;

	// if not yet done restore persisted owned items
	if (!fileRead)
	{
		// shell we perform another attempt to read the file?
		std::time_t now = std::time(0);
		if (lastFileReadTry + rereadInterval > now)
			return newEvents;
		lastFileReadTry = now;

		// open file
		FILE* file = fopen(config.getFileName().c_str(), "r");
		if (!file)
			logger.errorX() << "Can not open file " << config.getFileName() << " for reading" << endOfMsg();
		auto autoClose = finally([file] { fclose(file); });

		// read file and translate it to a DOM tree
		char buffer[4096];
		rapidjson::FileReadStream stream(file, buffer, sizeof(buffer));
		rapidjson::Document document;
		rapidjson::ParseResult result = document.ParseStream<rapidjson::kParseCommentsFlag|rapidjson::kParseTrailingCommasFlag>(stream);
		if (result.IsError())
			logger.errorX() << "JSON parse error '" << rapidjson::GetParseError_En(result.Code()) << "' at offset "
							<< result.Offset() << " in file " << config.getFileName() << endOfMsg();
		if (!document.IsObject())
			logger.errorX() << "JSON document from file " << config.getFileName() << " is not an object" << endOfMsg();

		// analyze DOM tree
		std::set<string> itemsInFile;
		for (auto iter = document.MemberBegin(); iter != document.MemberEnd(); iter++)
		{
			string itemId = iter->name.GetString();
			itemsInFile.insert(itemId);

			// verify item identifier
			auto itemPos = items.find(itemId);
			if (itemPos == items.end())
				logger.errorX() << "Item " << itemId << " is unknown" << endOfMsg();

			// verify that the item is owned
			if (itemPos->second.getOwnerId() != id)
				logger.errorX() << "Item " << itemId << " is not owned by the link" << endOfMsg();

			// determine item value
			Value value;
			if (iter->value.IsString())
				value = Value::newString(iter->value.GetString());
			else if (iter->value.IsBool())
				value = Value::newBoolean(iter->value.GetBool());
			else if (iter->value.IsNumber())
				value = Value::newNumber(iter->value.GetDouble());
			else if (iter->value.IsNull())
				value = Value::newUndefined();
			else
				logger.errorX() << "Value for item " << itemId << " is not supported" << endOfMsg();

			// generate STATE_IND for item
			newEvents.add(Event(id, itemId, EventType::STATE_IND, value));
		}

		// generate STATE_IND for all items not found in file
		for (auto& bindingPair : config.getBindings())
		{
			auto& binding = bindingPair.second;
			if (itemsInFile.find(binding.itemId) == itemsInFile.end())
				newEvents.add(Event(id, binding.itemId, EventType::STATE_IND, binding.initialValue));
		}

		fileRead = true;
	}

	return newEvents;
}

Events Storage::receive(const Items& items)
{
	try
	{
		return receiveX(items);
	}
	catch (const std::exception& ex)
	{
		logger.error() << ex.what() << endOfMsg();
	}

	return Events();
}

Events Storage::send(const Items& items, const Events& events)
{
	if (!fileRead)
		return Events();

	// analyze WRITE_REQ and determine changed values
	std::map<string, Value> newValues;
	for (auto& event : events)
		if (event.getType() == EventType::WRITE_REQ)
		{
			auto itemPos = items.find(event.getItemId());
			if (itemPos != items.end() && itemPos->second.getLastSendValue() != event.getValue())
				newValues[event.getItemId()] = event.getValue();
		}

	// if a value changed persist all owned items
	if (newValues.size())
	{
		// translate owned items to a DOM tree
		rapidjson::Document document;
		auto& allocator = document.GetAllocator();
		document.SetObject();
		for (auto& [itemId, item] : items)
		{
			auto newValuePos = newValues.find(itemId);
			const Value& value = newValuePos != newValues.end() ? newValuePos->second : item.getLastSendValue();
			if (item.getOwnerId() == id)
			{
				rapidjson::Value jsonValue;
				if (value.getType() == ValueType::STRING)
					jsonValue.SetString(value.getString(), allocator);
				else if (value.getType() == ValueType::BOOLEAN)
					jsonValue.SetBool(value.getBoolean());
				else if (value.getType() == ValueType::NUMBER)
					jsonValue.SetDouble(value.getNumber());
				else if (value.getType() == ValueType::UNDEFINED)
					jsonValue.SetNull();
				rapidjson::Value memberName(itemId, allocator);
				document.AddMember(memberName, jsonValue, allocator);
			}
		}

		// create file
		FILE* file = fopen(config.getFileName().c_str(), "w");
		if (!file)
		{
			logger.error() << "Can not open file " << config.getFileName() << " for writing" << endOfMsg();
			return Events();
		}
		auto autoClose = finally([file] { fclose(file); });

		// write DOM tree to file
		char buffer[4096];
		rapidjson::FileWriteStream stream(file, buffer, sizeof(buffer));
		rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(stream);
		document.Accept(writer);
	}

	// generate for every changed value a corresponding STATE_IND
	Events newEvents;
	for (auto& newValuePair : newValues)
		newEvents.add(Event(id, newValuePair.first, EventType::STATE_IND, newValuePair.second));

	return newEvents;
}
