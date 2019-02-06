#include <stdexcept>

#include <rapidjson/filereadstream.h>
#include <rapidjson/error/en.h>

#include "config.h"
#include "knx.h"
#include "mqtt.h"
#include "port.h"
#include "finally.h"

Config::Config(string filename)
{
	FILE* file = fopen(filename.c_str(), "r");
	if (!file)
		throw std::runtime_error("Could not open file " + filename);
	auto autoClose = finally([file] { fclose(file); });

	char buffer[4096];
	rapidjson::FileReadStream stream(file, buffer, sizeof(buffer));
	rapidjson::ParseResult result = document.ParseStream<rapidjson::kParseCommentsFlag|rapidjson::kParseTrailingCommasFlag>(stream);
	if (result.IsError())
	{
		std::ostringstream stream;
		stream << "Parse error '" << rapidjson::GetParseError_En(result.Code()) << "' at offset " << result.Offset() << " in file " << filename;
		throw std::runtime_error(stream.str());
	}
}

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
		return std::regex(pattern, std::regex_constants::extended | std::regex_constants::nosubs);
	}
	catch (const std::regex_error& ex)
	{
		std::ostringstream stream;
		stream << "Invalid value " << pattern << " for field " << fieldName << " in configuration (error code = " << ex.code() << ", error string = " << ex.what() << ")";
		throw std::runtime_error(stream.str());
	}
}
	
regex_t Config::convertPattern2(string fieldName, string pattern) const
{
	regex_t regExpr;
	int rc = regcomp(&regExpr, pattern.c_str(), REG_EXTENDED);
	if (rc != 0)
	{
		char errorStr[256];
		regerror(rc, &regExpr, errorStr, sizeof(errorStr));
		std::ostringstream stream;
		stream << "Invalid value " << pattern << " for field " << fieldName << " in configuration (error code = " << rc << ", error string = " << errorStr << ")";
		throw std::runtime_error(stream.str());
	}
	return regExpr;
}

GlobalConfig Config::getGlobalConfig() const
{
	bool logEvents = getBool(document, "logEvents", false);
	
	return GlobalConfig(logEvents);;
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

		float relDelta = getFloat(itemValue, "relDelta", 0.0); 
		float absDelta = getFloat(itemValue, "absDelta", 0.0); 

		items.add(Item(itemId, type, ownerId, relDelta, absDelta));
	}

	return items;
}

Links Config::getLinks(const Items& items) const
{
	auto& linksValue = getArray(document, "links"); 
	Links links;

	for (auto& linkValue : linksValue.GetArray())
	{
		string id = getString(linkValue, "id");
		
		Modifiers modifiers;
		if (hasMember(linkValue, "modifiers"))
		{
			const Value& modifiersValue = getArray(linkValue, "modifiers"); 
			for (auto& modifierValue : modifiersValue.GetArray())
			{
				string itemId = getString(modifierValue, "itemId");
				if (!items.exists(itemId))
					throw std::runtime_error("Invalid value " + itemId + " for field itemId in configuration");
				
				float factor = getFloat(modifierValue, "factor");
				
				modifiers.add(Modifier(itemId, factor));
			}
		}

		std::shared_ptr<Handler> handler;
		if (hasMember(linkValue, "knx"))
			handler.reset(new KnxHandler(id, *getKnxConfig(getObject(linkValue, "knx"), items), Logger(id)));
		else if (hasMember(linkValue, "mqtt"))
			handler.reset(new MqttHandler(id, *getMqttConfig(getObject(linkValue, "mqtt"), items), Logger(id)));
		else if (hasMember(linkValue, "port"))
			handler.reset(new PortHandler(id, *getPortConfig(getObject(linkValue, "port"), items), Logger(id)));
		else
			throw std::runtime_error("Link with unknown or missing type in configuration");

		links.add(Link(id, modifiers, handler, Logger(id)));
	}
	
	for (auto itemPair : items)
		if (!links.exists(itemPair.second.getOwnerId()))
			throw std::runtime_error("Item " + itemPair.first + " associated with unknown link " + itemPair.second.getOwnerId());

	return links;
}

