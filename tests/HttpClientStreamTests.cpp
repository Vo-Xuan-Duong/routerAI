#include "net/HttpClient.hpp"

#include <httplib.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main() {
    constexpr int port = 19092;
    httplib::Server server;

    server.Post("/stream", [](const httplib::Request&, httplib::Response& response) {
        auto sent = std::make_shared<bool>(false);
        response.set_chunked_content_provider(
            "text/event-stream",
            [sent](std::size_t, httplib::DataSink& sink) mutable {
                if (*sent) {
                    sink.done();
                    return true;
                }
                *sent = true;
                const std::string first = "data: one\n\n";
                const std::string second = "data: [DONE]\n\n";
                sink.write(first.data(), first.size());
                sink.write(second.data(), second.size());
                sink.done();
                return true;
            });
    });

    server.Post("/error", [](const httplib::Request&, httplib::Response& response) {
        response.status = 401;
        response.set_content("{\"error\":\"bad key\"}", "application/json");
    });

    require(server.bind_to_port("127.0.0.1", port), "mock HTTP server failed to bind test port");
    std::thread serverThread([&] { server.listen_after_bind(); });

    try {
        std::string streamed;
        const auto success = routerai::HttpClient::postJsonStream(
            "http://127.0.0.1:" + std::to_string(port) + "/stream",
            "{}",
            {},
            [&](std::string_view chunk) {
                streamed.append(chunk.data(), chunk.size());
                return true;
            },
            5);

        require(success.succeeded(), "streaming 2xx response must succeed");
        require(streamed.find("data: one") != std::string::npos, "stream callback must receive provider bytes");
        require(streamed.ends_with("data: [DONE]\n\n"), "stream callback must receive stream terminator");
        require(success.body.empty(), "successful streaming body should not be buffered");

        bool errorCallbackCalled = false;
        const auto failure = routerai::HttpClient::postJsonStream(
            "http://127.0.0.1:" + std::to_string(port) + "/error",
            "{}",
            {},
            [&](std::string_view) {
                errorCallbackCalled = true;
                return true;
            },
            5);

        require(failure.statusCode == 401, "non-2xx status must be preserved");
        require(!failure.succeeded(), "401 response must not report success");
        require(!errorCallbackCalled, "non-2xx provider body must not be forwarded as stream data");
        require(failure.body.find("bad key") != std::string::npos, "non-2xx provider body must remain buffered");

        server.stop();
        if (serverThread.joinable()) serverThread.join();
        std::cout << "HttpClientStreamTests: OK\n";
    } catch (const std::exception& exception) {
        server.stop();
        if (serverThread.joinable()) serverThread.join();
        std::cerr << "HttpClientStreamTests: FAILED: " << exception.what() << '\n';
        return 1;
    }

    return 0;
}
