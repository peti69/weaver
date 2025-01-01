#include <stdexcept>

#include <rapidjson/filereadstream.h>
#include <rapidjson/error/en.h>

#include "config.h"
#include "knx.h"
#include "mqtt.h"
#include "port.h"
#include "generator.h"
#include "calculator.h"
#include "tr064.h"
#include "http.h"
#include "tcp.h"
#include "modbus.h"
#include "storage.h"
#include "finally.h"

string identity(string s)
{
	return s;
}

int hasMember(const rapidjson::Value& value, string name)
{
	return value.HasMember(name.c_str()); 
}

const rapidjson::Value& getObject(const rapidjson::Value& value, string name)
{
	auto iter = value.FindMember(name.c_str());
	if (iter == value.MemberEnd())
		throw std::runtime_error("Field " + name + " not found");
	if (!iter->value.IsObject())
		throw std::runtime_error("Field " + name + " is not an object");
	return iter->value;
}

const rapidjson::Value& getArray(const rapidjson::Value& value, string name)
{
	auto iter = value.FindMember(name.c_str());
	if (iter == value.MemberEnd())
		throw std::runtime_error("Field " + name + " not found");
	if (!iter->value.IsArray())
		throw std::runtime_error("Field " + name + " is not an array");
	if (!iter->value.Size())
		throw std::runtime_error("Field " + name + " is an empty array");
	return iter->value;
}

string getString(
	const rapidjson::Value& value, string name,
	std::function<string(string)> modifier = identity)
{
	auto iter = value.FindMember(name.c_str());
	if (iter == value.MemberEnd())
		throw std::runtime_error("Field " + name + " not found");
	if (!iter->value.IsString())
		throw std::runtime_error("Field " + name + " is not a string");
	return modifier(iter->value.GetString());
}

string getString(
	const rapidjson::Value& value, string name, string defaultValue,
	std::function<string(string)> modifier = identity)
{
	if (!hasMember(value, name))
		return defaultValue;
	return getString(value, name, modifier);
}

template<class T>
std::unordered_set<T> getArrayItems(
	const rapidjson::Value& value, string name,
	std::function<T(string)> modifier)
{
	string arrayName = name + "s";
	std::unordered_set<T> items;
	if (hasMember(value, name))
		items.insert(modifier(getString(value, name)));
	else
		for (auto& itemValue : getArray(value, arrayName).GetArray())
		{
			if (!itemValue.IsString())
				throw std::runtime_error("Field " + arrayName + " is not a string array");
			items.insert(modifier(itemValue.GetString()));
		}
	return items;
}

std::unordered_set<string> getStrings(
	const rapidjson::Value& value, string name,
	std::function<string(string)> modifier = identity)
{
	return getArrayItems(value, name, modifier);
}

std::unordered_set<string> getStrings(
	const rapidjson::Value& value, string name,
	std::unordered_set<string> dfltValue,
	std::function<string(string)> modifier = identity)
{
	if (!hasMember(value, name) && !hasMember(value, name + "s"))
		return dfltValue;
	return getArrayItems(value, name, modifier);
}

int getInt(const rapidjson::Value& value, string name)
{
	auto iter = value.FindMember(name.c_str());
	if (iter == value.MemberEnd())
		throw std::runtime_error("Field " + name + " not found");
	if (!iter->value.IsInt())
		throw std::runtime_error("Field " + name + " is not an integer");
	return iter->value.GetInt();
}

template<class T>
T getInt(const rapidjson::Value& value, string name, T dfltValue)
{
	if (!hasMember(value, name))
		return dfltValue;
	return T(getInt(value, name));
}

float getFloat(const rapidjson::Value& value, string name)
{
	auto iter = value.FindMember(name.c_str());
	if (iter == value.MemberEnd())
		throw std::runtime_error("Field " + name + " not found");
	if (!iter->value.IsNumber())
		throw std::runtime_error("Field " + name + " is not a floating point number");
	return iter->value.GetFloat();
}

float getFloat(const rapidjson::Value& value, string name, float dfltValue)
{
	if (!hasMember(value, name))
		return dfltValue;
	return getFloat(value, name);
}

