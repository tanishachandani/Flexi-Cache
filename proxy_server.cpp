#include "proxy_server.hpp"
#include "proxy_parse.hpp"
#include <iostream>
#include <cstring>
#include <fstream>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <chrono>

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
    total_requests.fetch_add(1, std::memory_order_relaxed);
    auto start_time = std::chrono::steady_clock::now();
    ParsedRequest* parsed_request = ParsedRequest::create();
    if (parsed_request->parse(request.c_str(), request.length()) == 0) {
        std::cout << "Parsed request:" << std::endl;
        std::cout << "Method: " << parsed_request->method << std::endl;
        std::cout << "Protocol: " << parsed_request->protocol << std::endl;
        std::cout << "Host: " << parsed_request->host << std::endl;
        std::cout << "Path: " << parsed_request->path << std::endl;
        std::cout << "Version: " << parsed_request->version << std::endl;

        // Cache key for GET requests
        bool is_get = (parsed_request->method == "GET");
        std::string cache_key = parsed_request->host + parsed_request->path;

        if (is_get) {
            std::string cached;
            if (cache.get(cache_key, cached)) {
                cache_hits.fetch_add(1, std::memory_order_relaxed);
                send(client_socket, cached.c_str(), cached.length(), 0);
                auto end_time = std::chrono::steady_clock::now();
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
                std::cout << "[CACHE HIT] " << cache_key << " in " << ms << " ms" << std::endl;
                parsed_request->destroy();
                return;
            }
        }

        // For local demo
        if (is_get && parsed_request->path.find("/pics/") == 0) {
            std::string file_path = "." + parsed_request->path; // Adjust the path as needed
            std::ifstream file(file_path, std::ios::binary);
            if (file) {
                std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                std::string response = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(content.length()) + "\r\n\r\n" + content;
                if (is_get) {
                    cache.put(cache_key, response);
                }
                send(client_socket, response.c_str(), response.length(), 0);
                auto end_time = std::chrono::steady_clock::now();
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
                std::cout << "[LOCAL] " << cache_key << " in " << ms << " ms" << std::endl;
                parsed_request->destroy();
                return;
            }
        }

        // Forward to origin host:80 + relay response
        std::string origin_response;
        bool ok = forwardToOrigin(parsed_request->host,
                                  parsed_request->path,
                                  parsed_request->method,
                                  parsed_request->version.empty() ? std::string("HTTP/1.1") : parsed_request->version,
                                  request,
                                  origin_response);
        if (ok) {
            if (is_get) {
                cache.put(cache_key, origin_response);
            }
            send(client_socket, origin_response.c_str(), origin_response.length(), 0);
            auto end_time = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
            std::cout << "[FORWARDED] " << cache_key << " in " << ms << " ms | total=" << total_requests.load() << ", hits=" << cache_hits.load() << std::endl;
        } else {
            std::string response = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n";
            send(client_socket, response.c_str(), response.length(), 0);
        }
    }
    parsed_request->destroy();
}

bool ProxyServer::forwardToOrigin(const std::string& host,
                         const std::string& path,
                         const std::string& method,
                         const std::string& version,
                         const std::string&,
                         std::string& out_response) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    int rc = getaddrinfo(host.c_str(), "80", &hints, &res);
    if (rc != 0) {
        std::cerr << "getaddrinfo failed for host " << host << ": " << gai_strerror(rc) << std::endl;
        return false;
    }

    int origin_fd = -1;
    for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
        origin_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (origin_fd == -1) continue;
        if (connect(origin_fd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        close(origin_fd);
        origin_fd = -1;
    }
    freeaddrinfo(res);
    if (origin_fd == -1) {
        std::cerr << "Failed to connect to origin " << host << std::endl;
        return false;
    }

    // A minimal request to the origin
    std::string outbound = method + " " + path + " " + (version.empty() ? std::string("HTTP/1.1") : version) + "\r\n";
    outbound += "Host: " + host + "\r\n";
    outbound += "Connection: close\r\n";
    outbound += "User-Agent: FlexiCache/1.0\r\n\r\n";

    ssize_t sent = send(origin_fd, outbound.c_str(), outbound.size(), 0);
    if (sent < 0) {
        std::cerr << "Failed to send to origin" << std::endl;
        close(origin_fd);
        return false;
    }

    // Read the response
    std::string resp;
    char buf[8192];
    ssize_t n;
    while ((n = recv(origin_fd, buf, sizeof(buf), 0)) > 0) {
        resp.append(buf, buf + n);
    }
    close(origin_fd);
    if (resp.empty()) return false;
    out_response = resp;
    return true;
}
