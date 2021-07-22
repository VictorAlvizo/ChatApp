#include "../Networking/Server.h"

int main() {
	Server server(3000);

	server.Start();

	while(1) {
		server.Update();
	}

	return 0;
}