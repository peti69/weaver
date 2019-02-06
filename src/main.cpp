#include <sys/select.h>
#include <signal.h>
 
#include "config.h"
#include "logger.h"

void sighandler(int signo) 
{
}

int main(int argc, char* argv[])
{
	Logger logger("MAIN");

	// first log message
	logger.info() << "Started" << endOfMsg();

	// install the signal handler for SIGTERM.
	struct sigaction action;
	action.sa_handler = sighandler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGINT, &action, NULL);

	// block SIGTERM
	sigset_t sigset, oldset;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGTERM);
	sigaddset(&sigset, SIGINT);
	sigprocmask(SIG_BLOCK, &sigset, &oldset);
	
	// read configuration file
	Links links;
	Items items;
	GlobalConfig config;
	try
	{
		if (argc < 2)
			logger.errorX() << "File name for configuration missing" << endOfMsg();
		logger.info() << "Reading configuration from " << argv[1] << endOfMsg();
		Config configFile(argv[1]);

		config = configFile.getGlobalConfig();
		items = configFile.getItems();
		links = configFile.getLinks(items);
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
			for (auto& linkPair : links)
			{
				int readFd = linkPair.second.getReadDescriptor();
				if (readFd >= 0)
				{
					if (readFd > fdMax)
						fdMax = readFd;
					FD_SET(readFd, &readFds);
				}

				int writeFd = linkPair.second.getWriteDescriptor();
				if (writeFd >= 0)
				{
					if (writeFd > fdMax)
						fdMax = writeFd;
					FD_SET(writeFd, &writeFds);
				}
			}
			
			struct timespec timeout;
			timeout.tv_sec = 1;
			timeout.tv_nsec = 0;
			int rc = pselect(fdMax + 1, &readFds, &writeFds, 0, &timeout, &oldset);
			if (rc == -1)
				if (errno == EINTR)
					break;
				else
					logger.errorX() << unixError("select") << endOfMsg();
		}
		catch (const std::exception& error)
		{
			logger.error() << "Error when waiting for event: " << error.what() << endOfMsg();
			continue;
		}

		// receive events
		Events events;
		for (auto& linkPair : links)
			try
			{		
				events.splice(events.begin(), linkPair.second.receive(items));
			}
			catch (const std::exception& error)
			{
				logger.error() << "Error on link " << linkPair.first << " when receiving events: " << error.what() << endOfMsg();
			}
		
		// process events
		for (auto eventPos = events.begin(); eventPos != events.end();)
		{
			auto& event = *eventPos;

			// provide item
			auto itemPos = items.find(event.getItemId());
			if (itemPos == items.end())
			{
				eventPos++;
				continue;
			}
			auto& item = itemPos->second;

			// provide origin link
			auto originLinkPos = links.find(event.getOriginId());
			if (originLinkPos == links.end())
			{
				eventPos++;
				continue;
			}
			auto& originLink = originLinkPos->second;

			// provide owner link
			auto ownerLinkPos = links.find(item.getOwnerId());
			if (ownerLinkPos == links.end())
			{
				eventPos++;
				continue;
			}
			auto& ownerLink = ownerLinkPos->second;

			// suppress STATE_IND events in case the item value did not change and the origin link 
			// only supports STATE_IND
			if (  event.getType() == Event::STATE_IND 
			   && !originLink.supports(Event::READ_REQ)
			   && !originLink.supports(Event::WRITE_REQ)
			   && !item.updateValue(event.getValue())
			   )
			{
				// old and new value are identical or within tolerances
				eventPos = events.erase(eventPos);
				continue;
			}

			// add STATE_IND event in case the owner link does not support READ_REQ
			if (event.getType() == Event::READ_REQ && !ownerLink.supports(Event::READ_REQ))
			{
				const Value& value = item.getValue();
				if (!value.isNull())
					events.add(Event("auto", event.getItemId(), Event::STATE_IND, value));
			}
			
			eventPos++;
		}

		// log events
		if (config.getLogEvents())
			for (auto& event : events)
			{
				LogStream stream = logger.debug();
				switch (event.getType())
				{
					case Event::STATE_IND:
						stream << "STATE_IND"; break;
					case Event::WRITE_REQ:
						stream << "WRITE_REQ"; break;
					case Event::READ_REQ:
						stream << "READ_REQ"; break;
					default:
						stream << "?"; break;
				}
				stream << " for item " << event.getItemId();
				if (event.getType() != Event::READ_REQ)
					stream << ": " << event.getValue().toStr() << " [" << event.getValue().getType().toStr() << "]";
				stream << endOfMsg();
			}
		
		// send events
		for (auto& linkPair : links)
			try
			{
				linkPair.second.send(items, events);
			}
			catch (const std::exception& error)
			{
				logger.error() << "Error on link " << linkPair.first << " when sending events: " << error.what() << endOfMsg();
			}
	}

	// shutdown all links
	links.clear();

	// last log message
	logger.info() << "Stopped" << endOfMsg();
}