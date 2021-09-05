#include <iostream>
#include <regex>
#include <map>
#include "../Networking/Client.h"
bool ProcessKeys(std::string& input);
bool ProcessChatResponse(int responseCode, const std::string& receiver);
bool ProcessResponses(std::string& input, const std::string& sender);
void SendChatRequest(std::string& input);
void MessagePartner(std::string& input);
void ProcessPackets();
void ProcessPendingRequest(std::string& input);
void SendMsg(const std::string& message);
int NumberShift(int numI);
std::string GatherInput();
BOOL WINAPI ConsoleHandle(DWORD cEvent);

std::map<int, std::string> g_RejectionReasons;
Client * g_Client;

int main() {
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)ConsoleHandle, TRUE);

	g_RejectionReasons[0] = "Username is Already Taken";
	g_RejectionReasons[1] = "Username Not Found";
	g_RejectionReasons[2] = "Incorrect Password";
	g_RejectionReasons[3] = "Server Failed to Save Account";
	g_RejectionReasons[4] = "Server Failed to Read File";
	g_RejectionReasons[5] = "This Account is Currently Online";
	g_RejectionReasons[6] = "This Account is Banned";

	std::string username;
	std::string password;
	int option;

	std::cout << "Welcome to Alvizo's chatroom!" << std::endl;
	std::cout << "1.Log in\n2.Sign Up" << std::endl;

	bool validOption = false;
	while (!validOption) {
		std::cin >> option;
		std::cout << option << std::endl;

		if (option != 1 && option != 2) {
			std::cout << "Invalid Input! You must either register an account or log in!" << std::endl;
			std::cin.clear();
		}
		else {
			validOption = true;
		}

		std::cin.get();
	}

	std::cout << "Username: ";
	username = GatherInput();
	std::cout << "Password: ";
	password = GatherInput();

	g_Client = new Client();
	g_Client->Connect("127.0.0.1", 3000, username, password, option);

	bool leaveServer = false;
	std::string inputStr = ""; //User Input
	while (!leaveServer) {
		if (g_Client->isConnected()) {
			if (!g_Client->m_AccountProcessed) {
				g_Client->m_AccountProcessed = true; //This will become false if it gets rejected and information needs to be entered again
				std::cout << "Username: ";
				username = GatherInput();
				std::cout << "Password: ";
				password = GatherInput();
				g_Client->EnterAccount(username, password, option);
			}

			if (g_Client->m_Accepted && GetForegroundWindow() == GetConsoleWindow()) {
				if (ProcessKeys(inputStr) && inputStr != "") {
					if (g_Client->m_AwaitingRequest.size() > 0 && !g_Client->m_Chatting) { //Process awaiting connection requests by other users
						ProcessPendingRequest(inputStr);
					}
					else if (!g_Client->m_Chatting) { //Entering the username of the person the user wants to chat with
						SendChatRequest(inputStr);
					}
					else { //User is chatting with someone, handle the message they client wants to send
						MessagePartner(inputStr);
					}

					inputStr = "";
				}
			}

			ProcessPackets();
		}
		else {
			std::cout << "Error 404: Server Could Not Be Reached" << std::endl;
			leaveServer = true;
		}
	}

	std::cin.get();
	delete g_Client;
	g_Client = nullptr;

	return 0;
}

std::string GatherInput() {
	std::string str;
	bool validInput = false;
	
	while (!validInput) {
		std::getline(std::cin, str);

		if (str.size() > 15 || str.size() == 0 || !std::regex_match(str.begin(), str.end(), std::regex("^[a-zA-Z0-9]+$"))) {
			std::cout << "Invalid Input! Must be less then 15 characters long and not empty!" << std::endl << std::endl;
		}
		else {
			validInput = true;
		}
	}

	return str;
}

void SendMsg(const std::string& message) {
	Packet msg(PacketType::Message);
	msg << std::string(g_Client->getAccount().m_AccUser + ":" + message);
	g_Client->Send(msg);
	std::cout << "You: " << message << std::endl;
}