bool getBool(const rapidjson::Value& value, string name)
{
	auto iter = value.FindMember(name.c_str());
	if (iter == value.MemberEnd())
		throw std::runtime_error("Field " + name + " not found");
	if (!iter->value.IsBool())
		throw std::runtime_error("Field " + name + " is not a boolean");
	return iter->value.GetBool();
}

bool getBool(const rapidjson::Value& value, string name, bool dfltValue)
{
	if (!hasMember(value, name))
		return dfltValue;
	return getBool(value, name);
}

std::regex getRegEx(const rapidjson::Value& value, string name, string dfltValue = "")
{
	string pattern = dfltValue != "" ? getString(value, name, dfltValue) : getString(value, name);
	try
	{
		return std::regex(pattern, std::regex_constants::extended | std::regex_constants::optimize);
	}
	catch (const std::regex_error& ex)
	{
		std::ostringstream stream;
		stream << "Invalid value " << pattern << " for field " << name << " in configuration (error code = " << ex.code() << ", error string = " << ex.what() << ")";
		throw std::runtime_error(stream.str());
	}
}

Byte getByte(const rapidjson::Value& value, string name)
{
	auto iter = value.FindMember(name.c_str());
	if (iter == value.MemberEnd())
		throw std::runtime_error("Field " + name + " not found");
	if (!iter->value.IsInt())
		throw std::runtime_error("Field " + name + " is not an integer");
	return iter->value.GetInt();
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
	bool logPSelectCalls = getBool(document, "logPSelectCalls", false);
	bool logEvents = getBool(document, "logEvents", false);
	bool logSuppressedEvents = getBool(document, "logSuppressedEvents", true);
	bool logGeneratedEvents = getBool(document, "logGeneratedEvents", true);

	return GlobalConfig(logPSelectCalls, logEvents, logSuppressedEvents, logGeneratedEvents);
}

LogConfig Config::getLogConfig() const
{
	string fileName = getString(document, "logFileName", "");
	int maxFileSize = getInt(document, "maxLogFileSize", 0);
	int maxFileCount = getInt(document, "maxLogFileCount", 0);
	LogLevel minLevel;
	string str = getString(document, "minLogLevel", "info");
	if (str == "debug")
		minLevel = LogLevel::DEBUG;
	else if (str == "info")
		minLevel = LogLevel::INFO;
	else if (str == "warn")
		minLevel = LogLevel::WARN;
	else if (str == "error")
		minLevel = LogLevel::ERROR;
	else
		throw std::runtime_error("Invalid value " + str + " for field minLogLevel in configuration");

	return LogConfig(fileName, maxFileSize, maxFileCount, minLevel);
}

Items Config::getItems() const
{
	Items items;
	for (auto& itemValue : getArray(document, "items").GetArray())
	{
		string itemId = getString(itemValue, "id"); 
		if (items.find(itemId) != items.end())
			throw std::runtime_error("Item " + itemId + " defined twice in configuration");

		Item item(itemId);

		auto modifier = [](string str)
		{
			if (ValueType type; ValueType::fromStr(str, type))
				return type;
			throw std::runtime_error("Invalid value " + str + " for field type(s) in configuration");
		};
		item.setValueTypes(getArrayItems<ValueType>(itemValue, "type", modifier));

		Unit unit;
		if (string str = getString(itemValue, "unit", "unknown"); !Unit::fromStr(str, unit))
			throw std::runtime_error("Invalid value " + str + " for field unit in configuration");
		item.setUnit(unit);

		item.setOwnerId(getString(itemValue, "ownerId"));

		item.setReadable(getBool(itemValue, "readable", true));
		item.setWritable(getBool(itemValue, "writable", true));
		item.setResponsive(getBool(itemValue, "responsive", true));

		item.setPollingInterval(getInt(itemValue, "pollingInterval", Seconds::zero()));

		item.setHistoryPeriod(getInt(itemValue, "historyPeriod", Seconds::zero()));

		if (hasMember(itemValue, "sendOnTimer"))
		{
			auto& sendOnTimerValue = getObject(itemValue, "sendOnTimer");
			Item::SendOnTimerParams params;
			params.active = true;
			params.interval = Seconds(getInt(sendOnTimerValue, "interval"));
			item.setSendOnTimerParams(params);
		}

		if (hasMember(itemValue, "sendOnChange"))
		{
			auto& sendOnChangeValue = getObject(itemValue, "sendOnChange");
			Item::SendOnChangeParams params;
			params.active = true;
			params.absVariation = getFloat(sendOnChangeValue, "absVariation", params.absVariation);
			params.relVariation = getFloat(sendOnChangeValue, "relVariation", params.relVariation);
			params.minimum = getFloat(sendOnChangeValue, "minimum", params.minimum);
			params.maximum = getFloat(sendOnChangeValue, "maximum", params.maximum);
			item.setSendOnChangeParams(params);
		}

		items.add(item);
	}

	return items;
}

