#include <arpa/inet.h>
#include <unistd.h>

#include <stdexcept>
#include <iomanip>
#include <cstdint>
#include <limits>

#include "knx.h"
#include "finally.h"

string IpAddr::toStr() const
{
	in_addr addr;
	addr.s_addr = htonl(value);
	return inet_ntoa(addr);
}

bool IpAddr::fromStr(string ipStr, IpAddr& ip)
{
	ip = ntohl(inet_addr(ipStr.c_str()));
	return ip != INADDR_NONE;
}

string ServiceType::toStr() const
{
	switch (value)
	{
		case CONN_REQ:
			return "CONN_REQ";
		case CONN_RESP:
			return "CONN_RESP";
		case CONN_STATE_REQ:
			return "CONN_STATE_REQ";
		case CONN_STATE_RESP:
			return "CONN_STATE_RESP";
		case DISC_REQ:
			return "DISC_REQ";
		case DISC_RESP:
			return "DISC_RESP";
		case TUNNEL_REQ:
			return "TUNNEL_REQ";
		case TUNNEL_ACK:
			return "TUNNEL_ACK";
		default:
			return "?0x" + cnvToHexStr(value) + "?";
	}
}

string MsgCode::toStr() const
{
	if (value == MsgCode::LDATA_IND)
		return "L_Data.ind";
	else if (value == MsgCode::LDATA_CON)
		return "L_Data.con";
	else if (value == MsgCode::LDATA_REQ)
		return "L_Data.req";
	else
		return "?0x" + cnvToHexStr(value) + "?";
}

const DatapointType DPT_1_001(1, 1, "on/off");
const DatapointType DPT_5_001(5, 1, "%");
const DatapointType DPT_5_004(5, 4, "%");
const DatapointType DPT_7_013(7, 13, "lux");
const DatapointType DPT_9_001(9, 1, "C");
const DatapointType DPT_9_004(9, 4, "lux");
const DatapointType DPT_13_013(13, 13, "kWh");
const DatapointType DPT_14_056(13, 13, "W");

string DatapointType::toStr() const
{
	std::ostringstream stream;
	stream << mainNo << '.' << std::setw(3) << std::setfill('0') << subNo;
	return stream.str();
}

bool DatapointType::fromStr(string dptStr, DatapointType& dpt)
{
	try
	{
		string::size_type pos = dptStr.find('.');
		if (pos == 0 || pos == string::npos)
			return false;
	
		int mainNo = std::stoi(dptStr.substr(0, pos));
		if (mainNo < 0 || mainNo > 999)
			return false;
		int subNo = std::stoi(dptStr.substr(pos + 1));
		if (subNo < 0 || subNo > 999)
			return false;
		
		dpt = DatapointType(mainNo, subNo, "?");
		
		return true;
	}
	catch (const std::invalid_argument&)
	{
		return false;
	}
}

ByteString DatapointType::exportValue(const Value& value) const
{
	if (!value.isBoolean() && !value.isVoid() && !value.isNumber())
		return ByteString();
		
	if (mainNo == 1)
	{
		bool b = value.isBoolean() ? value.getBoolean() : (value.isNumber() ? value.getNumber() > 0 : true);
		return b ? ByteString({0x01}) : ByteString({0x00});
	}
	else
	{
		double d = value.isBoolean() ? value.getBoolean() : (value.isNumber() ? value.getNumber() : 1);
		if (mainNo == 5 && subNo == 1)
		{
			if (d >= 0 && d <= 100)
			{
				uint8_t b = d * 255.0 / 100.0;
				return ByteString({0x00, b});
			}
		}
		else if (mainNo == 5)
		{
			if (d >= 0 && d <= 255)
			{
				uint8_t i = d;
				return ByteString({0x00, i});
			}
		}
		else if (mainNo == 7)
		{
			if (d >= 0 && d <= 65535)
			{
				uint16_t i = d;
				Byte bytes[3];
				bytes[0] = 0x00;
				bytes[1] = (i >> 8) & 0xFF;
				bytes[2] = i & 0xFF;
				return ByteString(bytes, sizeof(bytes));
			}
		}
		else if (mainNo == 9)
		{
//			uint32_t E = 0;
//			while ((d < -20.48 || d > 20.47) && E <= 15) { d = d / 2.0; E++; }
//			if (d >= -20.48 && d <= 20.47)
//			{
//				uint32_t M = d * 100.0;
//				cout << "PEW: " << M << endl;
//				Byte bytes[3];
//				bytes[0] = 0x00;
//				bytes[1] = ((M >> 24) & 0x80) | (E << 3) | ((M >> 8) & 0x07);
//				bytes[2] = M & 0xFF;
//				return ByteString(bytes, sizeof(bytes));
//			}
			int32_t E = 0;
			int32_t M = d * 100.0;
			while ((M < -2048 || M > 2047) && E <= 15) { M >>= 1; E++; }
			if (M >= -2048 && M <= 2047)
			{
				Byte bytes[3];
				bytes[0] = 0x00;
				bytes[1] = ((M >> 24) & 0x80) | (E << 3) | ((M >> 8) & 0x07);
				bytes[2] = M & 0xFF;
				return ByteString(bytes, sizeof(bytes));
			}
		}
		else if (mainNo == 12 || mainNo == 13)
		{
			uint32_t i = d;
			Byte bytes[5];
			bytes[0] = 0x00;
			bytes[1] = (i >> 24) & 0xFF;
			bytes[2] = (i >> 16) & 0xFF;
			bytes[3] = (i >> 8) & 0xFF;
			bytes[4] = i & 0xFF;
			return ByteString(bytes, sizeof(bytes));
		}
		else if (mainNo == 14)
		{
			static_assert(sizeof(uint32_t) == sizeof(float), "uint32_t and float do not have the same size");
			static_assert(std::numeric_limits<float>::is_iec559, "float is not IEEE-754 encoded");

			union { float f; uint32_t i; } u; 
			u.f = d;
			Byte bytes[5];
			bytes[0] = 0x00;
			bytes[1] = (u.i >> 24) & 0xFF;
			bytes[2] = (u.i >> 16) & 0xFF;
			bytes[3] = (u.i >> 8) & 0xFF;
			bytes[4] = u.i & 0xFF;
			return ByteString(bytes, sizeof(bytes));
		}
		else if (mainNo == 17)
		{
			if (d >= 0 && d <= 63)
			{
				uint8_t i = d;
				return ByteString({0x00, i});
			}
		}
		else if (mainNo == 20)
		{
			uint8_t i = d;
			return ByteString({0x00, i});
		}
	}
	return ByteString();
}

