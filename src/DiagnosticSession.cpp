#include "d4r0/DiagnosticSession.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif

namespace d4r0 {
namespace {
void append32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(std::uint8_t(value >> shift));
}
std::uint32_t crc(std::span<const std::uint8_t> bytes) {
  std::uint32_t result = 0xffffffff;
  for (auto byte : bytes) {
    result ^= byte;
    for (int bit = 0; bit < 8; ++bit) result = (result >> 1) ^ (result & 1 ? 0xedb88320U : 0);
  }
  return ~result;
}
void chunk(std::vector<std::uint8_t>& png, const char* name, std::span<const std::uint8_t> data) {
  append32(png, std::uint32_t(data.size()));
  const auto start = png.size();
  for (int i = 0; i < 4; ++i) png.push_back(std::uint8_t(name[i]));
  png.insert(png.end(), data.begin(), data.end());
  append32(png, crc(std::span(png).subspan(start)));
}
bool writePortablePng(const std::filesystem::path& path, unsigned width, unsigned height,
              std::span<const std::uint8_t> bgra) {
  if (!width || !height || width > 16384 || height > 16384 ||
      bgra.size() != std::size_t(width) * height * 4) return false;
  std::vector<std::uint8_t> raw;
  raw.reserve((std::size_t(width) * 4 + 1) * height);
  for (unsigned y = 0; y < height; ++y) {
    raw.push_back(0);
    for (unsigned x = 0; x < width; ++x) {
      const auto p = (std::size_t(y) * width + x) * 4;
      raw.insert(raw.end(), {bgra[p+2], bgra[p+1], bgra[p], bgra[p+3]});
    }
  }
  std::vector<std::uint8_t> deflate{0x78, 0x01};
  for (std::size_t offset = 0; offset < raw.size();) {
    const auto size = std::min<std::size_t>(65535, raw.size() - offset);
    deflate.push_back(offset + size == raw.size() ? 1 : 0);
    deflate.push_back(std::uint8_t(size)); deflate.push_back(std::uint8_t(size >> 8));
    const auto inverse = std::uint16_t(~size);
    deflate.push_back(std::uint8_t(inverse)); deflate.push_back(std::uint8_t(inverse >> 8));
    deflate.insert(deflate.end(), raw.begin() + offset, raw.begin() + offset + size);
    offset += size;
  }
  std::uint32_t a = 1, b = 0;
  for (auto byte : raw) { a = (a + byte) % 65521; b = (b + a) % 65521; }
  append32(deflate, (b << 16) | a);
  std::vector<std::uint8_t> png{137,80,78,71,13,10,26,10};
  std::vector<std::uint8_t> header;
  append32(header,width); append32(header,height);
  header.insert(header.end(), {8,6,0,0,0});
  chunk(png,"IHDR",header); chunk(png,"IDAT",deflate); chunk(png,"IEND",{});
  std::ofstream out(path,std::ios::binary);
  out.write(reinterpret_cast<const char*>(png.data()),std::streamsize(png.size()));
  return bool(out);
}
#ifdef _WIN32
bool writePng(const std::filesystem::path& path, unsigned width, unsigned height,
              std::span<const std::uint8_t> bgra) {
  if (!width || !height || bgra.size() != std::size_t(width)*height*4) return false;
  const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
  Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
  Microsoft::WRL::ComPtr<IWICStream> stream;
  Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
  Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
  Microsoft::WRL::ComPtr<IPropertyBag2> properties;
  bool ok=SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(&factory))) &&
      SUCCEEDED(factory->CreateStream(&stream)) &&
      SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE)) &&
      SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder)) &&
      SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache)) &&
      SUCCEEDED(encoder->CreateNewFrame(&frame,&properties)) &&
      SUCCEEDED(frame->Initialize(properties.Get())) &&
      SUCCEEDED(frame->SetSize(width,height));
  GUID format=GUID_WICPixelFormat32bppBGRA;
  ok=ok && SUCCEEDED(frame->SetPixelFormat(&format)) && format==GUID_WICPixelFormat32bppBGRA &&
      SUCCEEDED(frame->WritePixels(height,width*4,UINT(bgra.size()),
                                   const_cast<std::uint8_t*>(bgra.data()))) &&
      SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
  frame.Reset(); properties.Reset(); encoder.Reset(); stream.Reset(); factory.Reset();
  if (SUCCEEDED(initialized)) CoUninitialize();
  if (!ok) { std::error_code error; std::filesystem::remove(path,error); }
  return ok;
}
#else
bool writePng(const std::filesystem::path& path, unsigned width, unsigned height,
              std::span<const std::uint8_t> bgra) {
  return writePortablePng(path,width,height,bgra);
}
#endif
std::string timestamp() {
  const auto now = std::chrono::system_clock::now();
  const auto seconds = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local,&seconds);
#else
  localtime_r(&seconds,&local);
#endif
  std::ostringstream out; out << std::put_time(&local,"%Y%m%d-%H%M%S");
  return out.str();
}
}

DiagnosticSession::DiagnosticSession(std::filesystem::path root, std::uintmax_t byteLimit)
    : root_(std::move(root)), byteLimit_(byteLimit) {}
