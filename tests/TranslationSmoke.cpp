#include "d4r0/LocalTranslator.h"
#include <winrt/base.h>
#include <iostream>

int wmain(int argc, wchar_t** argv) {
  if (argc != 3) return 2;
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  int result = 0;
  try {
    d4r0::PipelineSettings settings;
    settings.llamaExecutable = argv[1]; settings.model4b = argv[2];
    d4r0::LocalTranslator translator(settings);
    const auto text = translator.translate({"Die Welt ist voller Wunder."});
    if (text != std::vector<std::string>{"The world is full of wonders."}) result = 1;
    const auto batch = translator.translate({"Speichern", "Leben: 42"});
    if (batch.size() != 2 || batch[0].find("Save") == std::string::npos || batch[1].find("42") == std::string::npos) result = 1;
    std::cout << (result ? "Synthetic translation failed\n" : "Owned local translation passed\n");
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
  catch (const winrt::hresult_error& error) { std::cerr << "Translation HRESULT " << std::hex << error.code().value << '\n'; result = 1; }
  winrt::uninit_apartment();
  return result;
}
