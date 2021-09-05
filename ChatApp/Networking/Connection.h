#pragma once
#include "NetIncludes.h"
#include "TSQueue.h"
#include "Packet.h"

enum class Owner {
	Server, Client
};

enum class ChatStatus {
	Server, Chatting, Open
};

struct Account {
	void SetInfo(const std::string& username, const std::string& password, int option) {
		m_AccUser = username;
		m_AccPass = password;
		m_AccOpt = option;
	}

	std::string m_AccUser, m_AccPass;
	int m_AccOpt; //Used to know if logging in or signing up
};

class Connection : public std::enable_shared_from_this<Connection> {
public:
	Connection(asio::io_context& context, asio::ip::tcp::socket socket, TSQueue<OwnedPacket>& pack, Owner owner = Owner::Server)
		:m_AsioContext(context), m_Socket(std::move(socket)), m_IncomingPackets(pack), m_Owner(owner)
	{
		if (m_Owner == Owner::Server) {
			std::srand(std::time(nullptr));
			m_HandshakeOut = std::rand() % 264685356;
			m_HandshakeCheck = Rearrange(m_HandshakeOut);
		}
	}

	void ConnectToServer(Account acc, const asio::ip::tcp::resolver::results_type& endpoints) {
		if (m_Owner == Owner::Client) {
			m_Status = ChatStatus::Server;
			m_Account = acc;

			asio::async_connect(m_Socket, endpoints, [this](std::error_code ec, asio::ip::tcp::endpoint endpoint) {
				if (!ec) {
					ReadValidation();
				}
				else {
					std::cout << "Failed To Connect To Server: " << ec.message() << std::endl;
					m_Socket.close();
				}
			});
		}
	}

	void SetAccount(Account acc) { //To be used when the account info was wrong initally
		if (m_Owner == Owner::Client) {
			m_Account = acc;
			WriteAccountInfo();
		}
	}

	void ConnectToClient(unsigned int index, uint32_t id = 0) {
		if (m_Owner == Owner::Server) {
			if (m_Socket.is_open()) {
				m_ID = id;
				m_Status = ChatStatus::Open;
				m_PermIndex = index;
				WriteValidation();
				ReadValidation();
			}
		}
	}

	void ClientConnectionAction(bool accepted) {
		if (m_Owner == Owner::Server) {
			if (accepted) {
				m_ServerApproved = true;
				ReadPacketHeader();
			}
			else {
				ReadAccountInfo();
			}
		}
	}

	void IgnoreConnection() { //Connection is no longer part of the system, mark it as such
		m_Account.m_AccUser = "$invalid";
		m_ID = 0;
	}

	void Disconnect() {
		if (isConnected()) {
			asio::post(m_AsioContext, [this]() {
				m_Socket.shutdown(asio::ip::tcp::socket::shutdown_both);
				m_Socket.close(); 
			});
		}
	}

	void Send(const Packet& packet) {
		asio::post(m_AsioContext, [this, packet]() {
			bool writingPackets = !m_OutgoingPackets.isEmpty();
			m_OutgoingPackets.PushBack(packet);

			if (!writingPackets) {
				WritePacketHeader();
			}
		});
	}

	bool isConnected() {
		return m_Socket.is_open();
	}

	inline uint32_t getID() const {
		return m_ID;
	}

	inline unsigned int getPermIndex() const { //For server side only
		return m_PermIndex;
	}

	inline Account getAccount() const {
		return m_Account;
	}

	inline bool isApproved() const { //For server side only
		return m_ServerApproved;
	}

	ChatStatus m_Status;

private:
	void ReadPacketHeader() {
		asio::async_read(m_Socket, asio::buffer(&m_TempPacket.m_Header, sizeof(PacketHeader)), [this](std::error_code ec, size_t length) {
			//If the body has information as well, process that as well
			if (!ec) {
				if ((static_cast<int>(m_TempPacket.m_Header.m_ID) & 1) == 0) {
					if (m_TempPacket.m_Header.m_Size > 0) {
						m_TempPacket.m_StrBody.resize(m_TempPacket.m_Header.m_Size);
						ReadPacketBodyStr();
					}
					else {
						AddIncomingMessage();
					}
				}else if (m_TempPacket.m_Header.m_Size > 0) {
					m_TempPacket.m_Body.resize(m_TempPacket.m_Header.m_Size);
					ReadPacketBody();
				}
				else {
					AddIncomingMessage();
				}
			}else {
				std::cout << "ID: " << m_ID << " Failed To Read The Packet Header. Reason Provided: " << ec.message() << std::endl;
				m_Socket.close();
			}
		});
	}

