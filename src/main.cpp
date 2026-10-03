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
#include <unordered_map>

#include "core/audio_engine.hpp"
#include "core/component.hpp"
#include "core/game_state.hpp"
#include "core/key_events.hpp"
#include "core/mission_end.hpp"
#include "core/office.hpp"
#include "core/screen.hpp"
#include "core/station.hpp"
#include "core/title.hpp"
#include "host/asset_manager.hpp"
#include "host/audio_output.hpp"
#include "host/renderer.hpp"
#include "host/sfml_input.hpp"

namespace hp2 {

namespace {

// Long-lived engine storage: the decoded assets, the screen and the audio ring are large and never
// move.
host::AssetManager asset_manager;
Screen screen;
KeyEvents key_events;
AudioEngine audio;
GameState game;

// Where the program starts and the game state it starts with: the station and the endings are
// reached only from the highway, which is not ported yet, so they can be opened directly.
struct Options {
  std::filesystem::path disk_image;
  std::uint32_t scaling{};
  ComponentType start{ComponentType::kTitle};
  std::optional<EndReason> ending;
  bool station_robbed{};
  std::uint32_t score{};
};

// Logs the frames the audio ring lost since the last call, either way.
void ReportAudioGaps(host::AudioOutput& output) {
  if (const auto missing = output.TakeMissingFrames(); missing != 0) {
    spdlog::warn("Audio: {} frames of silence padded (buffer underflow)", missing);
  }
  if (const auto dropped = audio.TakeDroppedFrames(); dropped != 0) {
    spdlog::warn("Audio: {} frames dropped (buffer overflow)", dropped);
  }
}

int Run(const Options& options) {
  auto status = 0;
  const auto scaling = options.scaling;
  if (not asset_manager.Load(options.disk_image)) {
    spdlog::error("Could not load the game files from {}", options.disk_image.string());
    status = 1;
  } else {
    // 320x200 shown at 4:3: the window is 6/5 taller than the pixel count.
    auto window =
        sf::RenderWindow{sf::VideoMode{{kScreenWidth * scaling, kScreenHeight * scaling * 6 / 5}}, "Highway Patrol II"};
    window.setVerticalSyncEnabled(true);
    window.setKeyRepeatEnabled(false);
    auto renderer = host::Renderer{window};
    auto input = host::SfmlInput{key_events};
    auto audio_output = host::AudioOutput{audio};
    audio_output.play();

    game.end_reason = options.ending;
    game.station_robbed = options.station_robbed;
    game.score = options.score;
    if (options.start == ComponentType::kStation) {
      // A car that needs both services.
      game.tires = 1;
      game.fuel = 0.5F;
    }
    const auto& assets = asset_manager.Engine();
    auto engine = Engine<Title, Office, Station, MissionEnd>{
        Title{assets, screen, key_events, audio}, Office{assets, screen, key_events, game},
        Station{assets, screen, key_events, game}, MissionEnd{assets, screen, key_events, game}};
    auto running = engine.Start(options.start);
    auto clock = sf::Clock{};
    while (running and window.isOpen()) {
      while (const auto event = window.pollEvent()) {
        if (input.Handle(*event)) {
          window.close();
        }
      }
      const auto elapsed = clock.restart();
      running = engine.Step(elapsed.asSeconds()) and engine.Running();
      if (not running and engine.Running()) {
        spdlog::info("The highway is not ported yet");
      }
      audio.Step(static_cast<std::uint32_t>(elapsed.asMicroseconds()));
      ReportAudioGaps(audio_output);
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
  auto disk = args::ValueFlag<std::string>{parser, "disk", "Game disk image (ADF)", {'d', "disk"}, "assets/hp2.adf"};
  const auto screens = std::unordered_map<std::string, hp2::ComponentType>{{"title", hp2::ComponentType::kTitle},
                                                                           {"office", hp2::ComponentType::kOffice},
                                                                           {"station", hp2::ComponentType::kStation},
                                                                           {"ending", hp2::ComponentType::kMissionEnd}};
  auto start = args::MapFlag<std::string, hp2::ComponentType>{
      parser,    "screen", "Screen to start on: title, office, station or ending",
      {"start"}, screens,  hp2::ComponentType::kTitle};
  const auto endings = std::unordered_map<std::string, hp2::EndReason>{
      {"out-of-fuel", hp2::EndReason::kOutOfFuel}, {"stations-robbed", hp2::EndReason::kStationsRobbed},
      {"overheated", hp2::EndReason::kOverheated}, {"wrecked", hp2::EndReason::kWrecked},
      {"tires-gone", hp2::EndReason::kTiresGone},  {"shot", hp2::EndReason::kShot},
      {"arrest", hp2::EndReason::kArrest},         {"bounty-gone", hp2::EndReason::kBountyGone}};
  auto ending = args::MapFlag<std::string, hp2::EndReason>{
      parser,
      "reason",
      "With --start ending: out-of-fuel, stations-robbed, overheated, wrecked, tires-gone, shot, arrest or bounty-gone",
      {"ending"},
      endings};
  auto robbed = args::Flag{parser, "robbed", "With --start station: the station has been robbed", {"robbed"}};
  auto score = args::ValueFlag<std::uint32_t>{parser, "score", "Score to start with", {"score"}, 0};

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
  return hp2::Run(hp2::Options{.disk_image = std::filesystem::path{disk.Get()},
                               .scaling = scaling.Get(),
                               .start = start.Get(),
                               .ending = ending ? std::optional<hp2::EndReason>{ending.Get()} : std::nullopt,
                               .station_robbed = robbed.Get(),
                               .score = score.Get()});
} catch (const std::exception& error) {
  std::cerr << "Highway Patrol II: " << error.what() << '\n';
  return 1;
} catch (...) {
  std::cerr << "Highway Patrol II: unknown error\n";
  return 1;
}
