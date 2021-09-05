#pragma once

enum class PacketType { //If the packet type uses strings it will be even, if not odd
	ServerAccept = 1,
	ServerReject = 7, //Account was rejected upon inital login / sign up information
	OnlineList = 0,
	Message = 2,
	ServerMessage = 4,
	MessageAll = 6,
	ChatRequest = 8, //The inital request
	ChatAlert = 10, //Letting the other user know someone is requesting to chat to them
	ChatAlertResponse = 12, //The response from the other user to the responder if they accept or not
	ChatResponse = 14, //The final response if the conversation is going to happen or not
	AccountInfo = 18,
	ChangePassword = 20,
	Validated = 5,
	LeaveConvo = 16,
	LeaveServer = 3, //Force said client to leave the server
	ClientExit = 9, //Client has exited the application
	ServerExit = 11
};

#define ASIO_STANDALONE
#include <asio.hpp>
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include <deque>
#include <fstream>
#include <ctime>
#include <algorithm>
#include <stdlib.h>