Value DatapointType::importValue(ByteString bytes) const
{
	if (mainNo == 1 && bytes.length() == 1)
		return Value::newBoolean((bytes[0] & 0x01) == 0x01);
	else if (mainNo == 5 && subNo == 1 && bytes.length() == 2)
		return Value::newNumber(bytes[1] * 100.0 / 255.0);
	else if (mainNo == 5 && bytes.length() == 2)
		return Value::newNumber(1.0 * bytes[1]);
	else if (mainNo == 7 && bytes.length() == 3)
		return Value::newNumber(1.0 * (bytes[1] << 8 | bytes[2]));
	else if (mainNo == 9)
	{
		int32_t E = (bytes[1] >> 3) & 0x0F;
		int32_t M = (bytes[1] & 0x07) << 8 | bytes[2];
		if (bytes[1] & 0x80)
			return Value::newNumber(((2048 - M) << E) / -100.0);
		else
			return Value::newNumber((M << E) / 100.0);
	}
	else if (mainNo == 12 && bytes.length() == 5)
		return Value::newNumber(1.0 * (bytes[1] << 24 | bytes[2] << 16 | bytes[3] << 8 | bytes[4]));
	else if (mainNo == 13 && bytes.length() == 5)
		return Value::newNumber(1.0 * (bytes[1] << 24 | bytes[2] << 16 | bytes[3] << 8 | bytes[4]));
	else if (mainNo == 14 && bytes.length() == 5)
	{
		// assumption: float is encoded in IEEE 754 floating point format
		assert(sizeof(uint32_t) == sizeof(float));
		union { float f; uint32_t i; } u; 
		u.i = bytes[1] << 24 | bytes[2] << 16 | bytes[3] << 8 | bytes[4];
		return Value::newNumber(u.f);
	}
	else if (mainNo == 16)
	{
		auto pos = bytes.find_last_not_of('\0');
		if (pos != ByteString::npos)
			return Value::newString(cnvToAsciiStr(bytes.substr(1, pos)));
	}
	else if (mainNo == 17 && bytes.length() == 2)
		return Value::newNumber(1.0 * bytes[1]);
	else if (mainNo == 20 && bytes.length() == 2)
		return Value::newNumber(1.0 * bytes[1]);
	return Value();
}

string GroupAddr::toStr() const
{
	if (null)
		return "null";
	else
		return cnvToStr(value >> 11) + "/" + cnvToStr((value >> 8) & 0x07) + "/" + cnvToStr(value & 0xFF);
}

bool GroupAddr::fromStr(string gaStr, GroupAddr& ga)
{
	try
	{
		string::size_type pos1 = gaStr.find('/');
		if (pos1 == 0 || pos1 == string::npos)
			return false;
		string::size_type pos2 = gaStr.find('/', pos1 + 1);
		if (pos2 == pos1 + 1 || pos2 == gaStr.length() - 1 || pos2 == string::npos)
			return false;
	
		int main = std::stoi(gaStr.substr(0, pos1));
		if (main < 0 || main > 31)
			return false;
		int middle = std::stoi(gaStr.substr(pos1 + 1, pos2 - pos1));
		if (middle < 0 || middle > 7)
			return false;
		int sub = std::stoi(gaStr.substr(pos2 + 1));
		if (sub < 0 || sub > 255)
			return false;

		ga = GroupAddr(main, middle, sub);
		
		return true;
	}
	catch (const std::invalid_argument&)
	{
		return false;
	}
}

string PhysicalAddr::toStr() const
{
	return cnvToStr(value >> 12) + "." + cnvToStr(value >> 8 & 0x0F) + "." + cnvToStr(value & 0xFF);
}

