#include <iostream>
#include <regex>
#include "../Networking/Client.h"
bool ProcessKeys(std::string& input, bool& accepted);
bool ProcessChatResponse(int responseCode, const std::string& receiver);
bool ProcessResponses(Client& client, const std::string& input, const std::string& sender);
void SendMsg(Client& client, const std::string& message);
int NumberShift(int numI);

int main() {
	std::string username;

	std::cout << "Welcome to Alvizo's chatroom!" << std::endl;

	bool validUsername = false;
	while (!validUsername) {
		std::cout << "Please Enter Your Username: ";
		std::getline(std::cin, username);



		if (username.size() > 15 || username.size() == 0 || !std::regex_match(username.begin(), username.end(), std::regex("^[a-zA-Z0-9]+$"))) {
			std::cout << "Invalid Username! Must be less then 15 characters long and not empty!" << std::endl << std::endl;
		}
		else {
			validUsername = true;
		}
	}
	
	Client client(username);
	client.Connect("127.0.0.1", 3000);
	std::vector<std::string> awaitingResponse; //Waiting for a response by users requesting to chat with this client
	
	bool leaveServer = false, accepted = false, chatting = false;
	char c;
	std::string inputStr = ""; //User Input
	std::string chatUser = "";
	while (!leaveServer) {
		if (client.isConnected()) {
			if (accepted && GetForegroundWindow() == GetConsoleWindow()) {
				if (ProcessKeys(inputStr, accepted) && inputStr != "") {
					if (awaitingResponse.size() > 0 && !chatting) { //Process awaiting connection requests by other users
						if(inputStr != "y" && inputStr != "n") {
							std::cout << "Invalid Response, Enter 'y' to Accept, 'n' to Reject" << std::endl;
						}else if (ProcessResponses(client, inputStr, awaitingResponse.back())) {
							chatUser = awaitingResponse.back();
							chatting = true;
							awaitingResponse.clear();
						}
						else {
							awaitingResponse.pop_back();

							if (!awaitingResponse.empty()) {
								std::cout << "The User " << awaitingResponse.back() << " Wants To Chat With You, Accept? (y/n)" << std::endl;
							}
							else {
								std::cout << "Enter The User You Want To Talk To" << std::endl;
							}
						}
					}
					else if (!chatting) { //Entering the username of the person the user wants to chat with
						if (inputStr != client.getUsername()) {
							Packet requestPacket(PacketType::ChatRequest);
							std::string str = inputStr;
							requestPacket << str;
							client.Send(requestPacket);
							std::cout << "Chatting Request Sent To " << inputStr << std::endl;
						}
						else {
							std::cout << "Invalid Response, You Cannot Chat With Yourself!" << std::endl;
						}
					}
					else { //User is chatting with someone
						if (inputStr == "!leave") {
							std::cout << "You Have Left the Conversation With " << chatUser << std::endl;

							Packet leavePacket(PacketType::LeaveConvo);
							leavePacket << client.getUsername();
							client.Send(leavePacket);
							chatUser = "";
							chatting = false;
						}
						else {
							SendMsg(client, inputStr);
						}
					}

					inputStr = "";
				}
			}

			//Read any packets coming in from the server
			if (!client.Incoming().isEmpty()) {
				Packet packet = client.Incoming().PopFront().m_Packet;

				switch (packet.m_Header.m_ID) {
					case PacketType::ServerAccept: {
						std::cout << username << ", You Are Now Online!" << std::endl;
						break;
					}

					case PacketType::OnlineList: {
						std::string list;
						packet >> list;
						std::cout << std::endl << "Users Online:" << std::endl << list;
						
						if (awaitingResponse.empty() && !chatting) {
							std::cout << "Enter The User You Want To Talk To" << std::endl;
						}

						inputStr = "";
						accepted = true;
						break;
					}

					case PacketType::Message: {
						std::string msg;
						packet >> msg;

						std::cout << chatUser << ": " << msg << std::endl;
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

						if (responseCode == 6) {
							chatting = false;
							chatUser = "";
							std::cout << "Enter The User You Want To Talk To" << std::endl;
						}

						if (ProcessChatResponse(responseCode, rec)) {
							chatting = true;
							chatUser = rec;
							awaitingResponse.clear();
						}

						break;
					}

					case PacketType::ChatAlert: {
						std::string userRequesting;
						packet >> userRequesting;
						awaitingResponse.push_back(userRequesting);
						std::cout << "The User " << awaitingResponse.back() << " Wants To Chat With You, Accept? (y/n)" << std::endl;
						break;
					}
					
					case PacketType::LeaveConvo: {
						std::cout << chatUser << " Has Left the Conversation!" << std::endl;
						chatUser = "";
						chatting = false;
						break;
					}

					default: {
						std::cout << "Error 401: Unknown information received" << std::endl;
						break;
					}
				}
			}
		}
		else {
			std::cout << "Error 404: Server Could Not Be Reached" << std::endl;
			leaveServer = true;
		}
	}

	std::cin.get();

	return 0;
}

void SendMsg(Client& client, const std::string& message) {
	Packet msg(PacketType::Message);
	std::string msgPacket = client.getUsername() + ":" + message;
	msg << msgPacket;
	client.Send(msg);
	std::cout << "You: " << message << std::endl;
}

bool ProcessResponses(Client& client, const std::string& input, const std::string& sender) {
	if (input == "y") {
		Packet acceptRequest(PacketType::ChatAlertResponse);
		std::string acceptMsg = client.getUsername() + ":" + sender + ":t";
		acceptRequest << acceptMsg;
		client.Send(acceptRequest);
		std::cout << "You Are Now Chatting With " << sender << ", Say Hello! Enter \"!leave\" to Exit the Conversation" << std::endl;
		return true;
	}
	else {
		Packet rejectRequest(PacketType::ChatAlertResponse);
		std::string rejectMsg = client.getUsername() + ":" + sender + ":f";
		rejectRequest << rejectMsg;
		client.Send(rejectRequest);
		return false;
	}
}

bool ProcessKeys(std::string& input, bool& accepted) {
	if (accepted) {
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

		case 6:
			std::cout << "Can't Chat! Reason: The User " << receiver << " Was Unable to be Reached!" << std::endl;
			return false;

		default:
			std::cout << "Unknown Error Code: " << responseCode << " Inquire To Developer!" << std::endl;
			return false;
	}
}