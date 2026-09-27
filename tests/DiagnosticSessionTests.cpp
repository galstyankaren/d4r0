#include "d4r0/DiagnosticSession.h"
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>

int main() {
  const auto root=std::filesystem::temp_directory_path() /
      ("d4r0-diagnostic-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto old=root/"old";
  const auto newer=root/"newer";
  std::filesystem::create_directory(old);
  std::filesystem::create_directory(newer);
  { std::ofstream file(old/"data",std::ios::binary); file << std::string(4000,'a'); }
  { std::ofstream file(newer/"data",std::ios::binary); file << std::string(4000,'b'); }
  std::filesystem::last_write_time(old,std::filesystem::file_time_type::clock::now()-std::chrono::hours(2));
  std::filesystem::last_write_time(newer,std::filesystem::file_time_type::clock::now()-std::chrono::hours(1));
  d4r0::DiagnosticSession session(root,10000);
  assert(!session.enabled());
  session.setEnabled(true);
  assert(session.enabled());
  assert(!std::filesystem::exists(old) && std::filesystem::exists(newer));
  const auto directory=session.directory();
  const std::array<std::uint8_t,16> pixels{0,0,0,255, 0,0,255,255, 0,255,0,255, 255,0,0,255};
  assert(session.sourceDue(100));
  assert(session.recordSource(42,100,2,2,pixels)==1);
  assert(!session.sourceDue(101));
  assert(session.overlayDue());
  session.recordOverlay(2,2,pixels);
  assert(!session.overlayDue());
  assert(!session.sourceDue(599,500));
  assert(session.sourceDue(600,500));
  assert(session.recordSource(42,600,2,2,pixels,500)==0); // Same frame is not copied twice.
  assert(session.recordSource(43,600,2,2,pixels,500)==2);
  session.recordOverlay(2,2,pixels);
  assert(std::filesystem::exists(directory/"source-1.png"));
  assert(std::filesystem::exists(directory/"overlay-1.png"));
  session.event("ocr",42,"\"text\":"+d4r0::DiagnosticSession::quote("A\nB"));
  session.setEnabled(false);
  assert(!session.enabled() && !session.overlayDue());
  assert(session.recordSource(43,2200,2,2,pixels)==0);
  assert(!std::filesystem::exists(directory/"source-3.png"));
  std::filesystem::remove_all(root);
}
