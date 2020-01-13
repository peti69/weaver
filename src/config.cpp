#include <stdexcept>

#include <rapidjson/filereadstream.h>
#include <rapidjson/error/en.h>

#include "config.h"
#include "knx.h"
#include "mqtt.h"
#include "port.h"
#include "generator.h"
#include "tr064.h"
#include "http.h"
#include "tcp.h"
#include "storage.h"
#include "finally.h"

int Config::hasMember(const Value& value, string name) const
{
	return value.HasMember(name.c_str()); 
}

string Config::getString(const Value& value, string name) const
{
	Iterator stringIter = value.FindMember(name.c_str());
	if (stringIter == value.MemberEnd())
		throw std::runtime_error("Field " + name + " not found");
	if (!stringIter->value.IsString())
		throw std::runtime_error("Field " + name + " is not a string");
	return stringIter->value.GetString();
}

string Config::getString(const Value& value, string name, string defaultValue) const
{
	Iterator stringIter = value.FindMember(name.c_str());
	if (stringIter == value.MemberEnd())
		return defaultValue;
	if (!stringIter->value.IsString())
		throw std::runtime_error("Field " + name + " is not a string");
	return stringIter->value.GetString();
}

int Config::getInt(const Value& value, string name) const
{
	Iterator intIter = value.FindMember(name.c_str());
	if (intIter == value.MemberEnd())
		throw std::runtime_error("Field " + name + " not found");
	if (!intIter->value.IsInt())
		throw std::runtime_error("Field " + name + " is not an integer");
	return intIter->value.GetInt();
}

int Config::getInt(const Value& value, string name, int defaultValue) const
{
	Iterator intIter = value.FindMember(name.c_str());
	if (intIter == value.MemberEnd())
		return defaultValue;
	if (!intIter->value.IsInt())
		throw std::runtime_error("Field " + name + " is not an integer");
	return intIter->value.GetInt();
}

float Config::getFloat(const Value& value, string name) const
{
	Iterator iter = value.FindMember(name.c_str());
	if (iter == value.MemberEnd())
		throw std::runtime_error("Field " + name + " not found");
	if (!iter->value.IsNumber())
		throw std::runtime_error("Field " + name + " is not a floating point number");
	return iter->value.GetFloat();
}

float Config::getFloat(const Value& value, string name, float defaultValue) const
{
	Iterator iter = value.FindMember(name.c_str());
	if (iter == value.MemberEnd())
		return defaultValue;
	if (!iter->value.IsNumber())
		throw std::runtime_error("Field " + name + " is not a floating point number");
	return iter->value.GetFloat();
}

bool Config::getBool(const Value& value, string name) const
{
	Iterator boolIter = value.FindMember(name.c_str());
	if (boolIter == value.MemberEnd())
		throw std::runtime_error("Field " + name + " not found");
	if (!boolIter->value.IsBool())
		throw std::runtime_error("Field " + name + " is not a boolean");
	return boolIter->value.GetBool();
}

bool Config::getBool(const Value& value, string name, bool defaultValue) const
{
	Iterator boolIter = value.FindMember(name.c_str());
	if (boolIter == value.MemberEnd())
		return defaultValue;
	if (!boolIter->value.IsBool())
		throw std::runtime_error("Field " + name + " is not a boolean");
	return boolIter->value.GetBool();
}

const Config::Value& Config::getObject(const Value& value, string name) const
{
	Iterator iter = value.FindMember(name.c_str());
	if (iter == value.MemberEnd())
		throw std::runtime_error("Field " + name + " not found");
	if (!iter->value.IsObject())
		throw std::runtime_error("Field " + name + " is not an object");
	return iter->value;
}

const Config::Value& Config::getArray(const Value& value, string name) const
{
	Iterator iter = value.FindMember(name.c_str());
	if (iter == value.MemberEnd())
		throw std::runtime_error("Field " + name + " not found");
	if (!iter->value.IsArray())
		throw std::runtime_error("Field " + name + " is not an array");
	return iter->value;
}

