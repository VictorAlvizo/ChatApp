#pragma once
#include "NetIncludes.h"

struct PacketHeader {
	PacketType m_ID; //What type of message it will be
	uint32_t m_Size; //The size of the body so it can allocate enough space to read it
};

class Packet {
public:
	Packet(PacketType type = PacketType::Message) {
		m_Header.m_ID = type;
	}

	template<typename T>
	Packet(PacketType type, const T& data) {
		m_Header.m_ID = type;
		static_assert(std::is_standard_layout<T>::value, "Data is too complex to be used");

		//Input the new data and update the size of the body
		size_t offset = m_Body.size();
		m_Body.resize(m_Body.size() + sizeof(T));
		std::memcpy(m_Body.data() + offset, &data, sizeof(T));

		m_Header.m_Size = m_Body.size(); //Set the header size information = body so it can allocate the correct amount
	}

	Packet(PacketType type, const std::string& str) {
		m_Header.m_ID = type;
		std::copy(str.begin(), str.end(), std::back_inserter(m_StrBody));
		m_Header.m_Size = m_StrBody.size();
	}

	//For printing out the general information of the packet
	friend std::ostream& operator<<(std::ostream& os, const Packet& packet) {
		std::string type = "";
		
		switch (packet.m_Header.m_ID) {
			case PacketType::ServerAccept:
				type = "Server Accept";
				break;

			case PacketType::ServerReject:
				type = "Server Reject";
				break;

			case PacketType::OnlineList:
				type = "Online List";
				break;

			case PacketType::Message:
				type = "General Message";
				break;

			case PacketType::ServerMessage:
				type = "Server Message";
				break;

			case PacketType::MessageAll:
				type = "Message All";
				break;

			case PacketType::AccountInfo:
				type = "Account Information";
				break;

			case PacketType::ChatRequest:
				type = "Chatting Request";
				break;

			case PacketType::ChatAlert:
				type = "Chatting Request Alert";
				break;

			case PacketType::ChatAlertResponse:
				type = "Chatting Alert Response";
				break;

			case PacketType::ChatResponse:
				type = "Chatting Response";
				break;

			case PacketType::ChangePassword: 
				type = "Change Password";
				break;

			case PacketType::LeaveConvo:
				type = "Leave Conversation";
				break;

			case PacketType::LeaveServer:
				type = "Leave Server";
				break;

			case PacketType::ClientExit:
				type = "Client Exit";
				break;

			default:
				type = "Packet Type Unknown";
				break;
		}

		os << "Packet Type: " << type << "\nTotal Body Size: " << packet.m_Body.size() << " bytes\n";
		os << "Total String Body Size: " << packet.m_StrBody.size() << " bytes";
		return os;
	}

	//Pushing data
	template<typename T>
	friend Packet& operator<<(Packet& packet, const T& data) {
		static_assert(std::is_standard_layout<T>::value, "Data is too complex to be used");

		//Input the new data and update the size of the body
		size_t offset = packet.m_Body.size();
		packet.m_Body.resize(packet.m_Body.size() + sizeof(T));
		std::memcpy(packet.m_Body.data() + offset, &data, sizeof(T));

		packet.m_Header.m_Size = packet.m_Body.size();
		return packet;
	}

	friend Packet& operator<<(Packet& packet, const std::string& str) {
		std::copy(str.begin(), str.end(), std::back_inserter(packet.m_StrBody));
		packet.m_Header.m_Size = packet.m_StrBody.size();
		return packet;
	}

	//Extracting Data
	template<typename T>
	friend Packet& operator>>(Packet& packet, T& data) {
		static_assert(std::is_standard_layout<T>::value, "Data is too complex to be used");

		size_t offset = packet.m_Body.size() - sizeof(T); //Size of the body without the data
		std::memcpy(&data, packet.m_Body.data() + offset, sizeof(T));
		packet.m_Body.resize(offset);

		packet.m_Header.m_Size = packet.m_Body.size();
		return packet;
	}

	friend Packet& operator>>(Packet& packet, std::string& str) {
		str = std::string(packet.m_StrBody.begin(), packet.m_StrBody.end());

		//Will take the whole string in one take, clear the vector
		packet.m_StrBody.clear();
		packet.m_StrBody.resize(0);
		packet.m_Header.m_Size = packet.m_Body.size();

		return packet;
	}

	PacketHeader m_Header;
	std::vector<uint8_t> m_Body;
	std::vector<char> m_StrBody;
};

//Foward declartion due to circular dependency
class Connection;

struct OwnedPacket { //Packets owned by someone else with a connection to the sender (m_Owner)
	std::shared_ptr<Connection> m_Owner = nullptr;
	Packet m_Packet;
};