#include <sstream>
#include <cstring>
#include <stdexcept>
#include <unistd.h>
	 
#include "mqtt.h"
#include "finally.h"

void onMqttMessage(struct mosquitto* client, void* handler, const struct mosquitto_message* msg)
{ 
	//static_cast<MqttHandler*>(handler)->onMessage(msg);
	static_cast<MqttHandler*>(handler)->onMessage(MqttHandler::Msg(msg->topic, string(static_cast<char*>(msg->payload), msg->payloadlen)));
}

MqttHandler::MqttHandler(string _id, MqttConfig _config, Logger _logger) : 
	id(_id), config(_config), logger(_logger), client(0), connected(false), lastConnectTry(0)
{
	mosquitto_lib_init();
	client = mosquitto_new((config.getClientIdPrefix() + "." + cnvToStr(getpid())).c_str(), true, this);
	if (!client)
		logger.errorX() << "Function mosquitto_new() returned null" << endOfMsg();
	
	int major, minor, revision;
	mosquitto_lib_version(&major, &minor, &revision);
	logger.info() << "Mosquitto library has version " << major << "." << minor << "." << revision << endOfMsg();
}
	
MqttHandler::~MqttHandler()
{
	disconnect();
	
	mosquitto_destroy(client);
	mosquitto_lib_cleanup();
}

bool MqttHandler::connect(const Items& items)
{
	if (connected)
		return true;
	
	std::time_t now = std::time(0);
	if (lastConnectTry + config.getReconnectInterval() > now)
		return false;
	lastConnectTry = now;

	int ec = mosquitto_connect(client, config.getHostname().c_str(), config.getPort(), 60);
	handleError("mosquitto_connect", ec);
	connected = true;

	logger.info() << "Connected to MQTT broker " << config.getHostname() << ":" << config.getPort() << endOfMsg();

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

	return true;
}
	
void MqttHandler::disconnect()
{
	if (!connected)
		return;

	mosquitto_disconnect(client);
	connected = false;
	//lastConnectTry = 0;

	logger.info() << "Disconnected from MQTT broker " << config.getHostname() << ":" << config.getPort() << endOfMsg();
}

void MqttHandler::handleError(string funcName, int errorCode)
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

void MqttHandler::onMessage(const Msg& msg)
{
	if (config.getLogMsgs())
		logger.debug() << "R " << msg.topic << ": " << msg.payload << endOfMsg();

	receivedMsgs.push_back(msg);
}

Events MqttHandler::receive(const Items& items)
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
}
	
Events MqttHandler::receiveX(const Items& items)
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
			bool owner = items.getOwnerId(binding.itemId) == id;

			if (binding.writeTopic == msg.topic && !owner)
				events.add(Event(id, binding.itemId, EventType::WRITE_REQ, msg.payload));
			else if (binding.readTopic == msg.topic && !owner)
				events.add(Event(id, binding.itemId, EventType::READ_REQ, Value()));
			else if (binding.stateTopics.find(msg.topic) != binding.stateTopics.end() && owner)
				events.add(Event(id, binding.itemId, EventType::STATE_IND, msg.payload));
		}

		if (eventCount == events.size() && config.getSubTopics().size() == 0)
			logger.warn() << "No item for topic " << msg.topic << endOfMsg();
	}

	receivedMsgs.clear();
	
	return events;
}

Events MqttHandler::send(const Items& items, const Events& events)
{
	if (!connected)
		return Events();

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

Events MqttHandler::sendX(const Items& items, const Events& events)
{
	int ec = mosquitto_loop(client, 0, 1);
	handleError("mosquitto_loop", ec);

	auto& bindings = config.getBindings();
	for (auto& event : events)
	{
		string itemId = event.getItemId();

		auto bindingPos = bindings.find(itemId);
		if (bindingPos != bindings.end())
		{
			auto& binding = bindingPos->second;
			bool owner = items.getOwnerId(itemId) == id;
			string payload = event.getValue().toStr();

			ec = MOSQ_ERR_SUCCESS;
			if (!owner && event.getType() == EventType::STATE_IND)
				for (string stateTopic : binding.stateTopics)
					sendMessage(stateTopic, payload, config.getRetainFlag());
			if (binding.writeTopic != "" && owner && event.getType() == EventType::WRITE_REQ)
				sendMessage(binding.writeTopic, payload, false);
			if (binding.readTopic != "" && owner && event.getType() == EventType::READ_REQ)
				sendMessage(binding.readTopic, "", false);
		}
	}
	
	return Events();
}

void MqttHandler::sendMessage(string topic, string payload, bool retain)
{
	if (config.getLogMsgs())
		logger.debug() << "S " << topic << ": " << payload << endOfMsg();

	int ec = mosquitto_publish(client, 0, topic.c_str(), payload.length(), 
		reinterpret_cast<const uint8_t*>(payload.data()), 0, retain);
	handleError("mosquitto_publish", ec);
}