std::regex Config::convertPattern(string fieldName, string pattern) const
{
	try
	{
		return std::regex(pattern, std::regex_constants::extended);
	}
	catch (const std::regex_error& ex)
	{
		cout << std::regex_constants::error_paren << endl;
		std::ostringstream stream;
		stream << "Invalid value " << pattern << " for field " << fieldName << " in configuration (error code = " << ex.code() << ", error string = " << ex.what() << ")";
		throw std::runtime_error(stream.str());
	}
}

void Config::read(string fileName)
{
	FILE* file = fopen(fileName.c_str(), "r");
	if (!file)
		throw std::runtime_error("Can not open file " + fileName);
	auto autoClose = finally([file] { fclose(file); });

	char buffer[4096];
	rapidjson::FileReadStream stream(file, buffer, sizeof(buffer));
	rapidjson::ParseResult result = document.ParseStream<rapidjson::kParseCommentsFlag|rapidjson::kParseTrailingCommasFlag>(stream);
	if (result.IsError())
	{
		std::ostringstream stream;
		stream << "Parse error '" << rapidjson::GetParseError_En(result.Code()) << "' at offset " << result.Offset() << " in file " << fileName;
		throw std::runtime_error(stream.str());
	}
}

GlobalConfig Config::getGlobalConfig() const
{
	bool logEvents = getBool(document, "logEvents", false);
	bool logSuppressedEvents = getBool(document, "logSuppressedEvents", false);
	bool logGeneratedEvents = getBool(document, "logGeneratedEvents", false);

	return GlobalConfig(logEvents, logSuppressedEvents, logGeneratedEvents);
}

LogConfig Config::getLogConfig() const
{
	string fileName = getString(document, "logFileName", "");
	int maxFileSize = getInt(document, "maxLogFileSize", 0);
	int maxFileCount = getInt(document, "maxLogFileCount", 0);
	
	return LogConfig(fileName, maxFileSize, maxFileCount);
}

Items Config::getItems() const
{
	const Value& itemsValue = getArray(document, "items"); 
	Items items;

	for (auto& itemValue : itemsValue.GetArray())
	{
		string itemId = getString(itemValue, "id"); 
		if (items.find(itemId) != items.end())
			throw std::runtime_error("Item " + itemId + " defined twice in configuration");

		string typeStr = getString(itemValue, "type");
		ValueType type;
		if (!ValueType::fromStr(typeStr, type))
			throw std::runtime_error("Invalid value " + typeStr + " for field type in configuration");

		string ownerId = getString(itemValue, "ownerId");

		Item item(itemId, type, ownerId);

		item.setReadable(getBool(itemValue, "readable", true));
		item.setWritable(getBool(itemValue, "writable", true));

		item.setPollInterval(getInt(itemValue, "pollInterval", 0));

		if (hasMember(itemValue, "sendOnTimer"))
		{
			auto& sendOnTimerValue = getObject(itemValue, "sendOnTimer");
			item.setSendOnTimer(true);
			item.setDuration(getInt(sendOnTimerValue, "duration", 300));
		}

		if (hasMember(itemValue, "sendOnChange"))
		{
			auto& sendOnChangeValue = getObject(itemValue, "sendOnChange");
			item.setSendOnChange(true);
			item.setAbsVariation(getFloat(sendOnChangeValue, "absVariation", 0.0));
			item.setRelVariation(getFloat(sendOnChangeValue, "relVariation", 0.0));
			item.setMinimum(getFloat(sendOnChangeValue, "minimum", std::numeric_limits<float>::lowest()));
			item.setMaximum(getFloat(sendOnChangeValue, "maximum", std::numeric_limits<float>::max()));
		}

		items.add(item);
	}

	return items;
}

