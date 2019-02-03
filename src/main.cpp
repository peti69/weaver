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
				int readFd = link.getReadDescriptor();
				if (readFd >= 0)
				{
					if (readFd > fdMax)
						fdMax = readFd;
					FD_SET(readFd, &readFds);
				}

				int writeFd = link.getWriteDescriptor();
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
		for (auto& link : links)
			try
			{		
				events.splice(events.begin(), link.receive());
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
				link.send(events);
			}
			catch (const std::exception& error)
			{
				logger.error() << "Error on link " << link.getId() << " when sending events: " << error.what() << endOfMsg();
			}
	}

	// shutdown all links
	links.clear();

	// last log message
	logger.info() << "Stopped" << endOfMsg();
}