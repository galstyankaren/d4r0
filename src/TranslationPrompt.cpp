#include "d4r0/TranslationPrompt.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <optional>
namespace d4r0 {
std::string makeTranslationPrompt(const std::vector<std::string>& blocks) {
  std::ostringstream p;
  p << "Translate each numbered German game UI string to natural English. Preserve placeholders, numbers, keyboard shortcuts, markup, line breaks, names, and tokens exactly. Return only one numbered translation per input.\n";
  for (std::size_t i = 0; i < blocks.size(); ++i) p << (i + 1) << ". " << blocks[i] << '\n';
  return p.str();
}
std::vector<std::string> parseNumberedTranslations(std::string_view output, std::size_t expected) {
  std::vector<std::string> result(expected); std::istringstream stream{std::string(output)}; std::string line;
  std::optional<std::size_t> current;
  while (std::getline(stream, line)) {
    const auto dot = line.find('.');
    bool numbered = false;
    if (dot != std::string::npos && dot > 0 && std::all_of(line.begin(),line.begin()+dot,
        [](unsigned char value) { return std::isdigit(value) != 0; })) {
      try {
        const auto number = std::stoul(line.substr(0,dot));
        if (number >= 1 && number <= expected) {
          current = number-1; result[*current] = line.substr(dot+1); numbered = true;
        }
      } catch (const std::exception&) {}
    }
    if (!numbered && current) result[*current] += '\n'+line;
  } return result;
}
bool preservesProtectedTokens(std::string_view source, std::string_view translation) {
  if (std::count(source.begin(),source.end(),'\n') != std::count(translation.begin(),translation.end(),'\n')) return false;
  std::vector<std::string> tokens;
  for (std::size_t i = 0; i < source.size();) {
    const char value = source[i];
    const char closing = value == '{' ? '}' : value == '[' ? ']' : value == '<' ? '>' : 0;
    if (closing) {
      const auto end = source.find(closing,i+1);
      if (end != std::string_view::npos) { tokens.emplace_back(source.substr(i,end-i+1)); i = end+1; continue; }
    }
    if (value == '%' && i+1 < source.size()) {
      auto end = i+1;
      while (end < source.size() && (std::isalnum(static_cast<unsigned char>(source[end])) || source[end] == '$')) ++end;
      tokens.emplace_back(source.substr(i,std::max<std::size_t>(2,end-i))); i = std::max(i+2,end); continue;
    }
    const bool signedNumber = (value == '+' || value == '-') && i+1 < source.size() &&
                              std::isdigit(static_cast<unsigned char>(source[i+1]));
    if (std::isdigit(static_cast<unsigned char>(value)) || signedNumber) {
      auto end = i+(signedNumber ? 1 : 0);
      while (end < source.size()) {
        if (std::isdigit(static_cast<unsigned char>(source[end])) || source[end] == '%') { ++end; continue; }
        if ((source[end] == '.' || source[end] == ',' || source[end] == ':' || source[end] == '/') &&
            end+1 < source.size() && std::isdigit(static_cast<unsigned char>(source[end+1]))) { ++end; continue; }
        break;
      }
      tokens.emplace_back(source.substr(i,end-i)); i = end; continue;
    }
    if (std::isalnum(static_cast<unsigned char>(value))) {
      auto end = i+1;
      while (end < source.size() && (std::isalnum(static_cast<unsigned char>(source[end])) || source[end] == '+')) ++end;
      const auto token = source.substr(i,end-i);
      if (token.find('+') != std::string_view::npos) tokens.emplace_back(token);
      i = end; continue;
    }
    ++i;
  }
  for (const auto& token : tokens) {
    const auto required = std::count_if(tokens.begin(),tokens.end(),[&](const auto& other) { return other == token; });
    std::size_t found{}, position{};
    while ((position = translation.find(token,position)) != std::string_view::npos) { ++found; position += token.size(); }
    if (found < std::size_t(required)) return false;
  }
  return true;
}
} // namespace d4r0