Links Config::getLinks(const Items& items, Log& log) const
{
	Links links;
	for (auto& linkValue : getArray(document, "links").GetArray())
	{
		string id = getString(linkValue, "id");
		bool enabled = getBool(linkValue, "enabled", true);

		bool suppressReadEvents = getBool(linkValue, "suppressReadEvents", false);
		string operationalItemId = getString(linkValue, "operationalItemId", "");
		string errorCounterItemId = getString(linkValue, "errorCounterItemId", "");
		int maxReceiveDuration = getInt(linkValue, "maxReceiveDuration", 20);
		int maxSendDuration = getInt(linkValue, "maxSendDuration", 20);

		bool numberAsString = hasMember(linkValue, "numberAsString");

		bool booleanAsString = hasMember(linkValue, "booleanAsString");
		string falseValue;
		string trueValue;
		string unwritableFalseValue;
		string unwritableTrueValue;
		if (booleanAsString)
		{
			auto& booleanAsStringValue = getObject(linkValue, "booleanAsString");
			falseValue = getString(booleanAsStringValue, "falseValue");
			trueValue = getString(booleanAsStringValue, "trueValue");
			unwritableFalseValue = getString(booleanAsStringValue, "unwritableFalseValue", falseValue);
			unwritableTrueValue = getString(booleanAsStringValue, "unwritableTrueValue", trueValue);
		}

		bool timePointAsString = hasMember(linkValue, "timePointAsString");
		string timePointFormat;
		if (timePointAsString)
			timePointFormat = getString(getObject(linkValue, "timePointAsString"), "format");

		bool voidAsString = hasMember(linkValue, "voidAsString");
		string voidValue;
		string unwritableVoidValue;
		if (voidAsString)
		{
			auto& voidAsStringValue = getObject(linkValue, "voidAsString");
			voidValue = getString(voidAsStringValue, "value");
			unwritableVoidValue = getString(voidAsStringValue, "unwritableValue", voidValue);
		}

		bool voidAsBoolean = hasMember(linkValue, "voidAsBoolean");

		bool undefinedAsString = hasMember(linkValue, "undefinedAsString");
		string undefinedValue;
		if (undefinedAsString)
			undefinedValue = getString(getObject(linkValue, "undefinedAsString"), "value");
		bool suppressUndefined = getBool(linkValue, "suppressUndefined", false);

		Modifiers modifiers;
		if (hasMember(linkValue, "modifiers"))
			for (auto& modifierValue : getArray(linkValue, "modifiers").GetArray())
			{
				Modifier modifier;

				if (string str = getString(modifierValue, "unit", "unknown"); !Unit::fromStr(str, modifier.unit))
					throw std::runtime_error("Invalid value " + str + " for field unit in configuration");

				modifier.factor = getFloat(modifierValue, "factor", 1.0);
				modifier.summand = getFloat(modifierValue, "summand", 0.0);
				modifier.round = getBool(modifierValue, "round", false);

				modifier.inObisCode = getString(modifierValue, "inObisCode", "");
				modifier.inJsonPointer = getString(modifierValue, "inJsonPointer", "");
				modifier.inPattern = getRegEx(modifierValue, "inPattern", "^(.*)$");
				if (hasMember(modifierValue, "inMappings"))
					for (auto& mappingValue : getArray(modifierValue, "inMappings").GetArray())
						modifier.addInMapping(getString(mappingValue, "from"), getString(mappingValue, "to"));

				modifier.outPattern = getString(modifierValue, "outPattern", "%EventValue%");
				if (hasMember(modifierValue, "outMappings"))
					for (auto& mappingValue : getArray(modifierValue, "outMappings").GetArray())
						modifier.addOutMapping(getString(mappingValue, "from"), getString(mappingValue, "to"));

				for (string itemId : getStrings(modifierValue, "itemId"))
				{
					modifier.itemId = itemId;
					modifiers.add(modifier);
				}
			}

		Logger logger = log.newLogger(enabled ? id : "(" + id + ")");
		std::shared_ptr<HandlerIf> handler;
		if (hasMember(linkValue, "knx"))
			handler.reset(new KnxHandler(id, getKnxConfig(getObject(linkValue, "knx")), logger));
		else if (hasMember(linkValue, "mqtt"))
			handler.reset(new mqtt::Handler(id, getMqttConfig(getObject(linkValue, "mqtt")), logger));
		else if (hasMember(linkValue, "port"))
			handler.reset(new PortHandler(id, getPortConfig(getObject(linkValue, "port")), logger));
		else if (hasMember(linkValue, "http"))
			handler.reset(new HttpHandler(id, getHttpConfig(getObject(linkValue, "http")), logger));
		else if (hasMember(linkValue, "tcp"))
			handler.reset(new TcpHandler(id, getTcpConfig(getObject(linkValue, "tcp")), logger));
		else if (hasMember(linkValue, "modbus"))
			handler.reset(new modbus::Handler(id, getModbusConfig(getObject(linkValue, "modbus")), logger));
		else if (hasMember(linkValue, "generator"))
			handler.reset(new Generator(id, getGeneratorConfig(getObject(linkValue, "generator")), logger));
		else if (hasMember(linkValue, "calculator"))
			handler.reset(new calculator::Handler(id, getCalculatorConfig(getObject(linkValue, "calculator")), logger));
		else if (hasMember(linkValue, "tr064"))
			handler.reset(new Tr064(id, getTr064Config(getObject(linkValue, "tr064")), logger));
		else if (hasMember(linkValue, "storage"))
			handler.reset(new storage::Handler(id, getStorageConfig(getObject(linkValue, "storage")), logger));
		else
			throw std::runtime_error("Link " + id + " with unknown or missing type in configuration");

		links.add(Link(id, enabled, suppressReadEvents, operationalItemId, errorCounterItemId,
			maxReceiveDuration, maxSendDuration, numberAsString,
			booleanAsString, falseValue, trueValue, unwritableFalseValue, unwritableTrueValue,
			timePointAsString, timePointFormat, voidAsString, voidValue, unwritableVoidValue,
			voidAsBoolean, undefinedAsString, undefinedValue, suppressUndefined,
			modifiers, handler, logger));
	}

	return links;
}