bool PhysicalAddr::fromStr(string paStr, PhysicalAddr& pa)
{
	try
	{
		string::size_type pos1 = paStr.find('.');
		if (pos1 == 0 || pos1 == string::npos)
			return false;
		string::size_type pos2 = paStr.find('.', pos1 + 1);
		if (pos2 == pos1 + 1 || pos2 == paStr.length() - 1 || pos2 == string::npos)
			return false;
	
		int area = std::stoi(paStr.substr(0, pos1));
		if (area < 0 || area > 15)
			return false;
		int line = std::stoi(paStr.substr(pos1 + 1, pos2 - pos1));
		if (line < 0 || line > 15)
			return false;
		int device = std::stoi(paStr.substr(pos2 + 1));
		if (device < 0 || device > 255)
			return false;

		pa = PhysicalAddr(area, line, device);
		
		return true;
	}
	catch (const std::invalid_argument&)
	{
		return false;
	}
}

KnxHandler::KnxHandler(string _id, KnxConfig _config, Logger _logger) : 
	id(_id), config(_config), logger(_logger), state(DISCONNECTED)
{
	handlerState.errorCounter = 0;
}

KnxHandler::~KnxHandler() 
{ 
	disconnect();
}

void KnxHandler::validate(Items& items)
{
	auto& bindings = config.getBindings();

	for (auto& [itemId, item] : items)
		if (item.getOwnerId() == id && !bindings.count(itemId))
			throw std::runtime_error("Item " + itemId + " has no binding for link " + id);

	for (auto& [itemId, binding] : bindings)
	{
		auto& item = items.validate(itemId);
		if (item.getOwnerId() == id)
			item.setWritable(!binding.writeGa.isNull());
	}
}

void KnxHandler::close()
{
	if (state == DISCONNECTED)
		return;

	if (state == CONNECTED)
	{
		lastConnectTry.setToNull();

		logger.info() << "Disconnected from KNX/IP gateway " << config.getIpAddr().toStr() << ":" << config.getIpPort() << endOfMsg();
	}
	else
		lastConnectTry = Clock::now();

	::close(socket);
	state = DISCONNECTED;
}

void KnxHandler::disconnect()
{
	if (state == CONNECTED)
		sendControlMsg(createDiscReq());

	close();
}

long KnxHandler::collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd)
{
	if (state != DISCONNECTED)
	{
		FD_SET(socket, readFds);
		*maxFd = std::max(*maxFd, socket);
	}

	return -1;
}

Events KnxHandler::receive(const Items& items)
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

