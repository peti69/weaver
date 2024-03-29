#include <sstream>
#include <cstring>
#include <stdexcept>
#include <unistd.h>

#include "mqtt.h"
#include "finally.h"

namespace mqtt
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

	string clientId = config.getClientId();
	client = mosquitto_new(clientId == "" ? nullptr : clientId.c_str(), true, this);
	if (!client)
		logger.errorX() << "Function mosquitto_new() returned null" << endOfMsg();

	mosquitto_connect_callback_set(client, mqtt::onConnect);
	mosquitto_message_callback_set(client, mqtt::onMessage);
	mosquitto_log_callback_set(client, mqtt::onLog);

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

void Handler::validate(Items& items)
{
	auto& bindings = config.getBindings();

	for (auto& [itemId, item] : items)
		if (item.getOwnerId() == id)
			if (auto bindingPos = bindings.find(itemId); bindingPos != bindings.end())
			{
				auto& binding = bindingPos->second;
				item.setReadable(!binding.readTopic.empty());
				item.setWritable(!binding.writeTopic.empty());
			}
			else
			{
				item.setReadable(!config.getOutReadTopicPattern().isNull());
				item.setWritable(!config.getOutWriteTopicPattern().isNull());
			}

	for (auto& [itemId, binding] : bindings)
		items.validate(itemId);
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

		std::unordered_set<string> topics;
		for (auto& [itemId, binding] : config.getBindings())
			if (items.getOwnerId(itemId) == id)
				topics.insert(binding.stateTopics.begin(), binding.stateTopics.end());
			else
			{
				if (binding.writeTopic != "")
					topics.insert(binding.writeTopic);
				if (binding.readTopic != "")
					topics.insert(binding.readTopic);
			}
		for (string topic : topics)
		{
			int ec = mosquitto_subscribe(client, 0, topic.c_str(), 0);
			handleError("mosquitto_subscribe", ec);
		}

		auto subscribe = [&] (TopicPattern topicPattern)
		{
			if (topicPattern.isNull())
				return;
			int ec = mosquitto_subscribe(client, 0, topicPattern.createSubTopicPattern().c_str(), 0);
			handleError("mosquitto_subscribe", ec);
		};
		subscribe(config.getInStateTopicPattern());
		subscribe(config.getInWriteTopicPattern());
		subscribe(config.getInReadTopicPattern());

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
		// try explicit matching
		bool matched = false;
		for (auto& [itemId, binding] : bindings)
		{
			auto analyzePayload = [&] (EventType type)
			{
				if (std::regex_search(msg.payload, binding.msgPattern))
				{
					events.add(Event(id, itemId, type, Value::newString(msg.payload)));
					matched = true;
				}
			};
			if (binding.readTopic == msg.topic)
				events.add(Event(id, itemId, EventType::READ_REQ, Value()));
			else if (binding.writeTopic == msg.topic)
				analyzePayload(EventType::WRITE_REQ);
			else if (binding.stateTopics.find(msg.topic) != binding.stateTopics.end())
				analyzePayload(EventType::STATE_IND);
		}
		if (matched)
			continue;

		// try implicit matching
		string itemId;
		auto getItemId = [&] (TopicPattern topicPattern)
		{
			if (topicPattern.isNull())
				return false;
			itemId = topicPattern.getItemId(msg.topic);
			if (!items.exists(itemId))
				return false; // ignore unknown item ids
			return true;
		};
		if (getItemId(config.getInStateTopicPattern()))
			events.add(Event(id, itemId, EventType::STATE_IND, Value::newString(msg.payload)));
		else if (getItemId(config.getInWriteTopicPattern()))
			events.add(Event(id, itemId, EventType::WRITE_REQ, Value::newString(msg.payload)));
		else if (getItemId(config.getInReadTopicPattern()))
			events.add(Event(id, itemId, EventType::READ_REQ, Value::newVoid()));
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

		// determine default topics
		std::unordered_set<string> topics;
		if (event.getType() == EventType::STATE_IND)
		{
			if (!config.getOutStateTopicPattern().isNull())
				topics = {config.getOutStateTopicPattern().createPubTopic(itemId)};
		}
		else if (event.getType() == EventType::WRITE_REQ)
		{
			if (!config.getOutWriteTopicPattern().isNull())
				topics = {config.getOutWriteTopicPattern().createPubTopic(itemId)};
		}
		else if (event.getType() == EventType::READ_REQ)
		{
			if (!config.getOutReadTopicPattern().isNull())
				topics = {config.getOutReadTopicPattern().createPubTopic(itemId)};
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

		// determine payload
		string payload;
		if (event.getType() != EventType::READ_REQ)
		{
			if (!event.getValue().isString())
			{
				logger.error() << "Event value type is not STRING for item " << itemId << endOfMsg();
				continue;
			}
			payload = event.getValue().getString();
		}

		// send message
		for (string topic : topics)
			sendMsg(topic, payload, event.getType() == EventType::STATE_IND ? config.getRetainFlag() : false);
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