	void ReadPacketBody() {
		asio::async_read(m_Socket, asio::buffer(m_TempPacket.m_Body.data(), m_TempPacket.m_Body.size()), [this](std::error_code ec, size_t length) {
			if (!ec) {
				AddIncomingMessage();
			}
			else {
				std::cout << "ID: " << m_ID << " Failed To Read The Packet Body. Reason Provided: " << ec.message() << std::endl;
				m_Socket.close();
			}
		});
	}

	void ReadPacketBodyStr() {
		asio::async_read(m_Socket, asio::buffer(m_TempPacket.m_StrBody.data(), m_TempPacket.m_StrBody.size()), [this](std::error_code ec, size_t length) {
			if (!ec) {
				AddIncomingMessage();
			}
			else {
				std::cout << "ID: " << m_ID << " Failed To Read The Packet Body. Reason Provided: " << ec.message() << std::endl;
				m_Socket.close();
			}
		});
	}

	//After reading outgoing packets, now transfer them over to the incoming queue so they can be read by the client / server
	void AddIncomingMessage() {
		if (m_Owner == Owner::Server) {
			m_IncomingPackets.PushBack({ this->shared_from_this(), m_TempPacket });
		}
		else { //If the owner is a client we know the packet is coming from the server
			m_IncomingPackets.PushBack({ nullptr, m_TempPacket });
		}

		ReadPacketHeader(); //Never stop reading!
	}

	void WritePacketHeader() {
		asio::async_write(m_Socket, asio::buffer(&m_OutgoingPackets.Front().m_Header, sizeof(PacketHeader)), [this](std::error_code ec, size_t length) {
			if (!ec) {
				//Check if there is information in the body to be written as well
				if (m_OutgoingPackets.Front().m_Body.size() > 0) { //Body will only > 0 if its not a string packet
					WritePacketBody();
				}else if ((static_cast<int>(m_OutgoingPackets.Front().m_Header.m_ID) & 1) == 0) {
					if (m_OutgoingPackets.Front().m_StrBody.size() > 0) {
						WritePacketBodyStr();
					}
					else {
						m_OutgoingPackets.PopFront();

						if (!m_OutgoingPackets.isEmpty()) {
							WritePacketHeader();
						}
					}
				} else {
					m_OutgoingPackets.PopFront(); //Done writing it, take it off the list

					//If it's not done writing all the headers keep writing
					if (!m_OutgoingPackets.isEmpty()) {
						WritePacketHeader();
					}
				}
			}
			else {
				std::cout << "ID: " << m_ID << " Failed To Write The Packet Header. Reason Provided: " << ec.message() << std::endl;
				m_Socket.close();
			}
		});
	}

	void WritePacketBody() { 
		asio::async_write(m_Socket, asio::buffer(m_OutgoingPackets.Front().m_Body.data(), m_OutgoingPackets.Front().m_Body.size()), [this](std::error_code ec, size_t length) {
			if (!ec) {
				m_OutgoingPackets.PopFront(); //Done writing it, take it off the list

				//If it's not done writing all the headers keep writing
				if (!m_OutgoingPackets.isEmpty()) {
					WritePacketHeader();
				}
			}
			else {
				std::cout << "ID: " << m_ID << " Failed To Write Packet Body. Reason Provided: " << ec.message() << std::endl;
				m_Socket.close();
			}
		});
	}

	void WritePacketBodyStr() {
		asio::async_write(m_Socket, asio::buffer(m_OutgoingPackets.Front().m_StrBody.data(), m_OutgoingPackets.Front().m_StrBody.size()), [this](std::error_code ec, size_t length) {
			if (!ec) {
				m_OutgoingPackets.PopFront();

				if (!m_OutgoingPackets.isEmpty()) {
					WritePacketHeader();
				}
			}
			else {
				std::cout << "ID: " << m_ID << " Failed To Write Packet Body. Reason Provided: " << ec.message() << std::endl;
				m_Socket.close();
			}
		});
	}