Events KnxHandler::receiveX(const Items& items)
{
	TimePoint now = Clock::now();

	Events events;

	if (state == DISCONNECTED)
	{
		if (lastConnectTry + config.getReconnectInterval() > now)
			return events;
		lastConnectTry = now;

		socket = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
		if (socket == -1)
			logger.errorX() << unixError("socket") << endOfMsg();
		auto autoClose = finally([this] { ::close(socket); state = DISCONNECTED; });

		sockaddr_in localAddr;
		localAddr.sin_family = AF_INET;
		localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
		localAddr.sin_port = 0;
		int rc = bind(socket, reinterpret_cast<sockaddr*>(&localAddr), sizeof(localAddr));
		if (rc == -1)
			logger.errorX() << unixError("bind") << endOfMsg();

		socklen_t localAddrLen = sizeof(localAddr);
		rc = getsockname(socket, reinterpret_cast<sockaddr*>(&localAddr), &localAddrLen);
		if (rc == -1)
			logger.errorX() << unixError("getsockname") << endOfMsg();
		localIpPort = ntohs(localAddr.sin_port);

		logger.debug() << "Using port " << localIpPort << " as local control and data endpoint " << endOfMsg();
		if (config.getNatMode())
			logger.debug() << "Using NAT mode" << endOfMsg();

		sendControlMsg(createConnReq());

		state = WAIT_FOR_CONN_RESP;
		lastControlReqSendTime = now;
		autoClose.disable();
	}
	else if (state == CONNECTED)
	{
		if (ongoingConnStateReq && lastControlReqSendTime + config.getControlRespTimeout() <= now)
			logger.errorX() << "CONNECTION STATE REQUEST not answered in time" << endOfMsg();

		if (!ongoingConnStateReq && lastControlReqSendTime + config.getConnStateReqInterval() <= now)
		{
			lastControlReqSendTime = now;
			ongoingConnStateReq = true;
			sendControlMsg(createConnStateReq());
		}

		processPendingTunnelAck();
		processPendingLDataCons();
	}
	else if (state == WAIT_FOR_CONN_RESP)
	{
		if (lastControlReqSendTime + config.getControlRespTimeout() <= now)
			logger.errorX() << "CONNECTION REQUEST not answered in time" << endOfMsg();
	}

	ByteString msg;
	IpAddr senderIpAddr;
	IpPort senderIpPort;
	while (state != DISCONNECTED && receiveMsg(msg, senderIpAddr, senderIpPort))
	{
		checkMsg(msg);

		ServiceType serviceType(msg[2], msg[3]);
		if (state == CONNECTED && serviceType == ServiceType::TUNNEL_REQ)
		{
			checkTunnelReq(msg);
			logTunnelReq(msg, true);

			Byte seqNo = msg[8];
			Byte expectedSeqNo = (lastReceivedSeqNo + 1) & 0xFF;
			if (seqNo == lastReceivedSeqNo)
			{
				logger.warn() << "Received TUNNEL REQUEST has last sequence number 0x" << cnvToHexStr(seqNo)
				              << " (expected: 0x" << cnvToHexStr(expectedSeqNo) << ")" << endOfMsg();

				sendDataMsg(createTunnelAck(seqNo));
				continue;
			}
			if (seqNo != expectedSeqNo)
			{
				logger.warn() << "Received TUNNEL REQUEST has invalid sequence number 0x" << cnvToHexStr(seqNo)
				              << " (expected: 0x" << cnvToHexStr(expectedSeqNo) << ")" << endOfMsg();

//				lastReceivedSeqNo = seqNo;
				continue;
			}
			lastReceivedSeqNo = seqNo;
			sendDataMsg(createTunnelAck(seqNo));

			MsgCode msgCode = msg[10];
			if (msgCode == MsgCode::LDATA_IND)
				processReceivedLDataInd(msg, items, events);
			else if (msgCode == MsgCode::LDATA_CON)
				processReceivedLDataCon(msg);
			else
				logger.warn() << "Received TUNNEL REQUEST has unknown message code 0x" + cnvToHexStr(msgCode) << endOfMsg();
		}
		else if (state == CONNECTED && serviceType == ServiceType::TUNNEL_ACK)
		{
			checkTunnelAck(msg);

			processReceivedTunnelAck(msg);
		}
		else if (state == CONNECTED && serviceType == ServiceType::CONN_STATE_RESP && ongoingConnStateReq)
		{
			checkConnStateResp(msg, channelId);

			ongoingConnStateReq = false;
		}
		else if (state == WAIT_FOR_CONN_RESP && serviceType == ServiceType::CONN_RESP)
		{
			checkConnResp(msg);

			channelId = msg[6];
			dataIpAddr = IpAddr(msg[10], msg[11], msg[12], msg[13]);
			dataIpPort = IpPort(msg[14] << 8 | msg[15]);
			if (config.getNatMode() && (dataIpAddr == 0 || dataIpPort == 0))
			{
				dataIpPort = senderIpPort;
				dataIpAddr = senderIpAddr;
			}
			if (msg[18] != 0 || msg[19] != 0)
				physicalAddr = PhysicalAddr(msg[18], msg[19]);
			else
				physicalAddr = config.getPhysicalAddr();

			state = CONNECTED;
			ongoingConnStateReq = false;
			waitingLDataReqs.clear();
			sentLDataReqs.clear();
			lastReceivedSeqNo = 0xFF;
			lastSentSeqNo = 0xFF;
			lastTunnelReqSendTime.setToNull();
			receivedReadReqs.clear();

			logger.debug() << "Using channel 0x" << cnvToHexStr(channelId) << endOfMsg();
			logger.debug() << "Using " << dataIpAddr.toStr() << ":" << dataIpPort << " as remote data endpoint" << endOfMsg();
			logger.info() << "Connected to KNX/IP gateway " << config.getIpAddr().toStr() << ":" << config.getIpPort()
			              << " with physical address " << physicalAddr.toStr() << endOfMsg();
		}
		else if (state == CONNECTED && serviceType == ServiceType::DISC_REQ)
		{
			logger.error() << "Received DISCONNECT REQUEST" << endOfMsg();

			sendControlMsg(createDiscResp());
			handlerState.errorCounter++;
			close();
		}
		else
			logger.warn() << "Received unexpected message with service type " << serviceType.toStr() << endOfMsg();
	}

	if (state == CONNECTED)
		processWaitingLDataReqs();

	return events;
}

Events KnxHandler::send(const Items& items, const Events& events)
{
	if (state != CONNECTED)
		return Events();

	try
	{
		return sendX(items, events);
	}
	catch (const std::exception& ex)
	{
		handlerState.errorCounter++;

		logger.error() << ex.what() << endOfMsg();
	}

	disconnect();
	return Events();
}

Events KnxHandler::sendX(const Items& items, const Events& events)
{
	auto& bindings = config.getBindings();

	for (auto& event : events)
	{
		string itemId = event.getItemId();

		auto bindingPos = bindings.find(itemId);
		if (bindingPos != bindings.end())
		{
			auto& binding = bindingPos->second;
			bool owner = items.getOwnerId(itemId) == id;
			const Value& value = event.getValue();

			// create data/APDU for L_Data.req
			ByteString data;
			if (event.getType() == EventType::READ_REQ)
				data = ByteString({0x00});
			else
			{
				assert(event.getType() == EventType::WRITE_REQ || event.getType() == EventType::STATE_IND);

				data = binding.dpt.exportValue(value);
				if (!data.length())
				{
					logger.error() << "Event value '" << value.toStr()
					               << "' (type " << value.getType().toStr() << ") of item " << itemId
					               << " can not be converted to DPT " << binding.dpt.toStr() << endOfMsg();
					continue;
				}
				if (event.getType() == EventType::WRITE_REQ)
					data[0] |= 0x80;
				else // STATE_IND
				{
					auto pos = receivedReadReqs.find(event.getItemId());
					if (pos != receivedReadReqs.end())
					{
						data[0] |= 0x40;
						receivedReadReqs.erase(pos);
					}
					else
						data[0] |= 0x80;
				}
			}

			// send L-Data.req
			if (event.getType() == EventType::READ_REQ && owner)
			{
				if (!binding.stateGa.isNull())
					waitingLDataReqs.emplace_back(itemId, binding.stateGa, data);
				else if (!binding.writeGa.isNull())
					waitingLDataReqs.emplace_back(itemId, binding.writeGa, data);
			}
			else if (event.getType() == EventType::STATE_IND && !owner && !binding.stateGa.isNull())
				waitingLDataReqs.emplace_back(itemId, binding.stateGa, data);
			else if (event.getType() == EventType::WRITE_REQ && owner && !binding.writeGa.isNull())
				waitingLDataReqs.emplace_back(itemId, binding.writeGa, data);
		}
	}

	processWaitingLDataReqs();

	return Events();
}

