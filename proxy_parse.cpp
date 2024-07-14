#include "proxy_parse.hpp"
#include <sstream>
#include <iostream>

ParsedRequest* ParsedRequest::create() {
    return new ParsedRequest();
}

void ParsedRequest::destroy() {
    delete this;
}

int ParsedRequest::parse(const char* buf, int buflen) {
    std::string request(buf, buflen);
    std::istringstream request_stream(request);
    std::string request_line;
    std::getline(request_stream, request_line);

    std::istringstream request_line_stream(request_line);
    if (!(request_line_stream >> method >> path >> version)) {
        std::cerr << "Invalid request line format" << std::endl;
        return -1;
    }

    // Extract protocol and host from the path
    size_t protocol_end = path.find("://");
    if (protocol_end != std::string::npos) {
        protocol = path.substr(0, protocol_end);
        path = path.substr(protocol_end + 3);
    } else {
        protocol = "http"; // default to http if protocol is not specified
    }

    size_t host_end = path.find('/');
    if (host_end != std::string::npos) {
        host = path.substr(0, host_end);
        path = path.substr(host_end);
    } else {
        host = path;
        path = "/";
    }

    return 0;
}
