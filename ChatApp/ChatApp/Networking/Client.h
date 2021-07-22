#include "Connection.h"

class Client {
public:
	Client(const std::string& username) 
		:m_Username(username)
	{
		//holder
	}

	~Client() {
		Disconnect();
	}

	bool Connect(const std::string& host, const uint16_t port) {
		try {
			asio::ip::tcp::resolver resolver(m_Context);
			asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(host, std::to_string(port));
			m_Connection = std::make_unique<Connection>(m_Context, asio::ip::tcp::socket(m_Context), m_IncomingMessages, Owner::Client);
			m_Connection->ConnectToServer(m_Username, endpoints);
			m_ContextThread = std::thread([this]() {m_Context.run(); });
		} catch (const std::exception& e)
		{ 
			std::cout << "Client Failed To Connect! Exception Thrown: " << e.what() << std::endl;
			return false;
		}

		return true;
	}

	void Disconnect() { 
		if(isConnected()) {
			m_Connection->Disconnect();
		}

		m_Context.stop();

		if (m_ContextThread.joinable()) {
			m_ContextThread.join();
		}

		m_Connection.release();
	}

	void Send(const Packet& packet) {
		if (isConnected()) {
			m_Connection->Send(packet);
		}
	}

	bool isConnected() {
		if (m_Connection) {
			return m_Connection->isConnected();
		}
		else {
			return false; //Connection is nullptr
		}
	}

	//Retrive any incoming messages
	TSQueue<OwnedPacket>& Incoming() {
		return m_IncomingMessages;
	}

	inline std::string getUsername() const {
		return m_Username;
	}

private:
	asio::io_context m_Context;
	std::thread m_ContextThread;
	std::unique_ptr<Connection> m_Connection; //Connection to the server

	TSQueue<OwnedPacket> m_IncomingMessages;
	std::string m_Username;
	//TODO: Save account information after the networking side is all done
};