Links Config::getLinks(const Items& items, Log& log) const
{
	auto& linksValue = getArray(document, "links"); 
	Links links;

	for (auto& linkValue : linksValue.GetArray())
	{
		string id = getString(linkValue, "id");

		bool numberAsString = hasMember(linkValue, "numberAsString");

		bool booleanAsString = hasMember(linkValue, "booleanAsString");
		string falseValue;
		string trueValue;
		string unwritableFalseValue;
		string unwritableTrueValue;
		if (booleanAsString)
		{
			auto& booleanAsStringValue = getObject(linkValue, "booleanAsString");
			falseValue = getString(booleanAsStringValue, "falseValue", "false");
			trueValue = getString(booleanAsStringValue, "trueValue", "true");
			unwritableFalseValue = getString(booleanAsStringValue, "unwritableFalseValue", falseValue);
			unwritableTrueValue = getString(booleanAsStringValue, "unwritableTrueValue", trueValue);
		}

		bool voidAsString = hasMember(linkValue, "voidAsString");
		string voidValue;
		if (voidAsString)
		{
			auto& voidAsStringValue = getObject(linkValue, "voidAsString");
			voidValue = getString(voidAsStringValue, "value", "");
		}

		Modifiers modifiers;
		if (hasMember(linkValue, "modifiers"))
		{
			auto& modifiersValue = getArray(linkValue, "modifiers");
			for (auto& modifierValue : modifiersValue.GetArray())
			{
				string itemId = getString(modifierValue, "itemId");
				if (!items.exists(itemId))
					throw std::runtime_error("Invalid value " + itemId + " for field itemId in configuration");

				Modifier modifier(itemId);
				modifier.setFactor(getFloat(modifierValue, "factor", 1.0));
				modifiers.add(modifier);
			}
		}

		Logger logger = log.newLogger(id);
		std::shared_ptr<HandlerIf> handler;
		if (hasMember(linkValue, "knx"))
			handler.reset(new KnxHandler(id, *getKnxConfig(getObject(linkValue, "knx"), items), logger));
		else if (hasMember(linkValue, "mqtt"))
			handler.reset(new Mqtt::Handler(id, *getMqttConfig(getObject(linkValue, "mqtt"), items), logger));
		else if (hasMember(linkValue, "port"))
			handler.reset(new PortHandler(id, *getPortConfig(getObject(linkValue, "port"), items), logger));
		else if (hasMember(linkValue, "http"))
			handler.reset(new HttpHandler(id, *getHttpConfig(getObject(linkValue, "http"), items), logger));
		else if (hasMember(linkValue, "tcp"))
			handler.reset(new TcpHandler(id, *getTcpConfig(getObject(linkValue, "tcp"), items), logger));
		else if (hasMember(linkValue, "generator"))
			handler.reset(new Generator(id, *getGeneratorConfig(getObject(linkValue, "generator"), items), logger));
		else if (hasMember(linkValue, "tr064"))
			handler.reset(new Tr064(id, *getTr064Config(getObject(linkValue, "tr064"), items), logger));
		else if (hasMember(linkValue, "storage"))
			handler.reset(new Storage(id, *getStorageConfig(getObject(linkValue, "storage"), items), logger));
		else
			throw std::runtime_error("Link with unknown or missing type in configuration");

		links.add(Link(id, numberAsString, booleanAsString, falseValue, trueValue, unwritableFalseValue,
				unwritableTrueValue, voidAsString, voidValue, modifiers, handler, logger));
	}

	for (auto itemPair : items)
	{
		auto& item = itemPair.second;

		auto linkPair = links.find(item.getOwnerId());
		if (linkPair == links.end())
			throw std::runtime_error("Item " + item.getId() + " associated with unknown link " + item.getOwnerId());
		auto& link = linkPair->second;

		if (!link.supports(EventType::READ_REQ))
			item.setReadable(false);
		if (!link.supports(EventType::WRITE_REQ))
			item.setWritable(false);
	}

	return links;
}