void ProcessPackets() {
	if (!g_Client->Incoming().isEmpty()) {
		Packet packet = g_Client->Incoming().PopFront().m_Packet;

		switch (packet.m_Header.m_ID) {
			case PacketType::ServerAccept: {
				std::cout << g_Client->getAccount().m_AccUser << ", You Are Now Online!" << std::endl;
				g_Client->m_Accepted = true;
				g_Client->m_AccountProcessed = true;
				break;
			}

			case PacketType::ServerReject: {
				g_Client->m_AccountProcessed = false;
				int reason;
				packet >> reason;
				std::cout << "You Have Been Rejected! Reason: " << g_RejectionReasons[reason] << std::endl;
				break;
			}

			case PacketType::OnlineList: {
				std::string list;
				packet >> list;
				std::cout << std::endl << "Users Online:" << std::endl << list;

				if (g_Client->m_AwaitingRequest.empty() && !g_Client->m_Chatting) {
					std::cout << "Enter The User You Want To Talk To" << std::endl;
				}
				break;
			}

			case PacketType::Message: {
				std::string msg;
				packet >> msg;

				std::cout << g_Client->m_ChattingWith << ": " << msg << std::endl;
				break;
			}

			case PacketType::MessageAll: {
				std::string message;
				packet >> message;
				std::cout << "Server [EVERYONE]: " << message << std::endl;
				break;
			}

			case PacketType::ServerMessage: {
				std::string message;
				packet >> message;
				std::cout << "Server: " << message << std::endl;
				break;
			}

			case PacketType::ChatResponse: {
				std::string response;
				packet >> response;

				std::string rec = response.substr(0, response.find(":"));
				int responseCode = std::stoi(response.substr(response.find(":") + 1, 1));

				if (responseCode == 4) {
					g_Client->m_Chatting = false;
					g_Client->m_ChattingWith = "";
					std::cout << "Enter The User You Want To Talk To" << std::endl;
				}

				if (ProcessChatResponse(responseCode, rec)) {
					g_Client->m_Chatting = true;
					g_Client->m_ChattingWith = rec;
					g_Client->m_AwaitingRequest.clear();
				}

				break;
			}

			case PacketType::ChatAlert: {
				std::string userRequesting;
				packet >> userRequesting;
				g_Client->m_AwaitingRequest.push_back(userRequesting);
				std::cout << "The User " << g_Client->m_AwaitingRequest.back() << " Wants To Chat With You, Accept? (y/n)" << std::endl;
				break;
			}

			case PacketType::LeaveConvo: {
				std::cout << g_Client->m_ChattingWith << " Has Left the Conversation!" << std::endl;
				g_Client->m_ChattingWith = "";
				g_Client->m_Chatting = false;
				break;
			}
										
			case PacketType::LeaveServer: {
				std::cout << "You Have Been Kicked From The Sever!" << std::endl;
				Packet leaveConversation(PacketType::LeaveConvo);
				leaveConversation << g_Client->getAccount().m_AccUser;
				g_Client->Send(leaveConversation);
				g_Client->m_Chatting = false;
				g_Client->Disconnect();
				break;
			}

			default: {
				std::cout << "Error 401: Unknown information received" << std::endl;
				break;
			}
		}
	}
}

void ProcessPendingRequest(std::string& input) {
	if (input != "y" && input != "n") {
		std::cout << "Invalid Response, Enter 'y' to Accept, 'n' to Reject" << std::endl;
	}
	else if (ProcessResponses(input, g_Client->m_AwaitingRequest.back())) {
		g_Client->m_ChattingWith = g_Client->m_AwaitingRequest.back();
		g_Client->m_Chatting = true;
		g_Client->m_AwaitingRequest.clear();
	}
	else {
		g_Client->m_AwaitingRequest.pop_back();

		if (!g_Client->m_AwaitingRequest.empty()) {
			std::cout << "The User " << g_Client->m_AwaitingRequest.back() << " Wants To Chat With You, Accept? (y/n)" << std::endl;
		}
		else {
			std::cout << "Enter The User You Want To Talk To" << std::endl;
		}
	}
}

bool ProcessResponses(std::string& input, const std::string& sender) {
	if (input == "y") {
		Packet acceptRequest(PacketType::ChatAlertResponse);
		acceptRequest << std::string(g_Client->getAccount().m_AccUser + ":" + sender + ":t");
		g_Client->Send(acceptRequest);
		std::cout << "You Are Now Chatting With " << sender << ", Say Hello! Enter \"!leave\" to Exit the Conversation" << std::endl;
		return true;
	}
	else {
		Packet rejectRequest(PacketType::ChatAlertResponse);
		rejectRequest << std::string(g_Client->getAccount().m_AccUser + ":" + sender + ":f");
		g_Client->Send(rejectRequest);
		return false;
	}
}