void KnxHandler::sendTunnelReq(const LDataReq& ldataReq, Byte seqNo)
{
	ByteString msg = createTunnelReq(seqNo, ldataReq.ga, ldataReq.data);
	sendDataMsg(msg);
	logTunnelReq(msg, false);
	lastTunnelReqSendTime = Clock::now();
}

void KnxHandler::sendLDataReq(const LDataReq& ldataReq)
{
	lastSentSeqNo = (lastSentSeqNo + 1) & 0xFF;
	lastSentLDataReq = ldataReq;
	lastTunnelReqSendAttempts = 0;
	sendTunnelReq(lastSentLDataReq, lastSentSeqNo);
}

void KnxHandler::processReceivedLDataInd(ByteString msg, const Items& items, Events& events)
{
	GroupAddr ga(msg[16], msg[17]);
	ByteString data = msg.substr(20, msg[18]);

	for (auto& bindingPair : config.getBindings())
	{
		auto& binding = bindingPair.second;
		bool owner = items.getOwnerId(binding.itemId) == id;

		if (ga == binding.stateGa || ga == binding.writeGa)
			if (data.length() == 1 && (data[0] & 0xC0) == 0x00)
			{
				if (!owner)
				{
					events.add(Event(id, binding.itemId, EventType::READ_REQ, Value()));
					receivedReadReqs.insert(binding.itemId);
				}
			}
			else
			{
				Value value = binding.dpt.importValue(data);
				if (value.isNull())
					logger.error() << "Unable to convert DPT " << binding.dpt.toStr() << " data '" << cnvToHexStr(data)
					               << "' to value for item " << binding.itemId << endOfMsg();
				else
					if (ga == binding.stateGa && owner)
						events.add(Event(id, binding.itemId, EventType::STATE_IND, value));
					else if (ga == binding.writeGa && !owner)
						events.add(Event(id, binding.itemId, EventType::WRITE_REQ, value));
			}
	}
}

void KnxHandler::processReceivedLDataCon(ByteString msg)
{
	GroupAddr ga(msg[16], msg[17]);
	ByteString data = msg.substr(20, msg[18]);

	for (auto pos = sentLDataReqs.begin(); pos != sentLDataReqs.end(); pos++)
		if (pos->ldataReq.ga == ga && pos->ldataReq.data == data)
		{
			sentLDataReqs.erase(pos);
			return;
		}

	logger.warn() << "Unexpected L_Data.con for GA " << ga.toStr()
	              << " received (Item " << getItemId(ga) << ")" << endOfMsg();
}

void KnxHandler::processReceivedTunnelAck(ByteString msg)
{
	if (!lastTunnelReqSendTime.isNull() && lastSentSeqNo == msg[8])
	{
		sentLDataReqs.emplace_back(lastSentLDataReq, Clock::now());
		lastTunnelReqSendTime.setToNull();
		return;
	}

	logger.warn() << "Received unexpected TUNNEL ACQ with sequence number 0x"
	              << cnvToHexStr(msg[8]) << endOfMsg();
}

void KnxHandler::processPendingTunnelAck()
{
	if (lastTunnelReqSendTime.isNull())
		return;

	if (lastTunnelReqSendTime + config.getTunnelAckTimeout() > Clock::now())
		return;

	if (lastTunnelReqSendAttempts > 0)
		logger.errorX() << "Second TUNNEL REQUEST with sequence number 0x" << cnvToHexStr(lastSentSeqNo)
		                << " for GA " << lastSentLDataReq.ga.toStr()
		                << " was not acknowledged in time (Item " << lastSentLDataReq.itemId << ")" << endOfMsg();


	logger.warn() << "First TUNNEL REQUEST with sequence number 0x" << cnvToHexStr(lastSentSeqNo)
	              << " for GA " << lastSentLDataReq.ga.toStr()
	              << " was not acknowledged in time (Item " << lastSentLDataReq.itemId << ")" << endOfMsg();

	sendTunnelReq(lastSentLDataReq, lastSentSeqNo);
	lastTunnelReqSendAttempts++;
}

void KnxHandler::processPendingLDataCons()
{
	TimePoint now = Clock::now();

	for (auto pos = sentLDataReqs.begin(); pos != sentLDataReqs.end();)
		if (pos->time + config.getLDataConTimeout() <= now)
		{
			LDataReq& ldataReq = pos->ldataReq;

			if (ldataReq.attempts == 0)
			{
				ldataReq.attempts++;
				waitingLDataReqs.push_front(ldataReq);

				logger.warn() << "First L_Data.req for GA " << ldataReq.ga.toStr()
				              << " was not confirmed in time (Item " << ldataReq.itemId << ")" << endOfMsg();
			}
			else
			{
				handlerState.errorCounter++;

				logger.error() << "Second L_Data.req for GA " << ldataReq.ga.toStr()
				               << " was not confirmed in time (Item " << ldataReq.itemId << ")" << endOfMsg();
			}
			pos = sentLDataReqs.erase(pos);
		}
		else
			pos++;
}

