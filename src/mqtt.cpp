#include <sstream>
#include <cstring>
#include <stdexcept>
#include <unistd.h>

#include "mqtt.h"
#include "finally.h"

namespace Mqtt
{

const string TopicPattern::variable = "%ItemId%";

string TopicPattern::getItemId(string topic) const
{
	string::size_type pos1 = topicPatternStr.find(variable);
	if (topic.substr(0, pos1) != topicPatternStr.substr(0, pos1))
		return "";
	string::size_type pos2 = topic.find("/", pos1);
	if (pos2 == string::npos)
		return topic.substr(pos1);
	if (topic.substr(pos2, topicPatternStr.length() - pos1 - variable.length()) != topicPatternStr.substr(pos1 + variable.length()))
		return "";
	return topic.substr(pos1, pos2 - pos1);
}

string TopicPattern::createPubTopic(string itemId) const
{
	string::size_type pos = topicPatternStr.find(variable);
	return topicPatternStr.substr(0, pos) + itemId + topicPatternStr.substr(pos + variable.length());
}

string TopicPattern::createSubTopicPattern() const
{
	string::size_type pos = topicPatternStr.find(variable);
	return topicPatternStr.substr(0, pos) + "+" + topicPatternStr.substr(pos + variable.length());
}

TopicPattern TopicPattern::fromStr(string topicPatternStr)
{
	if (topicPatternStr.find_first_of("+#") != string::npos)
		return TopicPattern();

	string::size_type pos = topicPatternStr.find(variable);
	if (pos == string::npos)
		return TopicPattern();

	if (pos > 0 && topicPatternStr[pos - 1] != '/')
		return TopicPattern();

	if (pos + variable.length() < topicPatternStr.length() && topicPatternStr[pos + variable.length()] != '/')
		return TopicPattern();

	return TopicPattern(topicPatternStr);
}

string Mappings::toInternal(string value) const
{
	for (auto& mapping : *this)
		if (value == mapping.external)
			return mapping.internal;
	return value;
}

string Mappings::toExternal(string value) const
{
	for (auto& mapping : *this)
		if (value == mapping.internal)
			return mapping.external;
	return value;
}

void onConnect(struct mosquitto* client, void* handler, int rc)
{
	static_cast<Handler*>(handler)->onConnect(rc);
}

void onMessage(struct mosquitto* client, void* handler, const struct mosquitto_message* msg)
{
	static_cast<Handler*>(handler)->onMessage(Handler::Msg(msg->topic, string(static_cast<char*>(msg->payload), msg->payloadlen), false));
}

void onLog(struct mosquitto* client, void* handler, int level, const char* msg)
{
	static_cast<Handler*>(handler)->onLog(level, msg);
}

Handler::Handler(string _id, Config _config, Logger _logger) :
	id(_id), config(_config), logger(_logger), client(0), state(DISCONNECTED),
	lastConnectTry(0), lastMsgSendTime(0)
{
	handlerState.errorCounter = 0;

	mosquitto_lib_init();
	client = mosquitto_new(config.getClientId().c_str(), true, this);
	if (!client)
		logger.errorX() << "Function mosquitto_new() returned null" << endOfMsg();

	mosquitto_connect_callback_set(client, Mqtt::onConnect);
	mosquitto_message_callback_set(client, Mqtt::onMessage);
	mosquitto_log_callback_set(client, Mqtt::onLog);

	int major, minor, revision;
	mosquitto_lib_version(&major, &minor, &revision);
	logger.info() << "Using Mosquitto library version " << major << "." << minor << "." << revision << endOfMsg();
}

Handler::~Handler()
{
	disconnect();

	mosquitto_destroy(client);
	mosquitto_lib_cleanup();
}

void Handler::validate(Items& items) const
{
	auto& bindings = config.getBindings();

	for (auto& bindingPair : bindings)
		items.validate(bindingPair.first);
}

void Handler::disconnect()
{
	if (state == DISCONNECTED)
		return;

	if (state == CONNECTED)
	{
		lastConnectTry = 0;

		logger.info() << "Disconnected from MQTT broker " << config.getHostname() << ":" << config.getPort() << endOfMsg();
	}
	else
		lastConnectTry = std::time(0);

	mosquitto_disconnect(client);
	state = DISCONNECTED;
	waitingMsgs.clear();
}

long Handler::collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd)
{
	int socket = mosquitto_socket(client);
	if (socket >= 0)
	{
		FD_SET(socket, readFds);
		*maxFd = std::max(*maxFd, socket);
	}
	return mosquitto_want_write(client) || state == CONNECTING_SUCCEEDED
		|| state == CONNECTING_FAILED || waitingMsgs.size() ? 0 : -1;
}

