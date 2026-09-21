#pragma once
#include "d4r0/Settings.h"
#include <memory>
#include <stop_token>

namespace d4r0 {
// Owns a loopback-only llama-server and terminates its process tree on exit.
// Caller initializes a WinRT apartment. Requests and model output stay in RAM.
class LocalTranslator {
 public:
  LocalTranslator(const PipelineSettings& settings, std::stop_token stop = {});
  ~LocalTranslator();
  LocalTranslator(const LocalTranslator&) = delete;
  [[nodiscard]] bool alive() const;
  std::vector<std::string> translate(const std::vector<std::string>& german, std::stop_token stop = {});
 private:
  struct State;
  std::unique_ptr<State> state_;
};
}