mqtt::Config Config::getMqttConfig(const rapidjson::Value& value) const
{
	string clientId = getString(value, "clientId", "");
	string hostname = getString(value, "hostname", "127.0.0.1");
	int port = getInt(value, "port", 1883);

	bool tlsFlag = hasMember(value, "tls");
	string caFile, caPath, ciphers;
	if (tlsFlag)
	{
		auto& tlsValue = getObject(value, "tls");
		caFile = getString(tlsValue, "caFile", "");
		caPath = getString(tlsValue, "caPath", "");
		ciphers = getString(tlsValue, "ciphers", "");
	}

	int reconnectInterval = getInt(value, "reconnectInterval", 60);
	int idleTimeout = getInt(value, "idleTimeout", 0);

	string username = getString(value, "username", "");
	string password = getString(value, "password", "");
	bool retainFlag = getBool(value, "retainFlag", true);

	string topicPrefix = getString(value, "topicPrefix", "");
	auto addPrefix = [&](string topic){ return topicPrefix + topic; };

	auto getTopicPattern = [&](string name)
	{
		if (!hasMember(value, name))
			return mqtt::TopicPattern();
		string topicPatternStr = addPrefix(getString(value, name));
		auto topicPattern = mqtt::TopicPattern::fromStr(topicPatternStr);
		if (topicPattern.isNull())
			throw std::runtime_error("Invalid value " + topicPatternStr + " for field " + name + " in configuration");
		return topicPattern;
	};
	mqtt::TopicPattern inStateTopicPattern = getTopicPattern("inStateTopicPattern");
	mqtt::TopicPattern inWriteTopicPattern = getTopicPattern("inWriteTopicPattern");
	mqtt::TopicPattern inReadTopicPattern = getTopicPattern("inReadTopicPattern");
	mqtt::TopicPattern outStateTopicPattern = getTopicPattern("outStateTopicPattern");
	mqtt::TopicPattern outWriteTopicPattern = getTopicPattern("outWriteTopicPattern");
	mqtt::TopicPattern outReadTopicPattern = getTopicPattern("outReadTopicPattern");

	mqtt::Config::Topics subTopics = getStrings(value, "subTopic", {}, addPrefix);
	bool logMsgs = getBool(value, "logMessages", false);
	bool logLibEvents = getBool(value, "logLibEvents", false);

	mqtt::Config::Bindings bindings;
	if (hasMember(value, "bindings"))
		for (auto& bindingValue : getArray(value, "bindings").GetArray())
		{
			mqtt::Config::Topics stateTopics = getStrings(bindingValue, "stateTopic", {}, addPrefix);
			string writeTopic = getString(bindingValue, "writeTopic", "", addPrefix);
			string readTopic = getString(bindingValue, "readTopic", "", addPrefix);

			std::regex msgPattern = getRegEx(bindingValue, "msgPattern", "^(.*)$");

			for (string itemId : getStrings(bindingValue, "itemId"))
				bindings.add(mqtt::Config::Binding(itemId, stateTopics, writeTopic, readTopic, msgPattern));
		}

	return mqtt::Config(clientId, hostname, port, tlsFlag, caFile, caPath, ciphers,
			reconnectInterval, idleTimeout, username, password, retainFlag, inStateTopicPattern,
			inWriteTopicPattern, inReadTopicPattern, outStateTopicPattern, outWriteTopicPattern,
			outReadTopicPattern, subTopics, logMsgs, logLibEvents, bindings);
}