void Handler::handleError(string funcName, int errorCode)
{
	if (errorCode != MOSQ_ERR_SUCCESS)
	{
		LogMsg logMsg = logger.errorX();
		logMsg << "Function " << funcName << "() returned error " << errorCode << " (" << mosquitto_strerror(errorCode) << ")";
		if (errorCode == MOSQ_ERR_ERRNO)
			logMsg << " due to system error " << errno << " (" << strerror(errno) << ")";
		logMsg << endOfMsg();
	}
}

void Handler::onLog(int level, string text)
{
	if (!config.getLogLibEvents())
		return;

	string levelStr;
	switch (level)
	{
		case MOSQ_LOG_INFO:
			levelStr = "INFO"; break;
		case MOSQ_LOG_NOTICE:
			levelStr = "NOTICE"; break;
		case MOSQ_LOG_WARNING:
			levelStr = "WARNING"; break;
		case MOSQ_LOG_ERR:
			levelStr = "ERROR"; break;
		case MOSQ_LOG_DEBUG:
			levelStr = "DEBUG"; break;
		default:
			levelStr = "???"; break;
	}

	logger.debug() << text << " (" << levelStr << ")" << endOfMsg();
}

void Handler::onConnect(int rc)
{
	if (state == CONNECTING)
		if (rc == 0)
			state = CONNECTING_SUCCEEDED;
		else
			state = CONNECTING_FAILED;
}

void Handler::onMessage(const Msg& msg)
{
	if (config.getLogMsgs())
		logger.debug() << "R " << msg.topic << ": " << msg.payload << endOfMsg();

	receivedMsgs.push_back(msg);
}

Events Handler::receive(const Items& items)
{
	try
	{
		return receiveX(items);
	}
	catch (const std::exception& ex)
	{
		handlerState.errorCounter++;

		logger.error() << ex.what() << endOfMsg();
	}

	disconnect();
	return Events();
}

