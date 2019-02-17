#include <sys/select.h>
#include <signal.h>
 
#include "config.h"
#include "logger.h"

void sighandler(int signo) 
{
}

int main(int argc, char* argv[])
{
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
	Config configFile;
	try
	{
		if (argc < 2)
			throw std::runtime_error("Configuration file name not specified");
		configFile.read(argv[1]);
	}
	catch (const std::exception& error)
	{
		cout << "Reading configuration file failed: " << error.what() << endl;
		return 1;
	}

	// initialize logging 
	Log log;
	try
	{
		log.init(configFile.getLogConfig());
	}
	catch (const std::exception& error)
	{
		cout << "Logging initialization failed: " << error.what() << endl;
		return 1;
	}
	Logger logger = log.newLogger("MAIN");

	// first log messages
	logger.info() << "Started" << endOfMsg();
	logger.info() << "Using configuration file " << argv[1] << endOfMsg();

	// initialize
	Links links;
	Items items;
	GlobalConfig config;
	try
	{
		config = configFile.getGlobalConfig();
		items = configFile.getItems();
		links = configFile.getLinks(items, log);
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
			long timeoutMs = 1000;
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
				
				timeoutMs = std::min(timeoutMs, linkPair.second.getTimeout());
			}

			struct timespec timeout;
			timeout.tv_sec = timeoutMs / 1000;
			timeout.tv_nsec = (timeoutMs % 1000) * 1000000;
			int rc = pselect(fdMax + 1, &readFds, &writeFds, 0, &timeout, &oldset);
			if (rc == -1)
				if (errno == EINTR)
					break;
				else
					logger.errorX() << unixError("select") << endOfMsg();
			//logger.info() << "pselect() done" << endOfMsg();
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

		// log events
		if (config.getLogEvents())
			for (auto& event : events)
			{
				LogMsg logMsg = logger.debug();
				logMsg << event.getType().toStr() << " from " << event.getOriginId() << " for " << event.getItemId();
				if (event.getType() != EventType::READ_REQ)
					logMsg << ": " << event.getValue().toStr() << " [" << event.getValue().getType().toStr() << "]";
				logMsg << endOfMsg();
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