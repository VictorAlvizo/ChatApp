#include "Connection.h"

class Client {
public:
	Client() {}

	~Client() {
		Disconnect();
	}

	bool Connect(const std::string& host, const uint16_t port, const std::string& username, const std::string& password, int option) {
		try {
			m_ClientAccount.SetInfo(username, password, option);
			asio::ip::tcp::resolver resolver(m_Context);
			asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(host, std::to_string(port));
			m_Connection = std::make_unique<Connection>(m_Context, asio::ip::tcp::socket(m_Context), m_IncomingMessages, Owner::Client);
			m_Connection->ConnectToServer(m_ClientAccount, endpoints);
			m_ContextThread = std::thread([this]() {m_Context.run(); });
		} catch (const std::exception& e)
		{ 
			std::cout << "Client Failed To Connect! Exception Thrown: " << e.what() << std::endl;
			return false;
		}

		return true;
	}

	void EnterAccount(const std::string& username, const std::string& password, int option) {
		m_ClientAccount.SetInfo(username, password, option);
		m_Connection->SetAccount(m_ClientAccount);
	}

	void Disconnect() { 
		if (isConnected()) {
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

	inline Account getAccount() const {
		return m_ClientAccount;
	}

	std::string m_ChattingWith = "";
	std::vector<std::string> m_AwaitingRequest; //Waiting for a response by users requesting to chat with this client
	bool m_Chatting = false, m_AccountProcessed = true, m_Accepted = false;

private:
	asio::io_context m_Context;
	std::thread m_ContextThread;
	std::unique_ptr<Connection> m_Connection; //Connection to the server

	TSQueue<OwnedPacket> m_IncomingMessages;
	Account m_ClientAccount;
};