void KnxHandler::processWaitingLDataReqs()
{
	if (  state != CONNECTED
	   || !lastTunnelReqSendTime.isNull()
	   || sentLDataReqs.size() > 4
	   )
		return;

	for (auto pos1 = waitingLDataReqs.begin(); pos1 != waitingLDataReqs.end(); pos1++)
	{
		auto pos2 = sentLDataReqs.begin();
		while (pos2 != sentLDataReqs.end() && pos1->ga != pos2->ldataReq.ga)
			pos2++;
		if (pos2 == sentLDataReqs.end())
		{
			sendLDataReq(*pos1);
			waitingLDataReqs.erase(pos1);
			return;
		}
	}
}

bool KnxHandler::receiveMsg(ByteString& msg, IpAddr& addr, IpPort& port) const
{
	Byte buffer[1024];
	sockaddr_in sockAddr;
	socklen_t sockAddrLen = sizeof(sockAddr);
	int rc = ::recvfrom(socket, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&sockAddr), &sockAddrLen);
	if (rc == -1)
		if (errno == EWOULDBLOCK || errno == EAGAIN)
			return false;
		else
			logger.errorX() << unixError("recvFrom") << endOfMsg();
	if (rc == 0)
		logger.errorX() << "Message size 0 returned by recvFrom()" << endOfMsg();

	if (sockAddrLen != sizeof(sockAddr))
		logger.errorX() << "Address returned by recvFrom() has unexpected size" << endOfMsg();
	if (sockAddr.sin_family != AF_INET)
		logger.errorX() << "Address returned by recvFrom() does not belong to family AF_INET" << endOfMsg();
	addr = IpAddr(ntohl(sockAddr.sin_addr.s_addr));
	port = ntohs(sockAddr.sin_port);
	msg = ByteString(buffer, rc);

	logMsg(msg, true);

	return true;
}

void KnxHandler::sendMsg(IpAddr addr, IpPort port, ByteString msg) const
{
	sockaddr_in sockAddr;
	sockAddr.sin_family = AF_INET;
	sockAddr.sin_addr.s_addr = htonl(addr);
	sockAddr.sin_port = htons(port);
	int rc = ::sendto(socket, msg.data(), msg.length(), 0, reinterpret_cast<sockaddr*>(&sockAddr), sizeof(sockAddr));
	if (rc == -1)
		logger.errorX() << unixError("sendto") << endOfMsg();
	if (rc != msg.length())
		logger.errorX() << "Message size returned by sendTo() differs from the passed one" << endOfMsg();

	logMsg(msg, false);
}

void KnxHandler::sendControlMsg(ByteString msg) const
{
	sendMsg(config.getIpAddr(), config.getIpPort(), msg);
}

void KnxHandler::sendDataMsg(ByteString msg) const
{
	sendMsg(dataIpAddr, dataIpPort, msg);
}

ByteString addHeader(ServiceType type, ByteString body)
{
	Byte header[6];
	header[0] = 0x06; 								// Header length (6 bytes)
	header[1] = 0x10; 								// KNXnet/IP version 1.0 
	header[2] = type.high();						// Hi-byte Service type descriptor
	header[3] = type.low();							// Lo-byte Service type descriptor
	header[4] = (body.length() + 6) >> 8 & 0xFF;	// Hi-byte total length
	header[5] = (body.length() + 6) & 0xFF; 		// Lo-byte total length
	return ByteString(header, sizeof(header)) + body;
}

ByteString createHpai(IpAddr addr, IpPort port)
{
	Byte hpai[8];
	hpai[0] = 0x08; 				// Host protocol address information (HPAI) length 
	hpai[1] = 0x01;					// Host protocol code (0x01 = IPV4_UDP, 0x02 = IPV6_TCP
	hpai[2] = addr.highHigh();
	hpai[3] = addr.highLow();
	hpai[4] = addr.lowHigh();
	hpai[5] = addr.lowLow();
	hpai[6] = port >> 8 & 0xFF;		// Hi-byte port
	hpai[7] = port & 0xFF;			// Lo-byte port
	return ByteString(hpai, sizeof(hpai));
}

ByteString createCRI()
{
	Byte cri[4];
	cri[0] = 0x04;			// Length (4 bytes)
	cri[1] = 0x04;			// Tunnel Connection
	cri[2] = 0x02;			// KNX Layer (Tunnel Link Layer)
	cri[3] = 0x00;			// Reserved
	return ByteString(cri, sizeof(cri));
}

ByteString createTunnelHeader(Byte channelId, Byte seqNo)
{
	Byte header[4];
	header[0] = 0x04;		// Length
	header[1] = channelId;
	header[2] = seqNo;
	header[3] = 0x00;		// Reserved
	return ByteString(header, sizeof(header));
}

ByteString createLongHpai(Byte channelId, IpAddr addr, IpPort port)
{
	return ByteString({channelId}) + ByteString({0x00}) + createHpai(addr, port);
}

