#include "d4r0/OcrRecognizer.h"
#include <windows.h>
#include <iostream>
#include <algorithm>
#include <vector>

int wmain(int argc, wchar_t** argv) {
  if (argc < 4) return 2;
  try {
    constexpr int width = 520, height = 160;
    auto dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bitmap) throw std::runtime_error("Cannot render OCR fixture");
    auto oldBitmap = SelectObject(dc, bitmap);
    PatBlt(dc, 0, 0, width, height, WHITENESS);
    auto font = CreateFontW(-38,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    auto oldFont = SelectObject(dc, font);
    SetTextColor(dc, RGB(0,0,0)); SetBkMode(dc, TRANSPARENT);
    const std::wstring source = L"Die Welt ist voller Wunder.";
    TextOutW(dc, 8, 8, source.data(), int(source.size()));
    const std::wstring second = L"F\u00fcr die T\u00fcr.";
    TextOutW(dc, 8, 90, second.data(), int(second.size()));
    GdiFlush();
    std::vector<std::uint8_t> crop(static_cast<std::uint8_t*>(bits), static_cast<std::uint8_t*>(bits)+width*height*4);
    SelectObject(dc, oldFont); SelectObject(dc, oldBitmap);
    DeleteObject(font); DeleteObject(bitmap); DeleteDC(dc);
    const int adapter = argc > 4 ? _wtoi(argv[4]) : -1;
    d4r0::OcrRecognizer recognizer(argv[1], argv[2], adapter);
    auto result = recognizer.recognize(std::span(crop).first(width * 64 * 4), width, 64);
    std::cout << result.text << " confidence=" << result.confidence << '\n';
    if (result.text != "Die Welt ist voller Wunder." || result.confidence < 0.8F) return 1;
    try { recognizer.recognize({}, 10, 10); return 1; }
    catch (const std::invalid_argument&) {}
    d4r0::OcrDetector detector(argv[3], adapter);
    const auto boxes = detector.detect(crop, width, height);
    if (boxes.size() != 2) throw std::runtime_error("Expected two detected lines");
    std::vector<std::string> recognized;
    for (const auto& box : boxes) {
      if (box.x < 0 || box.y < 0 || box.x + box.width > width || box.y + box.height > height)
        throw std::runtime_error("Detected box outside crop");
      std::vector<std::uint8_t> line(box.width * box.height * 4);
      for (int y = 0; y < box.height; ++y)
        std::copy_n(crop.data() + ((box.y+y)*width+box.x)*4, box.width*4, line.data()+y*box.width*4);
      auto text = recognizer.recognize(line, box.width, box.height);
      std::cout << text.text << " confidence=" << text.confidence << '\n';
      recognized.push_back(text.text);
    }
    if (recognized != std::vector<std::string>{"Die Welt ist voller Wunder.", "F\xc3\xbcr die T\xc3\xbcr."})
      throw std::runtime_error("Detected lines did not recognize correctly");
    std::fill(crop.begin(), crop.end(), 255);
    if (!detector.detect(crop, width, height).empty()) throw std::runtime_error("Blank crop detected as text");
    try { detector.detect({}, 10, 10); return 1; }
    catch (const std::invalid_argument&) {}
    try { detector.detect(crop, width, height, 0); return 1; }
    catch (const std::invalid_argument&) {}
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
