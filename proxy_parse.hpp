#ifndef PROXY_PARSE_HPP
#define PROXY_PARSE_HPP

#include <string>

class ParsedRequest {
public:
    static ParsedRequest* create();
    void destroy();
    int parse(const char* buf, int buflen);

    std::string method;
    std::string protocol;
    std::string host;
    std::string path;
    std::string version;

private:
    ParsedRequest() = default;
    ~ParsedRequest() = default;
};

#endif // PROXY_PARSE_HPP
