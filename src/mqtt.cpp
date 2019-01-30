#include <sstream>
#include <cstring>
#include <stdexcept>

#include "mqtt.h"
#include "finally.h"

void onMqttMessage(struct mosquitto* client, void* handler, const struct mosquitto_message* msg)
{ 
	static_cast<MqttHandler*>(handler)->onMessage(msg);
}

MqttHandler::MqttHandler(string _id, MqttConfig _config, Logger _logger) : 
	id(_id), config(_config), logger(_logger), client(0), connected(false), lastConnectTry(0)
{
	mosquitto_lib_init();
	client = mosquitto_new(config.getClientId().c_str(), true, this);
	if (!client)
		logger.errorX() << "Function mosquitto_new() returned null" << endOfMsg();
}
	
MqttHandler::~MqttHandler()
{
	disconnect();
	
	mosquitto_destroy(client);
	mosquitto_lib_cleanup();
}

bool MqttHandler::connect()
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

	logger.info() << "Connected to broker " << config.getHostname() << ":" << config.getPort() << endOfMsg();

	mosquitto_message_callback_set(client, onMqttMessage);

	for (auto& pair : config.getBindings())
	{
		auto& binding = pair.second;

		if (binding.stateTopic != "" && binding.owner)
		{
			ec = mosquitto_subscribe(client, 0, binding.stateTopic.c_str(), 0);
			handleError("mosquitto_subscribe", ec);
		}
		if (binding.writeTopic != "" && !binding.owner)
		{
			ec = mosquitto_subscribe(client, 0, binding.writeTopic.c_str(), 0);
			handleError("mosquitto_subscribe", ec);
		}
		if (binding.readTopic != "" && !binding.owner)
		{
			ec = mosquitto_subscribe(client, 0, binding.readTopic.c_str(), 0);
			handleError("mosquitto_subscribe", ec);
		}
	}
	
	return true;
}
	
void MqttHandler::disconnect()
{
	if (!connected)
		return;

	mosquitto_disconnect(client);
	connected = false;
	lastConnectTry = 0;

	logger.info() << "Disconnected from broker " << config.getHostname() << ":" << config.getPort() << endOfMsg();
}

void MqttHandler::handleError(string funcName, int errorCode)
{
	if (errorCode != MOSQ_ERR_SUCCESS)
	{
		LogStream stream = logger.errorX();
		stream << "Function " << funcName << "() returned error " << errorCode << " (" << mosquitto_strerror(errorCode) << ")";
		if (errorCode == MOSQ_ERR_ERRNO)
			stream << " due to system error " << errno << " (" << strerror(errno) << ")";
		stream << endOfMsg();

//		if (errorCode == MOSQ_ERR_CONN_LOST)
//			disconnect();
	}
}

void MqttHandler::onMessage(const mosquitto_message* msg)
{
	for (auto& pair : config.getBindings())
	{
		auto& binding = pair.second;
		
		string data = string(static_cast<char*>(msg->payload), msg->payloadlen);
		if (binding.stateTopic == msg->topic && binding.owner)
		{
			receivedEvents.add(Event(id, binding.itemId, Event::STATE_IND, data));
			return;
		}
		if (binding.writeTopic == msg->topic && !binding.owner)
		{
			receivedEvents.add(Event(id, binding.itemId, Event::WRITE_REQ, data));
			return;
		}
		if (binding.readTopic == msg->topic && !binding.owner)
		{
			receivedEvents.add(Event(id, binding.itemId, Event::READ_REQ, data));
			return;
		}
	}
	
	logger.warn() << "No item for topic " << msg->topic << endOfMsg();
}

Events MqttHandler::receive()
{
	try
	{
		return receiveX();
	}
	catch (const std::exception& ex)
	{
		logger.error() << ex.what() << endOfMsg();
	}
	disconnect();
}
	
Events MqttHandler::receiveX()
{
	Events events;
	
	if (!connect())
		return events;

	int ec = mosquitto_loop(client, 0, 1);
	handleError("mosquitto_loop", ec);

	events = receivedEvents;
	receivedEvents.clear();
	
	return events;
}

void MqttHandler::send(const Events& events)
{
	if (!connected)
		return;

	try
	{
		return sendX(events);
	}
	catch (const std::exception& ex)
	{
		logger.error() << ex.what() << endOfMsg();
	}
	disconnect();
}

void MqttHandler::sendX(const Events& events)
{
	int ec = mosquitto_loop(client, 0, 1);
	handleError("mosquitto_loop", ec);

	auto& bindings = config.getBindings();
	for (auto& event : events)
	{
		string itemId = event.getItemId();
		string value = event.getValue();

		auto bindingIter = bindings.find(itemId);
		if (bindingIter != bindings.end())
		{
			auto& binding = bindingIter->second;

			ec = MOSQ_ERR_SUCCESS;
			if (binding.stateTopic != "" && !binding.owner && event.getType() == Event::STATE_IND)
				ec = mosquitto_publish(client, 0, binding.stateTopic.c_str(), value.length(), 
					reinterpret_cast<const uint8_t*>(value.data()), 0, config.getRetainFlag());
			if (binding.writeTopic != "" && binding.owner && event.getType() == Event::WRITE_REQ)
				ec = mosquitto_publish(client, 0, binding.writeTopic.c_str(), value.length(), 
					reinterpret_cast<const uint8_t*>(value.data()), 0, false);
			if (binding.readTopic != "" && binding.owner && event.getType() == Event::READ_REQ)
				ec = mosquitto_publish(client, 0, binding.readTopic.c_str(), value.length(), 
					reinterpret_cast<const uint8_t*>(value.data()), 0, false);
			handleError("mosquitto_publish", ec);
		}
	}
}