KnxConfig Config::getKnxConfig(const rapidjson::Value& value) const
{
	IpAddr localIpAddr;
	if (string str = getString(value, "localIpAddr"); !IpAddr::fromStr(str, localIpAddr))
		throw std::runtime_error("Invalid value " + str + " for field localIpAddr in configuration");

	bool natMode = getBool(value, "natMode", false);

	IpAddr ipAddr;
	if (string str = getString(value, "ipAddr"); !IpAddr::fromStr(str, ipAddr))
		throw std::runtime_error("Invalid value " + str + " for field ipAddr in configuration");
	IpPort ipPort = getInt(value, "ipPort", 3671);

	Seconds reconnectInterval(getInt(value, "reconnectInterval", 60));
	Seconds connStateReqInterval(getInt(value, "connStateReqInterval", 60));
	Seconds controlRespTimeout(getInt(value, "controlRespTimeout", 10));
	Seconds tunnelAckTimeout(getInt(value, "tunnelAckTimeout", 1));
	Seconds ldataConTimeout(getInt(value, "ldataConTimeout", 3));

	PhysicalAddr physicalAddr;
	if (string str = getString(value, "physicalAddr", "0.0.0"); !PhysicalAddr::fromStr(str, physicalAddr))
		throw std::runtime_error("Invalid value " + str + " for field physicalAddr in configuration");

	bool logRawMsg = getBool(value, "logRawMessages", false);
	bool logData = getBool(value, "logData", false);
	
	KnxConfig::Bindings bindings;
	for (auto& bindingValue : getArray(value, "bindings").GetArray())
	{
		GroupAddr stateGa;
		if (string str = getString(bindingValue, "stateGa", ""); str != "" && !GroupAddr::fromStr(str, stateGa))
			throw std::runtime_error("Invalid value " + str + " for field stateGa in configuration");

		GroupAddr writeGa;
		if (string str = getString(bindingValue, "writeGa", ""); str != "" && !GroupAddr::fromStr(str, writeGa))
			throw std::runtime_error("Invalid value " + str + " for field writeGa in configuration");

		DatapointType dpt;
		if (string str = getString(bindingValue, "dpt"); !DatapointType::fromStr(str, dpt))
			throw std::runtime_error("Invalid value " + str + " for field dpt in configuration");

		bindings.add(KnxConfig::Binding(getString(bindingValue, "itemId"), stateGa, writeGa, dpt));
	}

	return KnxConfig(localIpAddr, natMode, ipAddr, ipPort, reconnectInterval,
			connStateReqInterval, controlRespTimeout, tunnelAckTimeout,
			ldataConTimeout, physicalAddr, logRawMsg, logData, bindings);
}

