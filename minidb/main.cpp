#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fstream>
#include <sstream>
#include <csignal>
#include <fcntl.h>
#include <sys/epoll.h>
#include <boost/algorithm/string.hpp>

# define MAXEVENTS 1

class Database
{
	private:
		std::map<std::string, std::string>  _db;
		int									_port;
		std::string 						_filename;
		int									_socket_fd;
		static bool							_signal;
		int									_epoll_fd;
		std::vector<int>					_clients;

	public:
		Database(const std::string &port, const std::string &file);
		~Database();

		void loadDB();
		int setUpSocket();
		void saveToFile(const std::string &filename);

		void runDB();
		static void signalHandler(int signum);

		//commands
		void execute_cmd(int fd, std::vector<std::string> cmd_lst);
		void handleIncomingNewClient();
		void handleClientConnection(int fd);
		int handlePost(int fd, std::vector<std::string> cmd_lst);
		int handleGet(int fd, std::vector<std::string> cmd_lst);
		int handleDelete(int fd, std::vector<std::string> cmd_lst);

		void eraseClientFd(int fd);
};

void	setupSignalHandler()
{
	signal(SIGINT, Database::signalHandler);
	signal(SIGQUIT, Database::signalHandler);
}

int main(int ac, char **av)
{
	if (ac == 3)
	{
		try
		{
			Database db(av[1], av[2]);
			setupSignalHandler();
			db.setUpSocket();
			db.runDB();
		}
		catch(const std::exception& e)
		{
			std::cerr << e.what() << '\n';
			return (-1);
		}
	}
	else
		std::cerr << "Usage ./mini_db <port> <file>" << std::endl;
	return (0);
}

bool Database::_signal = false;

Database::Database(const std::string &port, const std::string &file)
{
	_signal = false;
	_port = stoi(port);
	_filename = file;

	loadDB();
}

Database::~Database() {};

void Database::loadDB()
{
	std::ifstream						input_stream(_filename);
	std::map<std::string, std::string>	tmp_db;

	if (!input_stream)
		throw std::runtime_error("Failed to open file");

	std::string line;
	while (getline(input_stream, line))
	{
		if (line.empty())
			continue;

		size_t pos = line.find(' ');
		std::string key = line.substr(0, pos);
		std::string value = line.substr(pos + 1);

		tmp_db[key] = value;
	}
	input_stream.close();
	_db = tmp_db;
}

int Database::setUpSocket()
{
	struct sockaddr_in server;

	_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (_socket_fd < 0)
		throw std::runtime_error("socket creation failed.");

	server.sin_family = AF_INET;
	server.sin_port = htons(_port);
	server.sin_addr.s_addr = inet_addr("127.0.0.1");

	if (bind(_socket_fd, (struct sockaddr *)&server, sizeof server) < 0)
		throw std::runtime_error("socket binding failed.");

	if (listen(_socket_fd, 1) < 0)
		throw std::runtime_error("socket listening failed.");

	_epoll_fd = epoll_create1(0);
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = _socket_fd;

	epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, _socket_fd, &ev);

	std::cout << "ready : waiting at port: " << _port << "\n";
	return (0);
}

void Database::saveToFile(const std::string &filename)
{
	std::ofstream output(filename);
	std::map<std::string, std::string>::iterator it;
	for (it = _db.begin(); it != _db.end(); ++it)
	{
		output << it->first << " " << it->second << "\n";
	}
	output.close();
	std::cout << "Database saved to " << filename << '\n';
}


std::string trim(const std::string& str)
{
	size_t start = str.find_first_not_of(" \t\r\n");
	size_t end = str.find_last_not_of(" \t\r\n");
	if (start == std::string::npos || end == std::string::npos)
		return "";
	return str.substr(start, end - start + 1);
}

std::vector<std::string> splitString(const std::string& s, char delimiter)
{
	std::vector<std::string> tokens;
	std::stringstream ss(s);
	std::string token;

	while (std::getline(ss, token, delimiter))
	{
		token = trim(token);
		if (!token.empty())
			tokens.push_back(token);
	}
	return tokens;
}