std::shared_ptr<Mqtt::Config> Config::getMqttConfig(const Value& value, const Items& items) const
{
	string clientIdPrefix = getString(value, "clientIdPrefix", "weaver");
	string hostname = getString(value, "hostname");
	int port = getInt(value, "port", 1883);
	int reconnectInterval = getInt(value, "reconnectInterval", 60);
	bool retainFlag = getBool(value, "retainFlag", false);
	
	bool logMsgs = getBool(value, "logMessages", false);

	Mqtt::Config::Topics subTopics;
	if (hasMember(value, "subTopics"))
		for (auto& topicValue : getArray(value, "subTopics").GetArray())
		{
			if (!topicValue.IsString())
				throw std::runtime_error("Field subTopics is not a string array");
			subTopics.insert(topicValue.GetString());
		}

	auto getTopicPattern = [this, &value] (string fieldName)
	{
		if (!hasMember(value, fieldName))
			return Mqtt::TopicPattern();
		string topicPatternStr = getString(value, fieldName);
		auto topicPattern = Mqtt::TopicPattern::fromStr(topicPatternStr);
		if (topicPattern.isNull())
			throw std::runtime_error("Invalid value " + topicPatternStr + " for field " + fieldName + " in configuration");
		return topicPattern;
	};
	Mqtt::TopicPattern stateTopicPattern = getTopicPattern("stateTopicPattern");
	Mqtt::TopicPattern writeTopicPattern = getTopicPattern("writeTopicPattern");
	Mqtt::TopicPattern readTopicPattern = getTopicPattern("readTopicPattern");

	Mqtt::Config::Bindings bindings;
	if (hasMember(value, "bindings"))
	{
		const Value& bindingsValue = getArray(value, "bindings");
		for (auto& bindingValue : bindingsValue.GetArray())
		{
			string itemId = getString(bindingValue, "itemId");
			if (!items.exists(itemId))
				throw std::runtime_error("Invalid value " + itemId + " for field itemId in configuration");

			Mqtt::Config::Topics stateTopics;
			if (hasMember(bindingValue, "stateTopic"))
				stateTopics.insert(getString(bindingValue, "stateTopic"));
			if (hasMember(bindingValue, "stateTopics"))
				for (auto& topicValue : getArray(bindingValue, "stateTopics").GetArray())
				{
					if (!topicValue.IsString())
						throw std::runtime_error("Field stateTopics is not a string array");
					stateTopics.insert(topicValue.GetString());
				}
			string writeTopic = getString(bindingValue, "writeTopic", "");
			string readTopic = getString(bindingValue, "readTopic", "");

			bindings.add(Mqtt::Config::Binding(itemId, stateTopics, writeTopic, readTopic));
		}
	}

	return std::make_shared<Mqtt::Config>(clientIdPrefix, hostname, port, reconnectInterval, retainFlag, logMsgs, subTopics, stateTopicPattern, writeTopicPattern, readTopicPattern, bindings);
}

