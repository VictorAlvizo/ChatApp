#pragma once
#include "Connection.h"
#include <unordered_map>

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
			//Setting up the file to log the server's activity
			std::time_t currentTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
			char timeChars[11];
			std::strftime(&timeChars[0], sizeof(timeChars), "%m-%d-%Y", std::localtime(&currentTime));

			std::string timeStr = CharArrToStr(timeChars, (sizeof(timeChars) / sizeof(char)) - 1); //There is an extra ' ' when doing it this way, need to remove it
			m_LogFilePath = "ServerLog/" + timeStr + ".txt";
			WriteToLog("Server Started Running");

			//Start running the server connection. Prime it to listen to incoming connections and handle them.
			ListenForConnections();
			//Listen for connections first before running the context so it dosen't exit right away. Keep the context busy
			m_ContextThread = std::thread([this]() {m_Context.run(); });
		}
		catch (const std::exception& e) {
			std::cout << "Server Start Failed! Exception Thrown: " << e.what() << std::endl;
			WriteToLog("Server Start Failed! Exception Thrown: " + CharArrToStr((char *)e.what(), sizeof(e.what()) / sizeof(char)));
			return false;
		}

		std::cout << "The Server is Now Running!" << std::endl;
		return true;
	}

	void Stop() {
		for (auto& client : m_Connections) {
			client->Disconnect();
		}

		m_Context.stop();

		if (m_ContextThread.joinable()) {
			m_ContextThread.join();
		}

		std::cout << "The Server Has Stopped Running" << std::endl;
		WriteToLog("The Server Has Stopped Running");
	}

	void ListenForConnections() {
		m_ASIOAcceptor.async_accept([this](std::error_code ec, asio::ip::tcp::socket socket) {
			if (!ec) {
				std::cout << "New Connection With Client: " << socket.remote_endpoint() << std::endl;
				WriteToLog("New Connection With Client");
				std::shared_ptr<Connection> newConnection = std::make_shared<Connection>(m_Context, std::move(socket), m_IncomingPackets);

				if (OnClientConnect(newConnection)) {
					m_Connections.push_back(std::move(newConnection));
					m_Connections.back()->ConnectToClient(++m_UserIndex, m_IDCounter++);
					std::cout << m_Connections.back()->getID() << " Connection Approved" << std::endl;
					WriteToLog(std::to_string(m_Connections.back()->getID()) + " Connection Approved");
				}
				else {
					socket.close();
				}
			}
			else {
				std::cout << "Error Connecting to Client: " << ec.message();
				WriteToLog("Error Connecting to Client: " + ec.message());
			}

			ListenForConnections(); //Have to keep calling this so it keeps listining and the context dosen't die
		});
	}

	bool RemoveClient(std::shared_ptr<Connection> client) {
		//Not the solution I would like as now m_Connection is filled with redundent connections; but
		//trying to remove it from the queue results in the program crashing.
		if (m_Directory.find(client->getAccount().m_AccUser) == m_Directory.end()) {
			std::cout << "The Username " << client->getAccount().m_AccUser << " Was Not Found During the Removal Process" << std::endl;
			WriteToLog("The Username " + client->getAccount().m_AccUser + " Was Not Found During the Removal Process");
			return false;
		}
		else {
			std::cout << "The User " << client->getAccount().m_AccUser << " Has Been Removed" << std::endl;
			WriteToLog("The User " + client->getAccount().m_AccUser + " Has Been Removed");
			OnClientDisconnect(client->getAccount().m_AccUser);
			m_Directory.erase(client->getAccount().m_AccUser);
			return true;
		}
	}

	bool MessageClient(std::string username, const Packet& packet) {
		std::shared_ptr<Connection> client = m_Connections[m_Directory[username]];

		if (client && client->isConnected()) {
			client->Send(packet);
			return true;
		}
		else {
			std::cout << "Failed Sending Packet To " << username << std::endl;
			WriteToLog("Failed Sending Packet to " + username);
			if (RemoveClient(client)) {
				client->IgnoreConnection(); //Not in RemoveClient as this overwrites username which may still be needed to log etc
				client.reset();
				SendOnlineList();
			}

			return false;
		}
	}

	void MessageAll(const Packet& packet, std::shared_ptr<Connection> ignoreClient = nullptr) {
		for (const auto& client : m_Directory) {
			std::shared_ptr<Connection> curClient = m_Connections[client.second];

			if (curClient && curClient->isConnected() && curClient != ignoreClient) {
				curClient->Send(packet);
			}
			else {
				if (curClient != ignoreClient) {
					std::cout << "Failed Sending Packet To " << curClient->getAccount().m_AccUser << std::endl;
					WriteToLog("Failed Sending Packet to " + curClient->getAccount().m_AccUser);
					if (RemoveClient(curClient)) {
						curClient->IgnoreConnection();
						curClient.reset();
						SendOnlineList();
					}
				}
			}
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
			case PacketType::AccountInfo: {
				HandleAccount(client);
				break;
			}

			case PacketType::ChangePassword: {
				std::string str, user, newPass;
				packet >> str;
				user = str.substr(0, str.find("#"));
				newPass = str.substr(str.find("#") + 1, str.size());
				ChangePassword(user, newPass);
				break;
			}

			case PacketType::Validated: {
				int validationResult;
				packet >> validationResult;
				OnClientValidated(client->getID(), static_cast<bool>(validationResult));
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

			case PacketType::ClientExit: {
				if (client->m_Status == ChatStatus::Chatting) {
					LeavingConvo(client->getAccount().m_AccUser);
				}

				std::cout << "The User " << client->getAccount().m_AccUser << " Has Left" << std::endl;
				WriteToLog("The User " + client->getAccount().m_AccUser + " Has Left");
				RemoveClient(client);
				client->IgnoreConnection();
				client.reset();

				if (m_Directory.size() != 0) {
					SendOnlineList();
				}
				break;
			}

			default: {
				std::cout << "Packet Type Unknown! Packet Information:" << std::endl;
				std::cout << packet << std::endl;
				WriteToLog("Packet Type Unknown! Header Number: " + std::to_string((int)packet.m_Header.m_ID));
				break;
			}
		}
	}

	void LeavingConvo(const std::string& user, unsigned int presignedIndex = -1, std::string receiver = "") {
		unsigned int index = presignedIndex;

		if (presignedIndex == -1) { //Not set by ProcessMessage failure, have to find it
			for (unsigned int i = 0; i < m_OngoingConversations.count(); i++) {
				if (m_OngoingConversations[i].m_InitUser->getAccount().m_AccUser == user) {
					index = i;
					receiver = m_OngoingConversations[i].m_RecUser->getAccount().m_AccUser;
					break;
				}
				else if (m_OngoingConversations[i].m_RecUser->getAccount().m_AccUser == user) {
					index = i;
					receiver = m_OngoingConversations[i].m_InitUser->getAccount().m_AccUser;
					break;
				}
			}
		}

		std::cout << user << " is Leaving the Conversation With " << receiver << std::endl;
		WriteToLog(user + " is Leaving the Conversation With " + receiver);

		m_OngoingConversations[index].m_InitUser->m_Status = ChatStatus::Open;
		m_OngoingConversations[index].m_RecUser->m_Status = ChatStatus::Open;
		m_OngoingConversations.Erase(index);

		Packet leaveMessage(PacketType::LeaveConvo);
		MessageClient(receiver, leaveMessage);
	}

	void ProcessMessage(const std::string& str) {
		std::string sender = str.substr(0, str.find(":"));
		std::string fullMsg = str.substr(str.find(":") + 1, str.size());

		std::string receiver = "";
		unsigned int index = -1;
		for (unsigned int i = 0; i < m_OngoingConversations.count(); i++) {
			if (m_OngoingConversations[i].m_InitUser->getAccount().m_AccUser == sender) {
				index = i;
				receiver = m_OngoingConversations[i].m_RecUser->getAccount().m_AccUser;
				break;
			}
			else if (m_OngoingConversations[i].m_RecUser->getAccount().m_AccUser == sender) {
				index = i;
				receiver = m_OngoingConversations[i].m_InitUser->getAccount().m_AccUser;
				break;
			}
		}

		Packet msgPacket(PacketType::Message);
		msgPacket << fullMsg;

		if (!MessageClient(receiver, msgPacket)) {
			LeavingConvo(receiver, index, sender);
		}
	}

	void HandleChatAlertResponse(const std::string& init, const std::string& rec, bool accepted) {
		//Find the ChatParty in the possible pool of chatting connections
		size_t index = 0;
		for (unsigned int i = 0; i < m_PossibleParty.count(); i++) {
			if (m_PossibleParty[i].m_InitUser->getAccount().m_AccUser == init && m_PossibleParty[i].m_RecUser->getAccount().m_AccUser == rec) {
				index = i;
				break;
			}
		}

		if (index == -1) {
			std::cout << "Unable to Find the Party For " << init << " and " << rec << std::endl;
			WriteToLog("Unable to Find the Party For " + init + " and " + rec);
		}
		
		if (!m_Connections[m_Directory[init]] || !m_Connections[m_Directory[init]]->isConnected()) {
			std::cout << "User " << init << " Was Unable to be Reached During the Alert Process" << std::endl;
			WriteToLog("User " + init + " Was Unable to be Reached During the Alert Process");
			Packet unreachlePacket(PacketType::ChatResponse);
			std::string msg = init + ":" + std::to_string(4);
			m_PossibleParty.Erase(index);
			MessageClient(rec, unreachlePacket);
			RemoveClient(m_Connections[m_Directory[init]]);
			SendOnlineList();
		}
		else {
			//Handle the responses given
			if (accepted) {
				//Done here as I have a confirmed index as Back() might change to another chatparty and status remains as it's a pointer
				m_PossibleParty[index].m_InitUser->m_Status = ChatStatus::Chatting;
				m_PossibleParty[index].m_RecUser->m_Status = ChatStatus::Chatting;

				std::cout << m_PossibleParty[index].m_InitUser->getAccount().m_AccUser << " is Now Chatting With " << m_PossibleParty[index].m_RecUser->getAccount().m_AccUser << std::endl;
				WriteToLog(m_PossibleParty[index].m_InitUser->getAccount().m_AccUser + " is Now Chatting With " + m_PossibleParty[index].m_RecUser->getAccount().m_AccUser);

				m_OngoingConversations.PushBack(m_PossibleParty[index]);
				Packet acceptPacket(PacketType::ChatResponse);
				acceptPacket << std::string(rec + ":" + std::to_string(0));
				SendOnlineList(); //Sending it here first as the connection reads packet from the Front(), allows chatting bool in main to hold true
				MessageClient(init, acceptPacket);
			}
			else {
				Packet rejectPacket(PacketType::ChatResponse);
				rejectPacket << std::string(rec + ":" + std::to_string(5));
				MessageClient(init, rejectPacket);
			}

			m_PossibleParty.Erase(index);
		}
	}

	void HandleChatRequest(std::shared_ptr<Connection> client, const std::string& receiver) {
		if (m_Connections.size() == 1) { //User is alone, no one to connect to
			Packet rejectRequest(PacketType::ChatResponse);
			rejectRequest << std::string(receiver + ":" + std::to_string(1));
			MessageClient(client->getAccount().m_AccUser, rejectRequest);
		}
		else if (m_Directory.find(receiver) == m_Directory.end() || receiver == "$invalid") { //Can't find user
			Packet rejectRequest(PacketType::ChatResponse);
			rejectRequest << std::string(receiver + ":" + std::to_string(2));
			MessageClient(client->getAccount().m_AccUser, rejectRequest);
		}
		else if (m_Connections[m_Directory[receiver]]->m_Status == ChatStatus::Chatting) { //User is chatting
			Packet rejectRequest(PacketType::ChatResponse);
			rejectRequest << std::string(receiver + ":" + std::to_string(3));
			MessageClient(client->getAccount().m_AccUser, rejectRequest);
		}
		else {
			ChatParty party(m_Connections[m_Directory[client->getAccount().m_AccUser]], m_Connections[m_Directory[receiver]]);
			Packet alertReciever(PacketType::ChatAlert);
			alertReciever << client->getAccount().m_AccUser;

			if (!MessageClient(receiver, alertReciever)) {
				Packet rejectRequest(PacketType::ChatResponse);
				rejectRequest << std::string(receiver + ":" + std::to_string(4));
				MessageClient(client->getAccount().m_AccUser, rejectRequest);
			}
			else { //Possible party, push it into possible pool
				m_PossibleParty.PushBack(party);
			}
		}
	}

	void HandleAccount(std::shared_ptr<Connection> client) {
		if (isOnline(client->getAccount().m_AccUser)) {
			std::cout << "Someone Tried Logging onto " << client->getAccount().m_AccUser << " While Account Was Online" << std::endl;
			WriteToLog("Someone Tried Logging onto " + client->getAccount().m_AccUser + " While Account Online");
			RejectConnection(client, 5);
			return;
		}

		std::ifstream accountFile("./Accounts/AccStorage.txt", std::ios_base::binary);
		if (!accountFile.is_open()) {
			accountFile.close();
			RejectConnection(client, 4);
			std::cout << "Unable to Open The Account File!" << std::endl;
			WriteToLog("Unable to Open Account File!");
			return;
		}
		accountFile.close();

		Account tempAcc = GetAccDatabase(client->getAccount().m_AccUser);

		if (tempAcc.m_AccOpt == 0) {
			std::cout << "Banned User " << tempAcc.m_AccUser << " Has Tried Logging in" << std::endl;
			WriteToLog("Banned User " + tempAcc.m_AccUser + " Has Tried Logging in");
			RejectConnection(client, 6);
		}
		else if (tempAcc.m_AccOpt == -1 && client->getAccount().m_AccOpt == 1) {
			std::cout << "Login Attempt Failed! " << client->getAccount().m_AccUser << " Was Not Found!" << std::endl;
			WriteToLog("Login Attempt Failed! " + client->getAccount().m_AccUser + " Was Not Found!");
			RejectConnection(client, 1);
		}
		else if (tempAcc.m_AccOpt == -1 && client->getAccount().m_AccOpt == 2) {
			if (!RegisterAccount(client->getAccount().m_AccUser, client->getAccount().m_AccPass, "A")) {
				std::cout << "Error Writing Account to File!" << std::endl;
				WriteToLog("Error Writing Account to File!");
				RejectConnection(client, 3);
			}
			else {
				std::cout << client->getAccount().m_AccUser << " Has Now Registered and Connected With ID: " << client->getID() << std::endl;
				WriteToLog(client->getAccount().m_AccUser + " Has Now Registered and Connected With ID: " + std::to_string(client->getID()));
				AcceptConnection(client);
			}
		}
		else if (tempAcc.m_AccUser == client->getAccount().m_AccUser && client->getAccount().m_AccOpt == 2) {
			std::cout << "New Connection Getting Rejected For Having A Taken Username: " << client->getAccount().m_AccUser << std::endl;
			WriteToLog("New Connection Getting Rejected For Having A Taken Username: " + client->getAccount().m_AccUser);
			RejectConnection(client, 0);
		}
		else if (tempAcc.m_AccUser == client->getAccount().m_AccUser && tempAcc.m_AccPass == client->getAccount().m_AccPass && client->getAccount().m_AccOpt == 1) {
			std::cout << client->getAccount().m_AccUser << " is Now Connected With ID: " << client->getID() << std::endl;
			WriteToLog(client->getAccount().m_AccUser + " is Now Connected With ID: " + std::to_string(client->getID()));
			AcceptConnection(client);
		}
		else{
			std::cout << "Login Attempt to " << tempAcc.m_AccUser << " Failed! Incorrect Password" << std::endl;
			WriteToLog("Login Attempt to " + tempAcc.m_AccUser + " Failed! Incorrect Password");
			RejectConnection(client, 2);
		}
	}

	Account GetAccDatabase(const std::string& clientUsername) {
		char firstChar = clientUsername[0];
		Account account;
		bool accountFound = false;

		std::ifstream accountFile("./Accounts/AccStorage.txt", std::ios_base::binary);
		std::string initalKey = "=XrH'EW6!*K$98&3";

		std::string username, password;
		std::string line;
		int digitSize;
		while (std::getline(accountFile, line)) {
			//Tells how if the digit describing the size of the username is 1 (7) digit or 2 (13) digits long
			digitSize = static_cast<int>(line[0] ^ '~') - 48; //-48 to get the actual number not the asciis

			std::string digitStrForm;
			int offset = 1;
			for (int i = 0; i < digitSize; i++) {
				if (line[i + offset] == '\\') {
					digitStrForm += '\n' ^ initalKey[i % initalKey.size()];
					offset += 1;
				}
				else {
					digitStrForm += line[i + offset] ^ initalKey[i % initalKey.size()];
				}
			}
			int usernameLength = std::stoi(digitStrForm);

			int usernameOffset = offset + digitSize;
			std::string encryptedUsername;
			for (int i = 0; i < usernameLength; i++) {
				encryptedUsername += line[i + usernameOffset];

				if (i == 0) {
					if (XORString(encryptedUsername, initalKey)[0] != firstChar) {
						continue;
					}
				}
			}

			username = XORString(encryptedUsername, initalKey);
			if (username != clientUsername) {
				continue;
			}
			
			accountFound = true;
			std::string saltedKey = HashStr(username, initalKey);
			int passOffset = usernameOffset + usernameLength;

			std::string encryptedCombo;
			for (int i = 0; i < line.size() - passOffset; i++) {
				encryptedCombo += line[i + passOffset];
			}

			std::string comboStr = XORString(encryptedCombo, saltedKey);

			password = comboStr.substr(0, comboStr.find(" "));
			std::string status = comboStr.substr(comboStr.find(" ") + 1, 1);
			account.SetInfo(username, password, (status == "A") ? 1 : 0); //Option is used as status here (1: Valid 0: Banend)
			break;
		}

		if (!accountFound) {
			account.m_AccOpt = -1;
		}

		accountFile.close();
		return account;
	}

	void ChangePassword(const std::string& user, const std::string& newPassword) {
		std::vector<Account> organizedAccounts;
		std::ifstream readFile("./Accounts/AccStorage.txt", std::ios_base::binary);

		if (!readFile.is_open()) {
			std::cout << "Failed to Open Read File When Changing Password" << std::endl;
			WriteToLog("Failed to Open Read File When Changing Password");
			readFile.close();
			return;
		}

		std::string initalKey = "=XrH'EW6!*K$98&3";

		std::string line;
		int digitSize;
		while (std::getline(readFile, line)) {
			digitSize = static_cast<int>(line[0] ^ '~') - 48;

			std::string digitStrForm;
			int offset = 1;
			for (int i = 0; i < digitSize; i++) {
				if (line[i + offset] == '\\') {
					digitStrForm += '\n' ^ initalKey[i % initalKey.size()];
					offset += 1;
				}
				else {
					digitStrForm += line[i + offset] ^ initalKey[i % initalKey.size()];
				}
			}
			int usernameLength = std::stoi(digitStrForm);

			int usernameOffset = offset + digitSize;
			std::string encryptedUsername;
			for (int i = 0; i < usernameLength; i++) {
				encryptedUsername += line[i + usernameOffset];
			}

			std::string username = XORString(encryptedUsername, initalKey);
			std::string saltedKey = HashStr(username, initalKey);
			int passOffset = usernameOffset + usernameLength;

			std::string encryptedCombo;
			for (int i = 0; i < line.size() - passOffset; i++) {
				encryptedCombo += line[i + passOffset];
			}

			std::string comboStr = XORString(encryptedCombo, saltedKey);
			std::string password = comboStr.substr(0, comboStr.find(" "));
			std::string status = comboStr.substr(comboStr.find(" ") + 1, 1);
			
			organizedAccounts.push_back({ username, (user == username) ? newPassword : password, (status == "A" ? 1 : 0) });
		}
		readFile.close();

		std::remove("./Accounts/AccStorage.txt");

		std::sort(organizedAccounts.begin(), organizedAccounts.end(), [](const Account& left, const Account& right) {
			int cl = static_cast<int>((static_cast<int>(left.m_AccUser[0]) <= 57) ? left.m_AccUser[0] : std::toupper(left.m_AccUser[0]));
			int cr = static_cast<int>((static_cast<int>(right.m_AccUser[0]) <= 57) ? right.m_AccUser[0] : std::toupper(right.m_AccUser[0]));

			return cl < cr;
		});

		for (Account& acc : organizedAccounts) {
			RegisterAccount(acc.m_AccUser, acc.m_AccPass, (acc.m_AccOpt == 0) ? "B" : "A");
		}

		std::cout << user << " Has Changed Their Password" << std::endl;
		WriteToLog(user + " Changed Their Password");
	}

	bool RegisterAccount(std::string username, std::string password, std::string status, std::string filePath = "./Accounts/AccStorage.txt") {
		std::ofstream writeFile(filePath, std::ios_base::binary | std::ios_base::app);
		if (!writeFile.is_open()) {
			writeFile.close();
			return false;
		}

		std::string initalKey = "=XrH'EW6!*K$98&3";
		char digitSize = ((username.size() >= 10) ? '2' : '1') ^ '~';
		writeFile << digitSize;

		std::string usernameLength = XORString(std::to_string(static_cast<int>(username.size())), initalKey);
		writeFile << usernameLength;

		std::string encryptedUsername = XORString(username, initalKey);
		writeFile << encryptedUsername;

		std::string saltedKey = HashStr(username, initalKey);
		std::string encryptedCombo = XORString(password + " " + status, saltedKey);
		writeFile << encryptedCombo << "\n";

		return true;
	}

	std::string HashStr(std::string& str1, std::string& str2) {
		std::string hashedStr;
		for (int i = 0; i < (str1.size() + str2.size()) / 2; i++) {
			hashedStr += static_cast<char>((str1[i % str1.size()] & str2[i % str2.size()]) ^ str2[i % str2.size()]);
		}

		return hashedStr;
	}

	std::string XORString(std::string str, std::string key) {
		std::string encryptedStr;
		std::string fixedStr = str;

		if (str.find("\\n") != std::string::npos) {
			std::string fixedStr = ReplaceAll(str, "\\n", "\n");
			//Has to be here as the checking for "\n" will mess and undo everything ReplaceAll does
			for (int i = 0; i < fixedStr.size(); i++) {
				encryptedStr += fixedStr[i] ^ key[i % key.size()];
			}
		}
		else {
			for (int i = 0; i < fixedStr.size(); i++) {
				char c = fixedStr[i] ^ key[i % key.size()];

				if (c == '\n') {
					encryptedStr += "\\n";
				}
				else {
					encryptedStr += c;
				}
			}
		}

		return encryptedStr;
	}

	std::string ReplaceAll(std::string str, std::string replace, std::string replaceWith) {
		std::string newStr;

		int findPos = str.find(replace), lastPos = 0;
		while (findPos != std::string::npos) {
			newStr.append(str, 0, findPos); //Copy the part of string before the replace token
			newStr += replaceWith;
			lastPos = findPos + replace.length();
			findPos = str.find(replace, lastPos);
		}

		newStr += str.substr(lastPos); //Copy the rest
		return newStr;
	}

	void AcceptConnection(std::shared_ptr<Connection> client) {
		client->ClientConnectionAction(true);
		m_Directory[client->getAccount().m_AccUser] = client->getPermIndex();
		Packet accept(PacketType::ServerAccept);
		MessageClient(client->getAccount().m_AccUser, accept);
		SendOnlineList();
	}

	void RejectConnection(std::shared_ptr<Connection> client, int rejectionCode) {
		client->ClientConnectionAction(false);
		Packet invalidInfo(PacketType::ServerReject, rejectionCode);
		client->Send(invalidInfo);

		if (rejectionCode == 6) {
			Packet kickPacket(PacketType::LeaveServer);
			client->Send(kickPacket);
			client->IgnoreConnection();
			client.reset();
		}
	}

	bool isOnline(const std::string& username) {
		for (auto const& client : m_Directory) {
			if (client.first == username) {
				return true;
			}
		}

		return false;
	}

	void SendOnlineList() { //Send the client a list of users they can join
		if (m_Directory.size() > 1) {
			for (auto& client : m_Connections) {
				if (client->getID() != 0) {
					Packet onlineList(PacketType::OnlineList);
					std::string str = "";

					for (auto& printClient : m_Connections) {
						if (client->getID() != printClient->getID() && printClient->getID() != 0 && printClient->isApproved()) {
							str += printClient->getAccount().m_AccUser + " | " + StatusTranslator(printClient->m_Status) + "\n";
						}
					}

					onlineList << str;
					MessageClient(client->getAccount().m_AccUser, onlineList);
				}
			}
		}
		else {
			Packet onlineList(PacketType::OnlineList);
			std::string str = "You Are The Only One Online!\n";
			onlineList << str;

			MessageClient(m_Directory.begin()->first, onlineList);
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
		WriteToLog(username + " Has Disconnected");
	}

	bool OnClientConnect(std::shared_ptr<Connection> connection) {
		//Can handle whether or not to deal with connections based on ip or their inital information from here
		return true;
	}

	void OnClientValidated(uint32_t id, bool validationPassed) {
		if (validationPassed) {
			std::cout << "The Connection With Client ID: " << id << " Has Been Validated" << std::endl;
			WriteToLog("The Connection With Client ID: " + std::to_string(id) + " Has Been Validated");
		}
		else {
			std::cout << "Client ID: " << id << " Failed The Validation, Connection Unsucessful!" << std::endl;
			WriteToLog("Client ID: " + std::to_string(id) + " Failed The Validation, Connection Unsucessful!");
		}
	}

	void WriteToLog(const std::string& message) {
		std::ofstream file(m_LogFilePath, std::ios_base::app);

		//[09:23:02 PM]: This is an example message
		if (!file.is_open()) {
			std::cout << "Error Writing to Log File! Could Not Open Path: " << m_LogFilePath << std::endl;
		}
		else {
			std::time_t currentTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

			char hour24[3];
			std::strftime(&hour24[0], sizeof(hour24), "%H", std::localtime(&currentTime));
			bool isPM = (hour24[0] == '1' && static_cast<int>(hour24[1]) >= 2) || (hour24[0] == '2');

			char timeChars[9];
			std::strftime(&timeChars[0], sizeof(timeChars), "%I:%M:%S", std::localtime(&currentTime));
			std::string curTime = CharArrToStr(timeChars, (sizeof(timeChars) / sizeof(char)) - 1);
			std::string timeSig = (isPM) ? " PM" : " AM";

			std::string fullMessage = "[" + curTime + timeSig + "]: " + message;
			file << fullMessage << "\n";
		}

		file.close();
	}

	std::string CharArrToStr(char * cstr, int size) {
		std::string s = "";
		for (int i = 0; i < size; i++) {
			s += cstr[i];
		}

		return s;
	}

private:
	asio::io_context m_Context;
	std::thread m_ContextThread;
	asio::ip::tcp::acceptor m_ASIOAcceptor;
	
	std::string m_LogFilePath;

	//Does not need to be the thread safe version as only a single thread (server thread) will handle this
	std::deque<std::shared_ptr<Connection>> m_Connections;
	//Need this to work so two clients can message eachother with consent
	std::unordered_map<std::string, int> m_Directory; //Associate a username with a connection index

	TSQueue<OwnedPacket> m_IncomingPackets;

	unsigned int m_IDCounter = 1000;
	int m_UserIndex = -1; //To be used in m_Directory, and keep track of connection array index

	TSQueue<ChatParty> m_PossibleParty;
	TSQueue<ChatParty> m_OngoingConversations;
};
