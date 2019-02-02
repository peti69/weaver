#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <stdexcept>
#include <cassert>
#include <iomanip>

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
		case TUNNEL_RESP:
			return "TUNNEL_RESP";
		default:
			return "???";
	}
}

string MsgCode::toStr() const
{
	if (value == MsgCode::LDATA_IND)
		return "L-Data.ind";
	else if (value == MsgCode::LDATA_CON)
		return "L-Data.con";
	else if (value == MsgCode::LDATA_REQ)
		return "L-Data.req";
	else
		return "?? " + cnvToHexStr(value) + " ??";
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

ByteString DatapointType::exportValue(string value) const
{
	try
	{
		if (mainNo == 1)
		{
			if (value == "1" || value == "yes" || value == "YES" || value == "true" || value == "TRUE")
				return ByteString({0x81});
			else if (value == "0" || value == "no" || value == "NO" || value == "false" || value == "FALSE")
				return ByteString({0x80});
		}
		else if (mainNo == 5 && subNo == 1)
		{
			float f = std::stof(value);
			if (f >= 0 && f <= 100)
			{
				Byte b = f * 255.0 / 100.0;
				return ByteString({0x80, b});
			}
		}
		else if (mainNo == 5)
		{
			long l = std::stof(value);
			if (l >= 0 && l <= 255)
			{
				Byte b = l;
				return ByteString({0x80, b});
			}
		}
		else if (mainNo == 7)
		{
			long l = std::stod(value);
			if (l >= 0 && l <= 65535)
			{
				Byte bytes[3];
				bytes[0] = 0x80;
				bytes[1] = (l >> 8) & 0xFF;
				bytes[2] = l & 0xFF;
				return ByteString(bytes, sizeof(bytes));
			}
		}
		else if (mainNo == 9)
		{
			float f = std::stof(value);
			int E = 0;
			while ((f < -20.48 || f > 20.47) && E <= 15) { f = f / 2.0; E++; }
			if (f >= -20.48 && f <= 20.47)
			{
				int M = f * 100.0;
				bool sign = M < 0;
				if (sign) M *= -1;
				Byte bytes[3];
				bytes[0] = 0x80;
				bytes[1] = (E << 3) | ((M >> 8) & 0x07);
				bytes[2] = M & 0xFF;
				return ByteString(bytes, sizeof(bytes));
			}
		}
		else if (mainNo == 13)
		{
			long l = std::stod(value);
			Byte bytes[5];
			bytes[0] = 0x80;
			bytes[1] = (l >> 24) & 0xFF;
			bytes[2] = (l >> 16) & 0xFF;
			bytes[3] = (l >> 8) & 0xFF;
			bytes[4] = l & 0xFF;
			return ByteString(bytes, sizeof(bytes));
		}
		else if (mainNo == 14)
		{
			// assumption: float is encoded in IEEE 754 floating point format
			assert(sizeof(long) == sizeof(float));
			union { float f; long l; } u; 
			u.f = std::stof(value);
			Byte bytes[5];
			bytes[0] = 0x80;
			bytes[1] = (u.l >> 24) & 0xFF;
			bytes[2] = (u.l >> 16) & 0xFF;
			bytes[3] = (u.l >> 8) & 0xFF;
			bytes[4] = u.l & 0xFF;
			return ByteString(bytes, sizeof(bytes));
		}
	}
	catch (const std::exception& ex)
	{
		// ignore
	}
	return ByteString();
}

string DatapointType::importValue(ByteString bytes) const
{
	std::ostringstream stream;
	if (mainNo == 1 && bytes.length() == 1)
	{
		if (bytes[0] == 0x81)
			stream << "1";
		else if (bytes[0] == 0x80)
			stream << "0";
	}
	else if (mainNo == 5 && subNo == 1 && bytes.length() == 2)
		stream << bytes[1] * 100.0 / 255.0;
	else if (mainNo == 5 && bytes.length() == 2)
		stream << 1 * bytes[1];
	else if (mainNo == 7 && bytes.length() == 3)
		stream << (bytes[1] << 8 | bytes[2]);
	else if (mainNo == 9)
	{
		int E = bytes[1] >> 3 & 0x0F;
		int M = (bytes[1] & 0x07) << 8 | bytes[2];
		stream << (M << E) / 100.0;
	}
	else if (mainNo == 13 && bytes.length() == 5)
		stream << (bytes[1] << 24 | bytes[2] << 16 | bytes[3] << 8 | bytes[4]);
	else if (mainNo == 14 && bytes.length() == 5)
	{
		// assumption: float is encoded in IEEE 754 floating point format
		assert(sizeof(long) == sizeof(float));
		union { float f; long l; } u; 
		u.l = bytes[1] << 24 | bytes[2] << 16 | bytes[3] << 8 | bytes[4];
		stream << u.f;
	}
	return stream.str();
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

bool GroupAddr::operator==(const GroupAddr& x) 
{ 
	return null && x.null || !null && !x.null && value == x.value; 
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
	id(_id), config(_config), logger(_logger), state(DISCONNECTED), lastConnectTry(0)
{
}

KnxHandler::~KnxHandler() 
{ 
	disconnect();
}

void KnxHandler::disconnect()
{
	if (state == DISCONNECTED) 
		return;

	if (state == CONNECTED)
	{
		//lastConnectTry = 0;

		sendControlMsg(createDiscReq());

		logger.info() << "Disconnected from gateway " << config.getIpAddr().toStr() << ":" << config.getIpPort() << endOfMsg();
	}
	else
		lastConnectTry = std::time(0);

	::close(socket); 
	state = DISCONNECTED;
}

Events KnxHandler::receive()
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

Events KnxHandler::receiveX()
{
	std::time_t now = std::time(0);

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
		
		sockaddr_in clientAddr;
		clientAddr.sin_family = AF_INET;
		clientAddr.sin_addr.s_addr = htonl(INADDR_ANY);
		clientAddr.sin_port = 0;
		int rc = bind(socket, reinterpret_cast<sockaddr*>(&clientAddr), sizeof(clientAddr));
		if (rc == -1)
			logger.errorX() << unixError("bind") << endOfMsg();
		
		socklen_t clientAddrLen = sizeof(clientAddr);
		rc = getsockname(socket, reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrLen);
		if (rc == -1)
			logger.errorX() << unixError("getsockname") << endOfMsg();
		localIpPort = ntohs(clientAddr.sin_port);

		logger.debug() << "Using port " << localIpPort << " as local control and data endpoint " << endOfMsg();
		if (config.getNatMode())
			logger.debug() << "Using NAT mode" << endOfMsg();
		
		sendControlMsg(createConnReq());
		state = WAIT_FOR_CONN_RESP;
		controlReqSendTime = now;
		autoClose.disable();

		return events;
	}
	
	if (state == CONNECTED && !ongoingConnStateReq && controlReqSendTime + config.getConnStateReqInterval() <= now)
	{
		controlReqSendTime = now;
		ongoingConnStateReq = true;
		sendControlMsg(createConnStateReq());
	}
	else if (state == CONNECTED && ongoingConnStateReq && controlReqSendTime + config.getControlRespTimeout() <= now)
		logger.errorX() << "CONNECTION STATE REQUEST not answered in time" << endOfMsg();
	else if (state == WAIT_FOR_CONN_RESP && controlReqSendTime + config.getControlRespTimeout() <= now)
		logger.errorX() << "CONNECTION REQUEST not answered in time" << endOfMsg();
	
	ByteString msg;
	IpAddr senderIpAddr;
	IpPort senderIpPort;
	if (!receiveMsg(msg, senderIpAddr, senderIpPort))
		return events;
	checkMsg(msg);

	ServiceType serviceType(msg[2], msg[3]);
	if (state == CONNECTED && serviceType == ServiceType::TUNNEL_REQ)
	{
		logTunnelReq(msg);
		checkTunnelReq(msg);

		Byte seqNo = msg[8];
		if (seqNo == lastReceivedSeqNo)
			return events;
		if (seqNo != ((lastReceivedSeqNo + 1) & 0xFF))
			logger.warn() << "Received TUNNEL REQUEST has invalid sequence number " << cnvToHexStr(seqNo) << " (last one: " << cnvToHexStr(lastReceivedSeqNo) << ")" << endOfMsg();
		lastReceivedSeqNo = seqNo;

		sendDataMsg(createTunnelResp(seqNo));

		MsgCode msgCode = msg[10];
		if (msgCode == MsgCode::LDATA_IND)
		{
			GroupAddr ga(msg[16], msg[17]);
			ByteString data = msg.substr(20, msg[18]);

			for (auto& pair : config.getBindings())
			{
				auto& binding = pair.second;
	
				if ((ga == binding.stateGa || ga == binding.writeGa) && !binding.owner && data.length() == 1 && (data[0] & 0x80) == 0)
					events.add(Event(id, binding.itemId, Event::READ_REQ, ""));
				else if ((ga == binding.stateGa && binding.owner) || (ga == binding.writeGa && !binding.owner))
				{
					string value = binding.dpt.importValue(data);
					if (value == "")
						logger.warn() << "Unable to convert DPT " << binding.dpt.toStr() << " data '" << cnvToHexStr(data) << "' to value for item " << binding.itemId << endOfMsg();
					else if (ga == binding.stateGa)
						events.add(Event(id, binding.itemId, Event::STATE_IND, value));
					else
						events.add(Event(id, binding.itemId, Event::WRITE_REQ, value));
				}
			}
		}
		else if (msgCode == MsgCode::LDATA_CON)
		{
			ongoingLDataReq = false;
			sendWaitingLDataReq();
		}
	}
	else if (state == CONNECTED && serviceType == ServiceType::TUNNEL_RESP)
	{
		checkTunnelResp(msg);
	}
	else if (state == CONNECTED && serviceType == ServiceType::CONN_STATE_RESP && ongoingConnStateReq)
	{
		checkConnStateResp(msg, channelId);

		ongoingConnStateReq = false;
	}
	else if (state == WAIT_FOR_CONN_RESP && serviceType == ServiceType::CONN_RESP)
	{
		checkConnResp(msg);

		dataIpAddr = IpAddr(msg[10], msg[11], msg[12], msg[13]);
		dataIpPort = IpPort(msg[14] << 8 | msg[15]);
		if (config.getNatMode() && (dataIpAddr == 0 || dataIpPort == 0))
		{
			dataIpPort = senderIpPort;
			dataIpAddr = senderIpAddr;
		}
		channelId = msg[6];
		
		state = CONNECTED;
		ongoingConnStateReq = false;
		ongoingLDataReq = false;
		waitingLDataReqs.clear();
		lastReceivedSeqNo = 0xFF;
		lastSentSeqNo = 0xFF;
		
		logger.debug() << "Using channel " << cnvToHexStr(channelId) << endOfMsg();
		logger.debug() << "Using " << dataIpAddr.toStr() << ":" << dataIpPort << " as remote data endpoint" << endOfMsg();
		logger.info() << "Connected to gateway " << config.getIpAddr().toStr() << ":" << config.getIpPort() << endOfMsg();
	}
	else if (state == CONNECTED && serviceType == ServiceType::DISC_REQ)
		logger.errorX() << "DISCONNECT REQUEST received" << endOfMsg();
	else
		logger.warn() << "Received unexpected message with service type " << serviceType.toStr() << endOfMsg();
	
	return events;
}

void KnxHandler::send(const Events& events)
{
	if (state != CONNECTED)
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

void KnxHandler::sendX(const Events& events)
{
	auto& bindings = config.getBindings();

	for (auto& event : events)
	{
		string itemId = event.getItemId();
		string value = event.getValue();

		auto bindingIter = bindings.find(itemId);
		if (bindingIter != bindings.end())
		{
			auto& binding = bindingIter->second;

			ByteString data;
			if (event.getType() == Event::READ_REQ)
				data = ByteString({0x00});
			else
			{
				data = binding.dpt.exportValue(value);
				if (!data.length())
				{
					logger.error() << "Unable to convert value " << value << " of item " << itemId << " to DPT " << binding.dpt.toStr() << endOfMsg();
					continue;
				}
			}

			if (binding.owner && event.getType() == Event::READ_REQ)
			{
				if (!binding.stateGa.isNull())
					waitingLDataReqs.emplace_back(binding.stateGa, data);
				else if (!binding.writeGa.isNull())
					waitingLDataReqs.emplace_back(binding.writeGa, data);
			}
			else if (!binding.owner && !binding.stateGa.isNull() && event.getType() == Event::STATE_IND)
				waitingLDataReqs.emplace_back(binding.stateGa, data);
			else if (binding.owner && !binding.writeGa.isNull() && event.getType() == Event::WRITE_REQ)
				waitingLDataReqs.emplace_back(binding.writeGa, data);
		}
	}
	
	sendWaitingLDataReq();
}

void KnxHandler::sendWaitingLDataReq()
{
	if (ongoingLDataReq || waitingLDataReqs.empty())
		return;
	
	lastSentSeqNo = (lastSentSeqNo + 1) & 0xFF;
	auto& req = waitingLDataReqs.front();
	ByteString msg = createTunnelReq(lastSentSeqNo, req.ga, req.data);
	sendDataMsg(msg);
	
	ongoingLDataReq = true;
	waitingLDataReqs.pop_front();

	logTunnelReq(msg);
}

bool KnxHandler::receiveMsg(ByteString& msg, IpAddr& addr, IpPort& port)
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

void KnxHandler::sendMsg(IpAddr addr, IpPort port, ByteString msg)
{
	sockaddr_in sockAddr;
	sockAddr.sin_family = AF_INET;
	sockAddr.sin_addr.s_addr = htonl(addr);
	sockAddr.sin_port = htons(port);
	int rc = ::sendto(socket, msg.data(), msg.length(), 0, reinterpret_cast<sockaddr*>(&sockAddr), sizeof(sockAddr));
	if (rc == -1)
		logger.errorX() << unixError("sendto") << endOfMsg();

	logMsg(msg, false);
}

void KnxHandler::sendControlMsg(ByteString msg)
{
	sendMsg(config.getIpAddr(), config.getIpPort(), msg);
}

void KnxHandler::sendDataMsg(ByteString msg)
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
	frame[2] = 0xBC;				// Control byte
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
		logger.errorX() << "Received message has length " << msg.length() << " (expected: >=8)" << endOfMsg();
	ByteString header = msg.substr(0, 6);
	if (header[0] != 0x06)
		logger.errorX() << "Received message contains header length " << cnvToStr(int(header[0])) << " (expected: 6)" << endOfMsg();
	if (header[1] != 0x10)
		logger.errorX() << "Received CONNECTION RESPONSE has KNXnet/IP version 0x" << cnvToHexStr(header[1]) << " (expected: 0x10)" << endOfMsg();
	unsigned short totalLength = header[4] << 8 | header[5];
	if (totalLength != msg.length())
		logger.errorX() << "Received message contains total length " << cnvToStr(totalLength) << " (actual length: " << msg.length() << ")" << endOfMsg();
}

void KnxHandler::checkTunnelReq(ByteString msg) const
{
	if (msg.length() < 20)
		logger.errorX() << "Received TUNNEL REQUEST has length " << msg.length() << " (expected: >=20)" << endOfMsg();
	Byte receivedChannelId = msg[7];
	if (receivedChannelId != channelId)
		logger.errorX() << "Received TUNNEL REQUEST has channel id 0x" << cnvToHexStr(receivedChannelId) << " (expected: 0x" << cnvToHexStr(channelId) << ")" << endOfMsg();
}

void KnxHandler::checkTunnelResp(ByteString msg) const
{
	if (msg.length() != 10)
		logger.errorX() << "Received TUNNEL RESPONSE has length " << msg.length() << " (expected: 10)" << endOfMsg();
	if (msg[9] != 0x00)
		logger.errorX() << "Received TUNNEL RESPONSE has status code 0x" << cnvToHexStr(msg[9]) << " (expected: 0x00)" << endOfMsg();
}

void KnxHandler::checkConnResp(ByteString msg) const
{
	if (msg.length() != 20)
		logger.errorX() << "Received CONNECTION RESPONSE has length " << msg.length() << " (expected: 20)" << endOfMsg();
	if (msg[7] != 0x00)
		logger.errorX() << "Received CONNECTION RESPONSE has status code 0x" << cnvToHexStr(msg[7]) << " (expected: 0x00)" << endOfMsg();
	if (msg[8] != 0x08)
		logger.errorX() << "Received CONNECTION RESPONSE has HPAI length " << cnvToStr(int(msg[8])) << " (expected: 8)" << endOfMsg();
	if (msg[9] != 0x01)
		logger.errorX() << "Received CONNECTION RESPONSE has protocol code 0x" << cnvToHexStr(msg[9]) << " (expected: 0x01 = IPV4_UDP)" << endOfMsg();
}

void KnxHandler::checkConnStateResp(ByteString msg, Byte channelId) const
{
	if (msg[6] != channelId)
		logger.errorX() << "Received CONNECTION STATE RESPONSE has channel id 0x" << cnvToHexStr(msg[7]) << " (expected: 0x" << cnvToHexStr(channelId) << ")" << endOfMsg();
	if (msg[7] != 0x00)
		logger.errorX() << "Received CONNECTION STATE RESPONSE has status code 0x" << cnvToHexStr(msg[7]) << " (expected: 0x00)" << endOfMsg();
}

void KnxHandler::logMsg(ByteString msg, bool received) const
{
	if (config.getLogRawMsg() && msg.length() >= 4)
	{
		ServiceType type(msg[2], msg[3]);
		logger.debug() << (received ? "R " : "S ") << cnvToHexStr(msg) << " (" << type.toStr() << ")" << endOfMsg();
	}
}

void KnxHandler::logTunnelReq(ByteString msg) const
{
	if (config.getLogData() && msg.length() >= 20)
	{
		PhysicalAddr pa(msg[14], msg[15]);
		GroupAddr ga(msg[16], msg[17]);
		MsgCode msgCode(msg[10]);

		logger.debug() << msgCode.toStr() << " " << pa.toStr() << " -> " << ga.toStr() << ": " 
		               << cnvToHexStr(msg.substr(20, msg[18])) << endOfMsg();
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

ByteString KnxHandler::createTunnelReq(Byte seqNo, GroupAddr ga, ByteString data) const
{
	return addHeader(ServiceType::TUNNEL_REQ, createTunnelHeader(channelId, seqNo) + createCemiFrame(config.getPhysicalAddr(), ga, data));
}

ByteString KnxHandler::createTunnelResp(Byte seqNo) const
{
	return addHeader(ServiceType::TUNNEL_RESP, createTunnelHeader(channelId, seqNo));
}
