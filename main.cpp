#include "proxy_server.hpp"

int main() {
    ProxyServer server(8080);
    server.start();

    // Keep the main thread alive to allow the server to run.
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
