#include "d4r0/ReplayBuffer.h"
#include "d4r0/DebugLog.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <mferror.h>
#include <d3d11_1.h>
#include <winrt/base.h>
#include <wrl/client.h>
#include <deque>
#include <mutex>
#include <limits>
namespace d4r0 {
using Microsoft::WRL::ComPtr;
namespace {
constexpr auto frameDuration = std::chrono::nanoseconds(1'000'000'000/30);
constexpr auto segmentDuration = std::chrono::seconds(30);
constexpr std::size_t segmentLimit = 20;
void setVideoType(IMFMediaType* type, const GUID& subtype, UINT width, UINT height) {
  winrt::check_hresult(type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));
  winrt::check_hresult(type->SetGUID(MF_MT_SUBTYPE,subtype));
  winrt::check_hresult(MFSetAttributeSize(type,MF_MT_FRAME_SIZE,width,height));
  winrt::check_hresult(MFSetAttributeRatio(type,MF_MT_FRAME_RATE,30,1));
  winrt::check_hresult(MFSetAttributeRatio(type,MF_MT_PIXEL_ASPECT_RATIO,1,1));
  winrt::check_hresult(type->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));
}
void cleanupDeadReplayDirectories(const std::filesystem::path& parent) {
  std::error_code error;
  for (std::filesystem::directory_iterator entries(parent,error); !error && entries != std::filesystem::directory_iterator{}; ++entries) {
    if (!entries->is_directory(error)) continue;
    const auto name = entries->path().filename().wstring();
    if (!name.starts_with(L"d4r0-replay-")) continue;
    DWORD processId{};
    try {
      const auto parsed = std::stoull(name.substr(12));
      if (!parsed || parsed > std::numeric_limits<DWORD>::max()) continue;
      processId = static_cast<DWORD>(parsed);
    } catch (const std::exception&) { continue; }
    if (processId == GetCurrentProcessId()) continue;
    const auto process = OpenProcess(SYNCHRONIZE,FALSE,processId);
    const bool exited = process ? WaitForSingleObject(process,0) == WAIT_OBJECT_0 : GetLastError() == ERROR_INVALID_PARAMETER;
    if (process) CloseHandle(process);
    if (exited) {
      error.clear(); std::filesystem::remove_all(entries->path(),error);
      if (error) debugLog("Could not delete a dead-process replay ring");
    }
  }
}
}
struct ReplayBuffer::State {
  mutable std::mutex mutex;
  std::filesystem::path directory;
  bool active{}, mfStarted{};
  std::wstring error;
  const wchar_t* operation{L"idle"};
  UINT resetToken{};
  DWORD stream{};
  UINT sourceWidth{}, sourceHeight{};
  std::uint64_t nextSegment{};
  std::chrono::steady_clock::time_point segmentStart{}, lastFrame{};
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<ID3D11VideoDevice> videoDevice;
  ComPtr<ID3D11VideoContext> videoContext;
  ComPtr<ID3D11VideoProcessorEnumerator> videoEnumerator;
  ComPtr<ID3D11VideoProcessor> videoProcessor;
  ComPtr<ID3D11Texture2D> processorInput;
  ComPtr<IMFDXGIDeviceManager> deviceManager;
  ComPtr<IMFVideoSampleAllocatorEx> sampleAllocator;
  ComPtr<IMFSinkWriter> writer;
  std::deque<std::filesystem::path> segments;

