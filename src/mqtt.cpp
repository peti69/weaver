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

void onMqttMessage(struct mosquitto* client, void* handler, const struct mosquitto_message* msg)
{
	//static_cast<Handler*>(handler)->onMessage(msg);
	static_cast<Handler*>(handler)->onMessage(Handler::Msg(msg->topic, string(static_cast<char*>(msg->payload), msg->payloadlen)));
}

Handler::Handler(string _id, Config _config, Logger _logger) :
	id(_id), config(_config), logger(_logger), client(0), connected(false), lastConnectTry(0)
{
	mosquitto_lib_init();
	client = mosquitto_new((config.getClientIdPrefix() + "." + cnvToStr(getpid())).c_str(), true, this);
	if (!client)
		logger.errorX() << "Function mosquitto_new() returned null" << endOfMsg();

	// TODO: Enable TCP_NO_DELAY when being on new Mosquitto library.

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

bool Handler::connect(const Items& items)
{
	if (connected)
		return true;

	std::time_t now = std::time(0);
	if (lastConnectTry + config.getReconnectInterval() > now)
		return false;
	lastConnectTry = now;

	int ec = mosquitto_connect(client, config.getHostname().c_str(), config.getPort(), 60);
	handleError("mosquitto_connect", ec);
	auto autoDisconnect = finally([this] { mosquitto_disconnect(client); });

	mosquitto_message_callback_set(client, onMqttMessage);

	for (auto bindingPair : config.getBindings())
	{
		auto& binding = bindingPair.second;
		bool owner = items.getOwnerId(binding.itemId) == id;

		if (owner)
			for (string stateTopic : binding.stateTopics)
			{
				ec = mosquitto_subscribe(client, 0, stateTopic.c_str(), 0);
				handleError("mosquitto_subscribe", ec);
			}
		if (binding.writeTopic != "" && !owner)
		{
			ec = mosquitto_subscribe(client, 0, binding.writeTopic.c_str(), 0);
			handleError("mosquitto_subscribe", ec);
		}
		if (binding.readTopic != "" && !owner)
		{
			ec = mosquitto_subscribe(client, 0, binding.readTopic.c_str(), 0);
			handleError("mosquitto_subscribe", ec);
		}
	}

	for (auto topic : config.getSubTopics())
	{
		ec = mosquitto_subscribe(client, 0, topic.c_str(), 0);
		handleError("mosquitto_subscribe", ec);
	}

	auto subscribe = [&] (TopicPattern topicPattern)
	{
		if (topicPattern.isNull())
			return;
		ec = mosquitto_subscribe(client, 0, topicPattern.createSubTopicPattern().c_str(), 0);
		handleError("mosquitto_subscribe", ec);
	};
//	subscribe(config.getStateTopicPattern());
	subscribe(config.getWriteTopicPattern());
	subscribe(config.getReadTopicPattern());

	autoDisconnect.disable();
	connected = true;
	logger.info() << "Connected to MQTT broker " << config.getHostname() << ":" << config.getPort() << endOfMsg();

	return true;
}
	
void Handler::disconnect()
{
	if (!connected)
		return;

	mosquitto_disconnect(client);
	connected = false;
	lastConnectTry = 0;

	logger.info() << "Disconnected from MQTT broker " << config.getHostname() << ":" << config.getPort() << endOfMsg();
}

long Handler::collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd)
{
	int socket = mosquitto_socket(client);
	if (socket >= 0)
	{
		FD_SET(socket, readFds);
		*maxFd = std::max(*maxFd, socket);
	}
	return mosquitto_want_write(client) ? 0 : -1;
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

//		if (errorCode == MOSQ_ERR_CONN_LOST)
//			disconnect();
	}
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
		logger.error() << ex.what() << endOfMsg();
	}

	disconnect();
	return Events();
}

