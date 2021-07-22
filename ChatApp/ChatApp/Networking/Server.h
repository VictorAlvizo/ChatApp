#pragma once
#include "Connection.h"
#include <unordered_map>

//TODO: Anytime I print out to the server write in a log, and write the time as well
//A new file for every time the server is starts running
//TODO: Inside the switch statements, put them inside methods instead of writing everything there

struct ChatParty {
	ChatParty(std::shared_ptr<Connection> first, std::shared_ptr<Connection> second)
		:m_InitUser(first), m_RecUser(second) { }

	std::shared_ptr<Connection> m_InitUser; //User who sent the request to chat
	std::shared_ptr<Connection> m_RecUser; //The user who got invited to chat
};


class Server {
public:
	Server(uint16_t port) 
		:m_ASIOAcceptor(m_Context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
	{
		//holder
	}

	~Server() {
		Stop();
	}

	bool Start() {
		try {
			ListenForConnections();
			//Listen for connections first before running the context so it dosen't exit right away. Keep the context busy
			m_ContextThread = std::thread([this]() {m_Context.run(); });
		}
		catch (const std::exception& e) {
			std::cout << "Server Start Failed! Exception Thrown: " << e.what() << std::endl;
			return false;
		}

		std::cout << "The Server is Now Running!" << std::endl;
		return true;
	}

	void Stop() {
		m_Context.stop();

		if (m_ContextThread.joinable()) {
			m_ContextThread.join();
		}

		std::cout << "The Server Has Stopped Running" << std::endl;
	}

	void ListenForConnections() {
		m_ASIOAcceptor.async_accept([this](std::error_code ec, asio::ip::tcp::socket socket) {
			if (!ec) {
				std::cout << "New Connection With the Server: " << socket.remote_endpoint() << std::endl;
				std::shared_ptr<Connection> newConnection = std::make_shared<Connection>(m_Context, std::move(socket), m_IncomingPackets);

				if (OnClientConnect(newConnection)) {
					m_Connections.push_back(std::move(newConnection));
					m_Connections.back()->ConnectToClient(m_IDCounter++);
					std::cout << m_Connections.back()->getID() << " Connection Approved" << std::endl;
				}
				else {
					socket.close();
				}
			}
			else {
				std::cout << "Error Connecting to Client: " << ec.message();
			}

			ListenForConnections(); //Have to keep calling this so it keeps listining and the context dosen't die
		});
	}

	bool RemoveClient(std::shared_ptr<Connection> client) {
		if (m_Directory.find(client->getUsername()) == m_Directory.end()) {
			std::cout << "The Username " << client->getUsername() << " Was Not Found During the Removal Process" << std::endl;
			return false;
		}else if (m_Connections.size() > 0) {
			//TODO: Can't delete properly as I hit a memory error
			OnClientDisconnect(client->getUsername());
			m_Directory.erase(client->getUsername());
			client.reset();

			return true;
		}

		SendOnlineList();
		return false;
	}

	bool MessageClient(std::string username, const Packet& packet) {
		std::shared_ptr<Connection> client = m_Connections[m_Directory[username]];

		if (client && client->isConnected()) {
			client->Send(packet);
			return true;
		}
		else {
			std::cout << "Failed Sending Packet To " << username << std::endl;
			RemoveClient(client);
			return false;
		}
	}

	void MessageAll(const Packet& packet, std::shared_ptr<Connection> ignoreClient = nullptr) {
		std::vector<std::string> invalidUsers;

		for (auto& client : m_Connections) {
			if (client && client->isConnected() && client != ignoreClient) {
				client->Send(packet);
			}
			else {
				invalidUsers.push_back(client->getUsername());
			}
		}

		//If there are some users in this list handle their deletion
		if (invalidUsers.size() > 0) {
			for (std::string user : invalidUsers) {
				std::shared_ptr<Connection> client = m_Connections[m_Directory[user]];
				OnClientDisconnect(user);
				client.reset();
				m_Connections.erase(std::remove(m_Connections.begin(), m_Connections.end(), nullptr), m_Connections.end());
				m_Directory.erase(user);
			}

			//Give the remaining users all new indexes i
			for (unsigned int i = 0; i < m_Connections.size(); i++) {
				m_Directory[m_Connections[i]->getUsername()] = i;
			}

			m_UserIndex = m_Connections.size();
			SendOnlineList();
		}
	}

	void Update(int maxRead = -1, bool wait = false) {
		if (wait) {
			m_IncomingPackets.Wait();
		}

		int packetCount = 0;
		while (maxRead < packetCount && !m_IncomingPackets.isEmpty()) {
			OwnedPacket packet = m_IncomingPackets.PopFront();
			OnMessage(packet.m_Owner, packet.m_Packet);
			packetCount++;
		}
	}

	void OnMessage(std::shared_ptr<Connection> client, Packet& packet) {
		switch (packet.m_Header.m_ID) {
			case PacketType::Username: {
				HandleUsername(client);
				break;
			}

			case PacketType::Validated: {
				OnClientValidated(client->getID());
				break;
			}

			case PacketType::ChatRequest: {
				std::string receiver;
				packet >> receiver;

				HandleChatRequest(client, receiver);
				break;
			}

			case PacketType::ChatAlertResponse: {
				std::string response;
				packet >> response;

				std::string rec, init, accpt;
				size_t firstHash = response.find(":");

				//Extract the data
				rec = response.substr(0, firstHash);
				init = response.substr(firstHash + 1, (response.find_last_of(":") - firstHash) - 1);
				accpt = response.substr(response.find_last_of(":") + 1, response.size());

				HandleChatAlertResponse(init, rec, (accpt == "t") ? true : false);
				break;
			}

			case PacketType::Message: {
				std::string msg;
				packet >> msg;
				ProcessMessage(msg);
				break;
			}

			case PacketType::LeaveConvo: {
				std::string user;
				packet >> user;
				LeavingConvo(user);
				break;
			}

			default: {
				std::cout << "Packet Type Unknown! Unable To Process The Packet" << std::endl;
				break;
			}
		}
	}

	void LeavingConvo(const std::string& user) {
		std::string receiver = "";
		unsigned int index = -1;

		for (unsigned int i = 0; i < m_OngoingConversations.count(); i++) {
			if (m_OngoingConversations[i].m_InitUser->getUsername() == user) {
				index = i;
				receiver = m_OngoingConversations[i].m_RecUser->getUsername();
				break;
			}
			else if (m_OngoingConversations[i].m_RecUser->getUsername() == user) {
				index = i;
				receiver = m_OngoingConversations[i].m_InitUser->getUsername();
				break;
			}
		}

		std::cout << user << " is Leaving the Conversation With " << receiver << std::endl;

		m_OngoingConversations[index].m_InitUser->m_Status = ChatStatus::Open;
		m_OngoingConversations[index].m_RecUser->m_Status = ChatStatus::Open;
		m_OngoingConversations.Erase(index);

		Packet leaveMessage(PacketType::LeaveConvo);
		if (!MessageClient(receiver, leaveMessage)) {
			RemoveClient(m_Connections[m_Directory[receiver]]);
		}
	}

	void ProcessMessage(const std::string& str) {
		std::string sender = str.substr(0, str.find(":"));
		std::string fullMsg = str.substr(str.find(":") + 1, str.size());

		std::string receiver = "";
		for (unsigned int i = 0; i < m_OngoingConversations.count(); i++) {
			if (m_OngoingConversations[i].m_InitUser->getUsername() == sender) {
				receiver = m_OngoingConversations[i].m_RecUser->getUsername();
				break;
			}
			else if (m_OngoingConversations[i].m_RecUser->getUsername() == sender) {
				receiver = m_OngoingConversations[i].m_InitUser->getUsername();
				break;
			}
		}

		Packet msgPacket(PacketType::Message);
		msgPacket << fullMsg;

		if (!MessageClient(receiver, msgPacket)) {
			LeavingConvo(receiver);
		}
	}

	void HandleChatAlertResponse(const std::string& init, const std::string& rec, bool accepted) {
		//Find the ChatParty in the possible pool of chatting connections
		size_t index = 0;
		for (unsigned int i = 0; i < m_PossibleParty.count(); i++) {
			if (m_PossibleParty[i].m_InitUser->getUsername() == init && m_PossibleParty[i].m_RecUser->getUsername() == rec) {
				index = i;
				break;
			}
		}

		if (index == -1) {
			std::cout << "Unable to Find the Party For " << init << " and " << rec << std::endl;
		}
		
		if (!m_Connections[m_Directory[init]] || !m_Connections[m_Directory[init]]->isConnected()) {
			std::cout << "User " << init << " Was Unable to be Reached During the Alert Process" << std::endl;
			Packet unreachlePacket(PacketType::ChatResponse);
			std::string msg = init + ":" + std::to_string(6);
			m_PossibleParty.Erase(index);
			MessageClient(rec, unreachlePacket);
			RemoveClient(m_Connections[m_Directory[init]]); //TODO: BUG HERE, remove works for when leaving though in same scenario
		}
		else {
			//Handle the responses given
			if (accepted) {
				//Done here as I have a confirmed index as Back() might change to another chatparty and status remains as it's a pointer
				m_PossibleParty[index].m_InitUser->m_Status = ChatStatus::Chatting;
				m_PossibleParty[index].m_RecUser->m_Status = ChatStatus::Chatting;

				std::cout << m_PossibleParty[index].m_InitUser->getUsername() << " is Now Chatting With " << m_PossibleParty[index].m_RecUser->getUsername() << std::endl;

				m_OngoingConversations.PushBack(m_PossibleParty[index]);
				Packet acceptPacket(PacketType::ChatResponse);
				std::string acc = rec + ":" + std::to_string(0);
				acceptPacket << acc;
				SendOnlineList(); //Sending it here first as the connection reads packet from the Front(), allows chatting bool in main to hold true
				MessageClient(init, acceptPacket);
			}
			else {
				Packet rejectPacket(PacketType::ChatResponse);
				std::string rej = rec + ":" + std::to_string(5);
				rejectPacket << rej;
				MessageClient(init, rejectPacket);
			}

			m_PossibleParty.Erase(index);
		}
	}

	void HandleChatRequest(std::shared_ptr<Connection> client, const std::string& receiver) {
		if (m_Connections.size() == 1) { //User is alone, no one to connect to
			Packet rejectRequest(PacketType::ChatResponse);
			std::string rej = receiver + ":" + std::to_string(1);
			rejectRequest << rej;
			MessageClient(client->getUsername(), rejectRequest);
		}
		else if (m_Directory.find(receiver) == m_Directory.end()) { //Can't find user
			Packet rejectRequest(PacketType::ChatResponse);
			std::string rej = receiver + ":" + std::to_string(2);
			rejectRequest << rej;
			MessageClient(client->getUsername(), rejectRequest);
		}
		else if (m_Connections[m_Directory[receiver]]->m_Status == ChatStatus::Chatting) {
			Packet rejectRequest(PacketType::ChatResponse);
			std::string rej = receiver + ":" + std::to_string(3);
			rejectRequest << rej;
			MessageClient(client->getUsername(), rejectRequest);
		}
		else {
			ChatParty party(m_Connections[m_Directory[client->getUsername()]], m_Connections[m_Directory[receiver]]);
			Packet alertReciever(PacketType::ChatAlert);
			alertReciever << client->getUsername();

			if (!MessageClient(receiver, alertReciever)) {
				Packet rejectRequest(PacketType::ChatRequest);
				std::string rej = receiver + ":" + std::to_string(4);
				rejectRequest << rej;
				MessageClient(client->getUsername(), rejectRequest);
			}
			else { //Possible party, push it into possible pool
				m_PossibleParty.PushBack(party);
			}
		}
	}

	void HandleUsername(std::shared_ptr<Connection> client) {
		bool invalidUsername = false;
		for (auto const& x : m_Directory) {
			if (x.first == client->getUsername()) {
				invalidUsername = true;
				std::cout << "New Connection Getting Rejected For Having A Taken Username: " << client->getUsername() << std::endl;
				break;
			}
		}

		if (!invalidUsername) {
			//Provides an index to the client in relation to the username
			m_Directory[client->getUsername()] = m_UserIndex++;
			std::cout << client->getUsername() << " is now connected with ID: " << client->getID() << std::endl;

			Packet accept(PacketType::ServerAccept);
			MessageClient(client->getUsername(), accept);
			SendOnlineList();
		}
		else {
			client->Disconnect();
			client.reset();
			m_Connections.erase(std::remove(m_Connections.begin(), m_Connections.end(), client), m_Connections.end());
		}
	}

	void SendOnlineList() { //Send the client a list of users they can join
		if (m_Directory.size() > 1) {
			for (auto& connection : m_Connections) {
				Packet onlineList(PacketType::OnlineList);
				std::string str = "";

				for (std::pair<std::string, int> client : m_Directory) { 

					if (connection->getUsername() != client.first) {
						str += client.first + " | " + StatusTranslator(m_Connections[m_Directory[client.first]]->m_Status) + "\n";
					}
				}

				onlineList << str;
				MessageClient(connection->getUsername(), onlineList);
			}
		}
		else {
			Packet onlineList(PacketType::OnlineList);
			std::string str = "You Are The Only One Online!\n";
			onlineList << str;

			MessageClient(m_Connections.front()->getUsername(), onlineList);
		}
	}

	//Just needed to convert the enum to string for SendOnlineList()
	std::string StatusTranslator(ChatStatus status) {
		std::string str = "";

		switch (status) {
			case ChatStatus::Server:
				str = "Server";
				break;
			
			case ChatStatus::Chatting:
				str = "Chatting";
				break;

			case ChatStatus::Open: 
				str = "Open";
				break;

			default:
				str = "Unknown";
				break;
		}

		return str;
	}

	void OnClientDisconnect(std::string username) {
		std::cout << username << " Has Disconnected" << std::endl;
	}

	bool OnClientConnect(std::shared_ptr<Connection> connection) {
		//TODO: Can handle whether or not to deal with connections based on ip or their inital info from here
		return true;
	}

	void OnClientValidated(uint32_t id) {
		std::cout << "The Connection With Client ID: " << id << " Has Been Validated" << std::endl;
	}

private:
	asio::io_context m_Context;
	std::thread m_ContextThread;
	asio::ip::tcp::acceptor m_ASIOAcceptor;

	//Does not need to be the thread safe version as only a single thread (server thread) will handle this
	std::deque<std::shared_ptr<Connection>> m_Connections;
	//Need this to work so two clients can message eachother with consent
	std::unordered_map<std::string, int> m_Directory; //Associate a username with a connection index

	TSQueue<OwnedPacket> m_IncomingPackets;

	unsigned int m_IDCounter = 1000;
	int m_UserIndex = 0; //To be used in m_Directory

	TSQueue<ChatParty> m_PossibleParty;
	TSQueue<ChatParty> m_OngoingConversations;
};