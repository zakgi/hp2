#include <spdlog/spdlog.h>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/VideoMode.hpp>
#include <args.hxx>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include "core/component.hpp"
#include "core/key_events.hpp"
#include "core/screen.hpp"
#include "core/title.hpp"
#include "host/asset_manager.hpp"
#include "host/renderer.hpp"
#include "host/sfml_input.hpp"

namespace hp2 {

namespace {

// Long-lived engine storage: the decoded assets and the screen are large and never move.
host::AssetManager asset_manager;
Screen screen;
KeyEvents key_events;

int Run(const std::filesystem::path& game_dir, std::uint32_t scaling) {
  auto status = 0;
  if (not asset_manager.Load(game_dir)) {
    spdlog::error("Could not load the game files from {}", game_dir.string());
    status = 1;
  } else {
    // 320x200 shown at 4:3: the window is 6/5 taller than the pixel count.
    auto window =
        sf::RenderWindow{sf::VideoMode{{kScreenWidth * scaling, kScreenHeight * scaling * 6 / 5}}, "Highway Patrol II"};
    window.setVerticalSyncEnabled(true);
    window.setKeyRepeatEnabled(false);
    auto renderer = host::Renderer{window};
    auto input = host::SfmlInput{key_events};

    auto engine = Engine<Title>{Title{asset_manager.Engine(), screen, key_events}};
    auto running = engine.Start(ComponentType::kTitle);
    auto clock = sf::Clock{};
    while (running and window.isOpen()) {
      while (const auto event = window.pollEvent()) {
        if (input.Handle(*event)) {
          window.close();
        }
      }
      const auto delta_seconds = clock.restart().asSeconds();
      running = engine.Step(delta_seconds) and engine.Running();
      renderer.Render(screen);
    }
  }
  return status;
}

}  // namespace
}  // namespace hp2

int main(int argc, char* argv[]) try {
  auto parser = args::ArgumentParser{"Highway Patrol II", "Native C++23 port."};
  auto help = args::HelpFlag{parser, "help", "Display this help menu", {'h', "help"}};
  auto scaling = args::ValueFlag<std::uint32_t>{parser, "scaling", "Window scale (1-8)", {'s', "scaling"}, 3};
  auto game = args::ValueFlag<std::string>{
      parser, "game", "Directory with hp.prg and DISK2_2/", {'g', "game"}, "assets/hp2/Highway Patrol II"};

  try {
    parser.ParseCLI(argc, argv);
  } catch (const args::Help&) {
    std::cout << parser;
    return 0;
  } catch (const args::ParseError& error) {
    std::cerr << error.what() << '\n' << parser;
    return 1;
  }

  if (scaling.Get() < 1 or scaling.Get() > 8) {
    spdlog::error("Window scale must be between 1 and 8.");
    return 1;
  }
  return hp2::Run(std::filesystem::path{game.Get()}, scaling.Get());
} catch (const std::exception& error) {
  std::cerr << "Highway Patrol II: " << error.what() << '\n';
  return 1;
} catch (...) {
  std::cerr << "Highway Patrol II: unknown error\n";
  return 1;
}
