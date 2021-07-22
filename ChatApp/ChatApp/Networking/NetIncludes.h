#pragma once

enum class PacketType { //If the packet type uses strings it will be even, if not odd
	ServerAccept = 1,
	OnlineList = 0,
	Message = 2,
	ServerMessage = 4,
	MessageAll = 6,
	ChatRequest = 8, //The inital request
	ChatAlert = 10, //Letting the other user know someone is requesting to chat to them
	ChatAlertResponse = 12, //The response from the other user to the responder if they accept or not
	ChatResponse = 14, //The final response if the conversation is going to happen or not
	Username = 3,
	Validated = 5,
	LeaveConvo = 16,
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
#include <algorithm>
#include <stdlib.h>