Events Handler::receiveX(const Items& items)
{
	Events events;

	if (state == DISCONNECTED)
	{
		if (config.getIdleTimeout() && !waitingMsgs.size())
			return events;

		std::time_t now = std::time(0);
		if (lastConnectTry + config.getReconnectInterval() > now)
			return events;
		lastConnectTry = now;

		int value = MQTT_PROTOCOL_V311;
		int ec = mosquitto_opts_set(client, MOSQ_OPT_PROTOCOL_VERSION, &value);
		handleError("mosquitto_opts_set#1", ec);

		// TODO: Enable TCP_NO_DELAY when being on new Mosquitto library.
		//value = 1;
		//ec = mosquitto_opts_set(client, MOSQ_OPT_TCP_NODELAY, &value);
		//handleError("mosquitto_opts_set#2", ec);

		string username = config.getUsername();
		string password = config.getPassword();
		ec = mosquitto_username_pw_set(client, username.empty() ? 0 : username.c_str(), password.empty() ? 0 : password.c_str());
		handleError("mosquitto_username_pw_set", ec);

		if (config.getTlsFlag())
		{
			string caFile = config.getCaFile();
			string caPath = config.getCaPath();
			ec = mosquitto_tls_set(client, caFile.empty() ? 0 : caFile.c_str(), caPath.empty() ? 0 : caPath.c_str(), 0, 0, 0);
			handleError("mosquitto_tls_set", ec);

			string ciphers = config.getCiphers();
			ec = mosquitto_tls_opts_set(client, 0, 0, ciphers.empty() ? 0 : ciphers.c_str());
			handleError("mosquitto_tls_opts_set", ec);

			//ec = mosquitto_tls_insecure_set(client, true);
			//handleError("mosquitto_tls_insecure_set", ec);
		}

		ec = mosquitto_connect(client, config.getHostname().c_str(), config.getPort(), 60);
		handleError("mosquitto_connect", ec);

		state = CONNECTING;
		receivedMsgs.clear();
	}

	int ec = mosquitto_loop(client, 0, 1);
	handleError("mosquitto_loop#1", ec);

	if (state == CONNECTING_SUCCEEDED)
	{
		state = CONNECTED;

		logger.info() << "Connected to MQTT broker " << config.getHostname() << ":" << config.getPort() << endOfMsg();

		for (auto& bindingPair : config.getBindings())
		{
			auto& binding = bindingPair.second;
			bool owner = items.getOwnerId(binding.itemId) == id;

			if (owner)
				for (string stateTopic : binding.stateTopics)
				{
					int ec = mosquitto_subscribe(client, 0, stateTopic.c_str(), 0);
					handleError("mosquitto_subscribe", ec);
				}
			else
			{
				if (binding.writeTopic != "")
				{
					int ec = mosquitto_subscribe(client, 0, binding.writeTopic.c_str(), 0);
					handleError("mosquitto_subscribe", ec);
				}
				if (binding.readTopic != "")
				{
					int ec = mosquitto_subscribe(client, 0, binding.readTopic.c_str(), 0);
					handleError("mosquitto_subscribe", ec);
				}
			}
		}

		auto subscribe = [&] (TopicPattern topicPattern)
		{
			if (topicPattern.isNull())
				return;
			int ec = mosquitto_subscribe(client, 0, topicPattern.createSubTopicPattern().c_str(), 0);
			handleError("mosquitto_subscribe", ec);
		};
		if (!config.getExportItems())
			subscribe(config.getStateTopicPattern());
		else
		{
			subscribe(config.getWriteTopicPattern());
			subscribe(config.getReadTopicPattern());
		}

		for (auto topic : config.getSubTopics())
		{
			int ec = mosquitto_subscribe(client, 0, topic.c_str(), 0);
			handleError("mosquitto_subscribe", ec);
		}

		for (auto& msg : waitingMsgs)
			sendMessage(msg.topic, msg.payload, msg.retainFlag);
		waitingMsgs.clear();
	}

	if (state == CONNECTED && config.getIdleTimeout() && lastMsgSendTime + config.getIdleTimeout() <= std::time(0))
	{
		disconnect();
		return events;
	}

	auto& bindings = config.getBindings();
	for (auto& msg : receivedMsgs)
	{
		auto getItemId = [&] (TopicPattern topicPattern, string& itemId)
		{
			if (topicPattern.isNull())
				return false;
			itemId = topicPattern.getItemId(msg.topic);
			return itemId.length() > 0;
		};

		// determine item id and event type
		EventType eventType;
		string itemId;
		if (!config.getExportItems())
		{
			if (getItemId(config.getStateTopicPattern(), itemId))
				eventType = EventType::STATE_IND;
		}
		else
		{
			if (getItemId(config.getWriteTopicPattern(), itemId))
				eventType = EventType::WRITE_REQ;
			else if (getItemId(config.getReadTopicPattern(), itemId))
				eventType = EventType::READ_REQ;
		}
		if (itemId == "")
			for (auto& [key, binding] : bindings)
			{
				if (binding.readTopic == msg.topic)
				{
					itemId = binding.itemId;
					eventType = EventType::READ_REQ;
				}
				else if (binding.writeTopic == msg.topic)
				{
					itemId = binding.itemId;
					eventType = EventType::WRITE_REQ;
				}
				else if (binding.stateTopics.find(msg.topic) != binding.stateTopics.end())
				{
					itemId = binding.itemId;
					eventType = EventType::STATE_IND;
				}
			}

		// ignore unknown item ids
		if (!items.exists(itemId))
			continue;

		// determine event value
		Value eventValue;
		if (eventType != EventType::READ_REQ)
			if (auto bindingPos = bindings.find(itemId); bindingPos != bindings.end())
			{
				auto& binding = bindingPos->second;

				std::smatch match;
				if (std::regex_search(msg.payload, match, binding.inPattern))
					if (match.size() > 1)
					{
						int i = 1;
						while (i < match.size() && !match[i].matched) i++;
						if (i < match.size()) // this should always be true
							eventValue = Value(binding.mappings.toInternal(string(match[i])));
					}
					else
						eventValue = Value::newVoid();
				else
					continue;
			}
			else
				eventValue = Value(msg.payload);
		else
			eventValue = Value();

		// generate event
		events.add(Event(id, itemId, eventType, eventValue));
	}
	receivedMsgs.clear();

	return events;
}

