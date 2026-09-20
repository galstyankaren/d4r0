#include "d4r0/TranslationPrompt.h"
#include <sstream>
namespace d4r0 {
std::string makeTranslationPrompt(const std::vector<std::string>& blocks) {
  std::ostringstream p;
  p << "Translate each numbered German game UI string to natural English. Preserve placeholders, numbers, keyboard shortcuts, markup, line breaks, names, and tokens exactly. Return only one numbered translation per input.\n";
  for (std::size_t i = 0; i < blocks.size(); ++i) p << (i + 1) << ". " << blocks[i] << '\n';
  return p.str();
}
std::vector<std::string> parseNumberedTranslations(std::string_view output, std::size_t expected) {
  std::vector<std::string> result(expected); std::istringstream stream{std::string(output)}; std::string line;
  while (std::getline(stream, line)) {
    const auto dot = line.find('.'); if (dot == std::string::npos) continue;
    try { auto n = std::stoul(line.substr(0, dot)); if (n >= 1 && n <= expected) result[n - 1] = line.substr(dot + 1); } catch (...) {}
  } return result;
}
} // namespace d4r0
