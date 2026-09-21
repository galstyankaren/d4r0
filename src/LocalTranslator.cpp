#include "d4r0/LocalTranslator.h"
#include "d4r0/TranslationPrompt.h"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <chrono>
#include <thread>
#include <array>
#include <stdexcept>

namespace d4r0 {
namespace {
struct InternetHandle {
  HINTERNET value{};
  explicit InternetHandle(HINTERNET handle) : value(handle) {
    if (!value) throw std::runtime_error("Cannot open local translation connection");
  }
  ~InternetHandle() { WinHttpCloseHandle(value); }
  InternetHandle(const InternetHandle&) = delete;
};
void check(BOOL ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::wstring quote(const std::filesystem::path& path) {
  auto value = std::filesystem::absolute(path).wstring();
  if (value.find(L'"') != std::wstring::npos) throw std::invalid_argument("Invalid runtime path");
  return L"\"" + value + L"\"";
}
std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  return first == std::string::npos ? "" : value.substr(first, value.find_last_not_of(" \t\r\n")-first+1);
}
}
struct LocalTranslator::State {
  HANDLE job{}, process{};
  unsigned short port{};
  std::wstring authorization;
  ~State() {
    if (job) { TerminateJobObject(job,0); CloseHandle(job); }
    if (process) { WaitForSingleObject(process,5000); CloseHandle(process); }
  }
  std::string request(const wchar_t* path, const std::string& body, bool health = false) {
    if (WaitForSingleObject(process,0) != WAIT_TIMEOUT) throw std::runtime_error("Local model process stopped");
    InternetHandle session(WinHttpOpen(L"d4r0",WINHTTP_ACCESS_TYPE_NO_PROXY,nullptr,nullptr,0));
    check(WinHttpSetTimeouts(session.value,1000,1000,5000,health ? 1000 : 60000),"Cannot set local request timeout");
    InternetHandle connection(WinHttpConnect(session.value,L"127.0.0.1",port,0));
    InternetHandle request(WinHttpOpenRequest(connection.value,health ? L"GET" : L"POST",path,nullptr,
                                             WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0));
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    check(WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy)),"Cannot disable redirects");
    DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    check(WinHttpSetOption(request.value,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled)),"Cannot disable ambient authentication");
    const auto headers = authorization + L"Content-Type: application/json\r\n";
    check(WinHttpSendRequest(request.value,headers.c_str(),DWORD(headers.size()),
        body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),DWORD(body.size()),DWORD(body.size()),0),
        "Cannot send local translation request");
    check(WinHttpReceiveResponse(request.value,nullptr),"Local translation response failed");
    DWORD status{}, size = sizeof(status);
    check(WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX),"Cannot read local response status");
    if (status != 200) throw std::runtime_error("Local model is not ready or rejected the request");
    std::string response;
    std::array<char,8192> buffer;
    DWORD read{};
    do {
      check(WinHttpReadData(request.value,buffer.data(),DWORD(buffer.size()),&read),"Cannot read local translation");
      if (response.size()+read > 1024*1024) throw std::runtime_error("Local model response exceeds limit");
      response.append(buffer.data(),read);
    } while (read);
    return response;
  }
};

LocalTranslator::LocalTranslator(const PipelineSettings& settings, std::stop_token stop)
    : state_(std::make_unique<State>()) {
  const auto model = settings.selectedModel == ModelChoice::TranslateGemma12B ? settings.model12b : settings.model4b;
  if (!std::filesystem::is_regular_file(settings.llamaExecutable) || !std::filesystem::is_regular_file(model))
    throw std::runtime_error("Configure a local llama-server executable and the explicitly selected GGUF model");
  if (stop.stop_requested()) throw std::runtime_error("Translation startup cancelled");
  std::array<unsigned char,34> random{};
  if (BCryptGenRandom(nullptr,random.data(),ULONG(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
    throw std::runtime_error("Cannot generate local authentication key");
  std::wstring key;
  for (unsigned i = 0; i < 32; ++i) {
    key += L"0123456789abcdef"[random[i] >> 4]; key += L"0123456789abcdef"[random[i] & 15];
  }
  state_->port = static_cast<unsigned short>(20000 + ((random[32] << 8) | random[33]) % 40000);
  state_->authorization = L"Authorization: Bearer " + key + L"\r\n";
  state_->job = CreateJobObjectW(nullptr,nullptr);
  if (!state_->job) throw std::runtime_error("Cannot create model process job");
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  check(SetInformationJobObject(state_->job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"Cannot contain model process");
  SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
  winrt::handle sink(CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                                 &security,OPEN_EXISTING,0,nullptr));
  if (!sink) throw std::runtime_error("Cannot redirect model output");
  SIZE_T bytes{};
  InitializeProcThreadAttributeList(nullptr,2,0,&bytes);
  std::vector<unsigned char> storage(bytes);
  auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
  check(InitializeProcThreadAttributeList(attributes,2,0,&bytes),"Cannot initialize model launch attributes");
  struct AttributeCleanup { LPPROC_THREAD_ATTRIBUTE_LIST value; ~AttributeCleanup() { DeleteProcThreadAttributeList(value); } } cleanup{attributes};
  HANDLE sinkHandle = sink.get();
  check(UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,&sinkHandle,sizeof(sinkHandle),nullptr,nullptr),"Cannot isolate inherited handles");
  check(UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_JOB_LIST,&state_->job,sizeof(state_->job),nullptr,nullptr),"Cannot assign model process job");
  STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes;
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = sinkHandle;
  auto command = quote(settings.llamaExecutable) + L" -m " + quote(model) +
      L" -ngl 99 -c 4096 -np 1 --host 127.0.0.1 --port " + std::to_wstring(state_->port) +
      L" --api-key " + key + L" --no-webui --no-jinja --chat-template gemma --log-disable";
  PROCESS_INFORMATION process{};
  const auto executable = std::filesystem::absolute(settings.llamaExecutable);
  check(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,
      CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,executable.parent_path().c_str(),
      &startup.StartupInfo,&process),"Cannot start local model runtime");
  state_->process = process.hProcess; CloseHandle(process.hThread);
  std::stop_callback cancel(stop,[this] { TerminateJobObject(state_->job,0); });
  const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(120);
  bool ready = false;
  while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline) {
    try { state_->request(L"/health",{},true); ready = true; break; } catch (const std::exception&) {}
    if (WaitForSingleObject(state_->process,100) != WAIT_TIMEOUT)
      throw std::runtime_error("Local model runtime exited during loading; check selected model and GPU runtime");
  }
  if (!ready) throw std::runtime_error("Local model loading timed out or was cancelled");
  if (translate({"Bereit"},stop).front().empty())
    throw std::runtime_error("Local model warm-up returned no translation");
}
LocalTranslator::~LocalTranslator() = default;
bool LocalTranslator::alive() const { return WaitForSingleObject(state_->process,0) == WAIT_TIMEOUT; }