std::shared_ptr<KnxConfig> Config::getKnxConfig(const Value& value, const Items& items) const
{
	string localIpAddrStr = getString(value, "localIpAddr");
	IpAddr localIpAddr;
	if (!IpAddr::fromStr(localIpAddrStr, localIpAddr))
		throw std::runtime_error("Invalid value " + localIpAddrStr + " for field localIpAddr in configuration");

	bool natMode = getBool(value, "natMode", false);

	string ipAddrStr = getString(value, "ipAddr");
	IpAddr ipAddr;
	if (!IpAddr::fromStr(ipAddrStr, ipAddr))
		throw std::runtime_error("Invalid value " + ipAddrStr + " for field ipAddr in configuration");
	IpPort ipPort = getInt(value, "ipPort", 3671);

	Seconds reconnectInterval(getInt(value, "reconnectInterval", 60));
	Seconds connStateReqInterval(getInt(value, "connStateReqInterval", 60));
	Seconds controlRespTimeout(getInt(value, "controlRespTimeout", 10));
	Seconds tunnelAckTimeout(getInt(value, "tunnelAckTimeout", 1));
	Seconds ldataConTimeout(getInt(value, "ldataConTimeout", 3));

	string physicalAddrStr = getString(value, "physicalAddr", "0.0.0");
	PhysicalAddr physicalAddr;
	if (!PhysicalAddr::fromStr(physicalAddrStr, physicalAddr))
		throw std::runtime_error("Invalid value " + physicalAddrStr + " for field physicalAddr in configuration");

	bool logRawMsg = getBool(value, "logRawMessages", false);
	bool logData = getBool(value, "logData", false);
	
	const Value& bindingsValue = getArray(value, "bindings");
	KnxConfig::Bindings bindings;
	for (auto& bindingValue : bindingsValue.GetArray())
	{
		string itemId = getString(bindingValue, "itemId");
		if (!items.exists(itemId))
			throw std::runtime_error("Invalid value " + itemId + " for field itemId in configuration");

		string stateGaStr = getString(bindingValue, "stateGa", "");
		GroupAddr stateGa;
		if (stateGaStr != "" && !GroupAddr::fromStr(stateGaStr, stateGa))
			throw std::runtime_error("Invalid value " + stateGaStr + " for field stateGa in configuration");

		string writeGaStr = getString(bindingValue, "writeGa", "");
		GroupAddr writeGa;
		if (writeGaStr != "" && !GroupAddr::fromStr(writeGaStr, writeGa))
			throw std::runtime_error("Invalid value " + writeGaStr + " for field writeGa in configuration");

//		if (writeGa == stateGa)
//			throw std::runtime_error("Values for fields stateGa and writeGa are equal in configuration");

		string dptStr = getString(bindingValue, "dpt");
		DatapointType dpt;
		if (!DatapointType::fromStr(dptStr, dpt))
			throw std::runtime_error("Invalid value " + dptStr + " for field dpt in configuration");

		bindings.add(KnxConfig::Binding(itemId, stateGa, writeGa, dpt));
	}

	return std::make_shared<KnxConfig>(localIpAddr, natMode, ipAddr, ipPort, reconnectInterval, connStateReqInterval, controlRespTimeout, tunnelAckTimeout, ldataConTimeout, physicalAddr, logRawMsg, logData, bindings);
}

std::shared_ptr<PortConfig> Config::getPortConfig(const Value& value, const Items& items) const 
{ 
	string name = getString(value, "name");

	std::regex msgPattern = convertPattern("msgPattern", getString(value, "msgPattern"));

	bool logRawData = getBool(value, "logRawData", false);
	bool logRawDataInHex = getBool(value, "logRawDataInHex", false);

	int baudRate = getInt(value, "baudRate");
	if (!PortConfig::isValidBaudRate(baudRate))
		throw std::runtime_error("Invalid value for field baudRate in configuration");

	int dataBits = getInt(value, "dataBits");
	if (!PortConfig::isValidDataBits(dataBits))
		throw std::runtime_error("Invalid value for field dataBits in configuration");

	int stopBits = getInt(value, "stopBits");
	if (!PortConfig::isValidStopBits(stopBits))
		throw std::runtime_error("Invalid value for field stopBits in configuration");

	string parityStr = getString(value, "parity");
	PortConfig::Parity parity;
	if (!PortConfig::isValidParity(parityStr, parity))
		throw std::runtime_error("Invalid value " + parityStr + " for field parity in configuration");

	int timeoutInterval = getInt(value, "timeoutInterval", 60);

	int reopenInterval = getInt(value, "reopenInterval", 60);

	const Value& bindingsValue = getArray(value, "bindings");
	PortConfig::Bindings bindings;
	for (auto& bindingValue : bindingsValue.GetArray())
	{
		string itemId = getString(bindingValue, "itemId");
		if (!items.exists(itemId))
			throw std::runtime_error("Invalid value " + itemId + " for field itemId in configuration");

		std::regex pattern = convertPattern("pattern", getString(bindingValue, "pattern"));

		bindings.add(PortConfig::Binding(itemId, pattern));
	}

	return std::make_shared<PortConfig>(name, baudRate, dataBits, stopBits, parity, timeoutInterval, reopenInterval, msgPattern, logRawData, logRawDataInHex, bindings);
}