ByteString createCemiFrame(PhysicalAddr pa, GroupAddr ga, ByteString data)
{
	Byte frame[10];
	frame[0] = MsgCode::LDATA_REQ;	// Message code
	frame[1] = 0x00;				// Additional info length
	frame[2] = 0x8C;				// Control byte
	frame[3] = 0xE0;				// DRL byte
	frame[4] = pa.high();  			// Hi-byte physical address
	frame[5] = pa.low();			// Lo-byte physical address
	frame[6] = ga.high();			// Hi-byte group address
	frame[7] = ga.low();			// Lo-byte group address
	frame[8] = data.length();		// Data/APDU length 
	frame[9] = 0x00;				// APDU, Transport protocol control information (TPCI)
	//frame[10] = 0x80;               // APDU, Application protocol control information (APCI)
	return ByteString(frame, sizeof(frame)) + data;
}

void KnxHandler::checkMsg(ByteString msg) const
{
	if (msg.length() < 8)
		logger.errorX() << "Received message has length " << msg.length()
		                << " - Expected: >=8" << endOfMsg();
	ByteString header = msg.substr(0, 6);
	if (header[0] != 0x06)
		logger.errorX() << "Received message contains header length " << cnvToStr(int(header[0]))
		                << " - Expected: 6" << endOfMsg();
	if (header[1] != 0x10)
		logger.errorX() << "Received message has KNXnet/IP version 0x" << cnvToHexStr(header[1])
		                << " - Expected: 0x10" << endOfMsg();
	unsigned short totalLength = header[4] << 8 | header[5];
	if (totalLength != msg.length())
		logger.errorX() << "Received message contains total length " << cnvToStr(totalLength)
		                << " (actual length: " << msg.length() << ")" << endOfMsg();
}

void KnxHandler::checkTunnelReq(ByteString msg) const
{
	if (msg.length() < 20)
		logger.errorX() << "Received TUNNEL REQUEST has length " << msg.length()
		                << " - Expected: >=20" << endOfMsg();
	Byte receivedChannelId = msg[7];
	if (receivedChannelId != channelId)
		logger.errorX() << "Received TUNNEL REQUEST has channel id 0x" << cnvToHexStr(receivedChannelId)
		                << " - Expected: 0x" << cnvToHexStr(channelId) << endOfMsg();
}

void KnxHandler::checkTunnelAck(ByteString msg) const
{
	if (msg.length() != 10)
		logger.errorX() << "Received TUNNEL ACK has length " << msg.length()
		                << " - Expected: 10" << endOfMsg();
	if (msg[9] != 0x00)
		logger.errorX() << "Received TUNNEL ACK has status code 0x" << cnvToHexStr(msg[9])
		                << " (" << getStatusCodeText(msg[9]) << ") - Expected: 0x00" << endOfMsg();
}

void KnxHandler::checkConnResp(ByteString msg) const
{
	if (msg[7] != 0x00)
		logger.errorX() << "Received CONNECTION RESPONSE has status code 0x" << cnvToHexStr(msg[7])
		                << " (" << getStatusCodeText(msg[7]) << ") - Expected: 0x00" << endOfMsg();
	if (msg.length() != 20)
		logger.errorX() << "Received CONNECTION RESPONSE has length " << msg.length()
		                << " - Expected: 20" << endOfMsg();
	if (msg[8] != 0x08)
		logger.errorX() << "Received CONNECTION RESPONSE has HPAI length " << cnvToStr(int(msg[8]))
		                << " - Expected: 8" << endOfMsg();
	if (msg[9] != 0x01)
		logger.errorX() << "Received CONNECTION RESPONSE has protocol code 0x" << cnvToHexStr(msg[9])
		                << " - Expected: 0x01 (IPV4_UDP)" << endOfMsg();
}

void KnxHandler::checkConnStateResp(ByteString msg, Byte channelId) const
{
	if (msg[6] != channelId)
		logger.errorX() << "Received CONNECTION STATE RESPONSE has channel id 0x" << cnvToHexStr(msg[6])
		                << " - Expected: 0x" << cnvToHexStr(channelId) << endOfMsg();
	if (msg[7] != 0x00)
		logger.errorX() << "Received CONNECTION STATE RESPONSE has status code 0x" << cnvToHexStr(msg[7])
		                << " (" << getStatusCodeText(msg[7]) << ") - Expected: 0x00" << endOfMsg();
}

void KnxHandler::logMsg(ByteString msg, bool received) const
{
	if (config.getLogRawMsg() && msg.length() >= 4)
	{
		ServiceType type(msg[2], msg[3]);
		logger.debug() << (received ? "R: " : "S: ") << cnvToHexStr(msg) << " (" << type.toStr() << ")" << endOfMsg();
	}
}

void KnxHandler::logTunnelReq(ByteString msg, bool received) const
{
	if (config.getLogData() && msg.length() >= 20)
	{
		PhysicalAddr pa(msg[14], msg[15]);
		GroupAddr ga(msg[16], msg[17]);
		MsgCode msgCode(msg[10]);
		ByteString data(msg.substr(20, msg[18]));

		string type = "?";
		if (data.length() > 0)
			if ((data[0] & 0xC0) == 0x00)
				type = "Read";
			else if ((data[0] & 0x80) == 0x80)
				type = "Write";
			else if ((data[0] & 0x40) == 0x40)
				type = "Response";

		logger.debug() << (received ? "R: " : "S: ") << msgCode.toStr() << " "
		               << pa.toStr() << " -> " << ga.toStr() << ": " << cnvToHexStr(data)
		               << " (" << type << " for item " << getItemId(ga) << ")" << endOfMsg();
	}
}