PortConfig Config::getPortConfig(const rapidjson::Value& value) const
{ 
	string name = getString(value, "name");

	int baudRate = getInt(value, "baudRate");
	if (!PortConfig::isValidBaudRate(baudRate))
		throw std::runtime_error("Invalid value for field baudRate in configuration");

	int dataBits = getInt(value, "dataBits");
	if (!PortConfig::isValidDataBits(dataBits))
		throw std::runtime_error("Invalid value for field dataBits in configuration");

	int stopBits = getInt(value, "stopBits");
	if (!PortConfig::isValidStopBits(stopBits))
		throw std::runtime_error("Invalid value for field stopBits in configuration");

	PortConfig::Parity parity;
	if (string str = getString(value, "parity"); !PortConfig::isValidParity(str, parity))
		throw std::runtime_error("Invalid value " + str + " for field parity in configuration");

	int timeoutInterval = getInt(value, "timeoutInterval", 60);
	int reopenInterval = getInt(value, "reopenInterval", 60);

	bool convertToHex = getBool(value, "convertToHex", false);

	std::regex msgPattern = getRegEx(value, "msgPattern");
	int maxMsgSize = getInt(value, "maxMsgSize", 1024);

	bool logRawData = getBool(value, "logRawData", false);

	ItemId inputItemId = getString(value, "inputItemId", "");

	PortConfig::Bindings bindings;
	for (auto& bindingValue : getArray(value, "bindings").GetArray())
	{
		std::regex pattern = getRegEx(bindingValue, "pattern");
		bool binMatching = getBool(bindingValue, "binMatching", false);

		for (string itemId : getStrings(bindingValue, "itemId"))
			bindings.add(PortConfig::Binding(itemId, pattern, binMatching));
	}

	return PortConfig(name, baudRate, dataBits, stopBits, parity, timeoutInterval,
			reopenInterval, convertToHex, msgPattern, maxMsgSize, logRawData, inputItemId, bindings);
}

GeneratorConfig Config::getGeneratorConfig(const rapidjson::Value& value) const
{ 
	GeneratorConfig::Bindings bindings;
	for (auto& bindingValue : getArray(value, "bindings").GetArray())
	{
		string value = getString(bindingValue, "value");
		int interval = getInt(bindingValue, "interval");
		EventType eventType;
		if (string str = getString(bindingValue, "eventType"); !EventType::fromStr(str, eventType))
			throw std::runtime_error("Invalid value " + str + " for field eventType in configuration");

		bindings.add(GeneratorConfig::Binding(getString(bindingValue, "itemId"), eventType, value, interval));
	}
	
	return GeneratorConfig(bindings);
}

calculator::Config Config::getCalculatorConfig(const rapidjson::Value& value) const
{
	calculator::Bindings bindings;
	for (auto& bindingValue : getArray(value, "bindings").GetArray())
	{
		string sourceItemId = getString(bindingValue, "sourceItemId");
		string periodItemId = getString(bindingValue, "periodItemId");
		calculator::Function function;
		string str = getString(bindingValue, "function");
		if (str == "maximum")
			function = calculator::Function::MAXIMUM;
		else if (str == "minimum")
			function = calculator::Function::MINIMUM;
		else
			throw std::runtime_error("Invalid value " + str + " for field function in configuration");

		bindings.add(calculator::Binding(getString(bindingValue, "itemId"), function, sourceItemId, periodItemId));
	}

	return calculator::Config(bindings);
}

Tr064Config Config::getTr064Config(const rapidjson::Value& value) const
{
	Tr064Config::Bindings bindings;
	for (auto& bindingValue : getArray(value, "bindings").GetArray())
	{
		string itemId = getString(bindingValue, "itemId");

		//bindings.add(GeneratorConfig::Binding(itemId, eventType, value, interval));
	}

	return Tr064Config(bindings);
}

