#include "../Networking/Server.h"
BOOL WINAPI ConsoleHandle(DWORD cEvent);

Server * g_Server = nullptr;

bool g_Running = true;

int main() {
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)ConsoleHandle, TRUE);

	g_Server = new Server(3000);
	g_Server->Start();

	while(g_Running) {
		g_Server->Update(-1, true);
	}

	delete g_Server;
	g_Server = nullptr;

	return 0;
}

BOOL WINAPI ConsoleHandle(DWORD cEvent) {
	if (cEvent == CTRL_CLOSE_EVENT) {
		g_Running = false;
		g_Server->Stop();
	}

	return true;
}