Events Handler::receiveX(const Items& items)
{
	Events events;
	
	if (!connect(items))
		return events;

	int ec = mosquitto_loop(client, 0, 1);
	handleError("mosquitto_loop", ec);

	for (auto& msg : receivedMsgs)
	{
		int eventCount = events.size();

		for (auto bindingPair : config.getBindings())
		{
			auto& binding = bindingPair.second;

			if (binding.writeTopic == msg.topic)
				events.add(Event(id, binding.itemId, EventType::WRITE_REQ, msg.payload));
			else if (binding.readTopic == msg.topic)
				events.add(Event(id, binding.itemId, EventType::READ_REQ, Value()));
			else if (binding.stateTopics.find(msg.topic) != binding.stateTopics.end())
				events.add(Event(id, binding.itemId, EventType::STATE_IND, msg.payload));
		}

		string itemId;
		auto getItemId = [&] (TopicPattern topicPattern)
		{
			if (topicPattern.isNull())
				return false;
			itemId = topicPattern.getItemId(msg.topic);
			return itemId.length() > 0;
		};
		if (getItemId(config.getWriteTopicPattern()))
			events.add(Event(id, itemId, EventType::WRITE_REQ, msg.payload));
		else if (getItemId(config.getReadTopicPattern()))
			events.add(Event(id, itemId, EventType::READ_REQ, msg.payload));
//		else if (getItemId(config.getStateTopicPattern()))
//			events.add(Event(id, itemId, EventType::STATE_IND, msg.payload));

		if (eventCount == events.size() && config.getSubTopics().size() == 0)
			logger.warn() << "No item for topic " << msg.topic << endOfMsg();
	}

	receivedMsgs.clear();

	return events;
}

Events Handler::send(const Items& items, const Events& events)
{
	try
	{
		return sendX(items, events);
	}
	catch (const std::exception& ex)
	{
		logger.error() << ex.what() << endOfMsg();
	}

	disconnect();
	return Events();
}

Events Handler::sendX(const Items& items, const Events& events)
{
	if (!connected)
		return Events();

	int ec = mosquitto_loop(client, 0, 1);
	handleError("mosquitto_loop", ec);

	auto& bindings = config.getBindings();
	for (auto& event : events)
	{
		string itemId = event.getItemId();

		auto sendMessageWithPayload = [&] (string topic, bool retainFlag)
		{
			if (!event.getValue().isString())
				logger.error() << "Event value type is not STRING for item " << itemId << endOfMsg();
			else
				sendMessage(topic, event.getValue().getString(), config.getRetainFlag());
		};

		auto bindingPos = bindings.find(itemId);
		if (bindingPos != bindings.end())
		{
			auto& binding = bindingPos->second;

			if (event.getType() == EventType::STATE_IND)
				for (string stateTopic : binding.stateTopics)
					sendMessageWithPayload(stateTopic, config.getRetainFlag());
			else if (event.getType() == EventType::WRITE_REQ)
			{
				if (binding.writeTopic != "")
					sendMessageWithPayload(binding.writeTopic, false);
			}
			else if (event.getType() == EventType::READ_REQ)
			{
				if (binding.readTopic != "")
					sendMessage(binding.readTopic, "", false);
			}
		}
		else
		{
			if (event.getType() == EventType::STATE_IND)
			{
				if (!config.getStateTopicPattern().isNull())
					sendMessageWithPayload(config.getStateTopicPattern().createPubTopic(itemId), config.getRetainFlag());
			}
//			else if (event.getType() == EventType::WRITE_REQ)
//			{
//				if (!config.getWriteTopicPattern().isNull())
//					sendMessage(config.getWriteTopicPattern().createPubTopic(itemId), payload, false);
//			}
//			else if (event.getType() == EventType::READ_REQ)
//			{
//				if (!config.getReadTopicPattern().isNull())
//					sendMessage(config.getReadTopicPattern().createPubTopic(itemId), "", false);
//			}
		}
	}
	
	return Events();
}

void Handler::sendMessage(string topic, string payload, bool retainFlag)
{
	if (config.getLogMsgs())
		logger.debug() << "S " << topic << ": " << payload << endOfMsg();

	int ec = mosquitto_publish(client, 0, topic.c_str(), payload.length(), 
		reinterpret_cast<const uint8_t*>(payload.data()), 0, retainFlag);
	handleError("mosquitto_publish", ec);
}

}