HttpConfig Config::getHttpConfig(const rapidjson::Value& value) const
{
	string user = getString(value, "user", "");
	string password = getString(value, "password", "");

	bool logTransfers = getBool(value, "logTransfers", false);
	bool verboseMode = getBool(value, "verboseMode", false);

	string dfltUrl = getString(value, "url", "");
	auto dfltHeaders = getStrings(value, "header", {}, identity);

	HttpConfig::Bindings bindings;
	for (auto& bindingValue : getArray(value, "bindings").GetArray())
	{
		string url = hasMember(bindingValue, "url") ? getString(bindingValue, "url") : dfltUrl;
		auto headers = getStrings(bindingValue, "header", dfltHeaders);

		string request = getString(bindingValue, "request", "");
		std::regex responsePattern = getRegEx(bindingValue, "responsePattern", "^.*$");

		for (string itemId : getStrings(bindingValue, "itemId"))
			bindings.add(HttpConfig::Binding(itemId, url, headers, request, responsePattern));
	}

	return HttpConfig(user, password, logTransfers, verboseMode, bindings);
}

TcpConfig Config::getTcpConfig(const rapidjson::Value& value) const
{
	string hostname = getString(value, "hostname");
	int port = getInt(value, "port");

	bool convertToHex = getBool(value, "convertToHex", false);

	std::regex msgPattern = getRegEx(value, "msgPattern");
	int maxMsgSize = getInt(value, "maxMsgSize", 1024);

	bool logRawData = getBool(value, "logRawData", false);

	int timeoutInterval = getInt(value, "timeoutInterval", 0);
	int reconnectInterval = getInt(value, "reconnectInterval", 60);

	TcpConfig::Bindings bindings;
	for (auto& bindingValue : getArray(value, "bindings").GetArray())
	{
		std::regex pattern = getRegEx(bindingValue, "pattern");
		bool binMatching = getBool(bindingValue, "binMatching", false);

		for (string itemId : getStrings(bindingValue, "itemId"))
			bindings.add(TcpConfig::Binding(itemId, pattern, binMatching));
	}

	return TcpConfig(hostname, port, timeoutInterval, reconnectInterval, convertToHex,
			msgPattern, maxMsgSize, logRawData, bindings);
}

modbus::Config Config::getModbusConfig(const rapidjson::Value& value) const
{
	string hostname = getString(value, "hostname");
	int port = getInt(value, "port", 502);

	bool logRawData = getBool(value, "logRawData", false);
	bool logMsgs = getBool(value, "logMessages", false);

	Seconds reconnectInterval(getInt(value, "reconnectInterval", 60));

	modbus::Config::Bindings bindings;
	for (auto& bindingValue : getArray(value, "bindings").GetArray())
	{
		Byte unitId = getByte(bindingValue, "unitId");
		int firstRegister = getInt(bindingValue, "firstRegister");
		int lastRegister = getInt(bindingValue, "lastRegister");
		int factorRegister = getInt(bindingValue, "factorRegister", -1);

		for (string itemId : getStrings(bindingValue, "itemId"))
		{
			if (firstRegister > lastRegister)
				throw std::runtime_error("Item " + itemId + " has invalid register query range");
			if (factorRegister > -1)
				if (factorRegister < firstRegister || factorRegister > lastRegister)
					throw std::runtime_error("Item " + itemId + " has factor register outside of register query range");

			bindings.add(modbus::Config::Binding(itemId, unitId, firstRegister, lastRegister, factorRegister));
		}
	}

	return modbus::Config(hostname, port, reconnectInterval, logRawData, logMsgs, bindings);
}

storage::Config Config::getStorageConfig(const rapidjson::Value& value) const
{
	string fileName = getString(value, "fileName");

	storage::Bindings bindings;
	for (auto& bindingValue : getArray(value, "bindings").GetArray())
	{
		Value initialValue;
		if (hasMember(bindingValue, "initialBoolean"))
			initialValue = Value::newBoolean(getBool(bindingValue, "initialBoolean"));
		else if (hasMember(bindingValue, "initialNumber"))
			initialValue = Value::newNumber(getFloat(bindingValue, "initialNumber"));
		else if (hasMember(bindingValue, "initialString"))
			initialValue = Value::newString(getString(bindingValue, "initialString"));
		else
			initialValue = Value::newUndefined();

		bool persistent = getBool(bindingValue, "persistent", true);

		bindings.add(storage::Binding(getString(bindingValue, "itemId"), initialValue, persistent));
	}

	return storage::Config(fileName, bindings);
}