  void finishSegment() {
    if (!writer) return;
    const auto result = writer->Finalize();
    writer.Reset();
    if (FAILED(result)) debugLog("Replay segment finalization failed");
  }
  void beginSegment(ID3D11Texture2D* texture, std::chrono::steady_clock::time_point timestamp) {
    D3D11_TEXTURE2D_DESC description{}; texture->GetDesc(&description);
    if (description.Format != DXGI_FORMAT_B8G8R8A8_UNORM || description.ArraySize != 1 ||
        description.MipLevels != 1 || description.SampleDesc.Count != 1)
      throw winrt::hresult_invalid_argument();
    ComPtr<ID3D11Device> sourceDevice; texture->GetDevice(&sourceDevice);
    if (device.Get() != sourceDevice.Get()) {
      finishSegment(); device = sourceDevice; deviceManager.Reset();
      device->GetImmediateContext(&context);
      winrt::check_hresult(device.As(&videoDevice));
      winrt::check_hresult(context.As(&videoContext));
      winrt::check_hresult(MFCreateDXGIDeviceManager(&resetToken,&deviceManager));
      winrt::check_hresult(deviceManager->ResetDevice(device.Get(),resetToken));
    }
    sourceWidth = description.Width; sourceHeight = description.Height;
    auto inputTextureDescription = description;
    inputTextureDescription.Usage = D3D11_USAGE_DEFAULT;
    inputTextureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    inputTextureDescription.CPUAccessFlags = 0; inputTextureDescription.MiscFlags = 0;
    winrt::check_hresult(device->CreateTexture2D(&inputTextureDescription,nullptr,&processorInput));
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC videoDescription{};
    videoDescription.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    videoDescription.InputFrameRate = {30,1};
    videoDescription.InputWidth = sourceWidth; videoDescription.InputHeight = sourceHeight;
    videoDescription.OutputFrameRate = {30,1};
    videoDescription.OutputWidth = 1920; videoDescription.OutputHeight = 1080;
    videoDescription.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    winrt::check_hresult(videoDevice->CreateVideoProcessorEnumerator(&videoDescription,&videoEnumerator));
    winrt::check_hresult(videoDevice->CreateVideoProcessor(videoEnumerator.Get(),0,&videoProcessor));
    auto path = directory/(L"segment-"+std::to_wstring(nextSegment++)+L".mp4");
    ComPtr<IMFAttributes> attributes;
    winrt::check_hresult(MFCreateAttributes(&attributes,3));
    winrt::check_hresult(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,TRUE));
    winrt::check_hresult(attributes->SetUnknown(MF_SINK_WRITER_D3D_MANAGER,deviceManager.Get()));
    operation = L"create MP4 sink";
    winrt::check_hresult(MFCreateSinkWriterFromURL(path.c_str(),nullptr,attributes.Get(),&writer));
    ComPtr<IMFMediaType> output;
    winrt::check_hresult(MFCreateMediaType(&output));
    setVideoType(output.Get(),MFVideoFormat_H264,1920,1080);
    winrt::check_hresult(output->SetUINT32(MF_MT_AVG_BITRATE,10'000'000));
    winrt::check_hresult(output->SetUINT32(MF_MT_MPEG2_PROFILE,77));
    operation = L"add H.264 stream";
    winrt::check_hresult(writer->AddStream(output.Get(),&stream));
    ComPtr<IMFMediaType> input;
    winrt::check_hresult(MFCreateMediaType(&input));
    setVideoType(input.Get(),MFVideoFormat_NV12,1920,1080);
    operation = L"set NV12 GPU input";
    winrt::check_hresult(writer->SetInputMediaType(stream,input.Get(),nullptr));
    sampleAllocator.Reset();
    winrt::check_hresult(MFCreateVideoSampleAllocatorEx(IID_PPV_ARGS(&sampleAllocator)));
    winrt::check_hresult(sampleAllocator->SetDirectXManager(deviceManager.Get()));
    ComPtr<IMFAttributes> allocatorAttributes;
    winrt::check_hresult(MFCreateAttributes(&allocatorAttributes,1));
    winrt::check_hresult(allocatorAttributes->SetUINT32(MF_SA_D3D11_BINDFLAGS,D3D11_BIND_RENDER_TARGET));
    winrt::check_hresult(sampleAllocator->InitializeSampleAllocatorEx(3,8,allocatorAttributes.Get(),input.Get()));
    operation = L"begin replay encoding";
    winrt::check_hresult(writer->BeginWriting());
    ComPtr<IMFTransform> encoder;
    operation = L"inspect replay encoder";
    winrt::check_hresult(writer->GetServiceForStream(stream,GUID_NULL,IID_PPV_ARGS(&encoder)));
    ComPtr<IMFAttributes> encoderAttributes;
    winrt::check_hresult(encoder->GetAttributes(&encoderAttributes));
    UINT32 hardwareUrlLength{};
    if (FAILED(encoderAttributes->GetStringLength(MFT_ENUM_HARDWARE_URL_Attribute,&hardwareUrlLength)) || !hardwareUrlLength)
      throw winrt::hresult_error(MF_E_TOPO_CODEC_NOT_FOUND,L"Media Foundation selected a software replay encoder");
    segmentStart = timestamp; lastFrame = {};
    segments.push_back(path);
    while (segments.size() > segmentLimit) {
      std::error_code error; std::filesystem::remove(segments.front(),error); segments.pop_front();
    }
  }
  void write(ID3D11Texture2D* texture, std::chrono::steady_clock::time_point timestamp) {
    if (!writer || timestamp-segmentStart >= segmentDuration) {
      finishSegment(); beginSegment(texture,timestamp);
    }
    if (lastFrame != std::chrono::steady_clock::time_point{} && timestamp-lastFrame < frameDuration) return;
    D3D11_TEXTURE2D_DESC description{}; texture->GetDesc(&description);
    ComPtr<ID3D11Device> sourceDevice; texture->GetDevice(&sourceDevice);
    if (description.Width != sourceWidth || description.Height != sourceHeight || sourceDevice.Get() != device.Get()) {
      finishSegment(); beginSegment(texture,timestamp);
    }
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDescription{};
    inputDescription.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11VideoProcessorInputView> inputView;
    operation = L"create replay input view";
    context->CopyResource(processorInput.Get(),texture);
    winrt::check_hresult(videoDevice->CreateVideoProcessorInputView(processorInput.Get(),videoEnumerator.Get(),&inputDescription,&inputView));
    ComPtr<IMFSample> sample;
    operation = L"allocate replay sample";
    winrt::check_hresult(sampleAllocator->AllocateSample(&sample));
    ComPtr<IMFMediaBuffer> buffer;
    winrt::check_hresult(sample->GetBufferByIndex(0,&buffer));
    ComPtr<IMFDXGIBuffer> dxgiBuffer;
    winrt::check_hresult(buffer.As(&dxgiBuffer));
    ComPtr<ID3D11Texture2D> encoderTexture;
    winrt::check_hresult(dxgiBuffer->GetResource(IID_PPV_ARGS(&encoderTexture)));
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDescription{};
    outputDescription.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11VideoProcessorOutputView> outputView;
    winrt::check_hresult(videoDevice->CreateVideoProcessorOutputView(encoderTexture.Get(),videoEnumerator.Get(),&outputDescription,&outputView));
    D3D11_VIDEO_PROCESSOR_STREAM streamData{};
    streamData.Enable = TRUE; streamData.pInputSurface = inputView.Get();
    operation = L"convert replay frame to NV12";
    winrt::check_hresult(videoContext->VideoProcessorBlt(videoProcessor.Get(),outputView.Get(),0,1,&streamData));
    winrt::check_hresult(buffer->SetCurrentLength(1920*1080*3/2));
    const auto sampleTime = std::chrono::duration_cast<std::chrono::nanoseconds>(timestamp-segmentStart).count()/100;
    winrt::check_hresult(sample->SetSampleTime(sampleTime));
    winrt::check_hresult(sample->SetSampleDuration(frameDuration.count()/100));
    operation = L"write GPU replay sample";
    winrt::check_hresult(writer->WriteSample(stream,sample.Get()));
    operation = L"idle";
    lastFrame = timestamp;
  }
};
ReplayBuffer::ReplayBuffer() : state_(std::make_unique<State>()) {
  std::error_code error;
  const auto parent = std::filesystem::temp_directory_path(error);
  cleanupDeadReplayDirectories(parent);
  state_->directory = parent/
      (L"d4r0-replay-"+std::to_wstring(GetCurrentProcessId()));
}
ReplayBuffer::~ReplayBuffer() {
  stop();
  std::error_code error; std::filesystem::remove_all(state_->directory,error);
  if (error) debugLog("Could not delete the current replay ring");
}
bool ReplayBuffer::start() {
  std::scoped_lock lock(state_->mutex);
  if (state_->active) return true;
  std::error_code error;
  error.clear(); std::filesystem::remove_all(state_->directory,error);
  error.clear(); std::filesystem::create_directories(state_->directory,error);
  if (error || FAILED(MFStartup(MF_VERSION,MFSTARTUP_FULL))) return false;
  state_->mfStarted = state_->active = true;
  return true;
}
void ReplayBuffer::stop() {
  bool shutdown{};
  {
    std::scoped_lock lock(state_->mutex);
    if (!state_->mfStarted) return;
    state_->active = false; state_->finishSegment();
    state_->sampleAllocator.Reset(); state_->processorInput.Reset(); state_->videoProcessor.Reset(); state_->videoEnumerator.Reset();
    state_->videoContext.Reset(); state_->videoDevice.Reset(); state_->context.Reset();
    state_->deviceManager.Reset(); state_->device.Reset();
    state_->mfStarted = false; shutdown = true;
  }
  if (shutdown) MFShutdown();
}
void ReplayBuffer::submitSourceFrame(ID3D11Texture2D* source, std::chrono::steady_clock::time_point timestamp) {
  if (!source) return;
  std::scoped_lock lock(state_->mutex);
  if (!state_->active) return;
  try { state_->write(source,timestamp); }
  catch (const winrt::hresult_error& error) {
    state_->error = std::wstring(state_->operation)+L": "+error.message()+L" (0x"+
        std::to_wstring(std::uint32_t(error.code().value))+L")";
    state_->active = false; state_->finishSegment();
    debugLog("Hardware replay stopped: " + winrt::to_string(state_->error));
  } catch (const std::exception& error) {
    state_->error.assign(error.what(),error.what()+std::char_traits<char>::length(error.what()));
    state_->active = false; state_->finishSegment();
    debugLog("Hardware replay stopped: " + std::string(error.what()));
  }
}
bool ReplayBuffer::active() const { std::scoped_lock lock(state_->mutex); return state_->active; }
std::wstring ReplayBuffer::lastError() const { std::scoped_lock lock(state_->mutex); return state_->error; }
const std::filesystem::path& ReplayBuffer::directory() const { return state_->directory; }
} // namespace d4r0