std::vector<std::string> LocalTranslator::translate(const std::vector<std::string>& german, std::stop_token stop) {
  if (german.empty()) return {};
  if (german.size() > 16) throw std::invalid_argument("Translation batch exceeds limit");
  std::size_t characters = 0;
  for (const auto& line : german) characters += line.size();
  if (characters > 12000) throw std::invalid_argument("Translation input exceeds local context limit");
  if (stop.stop_requested()) throw std::runtime_error("Translation cancelled");
  std::stop_callback cancel(stop,[this] { TerminateJobObject(state_->job,0); });
  auto requestBatch = [&](const std::vector<std::string>& source) {
    using namespace winrt::Windows::Data::Json;
    const auto input = source.size() == 1 ? source.front() : makeTranslationPrompt(source);
    const std::string prompt = "<bos><start_of_turn>user\nYou are a professional German (de) to English (en) translator. "
        "Your goal is to accurately convey the meaning and nuances of the original German text while adhering to English grammar, vocabulary, and cultural sensitivities.\n"
        "Produce only the English translation, without any additional explanations or commentary. Please translate the following German text into English:\n\n\n" +
        input + "<end_of_turn>\n<start_of_turn>model\n";
    JsonObject request;
    request.SetNamedValue(L"prompt",JsonValue::CreateStringValue(winrt::to_hstring(prompt)));
    request.SetNamedValue(L"n_predict",JsonValue::CreateNumberValue(1024));
    request.SetNamedValue(L"temperature",JsonValue::CreateNumberValue(0));
    request.SetNamedValue(L"cache_prompt",JsonValue::CreateBooleanValue(true));
    JsonArray stops; stops.Append(JsonValue::CreateStringValue(L"<end_of_turn>"));
    request.SetNamedValue(L"stop",stops);
    const auto response = JsonObject::Parse(winrt::to_hstring(
        state_->request(L"/completion",winrt::to_string(request.Stringify()))));
    if (response.GetNamedBoolean(L"truncated",false) || response.GetNamedBoolean(L"stopped_limit",false))
      throw std::runtime_error("Local translation was truncated");
    const auto content = winrt::to_string(response.GetNamedString(L"content"));
    return source.size() == 1 ? std::vector<std::string>{content}
                              : parseNumberedTranslations(content,source.size());
  };
  auto translated = requestBatch(german);
  std::vector<std::size_t> unresolved;
  for (std::size_t i = 0; i < translated.size(); ++i) {
    translated[i] = trim(std::move(translated[i]));
    if (translated[i].empty() || !preservesProtectedTokens(german[i],translated[i])) {
      translated[i].clear(); unresolved.push_back(i);
    }
  }
  if (!unresolved.empty()) {
    std::vector<std::string> retrySource;
    for (const auto index : unresolved) retrySource.push_back(german[index]);
    auto retry = requestBatch(retrySource);
    for (std::size_t i = 0; i < unresolved.size(); ++i) {
      retry[i] = trim(std::move(retry[i]));
      if (!retry[i].empty() && preservesProtectedTokens(german[unresolved[i]],retry[i]))
        translated[unresolved[i]] = std::move(retry[i]);
    }
  }
  return translated;
}
}