void Database::runDB()
{
	struct epoll_event events[MAXEVENTS];

	while (!_signal)
	{

		int n = epoll_wait(_epoll_fd, events, MAXEVENTS, -1);
		for (int i = 0; i < n; ++i)
		{
			int fd = events[i].data.fd;
			if (fd == _socket_fd)
				handleIncomingNewClient();
			else
				handleClientConnection(fd);
		}
	}
	saveToFile(_filename);
	close(_socket_fd);
	close(_epoll_fd);
	std::cout << "Shutting down database...\n";
}

void Database::handleIncomingNewClient()
{
	struct sockaddr_in clientAddr;
	socklen_t clientLen = sizeof(clientAddr);
	int client_fd = accept(_socket_fd, (struct sockaddr *)&clientAddr, &clientLen);

	fcntl(client_fd, F_SETFL, O_NONBLOCK);
	_clients.push_back(client_fd);

	struct epoll_event clientEvent;
	clientEvent.events = EPOLLIN;
	clientEvent.data.fd = client_fd;
	epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, client_fd, &clientEvent);
	std::cout << "New Client connected: " << client_fd << std::endl;
}

void Database::handleClientConnection(int fd)
{
	char buffer[512];
	ssize_t bytesRead = recv(fd, buffer, sizeof(buffer) - 1, 0);
	if (bytesRead <= 0)
	{
		std::cout << "Client disconnected\n";
		eraseClientFd(fd);
	}
	else
	{
		buffer[bytesRead] = '\0';
		std::string command(buffer);
		std::cout << "Received : " << command;

		std::vector<std::string> cmd_list = splitString(command, ' ');
		if (!cmd_list.empty())
			execute_cmd(fd, cmd_list);
	}
}

void Database::eraseClientFd(int fd)
{
	for (std::vector<int>::iterator it = _clients.begin(); it != _clients.end(); it++)
	{
		if ((*it) == fd)
		{
			_clients.erase(it);
			epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
			close(fd);
			break;
		}
	}
}

void Database::execute_cmd(int fd, std::vector<std::string> cmd_lst)
{
	std::string msg = "2\n";
	std::string &cmd = cmd_lst.front();

	if (cmd == "POST")
		handlePost(fd, cmd_lst);
	else if (cmd == "GET")
		handleGet(fd, cmd_lst);
	else if (cmd == "DELETE")
		handleDelete(fd, cmd_lst);
	else
		send(fd, msg.c_str(),  msg.size(), 0);
}

void Database::signalHandler(int signum)
{
	(void) signum;
	std::cout << "\nSignal Received! Stopping server..." << std::endl;
	_signal = true;
}


int Database::handlePost(int fd, std::vector<std::string> cmd_lst)
{
	const std::string &key = cmd_lst[1];
	const std::string &value = cmd_lst[2];
	_db[key] = value;
	std::string msg = "0\n";
	send(fd, msg.c_str(),  msg.size(), 0);
	return (0);
}

int Database::handleGet(int fd, std::vector<std::string> cmd_lst)
{
	const std::string &key = cmd_lst[1];
 	std::map<std::string, std::string>::iterator it = _db.find(key);
	if (it != _db.end()) {
		std::string msg = "0 " + it->second + "\n";
		send(fd, msg.c_str(),  msg.size(), 0);
	}
	else {
		std::string msg = "1\n";
		send(fd, msg.c_str(),  msg.size(), 0);
	}
	return (0);
}

int Database::handleDelete(int fd, std::vector<std::string> cmd_lst)
{
	const std::string &key = cmd_lst[1];
 	std::map<std::string, std::string>::iterator it = _db.find(key);
	if (it != _db.end()) {
		_db.erase(key);
		std::string msg = "0\n";
		send(fd, msg.c_str(),  msg.size(), 0);
	}
	else {
		std::string msg = "1\n";
		send(fd, msg.c_str(),  msg.size(), 0);
	}
	return (0);
}