ByteString KnxHandler::createConnReq() const
{
	ByteString hpai = config.getNatMode() ? createHpai(IpAddr(0), 0) : createHpai(config.getLocalIpAddr(), localIpPort);
	return addHeader(ServiceType::CONN_REQ, hpai + hpai + createCRI());
}

ByteString KnxHandler::createConnStateReq() const
{
	ByteString longHpai = config.getNatMode() ? createLongHpai(channelId, IpAddr(0), 0) : createLongHpai(channelId, config.getLocalIpAddr(), localIpPort);
	return addHeader(ServiceType::CONN_STATE_REQ, longHpai);
}

ByteString KnxHandler::createDiscReq() const
{
	ByteString longHpai = config.getNatMode() ? createLongHpai(channelId, IpAddr(0), 0) : createLongHpai(channelId, config.getLocalIpAddr(), localIpPort);
	return addHeader(ServiceType::DISC_REQ, longHpai);
}

ByteString KnxHandler::createDiscResp() const
{
	return addHeader(ServiceType::DISC_RESP, ByteString({channelId, 0x00}));
}

ByteString KnxHandler::createTunnelReq(Byte seqNo, GroupAddr ga, ByteString data) const
{
	return addHeader(ServiceType::TUNNEL_REQ, createTunnelHeader(channelId, seqNo) + createCemiFrame(physicalAddr, ga, data));
}

ByteString KnxHandler::createTunnelAck(Byte seqNo) const
{
	return addHeader(ServiceType::TUNNEL_ACK, createTunnelHeader(channelId, seqNo));
}

string KnxHandler::getStatusCodeName(Byte statusCode) const
{
	switch (statusCode)
	{
		case 0x00:
			return "NO_ERROR";
		case 0x01:
			return "HOST_PROTOCOL_TYPE";
		case 0x02:
			return "VERSION_NOT_SUPPORTED";
		case 0x04:
			return "SEQUENCE_NUMBER";
		case 0x0F:
			return "ERROR";
		case 0x21:
			return "CONNECTION_ID";
		case 0x22:
			return "CONNECTION_TYPE";
		case 0x23:
			return "CONNECTION_OPTION";
		case 0x24:
			return "NO_MORE_CONNECTIONS";
		case 0x25:
			return "NO_MORE_UNIQUE_CONNECTIONS";
		case 0x26:
			return "DATA_CONNECTION";
		case 0x27:
			return "KNX_CONNECTION";
		case 0x28:
			return "AUTHORIZATION";
		case 0x29:
			return "TUNNELING_LAYER";
		case 0x2D:
			return "NO_TUNNELING_ADDRESS";
		case 0x2E:
			return "CONNECTION_IN_USE";
	}

	return "?";
}

string KnxHandler::getStatusCodeExplanation(Byte statusCode) const
{
	switch (statusCode)
	{
		case 0x00:
			return "No error occurred.";
		case 0x01:
			return "The requested host protocol is not supported by the KNXnet/IP device.";
		case 0x02:
			return "The requested protocol version is not supported by the KNXnet/IP device.";
		case 0x04:
			return "The received sequence number is out of sync.";
		case 0x0F:
			return "An undefined, possibly implementation specific error occurred.";
		case 0x21:
			return "The KNXnet/IP server device cannot find an active data connection with the specified ID.";
		case 0x22:
			return "The KNXnet/IP server device does not support the requested connection type.";
		case 0x23:
			return "The KNXnet/IP server device does not support one or more requested connection options.";
		case 0x24:
			return "The KNXnet/IP server device cannot accept the new data connection because its maximum amount of concurrent connections is already used.";
		case 0x25:
			return "The KNXnet/IP tunneling server could provide a connection (in contrast to NO_MORE_CONNECTIONS) if only the KNXnet/IP tunneling address that would be assigned to the connection would be unique.";
		case 0x26:
			return "The KNXnet/IP server device detects an error concerning the data connection with the specified ID.";
		case 0x27:
			return "The KNXnet/IP server device detects an error concerning the KNX connection with the specified ID.";
		case 0x28:
			return "The KNXnet/IP client is not authorized to use the requested individual address in the extended connection request information (CRI) structure.";
		case 0x29:
			return "The requested tunneling layer is not supported by the KNXnet/IP server device.";
		case 0x2D:
			return "The address requested in the extended CRI structure is not a tunneling individual address.";
		case 0x2E:
			return "The individual address requested for this connection is already in use.";
	}

	return "?";
}

string KnxHandler::getStatusCodeText(Byte statusCode) const
{
	return getStatusCodeName(statusCode) + " = '" + getStatusCodeExplanation(statusCode) + "'";
}

string KnxHandler::getItemId(GroupAddr ga) const
{
	for (auto& bindingPair : config.getBindings())
	{
		auto& binding = bindingPair.second;
		if (binding.stateGa == ga || binding.writeGa == ga)
			return binding.itemId;
	}
	return "?";
}
