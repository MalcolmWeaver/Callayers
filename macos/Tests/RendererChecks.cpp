#include "Renderer.h"
#include <Ultralight/Ultralight.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

using callayers::mac::Renderer;
namespace {
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::string Read(const std::filesystem::path& path) {
    std::ifstream stream(path); Require(bool(stream), "Missing test fixture");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void Until(Renderer& renderer, const std::function<bool()>& done, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    do {
        renderer.tick();
        if (!renderer.problem().empty()) throw std::runtime_error(renderer.problem());
        if (done()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error(message);
}
void Pump(Renderer& renderer, int milliseconds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < until) {
        renderer.tick();
        if (!renderer.problem().empty()) throw std::runtime_error(renderer.problem());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
std::string Snapshot(Renderer& renderer) {
    const unsigned request = renderer.requestSnapshot();
    std::string state;
    Until(renderer, [&] { state = renderer.snapshot(request); return !state.empty(); }, "Snapshot timed out");
    return state;
}
void Click(Renderer& renderer, int x, int y) {
    renderer.mouse(0, x, y, 0); renderer.mouse(1, x, y, 1); renderer.mouse(2, x, y, 1);
    Pump(renderer, 60);
}
}
int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 4) throw std::runtime_error("Usage: RendererChecks RESOURCE_ROOT [OUTPUT_PNG]");
        const std::filesystem::path resources(argv[1]);
        Renderer renderer(resources.string());
        const auto card = Read(resources / "counter-card.html");
        renderer.loadCard(card);
        Until(renderer, [&] { return renderer.ready() && !renderer.frame().bgra.empty(); }, "Card did not become ready");
        Pump(renderer, 150);
        const auto& frame = renderer.frame();
        Require(frame.width == 1920 && frame.height == 1080, "Wrong frame dimensions");
        Require(frame.bgra[3] == 0, "The scene background is not transparent");
        const auto translucent = frame.bgra[size_t(40 * 1.5) * frame.rowBytes + size_t(50 * 1.5) * 4 + 3];
        Require(translucent > 0 && translucent < 255, "Card transparency was lost");
        auto pixels = frame.bgra;
        Pump(renderer, 450);
        Require(pixels != renderer.frame().bgra, "The animated card did not repaint");
        std::cout << "PASS transparent 1080p pixels and animation\n";

        // Card origin is (0,0). Exercise real input, not a script-triggered click.
        Click(renderer, 75, 225);
        auto state = Snapshot(renderer);
        Require(state.find("\"count\":1") != std::string::npos, "Pointer input did not increment the counter");
        Click(renderer, 120, 290);
        renderer.key(2, 'A', 0, 0, "", "", false);
        renderer.key(3, 'A', 0, 0, "a", "a", false);
        renderer.key(1, 'A', 0, 0, "", "", false);
        Pump(renderer, 50);
        state = Snapshot(renderer);
        Require(state.find("\"note\":\"a\"") != std::string::npos, "Keyboard text input failed");
        renderer.scale(.75); Pump(renderer, 80);
        Require(Snapshot(renderer) == state, "Scaling changed the interactive state");
        renderer.clear(); Pump(renderer, 80);
        Require(!renderer.ready(), "Cleared card still reports ready");
        renderer.loadCard(card, state);
        Until(renderer, [&] { return renderer.ready(); }, "Card state did not restore");
        Require(Snapshot(renderer) == state, "Reload lost saved values");
        if (argc >= 3) {
            const auto& image = renderer.frame();
            auto bitmap = ultralight::Bitmap::Create(image.width, image.height, ultralight::BitmapFormat::BGRA8_UNORM_SRGB,
                image.rowBytes, const_cast<uint8_t*>(image.bgra.data()), image.bgra.size(), true);
            Require(bitmap && bitmap->WritePNG(argv[2]), "Could not write rendered PNG");
        }
        std::cout << "PASS pointer, keyboard, scaling, unload and state restore\n";

        std::string network = std::getenv("CALLAYERS_TEST_NETWORK_URL") ? std::getenv("CALLAYERS_TEST_NETWORK_URL") : "http://127.0.0.1:9/blocked";
        Require(network.rfind("http://127.0.0.1:", 0) == 0 && network.find('\'') == std::string::npos, "Test URL must be loopback");
        renderer.loadCard("<script>let state={parentDenied:false,storageDenied:false,fetchDenied:false,fileDenied:false};"
            "try{parent.document.body.innerHTML='escaped'}catch(e){state.parentDenied=true}"
            "try{localStorage.setItem('escape','1')}catch(e){state.storageDenied=true}"
            "fetch('" + network + "').catch(()=>state.fetchDenied=true);"
            "fetch('file:///etc/passwd').catch(()=>state.fileDenied=true);"
            "window.CallayersCard={snapshot:()=>state};</script>");
        Until(renderer, [&] { return renderer.ready(); }, "Isolation fixture did not load");
        Pump(renderer, 300);
        state = Snapshot(renderer);
        Require(state.find("\"parentDenied\":true") != std::string::npos, "Card accessed the host document");
        Require(state.find("\"storageDenied\":true") != std::string::npos, "Card accessed browser storage");
        Require(state.find("\"fetchDenied\":true") != std::string::npos, "Card's network fetch was not rejected");
        Require(state.find("\"fileDenied\":true") != std::string::npos, "Card's file fetch was not rejected");
        renderer.loadCard("<script>window.CallayersCard={snapshot:()=>JSON.parse('{\"__proto__\":{\"bad\":1}}')};</script>");
        Until(renderer, [&] { return renderer.ready(); }, "Invalid-state fixture did not load");
        bool rejected = false;
        try { Snapshot(renderer); } catch (const std::runtime_error& e) { rejected = std::string(e.what()).find("invalid saved values") != std::string::npos; }
        Require(rejected, "Prototype-bearing saved state was accepted");
        renderer.loadCard(card, "[]");
        renderer.tick();
        Require(!renderer.problem().empty(), "Invalid restored root state was accepted");
        renderer.loadCard(card, "{}");
        Until(renderer, [&] { return renderer.ready(); }, "Renderer did not recover after invalid state");
        std::cout << "PASS isolated cards, blocked requests and state validation\n";
        Renderer second(resources.string());
        renderer.loadCard(card, "{\"count\":3,\"note\":\"first\"}");
        second.loadCard(card, "{\"count\":9,\"note\":\"second\"}");
        Until(renderer, [&] { second.tick(); return renderer.ready() && second.ready(); }, "Two cards did not become ready");
        Click(second, 75, 225);
        Require(Snapshot(second).find("\"count\":10") != std::string::npos, "Second card did not receive input");
        Require(Snapshot(renderer).find("\"count\":3") != std::string::npos, "Input leaked into another card");
        second.clear(); Pump(second, 80);
        Require(Snapshot(renderer).find("\"note\":\"first\"") != std::string::npos, "Unloading a card changed another card's state");
        std::cout << "PASS multiple renderer instances retain independent values and input\n";
        // The image fixture is produced by the real importer/store checks.
        if (argc == 4) {
            renderer.scale(1);
            renderer.loadCard(Read(argv[3]), "{}", 320, 160);
            Until(renderer, [&] { return renderer.ready(); }, "Imported image did not load");
            Pump(renderer, 300);
            const auto& image = renderer.frame();
            auto pixel = [&](int x, int y, int component) { return image.bgra[size_t(y) * image.rowBytes + x * 4 + component]; };
            Require(pixel(60, 60, 2) > 240 && pixel(60, 60, 3) == 255, "Imported image pixels were not rendered");
            Require(pixel(400, 180, 3) == 0, "Imported image transparency was lost");
            Require(pixel(500, 30, 3) == 0, "Card canvas dimensions were ignored");
            renderer.scale(.5); Pump(renderer, 80);
            Require(Snapshot(renderer) == "{}", "Image scaling changed state");
            std::cout << "PASS imported local pixels, transparency and custom canvas dimensions\n";
        }
        std::cout << "Ultralight " << Renderer::sdkVersion() << " renderer checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Renderer check failed: " << error.what() << '\n'; return 1;
    }
}
