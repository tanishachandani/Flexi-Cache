#include "proxy_server.hpp"
#include "proxy_parse.hpp"
#include <iostream>
#include <cstring>
#include <fstream>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>

ProxyServer::ProxyServer(int port) : port(port), running(false) {}

ProxyServer::~ProxyServer() {
    stop();
}

void ProxyServer::start() {
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return;
    }

    sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Failed to bind socket" << std::endl;
        return;
    }

    if (listen(server_socket, 10) < 0) {
        std::cerr << "Failed to listen on socket" << std::endl;
        return;
    }

    running = true;
    std::thread(&ProxyServer::acceptConnections, this).detach();
    std::cout << "Proxy server started on port " << port << std::endl;
}

void ProxyServer::stop() {
    running = false;
    close(server_socket);
    {
        std::unique_lock<std::mutex> lock(connection_mutex);
        while (!connection_queue.empty()) {
            close(connection_queue.front());
            connection_queue.pop();
        }
    }
    connection_cv.notify_all();
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    std::cout << "Proxy server stopped" << std::endl;
}

void ProxyServer::acceptConnections() {
    while (running) {
        int client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket < 0) {
            if (errno == EINTR) continue;
            std::cerr << "Failed to accept connection" << std::endl;
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(connection_mutex);
            connection_queue.push(client_socket);
        }
        connection_cv.notify_one();

        std::thread(&ProxyServer::handleClient, this, client_socket).detach();
    }
}

void ProxyServer::handleClient(int client_socket) {
    char buffer[4096];
    int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
    if (bytes_received > 0) {
        std::string request(buffer, bytes_received);
        proxyRequest(client_socket, request);
    }
    close(client_socket);
}

void ProxyServer::proxyRequest(int client_socket, const std::string& request) {
    ParsedRequest* parsed_request = ParsedRequest::create();
    if (parsed_request->parse(request.c_str(), request.length()) == 0) {
        std::cout << "Parsed request:" << std::endl;
        std::cout << "Method: " << parsed_request->method << std::endl;
        std::cout << "Protocol: " << parsed_request->protocol << std::endl;
        std::cout << "Host: " << parsed_request->host << std::endl;
        std::cout << "Path: " << parsed_request->path << std::endl;
        std::cout << "Version: " << parsed_request->version << std::endl;

        if (parsed_request->method == "GET" && parsed_request->path.find("/pics/") == 0) {
            std::string file_path = "." + parsed_request->path; // Adjust the path as needed
            std::ifstream file(file_path, std::ios::binary);
            if (file) {
                std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                std::string response = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(content.length()) + "\r\n\r\n" + content;
                send(client_socket, response.c_str(), response.length(), 0);
            } else {
                std::string response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
                send(client_socket, response.c_str(), response.length(), 0);
            }
        } else {
            // Forward the request to the destination server
            // Example: Send back a simple response for now
            std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!";
            send(client_socket, response.c_str(), response.length(), 0);
        }
    }
    parsed_request->destroy();
}
