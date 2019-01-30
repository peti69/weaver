 include <sys/select.h>

#include "config.h"
#include "logger.h"

int main(int argc, char* argv[])
{
	Logger logger("MAIN");
	
	std::list<Link> links;
	GlobalConfig config;
	
	try
	{
		if (argc < 2)
			logger.errorX() << "File name for configuration missing" << endOfMsg();
		logger.info() << "Reading configuration from " << argv[1] << endOfMsg();
		Config configFile(argv[1]);

		config = configFile.getGlobalConfig();
		links = configFile.getLinks(config.getItems());
	}
	catch (const std::exception& error)
	{
		logger.error() << "Initialization failed: " << error.what() << endOfMsg();
		return 1;
	}
	
	for (;;)
	{
		// wait for event
		try
		{
			fd_set readFds, writeFds;
			FD_ZERO(&readFds);
			FD_ZERO(&writeFds);
			int fdMax = 0;
			for (auto& link : links)
			{
				int readFd = link.getHandler().getReadDescriptor();
				if (readFd >= 0)
				{
					if (readFd > fdMax)
						fdMax = readFd;
					FD_SET(readFd, &readFds);
				}

				int writeFd = link.getHandler().getWriteDescriptor();
				if (writeFd >= 0)
				{
					if (writeFd > fdMax)
						fdMax = writeFd;
					FD_SET(writeFd, &writeFds);
				}
			}
			
			struct timeval timeout;
			timeout.tv_sec = 1;
			timeout.tv_usec = 0;
			int rc = ::select(fdMax + 1, &readFds, &writeFds, 0, &timeout);
			if (rc == -1)
				logger.errorX() << unixError("select") << endOfMsg();
		}
		catch (const std::exception& error)
		{
			logger.error() << "Error when waiting for event: " << error.what() << endOfMsg();
			continue;
		}

		// receive events
		Events events;
		for (auto& link : links)
			try
			{		
				events.splice(events.begin(), link.getHandler().receive());
			}
			catch (const std::exception& error)
			{
				logger.error() << "Error on link " << link.getId() << " when receiving events: " << error.what() << endOfMsg();
			}

		// log events
		if (config.getLogEvents())
			for (auto& event : events)
			{
				string typeStr;
				switch (event.getType())
				{
					case Event::STATE_IND:
						typeStr = "STATE_IND"; break;
					case Event::WRITE_REQ:
						typeStr = "WRITE_REQ"; break;
					case Event::READ_REQ:
						typeStr = "READ_REQ"; break;
					default:
						typeStr = "???"; break;
				}
				logger.debug() << typeStr << " for item " << event.getItemId() << ": " << event.getValue() << endOfMsg();
			}
		
		// send events
		for (auto& link : links)
			try
			{
				link.getHandler().send(events);
			}
			catch (const std::exception& error)
			{
				logger.error() << "Error on link " << link.getId() << " when sending events: " << error.what() << endOfMsg();
			}
	}
}