std::shared_ptr<MqttConfig> Config::getMqttConfig(const Value& value, const Items& items) const
{
	string clientIdPrefix = getString(value, "clientIdPrefix", "weaver");
	string hostname = getString(value, "hostname");
	int port = getInt(value, "port", 1883);
	int reconnectInterval = getInt(value, "reconnectInterval", 60);
	bool retainFlag = getBool(value, "retainFlag", false);
	
	bool logMsgs = getBool(value, "logMessages", false);

	const Value& bindingsValue = getArray(value, "bindings");
	MqttConfig::Bindings bindings;
	for (auto& bindingValue : bindingsValue.GetArray())
	{
		string itemId = getString(bindingValue, "itemId");
		if (!items.exists(itemId))
			throw std::runtime_error("Invalid value " + itemId + " for field itemId in configuration");

		MqttConfig::Binding::Topics stateTopics;
		if (hasMember(bindingValue, "stateTopic"))
			stateTopics.insert(getString(bindingValue, "stateTopic"));
		if (hasMember(bindingValue, "stateTopics"))
			for (auto& stateValue : getArray(bindingValue, "stateTopics").GetArray())
			{
				if (!stateValue.IsString())
					throw std::runtime_error("Field stateTopics is not a string array");
				stateTopics.insert(stateValue.GetString());
			}
		string writeTopic = getString(bindingValue, "writeTopic", "");
		string readTopic = getString(bindingValue, "readTopic", "");

		bindings.add(MqttConfig::Binding(itemId, stateTopics, writeTopic, readTopic));
	}

	return std::make_shared<MqttConfig>(clientIdPrefix, hostname, port, reconnectInterval, retainFlag, logMsgs, bindings);
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

	int reconnectInterval = getInt(value, "reconnectInterval", 60);
	int connStateReqInterval = getInt(value, "connStateReqInterval", 30);
	int controlRespTimeout = getInt(value, "controlRespTimeout", 5);
	int ldataConTimeout = getInt(value, "ldataConTimeout", 5);
	
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

		string dptStr = getString(bindingValue, "dpt");
		DatapointType dpt;
		if (!DatapointType::fromStr(dptStr, dpt))
			throw std::runtime_error("Invalid value " + dptStr + " for field dpt in configuration");

		bindings.add(KnxConfig::Binding(itemId, stateGa, writeGa, dpt));
	}

	return std::make_shared<KnxConfig>(localIpAddr, natMode, ipAddr, ipPort, reconnectInterval, connStateReqInterval, controlRespTimeout, ldataConTimeout, physicalAddr, logRawMsg, logData, bindings);
}

std::shared_ptr<PortConfig> Config::getPortConfig(const Value& value, const Items& items) const 
{ 
	string name = getString(value, "name");
	
	regex_t msgPattern = convertPattern2("msgPattern", getString(value, "msgPattern"));
	
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

		int reopenInterval = getInt(value, "reopenInterval", 60);
		
	const Value& bindingsValue = getArray(value, "bindings");
	PortConfig::Bindings bindings;
	for (auto& bindingValue : bindingsValue.GetArray())
	{
		string itemId = getString(bindingValue, "itemId");
		if (!items.exists(itemId))
			throw std::runtime_error("Invalid value " + itemId + " for field itemId in configuration");
		
		regex_t pattern = convertPattern2("pattern", getString(bindingValue, "pattern"));
			
		bindings.add(PortConfig::Binding(itemId, pattern));
	}
	
	return std::make_shared<PortConfig>(name, baudRate, dataBits, stopBits, parity, reopenInterval, msgPattern, logRawData, logRawDataInHex, bindings);
}

