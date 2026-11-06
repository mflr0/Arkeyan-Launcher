// ArkeyanLauncherTest's checks of the Toy Box window's link (launcher/toybox_link_client.h, shared/toybox_link.h): a fake game
// on the loopback writes its link file, greets the window, sends its seats and answers a command; another version, another
// window and the game's end. Scratch folders only. Called from launcher_test.cpp's main; returns the number of failures.
#include "launcher/toybox_link_client.h"
#include "shared/toybox_link.h"
#include "shared/toybox_socket.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>

namespace
{
    namespace fs = std::filesystem;
    using Client = toyboxlinkclient::Client;
    int g_linkFailures = 0;

#define LINK_EXPECT(condition) \
    do { if (!(condition)) { fprintf(stderr, "FAIL %s line %d: %s\n", __FILE__, __LINE__, #condition); g_linkFailures++; } } while (0)

    bool WaitFor(const std::function<bool()>& done, int ms = 3000)
    {
        for (int waited = 0; waited < ms; waited += 10)
        {
            if (done())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return done();
    }

    // A game's end of the link, with the window's lines it got.
    struct FakeGame
    {
        toyboxsocket::Handle listener = toyboxsocket::Invalid, window = toyboxsocket::Invalid;
        toyboxlink::LineBuffer buffer;
        std::string token = toyboxlink::NewToken();
        int port = 0;

        explicit FakeGame(const fs::path& file)
        {
            listener = toyboxsocket::Listen(port);
            std::ofstream(file) << toyboxlink::FormatEndpoint({ toyboxlink::Version, port, token, 1 });
        }
        ~FakeGame()
        {
            toyboxsocket::Close(window);
            toyboxsocket::Close(listener);
        }
        bool Accept()
        {
            if (toyboxsocket::Wait(&listener, 1, 3000) == 0)
                return false;
            window = toyboxsocket::Accept(listener);
            return window != toyboxsocket::Invalid;
        }
        bool Line(toyboxlink::Message& message)
        {
            for (int tries = 0; tries < 100; tries++)
            {
                std::string line;
                if (buffer.Next(line))
                    return toyboxlink::Parse(line, message);
                if (toyboxsocket::Wait(&window, 1, 30) == 0)
                    continue;
                char data[512];
                long got = toyboxsocket::Receive(window, data, sizeof(data));
                if (got <= 0)
                    return false;
                buffer.Append(data, size_t(got));
            }
            return false;
        }
        void Send(const toyboxlink::Message& message) { toyboxsocket::Send(window, toyboxlink::Format(message)); }
    };
}

int ToyboxLinkChecks(const fs::path& scratch)
{
    fs::remove_all(scratch);
    fs::create_directories(scratch);
    LINK_EXPECT(toyboxsocket::Startup());
    const fs::path file = scratch / toyboxlink::FileName;
    LINK_EXPECT(toyboxlinkclient::LinkFileBeside((scratch / "portal.txt").string()) == file.string());

    // No game yet: the window waits, sends nothing.
    {
        Client client(file.string(), "test");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        LINK_EXPECT(client.Current() == Client::State::Waiting);
        LINK_EXPECT(!client.Put(0, "x.sky"));
    }

    // The game comes: hello both ways, the seats, a command and its answer, then bye.
    {
        FakeGame game(file);
        Client client(file.string(), "test");
        LINK_EXPECT(game.Accept());
        toyboxlink::Message hello;
        LINK_EXPECT(game.Line(hello) && hello.verb == "hello" && hello.fields.size() == 3 &&
            hello.fields[0] == std::to_string(toyboxlink::Version) && hello.fields[1] == game.token && hello.fields[2] == "test");
        game.Send({ "hello", { std::to_string(toyboxlink::Version), "giants" } });
        toyboxlink::Seat spyro;
        spyro.kind = toyboxlink::Seat::Kind::Virtual;
        spyro.path = "/c/Spyro.sky";
        spyro.taken = true;
        toyboxlink::Seat real;
        real.kind = toyboxlink::Seat::Kind::Real;
        real.type = 30;
        real.known = true;
        game.Send(toyboxlink::SeatMessage(0, spyro));
        game.Send(toyboxlink::SeatMessage(1, real));
        game.Send(toyboxlink::SeatMessage(2, {}));
        game.Send({ "sync", { "3" } });
        LINK_EXPECT(WaitFor([&] { return client.Generation() == 1; }));
        LINK_EXPECT(client.Current() == Client::State::Connected && client.Game() == "giants");
        const std::vector<toyboxlink::Seat> seats = client.Seats();
        LINK_EXPECT(seats.size() == 3 && seats[0] == spyro && seats[1] == real && seats[2].kind == toyboxlink::Seat::Kind::Empty);

        LINK_EXPECT(client.Put(2, "/c/My figures/Gill Grunt.sky"));
        toyboxlink::Message put;
        LINK_EXPECT(game.Line(put) && put.verb == "put" && put.fields.size() == 2 && put.fields[0] == "2" &&
            put.fields[1] == "/c/My figures/Gill Grunt.sky");
        LINK_EXPECT(client.Off(1) && client.Swap() && client.Clear());
        toyboxlink::Message off, swap, clear;
        LINK_EXPECT(game.Line(off) && off.verb == "off" && off.fields == std::vector<std::string>{ "1" });
        LINK_EXPECT(game.Line(swap) && swap.verb == "swap" && game.Line(clear) && clear.verb == "clear");
        game.Send({ "done", { "put", "2", toyboxlink::refusal::Full } });
        Client::Done done;
        LINK_EXPECT(WaitFor([&] { return client.NextDone(done); }) && done.verb == "put" && done.seat == 2 && done.result == "full");
        LINK_EXPECT(!client.Put(0, "a\tb")); // a path with a tab cannot be sent

        game.Send({ "bye", {} });
        LINK_EXPECT(WaitFor([&] { return client.Current() == Client::State::Ended; }));
        LINK_EXPECT(!client.Put(0, "x.sky"));
    }

    // A game of another version: the window says which, and stops trying.
    {
        FakeGame game(file);
        Client client(file.string(), "test");
        LINK_EXPECT(game.Accept());
        toyboxlink::Message hello;
        LINK_EXPECT(game.Line(hello) && hello.verb == "hello");
        game.Send({ "refused", { "version", std::to_string(toyboxlink::Version + 1) } });
        LINK_EXPECT(WaitFor([&] { return client.Current() == Client::State::OtherVersion; }));
        LINK_EXPECT(client.GameVersion() == toyboxlink::Version + 1);
    }

    // An older game that greets with its own version: the same.
    {
        FakeGame game(file);
        Client client(file.string(), "test");
        LINK_EXPECT(game.Accept());
        toyboxlink::Message hello;
        LINK_EXPECT(game.Line(hello));
        game.Send({ "hello", { "0", "giants" } });
        LINK_EXPECT(WaitFor([&] { return client.Current() == Client::State::OtherVersion; }) && client.GameVersion() == 0);
    }

    // Another window has the game: this one says so and tries again.
    {
        FakeGame game(file);
        Client client(file.string(), "test");
        LINK_EXPECT(game.Accept());
        toyboxlink::Message hello;
        LINK_EXPECT(game.Line(hello));
        game.Send({ "refused", { "busy" } });
        LINK_EXPECT(WaitFor([&] { return client.Current() == Client::State::Busy; }));
    }

    // A game that went without a word (a crash): back to waiting, and its stale file does not hold the window.
    {
        auto game = std::make_unique<FakeGame>(file);
        Client client(file.string(), "test");
        LINK_EXPECT(game->Accept());
        toyboxlink::Message hello;
        LINK_EXPECT(game->Line(hello));
        game->Send({ "hello", { std::to_string(toyboxlink::Version), "ssa" } });
        LINK_EXPECT(WaitFor([&] { return client.Current() == Client::State::Connected; }));
        game.reset();
        LINK_EXPECT(WaitFor([&] { return client.Current() == Client::State::Waiting; }));
    }
    // A damaged link file is no game.
    std::ofstream(file) << "skytoybox one two";
    {
        Client client(file.string(), "test");
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        LINK_EXPECT(client.Current() == Client::State::Waiting);
    }
    fs::remove_all(scratch);
    printf("Toy Box link: %s\n", g_linkFailures == 0 ? "all passed" : "FAILED");
    return g_linkFailures;
}