void DiagnosticSession::setEnabled(bool enabled) {
  std::scoped_lock lock(mutex_);
  if (enabled == events_.is_open()) return;
  if (!enabled) { events_.close(); pendingOverlay_ = 0; directory_.clear(); return; }
  startLocked();
}
void DiagnosticSession::startLocked() {
  trimCompleted();
  std::error_code error;
  std::filesystem::create_directories(root_,error);
  if (error) return;
  auto candidate = root_ / timestamp();
  for (unsigned suffix = 1; std::filesystem::exists(candidate); ++suffix)
    candidate = root_ / (timestamp() + "-" + std::to_string(suffix));
  std::filesystem::create_directory(candidate,error);
  if (error) return;
  events_.open(candidate / "events.jsonl",std::ios::out | std::ios::trunc);
  if (!events_) { std::filesystem::remove(candidate,error); return; }
  directory_ = std::move(candidate);
  nextImage_ = pendingOverlay_ = lastSourceMs_ = lastFrameRevision_ = 0;
  activeBytes_ = 0;
}
bool DiagnosticSession::enabled() const { std::scoped_lock lock(mutex_); return events_.is_open(); }
bool DiagnosticSession::sourceDue(std::uint64_t nowMs, std::uint32_t intervalMs) const {
  std::scoped_lock lock(mutex_);
  return events_.is_open() && (!lastSourceMs_ || nowMs >= lastSourceMs_ + intervalMs);
}
bool DiagnosticSession::overlayDue() const {
  std::scoped_lock lock(mutex_); return events_.is_open() && pendingOverlay_ != 0;
}
std::uint64_t DiagnosticSession::recordSource(std::uint64_t revision, std::uint64_t nowMs,
                                               unsigned width, unsigned height,
                                               std::span<const std::uint8_t> bgra,
                                               std::uint32_t intervalMs) {
  std::scoped_lock lock(mutex_);
  if (!events_.is_open() || revision == lastFrameRevision_ ||
      (lastSourceMs_ && nowMs < lastSourceMs_ + intervalMs)) return 0;
  const auto expected = 2ULL * (std::uintmax_t(width) * height * 4 + height + 1024);
  if (expected > byteLimit_ / 2) return 0;
  if (activeBytes_ + expected > byteLimit_ / 2) {
    events_.close(); pendingOverlay_ = 0; directory_.clear();
    startLocked();
    if (!events_.is_open()) return 0;
  }
  const auto id = ++nextImage_;
  const auto path=directory_ / ("source-" + std::to_string(id) + ".png");
  if (!writePng(path,width,height,bgra)) return 0;
  std::error_code error; const auto bytes=std::filesystem::file_size(path,error);
  if (!error) activeBytes_ += bytes;
  lastSourceMs_ = nowMs;
  lastFrameRevision_ = revision;
  pendingOverlay_ = id;
  events_ << "{\"time\":" << quote(timestamp()) << ",\"kind\":\"frame\",\"image\":" << id
          << ",\"revision\":" << revision << ",\"width\":" << width << ",\"height\":" << height << "}\n";
  events_.flush();
  return id;
}
void DiagnosticSession::recordOverlay(unsigned width, unsigned height, std::span<const std::uint8_t> bgra) {
  std::scoped_lock lock(mutex_);
  if (!events_.is_open() || !pendingOverlay_) return;
  const auto path=directory_ / ("overlay-" + std::to_string(pendingOverlay_) + ".png");
  if (writePng(path,width,height,bgra)) {
    std::error_code error; const auto bytes=std::filesystem::file_size(path,error);
    if (!error) activeBytes_ += bytes;
    pendingOverlay_ = 0;
  }
}
void DiagnosticSession::event(const std::string& kind, std::uint64_t revision,
                               const std::string& fields) {
  std::scoped_lock lock(mutex_);
  if (!events_.is_open()) return;
  events_ << "{\"time\":" << quote(timestamp()) << ",\"kind\":" << quote(kind)
          << ",\"revision\":" << revision << ",\"image\":" << nextImage_;
  if (!fields.empty()) events_ << ',' << fields;
  events_ << "}\n";
  events_.flush();
}
std::filesystem::path DiagnosticSession::directory() const {
  std::scoped_lock lock(mutex_); return directory_;
}
std::string DiagnosticSession::quote(const std::string& value) {
  std::string result = "\"";
  constexpr char digits[] = "0123456789abcdef";
  for (unsigned char byte : value) {
    if (byte == '"' || byte == '\\') { result += '\\'; result += char(byte); }
    else if (byte < 32) { result += "\\u00"; result += digits[byte >> 4]; result += digits[byte & 15]; }
    else result += char(byte);
  }
  return result + '"';
}
void DiagnosticSession::trimCompleted() {
  std::error_code error;
  if (!std::filesystem::is_directory(root_,error)) return;
  struct Entry { std::filesystem::path path; std::filesystem::file_time_type time; std::uintmax_t bytes{}; };
  std::vector<Entry> entries;
  std::uintmax_t total{};
  for (const auto& dir : std::filesystem::directory_iterator(root_,error)) {
    if (!dir.is_directory(error) || dir.is_symlink(error) || dir.path() == directory_) continue;
    Entry entry{dir.path(),dir.last_write_time(error),0};
    for (const auto& file : std::filesystem::directory_iterator(dir.path(),error))
      if (file.is_regular_file(error) && !file.is_symlink(error)) entry.bytes += file.file_size(error);
    total += entry.bytes; entries.push_back(entry);
  }
  std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.time < b.time;});
  for (const auto& entry : entries) {
    if (total <= byteLimit_ / 2) break;
    std::filesystem::remove_all(entry.path,error);
    if (!error) total -= entry.bytes;
  }
}
}