void SendChatRequest(std::string& input) {
	if (input == "!changePass") {
		std::string oldPasswordGuess, temp, newPassword;
		std::cout << "Enter Your Old Password: ";
		
		std::getline(std::cin, temp);
		oldPasswordGuess = GatherInput();

		if (oldPasswordGuess == g_Client->getAccount().m_AccPass) {
			std::cout << std::endl << "Enter Your New Password: ";
			newPassword = GatherInput();

			std::cout << "New Password is Now: " << newPassword << std::endl;
			Packet chgPass(PacketType::ChangePassword);
			chgPass << std::string(g_Client->getAccount().m_AccUser + "#" + newPassword);
			g_Client->Send(chgPass);
		}
		else {
			std::cout << "The Password: " << oldPasswordGuess << " is Incorrect!" << std::endl;
		}
	}else if (input != g_Client->getAccount().m_AccUser) {
		Packet requestPacket(PacketType::ChatRequest);
		requestPacket << input;
		g_Client->Send(requestPacket);
		std::cout << "Chatting Request Sent To " << input << std::endl;
	}
	else {
		std::cout << "Invalid Response, You Cannot Chat With Yourself!" << std::endl;
	}
}

void MessagePartner(std::string& input) {
	if (input == "!leave") {
		std::cout << "You Have Left the Conversation With " << g_Client->m_ChattingWith << std::endl;

		Packet leavePacket(PacketType::LeaveConvo);
		leavePacket << g_Client->getAccount().m_AccUser;
		g_Client->Send(leavePacket);
		g_Client->m_ChattingWith = "";
		g_Client->m_Chatting = false;
	}
	else {
		SendMsg(input);
	}
}

bool ProcessKeys(std::string& input) {
	if (g_Client->m_Accepted) {
		bool upper = false;
		for (int i = 32; i < 122; i++) {
			upper = false;
			if (GetAsyncKeyState(VK_SHIFT)) {
				upper = true;
			}

			if (GetAsyncKeyState(i) & 0x01) {
				if (i >= 65 && i <= 90) {
					input += (upper) ? static_cast<char>(i) : static_cast<char>(i + 32);
				}
				else {
					if (upper) {
						input += static_cast<char>(NumberShift(i));
					}
					else {
						input += static_cast<char>(i);
					}
				}

				std::cout << input << std::endl;
			}

			if (GetAsyncKeyState(VK_BACK) & 0x01 && input.size() >= 1) {
				input.pop_back();
				std::cout << input << std::endl;
			}

			if (GetAsyncKeyState(VK_RETURN) & 0x01) {
				return true; //Return true once the input has finished
			}
		}

		return false; //Still writing input
	}
}

int NumberShift(int numI) {
	switch (numI) {
		case 48: //0
			return 41;

		case 49: //1
			return 33;

		case 50: //2
			return 64;

		case 51: //3
			return 35;

		case 52: //4
			return 36;

		case 53: //5
			return 37;

		case 54: //6
			return 94;
			
		case 55: //7
			return 38;

		case 56: //8
			return 42;

		case 57: //9
			return 40;

		default:
			return numI;
	}
}

bool ProcessChatResponse(int responseCode, const std::string& receiver) {
	switch (responseCode) {
		case 0:
			std::cout << "You Are Now Chatting With " << receiver << ", Say Hello! Enter \"!leave\" to Exit the Conversation" << std::endl;
			return true;

		case 1:
			std::cout << "Can't Chat! Reason: Unable to Chat as Nobody is Online" << std::endl;
			return false;

		case 2:
			std::cout << "Can't Chat! Reason: The User " << receiver << " is Not Online!" << std::endl;
			return false;

		case 3:
			std::cout << "Can't Chat! Reason: The User " << receiver << " is Already Chatting With Someone!" << std::endl;
			return false;

		case 4:
			std::cout << "Can't Chat! Reason: The User " << receiver << " Was Unable to be Reached!" << std::endl;
			return false;

		case 5:
			std::cout << "Can't Chat! Reason: The User " << receiver << " Has Rejected Your Request" << std::endl;
			return false;

		default:
			std::cout << "Unknown Error Code: " << responseCode << " Inquire To Developer!" << std::endl;
			return false;
	}
}

BOOL WINAPI ConsoleHandle(DWORD cEvent) {
	if (cEvent == CTRL_CLOSE_EVENT && g_Client != nullptr && g_Client->isConnected()) {
		if (g_Client->m_Accepted) {
			Packet leaving(PacketType::ClientExit);
			g_Client->Send(leaving);
			g_Client->m_ChattingWith = "";
			g_Client->m_Chatting = false;
		}

		g_Client->Disconnect();
	}

	return true;
}