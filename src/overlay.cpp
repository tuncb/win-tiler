#include "overlay.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <vector>

// Windows headers
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// Direct3D 11
#include <d3d11.h>

// DXGI
#include <dxgi1_2.h>

// Direct2D
#include <d2d1_1.h>

// DirectWrite
#include <dwrite.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwrite.lib")

namespace wintiler {
namespace overlay {

namespace {

// Window
HWND g_hwnd = nullptr;
HINSTANCE g_hInstance = nullptr;
const wchar_t* WINDOW_CLASS_NAME = L"WinTilerOverlayClass";

// Virtual screen metrics
int g_virtualX = 0;
int g_virtualY = 0;
int g_virtualWidth = 0;
int g_virtualHeight = 0;

// D3D11/DXGI
ID3D11Device* g_d3dDevice = nullptr;
ID3D11DeviceContext* g_d3dContext = nullptr;
IDXGISwapChain1* g_swapChain = nullptr;

// D2D
ID2D1Factory1* g_d2dFactory = nullptr;
ID2D1Device* g_d2dDevice = nullptr;
ID2D1DeviceContext* g_d2dContext = nullptr;
ID2D1Bitmap1* g_targetBitmap = nullptr;

// DWrite
IDWriteFactory* g_dwriteFactory = nullptr;
IDWriteTextFormat* g_textFormat = nullptr;

// Drawing state
std::vector<DrawRect> g_rects;

// Toast state with timing
struct ActiveToast {
  Toast toast;
  std::chrono::steady_clock::time_point start_time;
};
std::vector<ActiveToast> g_toasts;

bool g_initialized = false;
bool g_comInitialized = false;

// Helper to safely release COM objects
template <typename T>
void safe_release(T*& ptr) {
  if (ptr) {
    ptr->Release();
    ptr = nullptr;
  }
}

// Window procedure
LRESULT CALLBACK overlay_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Convert UTF-8 string to wide string
std::wstring utf8_to_wide(const std::string& str) {
  if (str.empty()) {
    return L"";
  }
  int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), result.data(), size);
  return result;
}

bool create_window() {
  g_hInstance = GetModuleHandleW(nullptr);

  // Register window class
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = overlay_wnd_proc;
  wc.hInstance = g_hInstance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = WINDOW_CLASS_NAME;

  if (!RegisterClassExW(&wc)) {
    DWORD err = GetLastError();
    if (err != ERROR_CLASS_ALREADY_EXISTS) {
      spdlog::error("Failed to register overlay window class: {}", err);
      return false;
    }
  }

  // Get virtual screen metrics
  g_virtualX = GetSystemMetrics(SM_XVIRTUALSCREEN);
  g_virtualY = GetSystemMetrics(SM_YVIRTUALSCREEN);
  g_virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  g_virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

  spdlog::info("Virtual screen: x={}, y={}, w={}, h={}", g_virtualX, g_virtualY, g_virtualWidth,
               g_virtualHeight);

  // Create layered, transparent, topmost window
  DWORD exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
  DWORD style = WS_POPUP;

  g_hwnd =
      CreateWindowExW(exStyle, WINDOW_CLASS_NAME, L"WinTilerOverlay", style, g_virtualX, g_virtualY,
                      g_virtualWidth, g_virtualHeight, nullptr, nullptr, g_hInstance, nullptr);

  if (!g_hwnd) {
    spdlog::error("Failed to create overlay window: {}", GetLastError());
    return false;
  }

  // Show the window
  ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
  UpdateWindow(g_hwnd);

  return true;
}

bool create_d3d_device() {
  D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                       D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};

  UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
  creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  D3D_FEATURE_LEVEL featureLevel;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, creationFlags,
                                 featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
                                 &g_d3dDevice, &featureLevel, &g_d3dContext);

  if (FAILED(hr)) {
    // Try without debug layer
    creationFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, creationFlags, featureLevels,
                           ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &g_d3dDevice, &featureLevel,
                           &g_d3dContext);
  }

  if (FAILED(hr)) {
    spdlog::error("Failed to create D3D11 device: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  spdlog::debug("Created D3D11 device with feature level 0x{:X}", static_cast<int>(featureLevel));
  return true;
}

bool create_swap_chain() {
  // Get DXGI device
  IDXGIDevice* dxgiDevice = nullptr;
  HRESULT hr = g_d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
  if (FAILED(hr)) {
    spdlog::error("Failed to get DXGI device: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  // Get DXGI adapter
  IDXGIAdapter* dxgiAdapter = nullptr;
  hr = dxgiDevice->GetAdapter(&dxgiAdapter);
  if (FAILED(hr)) {
    dxgiDevice->Release();
    spdlog::error("Failed to get DXGI adapter: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  // Get DXGI factory
  IDXGIFactory2* dxgiFactory = nullptr;
  hr = dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), (void**)&dxgiFactory);
  dxgiAdapter->Release();
  if (FAILED(hr)) {
    dxgiDevice->Release();
    spdlog::error("Failed to get DXGI factory: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  // Create swap chain
  DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
  swapDesc.Width = static_cast<UINT>(g_virtualWidth);
  swapDesc.Height = static_cast<UINT>(g_virtualHeight);
  swapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  swapDesc.SampleDesc.Count = 1;
  swapDesc.SampleDesc.Quality = 0;
  swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapDesc.BufferCount = 2;
  swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  swapDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

  hr = dxgiFactory->CreateSwapChainForComposition(g_d3dDevice, &swapDesc, nullptr, &g_swapChain);
  dxgiFactory->Release();
  dxgiDevice->Release();

  if (FAILED(hr)) {
    spdlog::error("Failed to create swap chain: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  return true;
}

bool create_d2d_resources() {
  // Create D2D factory
  D2D1_FACTORY_OPTIONS options = {};
#ifdef _DEBUG
  options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

  HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
                                 &options, reinterpret_cast<void**>(&g_d2dFactory));

  if (FAILED(hr)) {
    spdlog::error("Failed to create D2D factory: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  // Get DXGI device from D3D device
  IDXGIDevice* dxgiDevice = nullptr;
  hr = g_d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
  if (FAILED(hr)) {
    spdlog::error("Failed to get DXGI device for D2D: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  // Create D2D device
  hr = g_d2dFactory->CreateDevice(dxgiDevice, &g_d2dDevice);
  dxgiDevice->Release();
  if (FAILED(hr)) {
    spdlog::error("Failed to create D2D device: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  // Create D2D device context
  hr = g_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_d2dContext);
  if (FAILED(hr)) {
    spdlog::error("Failed to create D2D device context: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  return true;
}

bool create_render_target() {
  // Get back buffer from swap chain
  IDXGISurface* dxgiSurface = nullptr;
  HRESULT hr = g_swapChain->GetBuffer(0, __uuidof(IDXGISurface), (void**)&dxgiSurface);
  if (FAILED(hr)) {
    spdlog::error("Failed to get swap chain buffer: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  // Create bitmap from DXGI surface
  D2D1_BITMAP_PROPERTIES1 bitmapProps = {};
  bitmapProps.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
  bitmapProps.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
  bitmapProps.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

  hr = g_d2dContext->CreateBitmapFromDxgiSurface(dxgiSurface, &bitmapProps, &g_targetBitmap);
  dxgiSurface->Release();

  if (FAILED(hr)) {
    spdlog::error("Failed to create D2D bitmap: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  // Set render target
  g_d2dContext->SetTarget(g_targetBitmap);

  return true;
}

bool create_dwrite_resources() {
  HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&g_dwriteFactory));

  if (FAILED(hr)) {
    spdlog::error("Failed to create DWrite factory: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  // Create default text format
  hr = g_dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         14.0f, L"en-us", &g_textFormat);

  if (FAILED(hr)) {
    spdlog::error("Failed to create text format: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  return true;
}

bool bind_swap_chain_to_window() {
  // Use DirectComposition to bind swap chain to layered window
  // This is required for WS_EX_LAYERED with DXGI_ALPHA_MODE_PREMULTIPLIED

  // Get DComp device
  typedef HRESULT(WINAPI * PFN_DCompositionCreateDevice)(IDXGIDevice*, REFIID, void**);

  HMODULE dcompLib = LoadLibraryW(L"dcomp.dll");
  if (!dcompLib) {
    spdlog::error("Failed to load dcomp.dll");
    return false;
  }

  auto DCompositionCreateDevice =
      (PFN_DCompositionCreateDevice)GetProcAddress(dcompLib, "DCompositionCreateDevice");
  if (!DCompositionCreateDevice) {
    FreeLibrary(dcompLib);
    spdlog::error("Failed to get DCompositionCreateDevice");
    return false;
  }

  // Forward declare interfaces we need
  struct IDCompositionDevice;
  struct IDCompositionTarget;
  struct IDCompositionVisual;

  // Get DXGI device
  IDXGIDevice* dxgiDevice = nullptr;
  HRESULT hr = g_d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
  if (FAILED(hr)) {
    FreeLibrary(dcompLib);
    spdlog::error("Failed to get DXGI device for DComp: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  // Define IIDs manually
  static const GUID IID_IDCompositionDevice = {
      0xC37EA93A, 0xE7AA, 0x450D, {0xB1, 0x6F, 0x97, 0x46, 0xCB, 0x04, 0x07, 0xF3}};

  IUnknown* dcompDevice = nullptr;
  hr = DCompositionCreateDevice(dxgiDevice, IID_IDCompositionDevice, (void**)&dcompDevice);
  dxgiDevice->Release();

  if (FAILED(hr)) {
    FreeLibrary(dcompLib);
    spdlog::error("Failed to create DComp device: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  // Use vtable directly to call methods
  // IDCompositionDevice vtable: QueryInterface, AddRef, Release, Commit, WaitForCommitCompletion,
  //                             GetFrameStatistics, CreateTargetForHwnd, CreateVisual, ...

  struct IDCompositionDeviceVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(void*, REFIID, void**);
    ULONG(STDMETHODCALLTYPE* AddRef)(void*);
    ULONG(STDMETHODCALLTYPE* Release)(void*);
    HRESULT(STDMETHODCALLTYPE* Commit)(void*);
    HRESULT(STDMETHODCALLTYPE* WaitForCommitCompletion)(void*);
    HRESULT(STDMETHODCALLTYPE* GetFrameStatistics)(void*, void*);
    HRESULT(STDMETHODCALLTYPE* CreateTargetForHwnd)(void*, HWND, BOOL, void**);
    HRESULT(STDMETHODCALLTYPE* CreateVisual)(void*, void**);
  };

  struct IDCompositionTargetVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(void*, REFIID, void**);
    ULONG(STDMETHODCALLTYPE* AddRef)(void*);
    ULONG(STDMETHODCALLTYPE* Release)(void*);
    HRESULT(STDMETHODCALLTYPE* SetRoot)(void*, void*);
  };

  struct IDCompositionVisualVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(void*, REFIID, void**);
    ULONG(STDMETHODCALLTYPE* AddRef)(void*);
    ULONG(STDMETHODCALLTYPE* Release)(void*);
    HRESULT(STDMETHODCALLTYPE* SetOffsetX1)(void*, float);
    HRESULT(STDMETHODCALLTYPE* SetOffsetX2)(void*, void*);
    HRESULT(STDMETHODCALLTYPE* SetOffsetY1)(void*, float);
    HRESULT(STDMETHODCALLTYPE* SetOffsetY2)(void*, void*);
    HRESULT(STDMETHODCALLTYPE* SetTransform1)(void*, const void*);
    HRESULT(STDMETHODCALLTYPE* SetTransform2)(void*, void*);
    HRESULT(STDMETHODCALLTYPE* SetTransformParent)(void*, void*);
    HRESULT(STDMETHODCALLTYPE* SetEffect)(void*, void*);
    HRESULT(STDMETHODCALLTYPE* SetBitmapInterpolationMode)(void*, int);
    HRESULT(STDMETHODCALLTYPE* SetBorderMode)(void*, int);
    HRESULT(STDMETHODCALLTYPE* SetClip1)(void*, const void*);
    HRESULT(STDMETHODCALLTYPE* SetClip2)(void*, void*);
    HRESULT(STDMETHODCALLTYPE* SetContent)(void*, IUnknown*);
    HRESULT(STDMETHODCALLTYPE* AddVisual)(void*, void*, BOOL, void*);
    HRESULT(STDMETHODCALLTYPE* RemoveVisual)(void*, void*);
    HRESULT(STDMETHODCALLTYPE* RemoveAllVisuals)(void*);
    HRESULT(STDMETHODCALLTYPE* SetCompositeMode)(void*, int);
  };

  auto* deviceVtbl = *reinterpret_cast<IDCompositionDeviceVtbl**>(dcompDevice);

  // Create target for window
  IUnknown* dcompTarget = nullptr;
  hr = deviceVtbl->CreateTargetForHwnd(dcompDevice, g_hwnd, TRUE, (void**)&dcompTarget);
  if (FAILED(hr)) {
    dcompDevice->Release();
    FreeLibrary(dcompLib);
    spdlog::error("Failed to create DComp target: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  // Create visual
  IUnknown* dcompVisual = nullptr;
  hr = deviceVtbl->CreateVisual(dcompDevice, (void**)&dcompVisual);
  if (FAILED(hr)) {
    dcompTarget->Release();
    dcompDevice->Release();
    FreeLibrary(dcompLib);
    spdlog::error("Failed to create DComp visual: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  // Set swap chain as content
  auto* visualVtbl = *reinterpret_cast<IDCompositionVisualVtbl**>(dcompVisual);
  hr = visualVtbl->SetContent(dcompVisual, g_swapChain);
  if (FAILED(hr)) {
    dcompVisual->Release();
    dcompTarget->Release();
    dcompDevice->Release();
    FreeLibrary(dcompLib);
    spdlog::error("Failed to set DComp content: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  // Set visual as root
  auto* targetVtbl = *reinterpret_cast<IDCompositionTargetVtbl**>(dcompTarget);
  hr = targetVtbl->SetRoot(dcompTarget, dcompVisual);
  if (FAILED(hr)) {
    dcompVisual->Release();
    dcompTarget->Release();
    dcompDevice->Release();
    FreeLibrary(dcompLib);
    spdlog::error("Failed to set DComp root: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  // Commit
  hr = deviceVtbl->Commit(dcompDevice);
  if (FAILED(hr)) {
    dcompVisual->Release();
    dcompTarget->Release();
    dcompDevice->Release();
    FreeLibrary(dcompLib);
    spdlog::error("Failed to commit DComp: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  // Keep references alive (store them globally if needed for cleanup)
  // For now we'll leak them since they need to stay alive for the window lifetime
  // TODO: Store and release on shutdown

  spdlog::debug("DirectComposition binding successful");
  return true;
}

void draw_toast_internal(const Toast& toast) {
  if (!g_d2dContext || !g_dwriteFactory || !g_textFormat) {
    return;
  }

  std::wstring wideText = utf8_to_wide(toast.text);

  // Create text layout to measure
  IDWriteTextLayout* layout = nullptr;
  HRESULT hr =
      g_dwriteFactory->CreateTextLayout(wideText.c_str(), static_cast<UINT32>(wideText.length()),
                                        g_textFormat, 1000.0f, 100.0f, &layout);
  if (FAILED(hr) || !layout) {
    return;
  }

  DWRITE_TEXT_METRICS metrics = {};
  layout->GetMetrics(&metrics);

  float padding = 8.0f;
  float bgWidth = metrics.width + padding * 2;
  float bgHeight = metrics.height + padding * 2;

  // Adjust coordinates relative to virtual screen origin
  float adjustedX = toast.x - static_cast<float>(g_virtualX);
  float adjustedY = toast.y - static_cast<float>(g_virtualY);

  D2D1_RECT_F bgRect = {adjustedX, adjustedY, adjustedX + bgWidth, adjustedY + bgHeight};

  // Create background brush
  ID2D1SolidColorBrush* bgBrush = nullptr;
  g_d2dContext->CreateSolidColorBrush(
      D2D1::ColorF(toast.bg_color.r / 255.0f, toast.bg_color.g / 255.0f, toast.bg_color.b / 255.0f,
                   toast.bg_color.a / 255.0f),
      &bgBrush);

  if (bgBrush) {
    g_d2dContext->FillRectangle(bgRect, bgBrush);
    bgBrush->Release();
  }

  // Create text brush
  ID2D1SolidColorBrush* textBrush = nullptr;
  g_d2dContext->CreateSolidColorBrush(
      D2D1::ColorF(toast.text_color.r / 255.0f, toast.text_color.g / 255.0f,
                   toast.text_color.b / 255.0f, toast.text_color.a / 255.0f),
      &textBrush);

  if (textBrush) {
    D2D1_POINT_2F textOrigin = {adjustedX + padding, adjustedY + padding};
    g_d2dContext->DrawTextLayout(textOrigin, layout, textBrush, D2D1_DRAW_TEXT_OPTIONS_NONE);
    textBrush->Release();
  }

  layout->Release();
}

} // namespace

bool init() {
  if (g_initialized) {
    return true;
  }

  // Set DPI awareness (system-level only)
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

  // Initialize COM
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (SUCCEEDED(hr)) {
    g_comInitialized = true;
  } else if (hr != RPC_E_CHANGED_MODE) {
    spdlog::error("Failed to initialize COM: 0x{:08X}", static_cast<unsigned int>(hr));
    return false;
  }

  // Create window
  if (!create_window()) {
    shutdown();
    return false;
  }

  // Create D3D device
  if (!create_d3d_device()) {
    shutdown();
    return false;
  }

  // Create swap chain
  if (!create_swap_chain()) {
    shutdown();
    return false;
  }

  // Create D2D resources
  if (!create_d2d_resources()) {
    shutdown();
    return false;
  }

  // Create render target
  if (!create_render_target()) {
    shutdown();
    return false;
  }

  // Create DWrite resources
  if (!create_dwrite_resources()) {
    shutdown();
    return false;
  }

  // Bind swap chain to window using DirectComposition
  if (!bind_swap_chain_to_window()) {
    shutdown();
    return false;
  }

  g_initialized = true;
  spdlog::info("Overlay initialized successfully");
  return true;
}

void shutdown() {
  g_initialized = false;

  g_rects.clear();
  g_toasts.clear();

  safe_release(g_textFormat);
  safe_release(g_dwriteFactory);
  safe_release(g_targetBitmap);
  safe_release(g_d2dContext);
  safe_release(g_d2dDevice);
  safe_release(g_d2dFactory);
  safe_release(g_swapChain);
  safe_release(g_d3dContext);
  safe_release(g_d3dDevice);

  if (g_hwnd) {
    DestroyWindow(g_hwnd);
    g_hwnd = nullptr;
  }

  if (g_hInstance) {
    UnregisterClassW(WINDOW_CLASS_NAME, g_hInstance);
    g_hInstance = nullptr;
  }

  if (g_comInitialized) {
    CoUninitialize();
    g_comInitialized = false;
  }

  spdlog::info("Overlay shutdown complete");
}

void clear_rects() {
  g_rects.clear();
}

void add_rect(const DrawRect& rect) {
  g_rects.push_back(rect);
}

void show_toast(const Toast& toast) {
  ActiveToast active;
  active.toast = toast;
  active.start_time = std::chrono::steady_clock::now();
  g_toasts.push_back(active);
}

void clear_toasts() {
  g_toasts.clear();
}

void render() {
  if (!g_initialized || !g_d2dContext || !g_swapChain) {
    return;
  }

  // Pump messages for overlay window
  MSG msg;
  while (PeekMessageW(&msg, g_hwnd, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  // Begin drawing
  g_d2dContext->BeginDraw();
  g_d2dContext->Clear(D2D1::ColorF(0, 0, 0, 0)); // Fully transparent

  // Draw rectangles
  for (const auto& rect : g_rects) {
    ID2D1SolidColorBrush* brush = nullptr;
    g_d2dContext->CreateSolidColorBrush(D2D1::ColorF(rect.color.r / 255.0f, rect.color.g / 255.0f,
                                                     rect.color.b / 255.0f, rect.color.a / 255.0f),
                                        &brush);

    if (brush) {
      // Adjust coordinates relative to virtual screen origin
      float adjustedX = rect.x - static_cast<float>(g_virtualX);
      float adjustedY = rect.y - static_cast<float>(g_virtualY);

      D2D1_RECT_F d2dRect = {adjustedX, adjustedY, adjustedX + rect.width, adjustedY + rect.height};

      if (rect.border_width > 0) {
        g_d2dContext->DrawRectangle(d2dRect, brush, rect.border_width);
      } else {
        g_d2dContext->FillRectangle(d2dRect, brush);
      }
      brush->Release();
    }
  }

  // Remove expired toasts and draw active ones
  auto now = std::chrono::steady_clock::now();
  g_toasts.erase(
      std::remove_if(
          g_toasts.begin(), g_toasts.end(),
          [now](const ActiveToast& t) {
            auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - t.start_time).count();
            return static_cast<float>(elapsed) >= t.toast.duration_ms;
          }),
      g_toasts.end());

  for (const auto& active : g_toasts) {
    draw_toast_internal(active.toast);
  }

  // End drawing
  HRESULT hr = g_d2dContext->EndDraw();
  if (FAILED(hr)) {
    spdlog::error("EndDraw failed: 0x{:08X}", static_cast<unsigned int>(hr));
  }

  // Present
  hr = g_swapChain->Present(1, 0);
  if (FAILED(hr)) {
    spdlog::error("Present failed: 0x{:08X}", static_cast<unsigned int>(hr));
  }
}

bool is_initialized() {
  return g_initialized;
}

} // namespace overlay
} // namespace wintiler