	void ReadValidation() {
		asio::async_read(m_Socket, asio::buffer(&m_HandshakeIn, sizeof(uint64_t)), [this](std::error_code ec, size_t length) {
			if (!ec) {
				if (m_Owner == Owner::Client) {
					//Now rearrange those numbers to check they reach the same result
					m_HandshakeOut = Rearrange(m_HandshakeIn);
					WriteValidation(); //Now write it to transfer it over to the server to check
				}
				else { //Server is now reading the answer from client
					if (m_HandshakeCheck == m_HandshakeIn) {
						Packet validationPacket(PacketType::Validated, 1);
						m_IncomingPackets.PushBack({this->shared_from_this(), validationPacket});
						ReadAccountInfo();
					}
					else {
						m_Account.m_AccUser = "$invalid";
						m_ID = 0;
						Packet validationPacket(PacketType::Validated, 0);
						m_IncomingPackets.PushBack({ this->shared_from_this(), validationPacket });
						m_Socket.close();
					}
				}
			}
			else {
				std::cout << "ID: " << m_ID << " Failed To Read Validation. Reason Provided: " << ec.message() << std::endl;
				m_Socket.close();
			}
		});
	}

	void WriteValidation() { 
		asio::async_write(m_Socket, asio::buffer(&m_HandshakeOut, sizeof(uint64_t)), [this](std::error_code ec, size_t length) {
			if (!ec) {
				//The server has the give out the inital value not the actual check value out, that has to go through the method to verify
				//The Client will reach the if statement if it's validated due to the fact that otherwise, the server would close the connection
				if (m_Owner == Owner::Client) {
					WriteAccountInfo();
				}
			}
			else {
				std::cout << "ID: " << m_ID << " Failed To Write Validation. Reason Provided: " << ec.message() << std::endl;
				m_Socket.close();
			}
		});
	}

	//I know it looks weird for both to use m_Account but remember the server will be reciving, the client sending. Connection is seperate
	void ReadAccountInfo() {
		asio::async_read(m_Socket, asio::buffer(&m_Account, sizeof(Account)), [this](std::error_code ec, size_t length) {
			if (!ec) {
				Packet usernamePacket(PacketType::AccountInfo);
				m_IncomingPackets.PushBack({ this->shared_from_this(), usernamePacket });
			}
			else {
				std::cout << "Failure To Read Username! Reason Provided: " << ec.message() << std::endl;
				m_Socket.close();
			}
		});
	}

	void WriteAccountInfo() {
		asio::async_write(m_Socket, asio::buffer(&m_Account, sizeof(Account)), [this](std::error_code ec, size_t length) {
			if (!ec) {
				if (m_InitalWriteAcc) { //Only want ReadPacketHeader() called once. If it's called multiple times it won't work
					m_InitalWriteAcc = false;
					ReadPacketHeader();
				}
			}
			else {
				std::cout << "Failure To Write Username! Reason Provided: " << ec.message() << std::endl;
				m_Socket.close();
			}
		});
	}

	//Rearrange the validation key, this method will help check if both keys are correct to validate connection
	uint64_t Rearrange(uint64_t number) {
		uint64_t out = number ^ 0xFA7398B;
		out = (0x982F21C | 0x14A6D2 >> 2) | (0xF027BAC671FC << 16);
		return out;
	}

	asio::ip::tcp::socket m_Socket;
	asio::io_context& m_AsioContext; //Reference to the owner's context

	Packet m_TempPacket; //Packet used to process information when reading outgoing packets
	TSQueue<Packet> m_OutgoingPackets;
	TSQueue<OwnedPacket>& m_IncomingPackets; //This varible is what is responsible for transmitting the packets

	uint64_t m_HandshakeOut = 0;
	uint64_t m_HandshakeIn = 0;
	uint64_t m_HandshakeCheck = 0;

	Owner m_Owner;
	uint32_t m_ID = 0;
	unsigned int m_PermIndex;
	Account m_Account;
	bool m_InitalWriteAcc = true;
	bool m_ServerApproved = false; //For the server side when making the online list so invalid account information connections don't print
};