std::shared_ptr<GeneratorConfig> Config::getGeneratorConfig(const Value& value, const Items& items) const 
{ 
	const Value& bindingsValue = getArray(value, "bindings");
	GeneratorConfig::Bindings bindings;
	for (auto& bindingValue : bindingsValue.GetArray())
	{
		string itemId = getString(bindingValue, "itemId");
		if (!items.exists(itemId))
			throw std::runtime_error("Invalid value " + itemId + " for field itemId in configuration");
		string value = getString(bindingValue, "value");
		int interval = getInt(bindingValue, "interval");
		string eventTypeStr = getString(bindingValue, "eventType");
		EventType eventType;
		if (!EventType::fromStr(eventTypeStr, eventType))
			throw std::runtime_error("Invalid value " + eventTypeStr + " for field eventType in configuration");
		
		bindings.add(GeneratorConfig::Binding(itemId, eventType, value, interval));
	}
	
	return std::make_shared<GeneratorConfig>(bindings);
}

std::shared_ptr<Tr064Config> Config::getTr064Config(const Value& value, const Items& items) const
{
	const Value& bindingsValue = getArray(value, "bindings");
	Tr064Config::Bindings bindings;
	for (auto& bindingValue : bindingsValue.GetArray())
	{
		string itemId = getString(bindingValue, "itemId");
		if (!items.exists(itemId))
			throw std::runtime_error("Invalid value " + itemId + " for field itemId in configuration");
		
		//bindings.add(GeneratorConfig::Binding(itemId, eventType, value, interval));
	}

	return std::make_shared<Tr064Config>(bindings);
}

std::shared_ptr<HttpConfig> Config::getHttpConfig(const Value& value, const Items& items) const
{
	bool logTransfers = getBool(value, "logTransfers", false);

	bool verboseMode = getBool(value, "verboseMode", false);

	const Value& bindingsValue = getArray(value, "bindings");
	HttpConfig::Bindings bindings;
	for (auto& bindingValue : bindingsValue.GetArray())
	{
		string itemId = getString(bindingValue, "itemId");
		if (!items.exists(itemId))
			throw std::runtime_error("Invalid value " + itemId + " for field itemId in configuration");

		string url = getString(bindingValue, "url");

		std::list<string> headers;
		if (hasMember(bindingValue, "headers"))
			for (auto& headerValue : getArray(bindingValue, "headers").GetArray())
			{
				if (!headerValue.IsString())
					throw std::runtime_error("Field headers is not a string array");
				headers.push_back(headerValue.GetString());
			}

		string request = getString(bindingValue, "request", "");

		std::regex responsePattern = convertPattern("pattern", getString(bindingValue, "responsePattern"));

		bindings.add(HttpConfig::Binding(itemId, url, headers, request, responsePattern));
	}

	return std::make_shared<HttpConfig>(logTransfers, verboseMode, bindings);
}

std::shared_ptr<TcpConfig> Config::getTcpConfig(const Value& value, const Items& items) const
{
	string hostname = getString(value, "hostname");
	int port = getInt(value, "port");

	std::regex msgPattern = convertPattern("msgPattern", getString(value, "msgPattern"));

	bool logRawData = getBool(value, "logRawData", false);
	bool logRawDataInHex = getBool(value, "logRawDataInHex", false);

	int reconnectInterval = getInt(value, "reconnectInterval", 60);

	const Value& bindingsValue = getArray(value, "bindings");
	TcpConfig::Bindings bindings;
	for (auto& bindingValue : bindingsValue.GetArray())
	{
		string itemId = getString(bindingValue, "itemId");
		if (!items.exists(itemId))
			throw std::runtime_error("Invalid value " + itemId + " for field itemId in configuration");

		std::regex pattern = convertPattern("pattern", getString(bindingValue, "pattern"));

		bindings.add(TcpConfig::Binding(itemId, pattern));
	}

	return std::make_shared<TcpConfig>(hostname, port, reconnectInterval, msgPattern, logRawData, logRawDataInHex, bindings);
}

std::shared_ptr<StorageConfig> Config::getStorageConfig(const Value& value, const Items& items) const
{
	string fileName = getString(value, "fileName");

	return std::make_shared<StorageConfig>(fileName);
}