Events Handler::send(const Items& items, const Events& events)
{
	try
	{
		sendX(items, events);
		return Events();
	}
	catch (const std::exception& ex)
	{
		handlerState.errorCounter++;

		logger.error() << ex.what() << endOfMsg();
	}

	disconnect();
	return Events();
}

void Handler::sendX(const Items& items, const Events& events)
{
	auto sendMsg = [this] (string topic, string payload, bool retainFlag)
	{
		if (state != CONNECTED)
		{
			if (config.getIdleTimeout())
				waitingMsgs.push_back(Msg(topic, payload, retainFlag));
		}
		else
			sendMessage(topic, payload, retainFlag);
	};

	auto& bindings = config.getBindings();
	for (auto& event : events)
	{
		string itemId = event.getItemId();

		// skip STATE_IND events depending on configuration
		if (  event.getType() == EventType::STATE_IND
		   && !config.getStateTopicPattern().isNull() && !config.getExportItems()
		   )
			continue;

		// determine default topics
		std::unordered_set<string> topics;
		if (event.getType() == EventType::STATE_IND)
		{
			if (!config.getStateTopicPattern().isNull())
				topics = {config.getStateTopicPattern().createPubTopic(itemId)};
		}
		else if (event.getType() == EventType::WRITE_REQ)
		{
			if (!config.getWriteTopicPattern().isNull())
				topics = {config.getWriteTopicPattern().createPubTopic(itemId)};
		}
		else if (event.getType() == EventType::READ_REQ)
		{
			if (!config.getReadTopicPattern().isNull())
				topics = {config.getReadTopicPattern().createPubTopic(itemId)};
		}

		// override topics
		auto bindingPos = bindings.find(itemId);
		if (bindingPos != bindings.end())
		{
			auto& binding = bindingPos->second;

			if (event.getType() == EventType::STATE_IND)
			{
				if (binding.stateTopics.size())
					topics = binding.stateTopics;
			}
			else if (event.getType() == EventType::WRITE_REQ)
			{
				if (binding.writeTopic != "")
					topics = {binding.writeTopic};
			}
			else if (event.getType() == EventType::READ_REQ)
			{
				if (binding.readTopic != "")
					topics = {binding.readTopic};
			}
		}

		// skip events for those no topic could be found
		if (!topics.size())
			continue;

		// determine default value
		string valueStr;
		if (event.getType() != EventType::READ_REQ)
		{
			const Value& value = event.getValue();
			if (!value.isString())
			{
				logger.error() << "Event value type is not STRING for item " << itemId << endOfMsg();
				return;
			}
			valueStr = value.getString();
		}

		// override value
		if (bindingPos != bindings.end() && event.getType() != EventType::READ_REQ)
		{
			auto& binding = bindingPos->second;

			string valueStr = binding.mappings.toExternal(valueStr);
			string pattern = binding.outPattern;
			string::size_type pos = pattern.find("%time");
			if (pos != string::npos)
				pattern.replace(pos, 5, cnvToStr(std::time(0)));
			string str;
			str.resize(100);
			int n = snprintf(&str[0], str.capacity(), pattern.c_str(), valueStr.c_str());
			if (n >= str.capacity())
			{
				str.resize(n + 1);
				n = snprintf(&str[0], str.capacity(), pattern.c_str(), valueStr.c_str());
			}
			str.resize(n);
			valueStr = str;
		}

		// send message
		for (string topic : topics)
			sendMsg(topic, valueStr, event.getType() == EventType::STATE_IND ? config.getRetainFlag() : false);
	}

	if (state == CONNECTED)
	{
		int ec = mosquitto_loop(client, 0, 1);
		handleError("mosquitto_loop#2", ec);
	}
}

void Handler::sendMessage(string topic, string payload, bool retainFlag)
{
	lastMsgSendTime = std::time(0);

	if (config.getLogMsgs())
		logger.debug() << "S " << topic << ": " << payload << endOfMsg();

	int ec = mosquitto_publish(client, 0, topic.c_str(), payload.length(),
		reinterpret_cast<const uint8_t*>(payload.data()), 0, retainFlag);
	handleError("mosquitto_publish", ec);
}

}
