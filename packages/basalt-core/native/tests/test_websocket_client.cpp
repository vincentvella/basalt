// What a WebSocket says when the server on the other end goes away. React
// Native's own client logged the failed read and stopped, so JS never saw
// `onclose` and an app waiting on it to reconnect waited forever (Cutloop lost
// its daemon that way on every restart). See core/WebSocketClient.cpp.

#include "TestHarness.h"

#include <react/http/platform/cxx/WebSocketClient.h>

#include <boost/asio.hpp>
#include <boost/beast/websocket.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

struct Events {
  std::mutex mutex;
  std::condition_variable changed;
  bool connected = false;
  int closed = 0;
};

} // namespace

TEST(websocket_reports_closed_once_when_the_server_goes_away) {
  asio::io_context io;
  tcp::acceptor acceptor(io, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
  const auto port = acceptor.local_endpoint().port();

  // Accept one socket, finish the handshake, then drop the connection with no
  // close frame: what a killed process's socket does.
  std::thread server([&] {
    tcp::socket socket(io);
    acceptor.accept(socket);
    boost::beast::websocket::stream<tcp::socket> ws(std::move(socket));
    ws.accept();
    ws.next_layer().close();
  });

  Events events;
  {
    facebook::react::WebSocketClient client;
    client.setOnClosedCallback([&](const std::string&) {
      std::lock_guard<std::mutex> lock(events.mutex);
      ++events.closed;
      events.changed.notify_all();
    });
    client.connect("ws://127.0.0.1:" + std::to_string(port), [&](bool ok, const std::string&) {
      std::lock_guard<std::mutex> lock(events.mutex);
      events.connected = ok;
      events.changed.notify_all();
    });

    {
      std::unique_lock<std::mutex> lock(events.mutex);
      EXPECT(events.changed.wait_for(lock, std::chrono::seconds(5),
                                     [&] { return events.closed > 0; }));
      EXPECT(events.connected);
    }
    server.join();

    // What WebSocketModule does when JS closes it afterwards: no second
    // report, and it returns rather than hanging on the I/O thread.
    client.close("closed by the test");
  }
  EXPECT_EQ(events.closed, 1);
}
