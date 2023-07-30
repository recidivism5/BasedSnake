int _fltused;
#define COBJMACROS
#define _NO_CRT_STDIO_INLINE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <dxgidebug.h>
#include <dwmapi.h>

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include "res.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
typedef unsigned long long u64;
typedef signed char i8;
typedef signed short i16;
typedef signed long i32;
typedef signed long long i64;
typedef float f32;
typedef double f64;
#define COUNT(arr) (sizeof(arr)/sizeof(*arr))

HINSTANCE instance;
void end(i32 code){
	CoUninitialize();
	ExitProcess(code);
}
void error(u16 *msg){
	MessageBoxW(0,msg,L"Error",0);
	end(1);
}

typedef struct {
	i32 width,height,rowPitch;
	u32 *pixels;
}Image;
void LoadPngFromFile(u16 *path, Image *img){
	IWICImagingFactory2 *ifactory;
	if (!SUCCEEDED(CoCreateInstance(&CLSID_WICImagingFactory2,0,CLSCTX_INPROC_SERVER,&IID_IWICImagingFactory2,&ifactory))){
		error(L"LoadPng: failed to create IWICImagingFactory2");
	}
	IWICBitmapDecoder *pDecoder = 0;
	if (S_OK != ifactory->lpVtbl->CreateDecoderFromFilename(ifactory,path,0,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&pDecoder)){
		u16 msg[512];
		_snwprintf(msg,COUNT(msg),L"LoadPng: file not found: %s",path);
		error(msg);
	}
	IWICBitmapFrameDecode *pFrame = 0;
	pDecoder->lpVtbl->GetFrame(pDecoder,0,&pFrame);
	WICPixelFormatGUID pixelformat;
	pFrame->lpVtbl->GetPixelFormat(pFrame,&pixelformat);
	if (!IsEqualGUID(&pixelformat,&GUID_WICPixelFormat32bppBGRA)){
		u16 msg[512];
		_snwprintf(msg,COUNT(msg),L"LoadPng: pixel format of %s not GUID_WICPixelFormat32bppBGRA",path);
		error(msg);
	}
	IWICBitmapFlipRotator *pFlipRotator;
	ifactory->lpVtbl->CreateBitmapFlipRotator(ifactory,&pFlipRotator);
	pFlipRotator->lpVtbl->Initialize(pFlipRotator,pFrame,WICBitmapTransformFlipVertical);
	pFlipRotator->lpVtbl->GetSize(pFlipRotator,&img->width,&img->height);
	u32 size = img->width*img->height*sizeof(u32);
	img->rowPitch = img->width*sizeof(u32);
	img->pixels = malloc(size);
	if (S_OK != pFlipRotator->lpVtbl->CopyPixels(pFlipRotator,0,img->rowPitch,size,img->pixels)){
		error(L"LoadPng: CopyPixels failed");
	}
	pFlipRotator->lpVtbl->Release(pFlipRotator);
	pFrame->lpVtbl->Release(pFrame);
	pDecoder->lpVtbl->Release(pDecoder);
	ifactory->lpVtbl->Release(ifactory);
}
/*
Blit:
Copies rectangle sx,sy,swidth,sheight from src image to dst image at dx,dy.
Additive is an optional color to be bytewise added to the RGB of foreground pixels. If the alpha of additive is non-zero, the additive replaces the foreground pixel.
*/
void Blit(Image *src, i32 sx, i32 sy, i32 swidth, i32 sheight, Image *dst, i32 dx, i32 dy, u32 additive){
	if (dx >= dst->width || dy >= dst->height) return;
	if (dx < 0){
		sx -= dx;
		swidth += dx;
		if (swidth < 1) return;
		dx = 0;
	}
	if (dx+swidth > dst->width){
		swidth -= (dx+swidth)-dst->width;
		if (swidth < 1) return;
	}
	if (dy < 0){
		sy -= dy;
		sheight += dy;
		if (sheight < 1) return;
		dy = 0;
	}
	if (dy+sheight > dst->height){
		sheight -= (dy+sheight)-dst->height;
		if (sheight < 1) return;
	}
	if (!additive){
		for (i32 i = 0; i < sheight; i++){
			for (i32 j = 0; j < swidth; j++){
				u32 p = ((u32*)((u8*)src->pixels+(sy+i)*src->rowPitch))[sx+j];
				if (p) ((u32*)((u8*)dst->pixels+(dy+i)*dst->rowPitch))[dx+j] = p;
			}
		}
	} else if (additive & 0xff000000){
		for (i32 i = 0; i < sheight; i++){
			for (i32 j = 0; j < swidth; j++){
				u32 p = ((u32*)((u8*)src->pixels+(sy+i)*src->rowPitch))[sx+j];
				if (p) ((u32*)((u8*)dst->pixels+(dy+i)*dst->rowPitch))[dx+j] = additive;
			}
		}
	} else {
		for (i32 i = 0; i < sheight; i++){
			for (i32 j = 0; j < swidth; j++){
				u32 p = ((u32*)((u8*)src->pixels+(sy+i)*src->rowPitch))[sx+j];
				if (p){
					for (i32 k = 0; k < 3; k++) ((u8*)&p)[k] += ((u8*)additive)[k];
					((u32*)((u8*)dst->pixels+(dy+i)*dst->rowPitch))[dx+j] = p;
				}
			}
		}
	}
}
Image font;
u8 fontWidths[95];
void DrawString(u8 *str, Image *dst, i32 x, i32 y, u32 color){
	while (*str){
		i32 sr = *str - ' ';
		Blit(&font,(sr%16)*8,(sr/16)*8,fontWidths[sr],8,dst,x,y,color);
		x += fontWidths[sr]+1;
		str++;
	}
}
void DrawLine(Image *dst, i32 x0, i32 y0, i32 x1, i32 y1, u32 color){
	i32 dx = abs(x1-x0), sx = x0<x1 ? 1 : -1;
	i32 dy = abs(y1-y0), sy = y0<y1 ? 1 : -1;
	i32 err = (dx>dy ? dx : -dy)/2, e2;
	while (1){
		((u32*)((u8*)dst->pixels+y0*dst->rowPitch))[x0] = color;
		if (x0==x1 && y0==y1) break;
		e2 = err;
		if (e2 >-dx) { err -= dy; x0 += sx; }
		if (e2 < dy) { err += dx; y0 += sy; }
	}
}
void DrawRect(Image *dst, i32 x, i32 y, i32 width, i32 height, u32 color){
	for (i32 i = 0; i < height; i++){
		for (i32 j = 0; j < width; j++){
			((u32*)((u8*)dst->pixels+(y+i)*dst->rowPitch))[x+j] = color;
		} 
	}
}

u8 keys[256];
#define KEYMASK_DOWN 0x80

#define BOARD_WIDTH 16
#define GAME_WIDTH (BOARD_WIDTH*16)
#define GAME_HEIGHT (BOARD_WIDTH*16)
#define SCALE 2
typedef struct {
	u8 val,dir;
}Cell;
Cell board[BOARD_WIDTH*BOARD_WIDTH];
enum {
	LEFT,RIGHT,DOWN,UP
};
i32 hx,hy,tx,ty,hdir = RIGHT,lastdir = RIGHT;
typedef struct {
	i32 x,y;
}IVec2;
void SpawnApple(){
	board[hy*BOARD_WIDTH+hx].val = 2;
	i32 total = 1;
	i32 used = 0;
	IVec2 *opencells = malloc(total*sizeof(IVec2));
	for (i32 y = 0; y < BOARD_WIDTH; y++){
		for (i32 x = 0; x < BOARD_WIDTH; x++){
			if (!board[y*BOARD_WIDTH+x].val){
				if (used==total){
					total *= 2;
					opencells = realloc(opencells,total*sizeof(IVec2));
				}
				opencells[used++] = (IVec2){x,y};
			}
		}
	}
	IVec2 v = opencells[rand() % used];
	board[v.y*BOARD_WIDTH+v.x].val = 1;
	free(opencells);
	board[hy*BOARD_WIDTH+hx].val = 0;
}
u8 kbmessages[16];
i32 kbused = 0;
i64 WindowProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam){
	switch (msg){
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		case WM_CREATE:{
			i32 t = 1;
			DwmSetWindowAttribute(wnd,20,&t,sizeof(t));
			break;
		}
		case WM_KEYDOWN:{
			if ((wparam=='A' || wparam=='D' || wparam=='S' || wparam=='W') && kbused < COUNT(kbmessages) && (!kbused || kbmessages[kbused-1]!=wparam)){
				kbmessages[kbused++] = wparam;
				return 0;
			}
			break;
		}
	}
	return DefWindowProcW(wnd, msg, wparam, lparam);
}
void WinMainCRTStartup(){
	instance = GetModuleHandleW(0);
	CoInitialize(0);
	srand(0);

	Image background, tiles;
	LoadPngFromFile(L"background.png",&background);
	LoadPngFromFile(L"tiles.png",&tiles);
	LoadPngFromFile(L"font.png",&font);
	for (i32 i = 0; i < COUNT(fontWidths); i++){
		i32 x = 8*(i%16), y = 8*(i/16);
		i32 w = 0;
		for(;w < 8; w++){
			i32 yy = 0;
			for(;yy < 8; yy++){
				if (font.pixels[(x+w) + font.width*(y+yy)]) break;
			}
			if (yy==8) break;
		}
		if (!w) w = 5;
		fontWidths[i] = w;
	}

	WNDCLASSEXW wc = {
		.cbSize = sizeof(wc),
		.lpfnWndProc = WindowProc,
		.hInstance = instance,
		.hIcon = LoadIconW(instance,MAKEINTRESOURCEA(RID_ICON)),
		.hCursor = LoadCursor(0, IDC_ARROW),
		.lpszClassName = L"Pixels",
	};
	ATOM atom = RegisterClassExW(&wc);
	i32 width = CW_USEDEFAULT;
	i32 height = CW_USEDEFAULT;
	DWORD exstyle = WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP;
	DWORD style = WS_OVERLAPPEDWINDOW;
	style &= ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;
	RECT rect = { 0, 0, SCALE*GAME_WIDTH, SCALE*GAME_HEIGHT };
	AdjustWindowRectEx(&rect, style, FALSE, exstyle);
	width = rect.right - rect.left;
	height = rect.bottom - rect.top;
	HWND window = CreateWindowExW(exstyle,wc.lpszClassName,L"Based Snake",style,GetSystemMetrics(SM_CXSCREEN)/2-width/2,GetSystemMetrics(SM_CYSCREEN)/2-height/2,width,height,0,0,wc.hInstance,0);
	HRESULT hr;
	ID3D11Device* device;
	ID3D11DeviceContext* context;
	{
		UINT flags = 0;
#ifndef NDEBUG
		flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
		D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
		D3D11CreateDevice(0,D3D_DRIVER_TYPE_HARDWARE,0,flags,levels,ARRAYSIZE(levels),D3D11_SDK_VERSION,&device,0,&context);
	}

#ifndef NDEBUG
	{
		ID3D11InfoQueue* info;
		device->lpVtbl->QueryInterface(device,&IID_ID3D11InfoQueue,&info);
		info->lpVtbl->SetBreakOnSeverity(info,D3D11_MESSAGE_SEVERITY_CORRUPTION,1);
		info->lpVtbl->SetBreakOnSeverity(info,D3D11_MESSAGE_SEVERITY_ERROR,1);
		info->lpVtbl->Release(info);
	}

	HMODULE dxgiDebug = LoadLibraryW(L"dxgidebug.dll");
	if (dxgiDebug != 0){
		HRESULT (WINAPI *dxgiGetDebugInterface)(REFIID riid, void** ppDebug);
		*(FARPROC*)&dxgiGetDebugInterface = GetProcAddress(dxgiDebug, "DXGIGetDebugInterface");

		IDXGIInfoQueue* dxgiInfo;
		dxgiGetDebugInterface(&IID_IDXGIInfoQueue,&dxgiInfo);
		dxgiInfo->lpVtbl->SetBreakOnSeverity(dxgiInfo,DXGI_DEBUG_ALL,DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION,1);
		dxgiInfo->lpVtbl->SetBreakOnSeverity(dxgiInfo,DXGI_DEBUG_ALL,DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR,1);
		dxgiInfo->lpVtbl->Release(dxgiInfo);
	}
#endif

	IDXGISwapChain1* swapChain;
	{
		IDXGIDevice* dxgiDevice;
		device->lpVtbl->QueryInterface(device,&IID_IDXGIDevice,&dxgiDevice);

		IDXGIAdapter* dxgiAdapter;
		dxgiDevice->lpVtbl->GetAdapter(dxgiDevice,&dxgiAdapter);

		IDXGIFactory2* factory;
		dxgiAdapter->lpVtbl->GetParent(dxgiAdapter,&IID_IDXGIFactory2,&factory);

		DXGI_SWAP_CHAIN_DESC1 desc =
		{
			// default 0 value for width & height means to get it from HWND automatically
			//.Width = 0,
			//.Height = 0,

			// or use DXGI_FORMAT_R8G8B8A8_UNORM_SRGB for storing sRGB
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,

			// FLIP presentation model does not allow MSAA framebuffer
			// if you want MSAA then you'll need to render offscreen and manually
			// resolve to non-MSAA framebuffer
			.SampleDesc = { 1, 0 },

			.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
			.BufferCount = 2,

			// we don't want any automatic scaling of window content
			// this is supported only on FLIP presentation model
			.Scaling = DXGI_SCALING_NONE,

			// use more efficient FLIP presentation model
			// Windows 10 allows to use DXGI_SWAP_EFFECT_FLIP_DISCARD
			// for Windows 8 compatibility use DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL
			// for Windows 7 compatibility use DXGI_SWAP_EFFECT_DISCARD
			.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
		};

		factory->lpVtbl->CreateSwapChainForHwnd(factory,device,window,&desc,0,0,&swapChain);

		// disable silly Alt+Enter changing monitor resolution to match window size
		factory->lpVtbl->MakeWindowAssociation(factory,window,DXGI_MWA_NO_ALT_ENTER);

		factory->lpVtbl->Release(factory);
		dxgiAdapter->lpVtbl->Release(dxgiAdapter);
		dxgiDevice->lpVtbl->Release(dxgiDevice);
	}

	struct Vertex
	{
		float position[2];
		float uv[2];
	};

	ID3D11Buffer* vbuffer;
	{
		struct Vertex data[] =
		{
			{ {-1.0f, 1.0f }, { 0.0f, 1.0f } },
			{ {-1.0f, -1.0f }, {  0.0f,  0.0f }},
			{ { 1.0f, -1.0f }, { 1.0f,  0.0f }},

			{ {1.0f, -1.0f}, { 1.0f, 0.0f }},
			{ {1.0f, 1.0f}, {  1.0f,  1.0f }},
			{ {-1.0f, 1.0f}, { 0.0f,  1.0f }},
		};

		D3D11_BUFFER_DESC desc =
		{
			.ByteWidth = sizeof(data),
			.Usage = D3D11_USAGE_IMMUTABLE,
			.BindFlags = D3D11_BIND_VERTEX_BUFFER,
		};

		D3D11_SUBRESOURCE_DATA initial = { .pSysMem = data };
		device->lpVtbl->CreateBuffer(device,&desc,&initial,&vbuffer);
	}

	// vertex & pixel shaders for drawing triangle, plus input layout for vertex input
	ID3D11InputLayout* layout;
	ID3D11VertexShader* vshader;
	ID3D11PixelShader* pshader;
	{
		// these must match vertex shader input layout (VS_INPUT in vertex shader source below)
		D3D11_INPUT_ELEMENT_DESC desc[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(struct Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(struct Vertex, uv),       D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

#define STR2(x) #x
#define STR(x) STR2(x)
		const char hlsl[] =
			"#line " STR(__LINE__) "                                  \n\n" // actual line number in this file for nicer error messages
			"                                                           \n"
			"struct VS_INPUT                                            \n"
			"{                                                          \n"
			"     float2 pos   : POSITION;                              \n" // these names must match D3D11_INPUT_ELEMENT_DESC array
			"     float2 uv    : TEXCOORD;                              \n"
			"};                                                         \n"
			"                                                           \n"
			"struct PS_INPUT                                            \n"
			"{                                                          \n"
			"  float4 pos   : SV_POSITION;                              \n" // these names do not matter, except SV_... ones
			"  float2 uv    : TEXCOORD;                                 \n"
			"};                                                         \n"
			"                                                           \n"
			"cbuffer cbuffer0 : register(b0)                            \n" // b0 = constant buffer bound to slot 0
			"{                                                          \n"
			"    float4x4 uTransform;                                   \n"
			"}                                                          \n"
			"                                                           \n"
			"sampler sampler0 : register(s0);                           \n" // s0 = sampler bound to slot 0
			"                                                           \n"
			"Texture2D<float4> texture0 : register(t0);                 \n" // t0 = shader resource bound to slot 0
			"                                                           \n"
			"PS_INPUT vs(VS_INPUT input)                                \n"
			"{                                                          \n"
			"    PS_INPUT output;                                       \n"
			"    output.pos = mul(uTransform, float4(input.pos, 0, 1)); \n"
			"    output.uv = input.uv;                                  \n"
			"    return output;                                         \n"
			"}                                                          \n"
			"                                                           \n"
			"float4 ps(PS_INPUT input) : SV_TARGET                      \n"
			"{                                                          \n"
			"    return texture0.Sample(sampler0, input.uv);            \n"
			"}                                                          \n";
		;

		UINT flags = D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR | D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS;
#ifndef NDEBUG
		flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

		ID3DBlob* error;

		ID3DBlob* vblob;
		hr = D3DCompile(hlsl, sizeof(hlsl), 0, 0, 0, "vs", "vs_5_0", flags, 0, &vblob, &error);
		if (FAILED(hr))
		{
			const char* message = error->lpVtbl->GetBufferPointer(error);
			OutputDebugStringA(message);
		}

		ID3DBlob* pblob;
		hr = D3DCompile(hlsl, sizeof(hlsl), 0, 0, 0, "ps", "ps_5_0", flags, 0, &pblob, &error);
		if (FAILED(hr))
		{
			const char* message = error->lpVtbl->GetBufferPointer(error);
			OutputDebugStringA(message);
		}

		device->lpVtbl->CreateVertexShader(device,vblob->lpVtbl->GetBufferPointer(vblob),vblob->lpVtbl->GetBufferSize(vblob),0,&vshader);
		device->lpVtbl->CreatePixelShader(device,pblob->lpVtbl->GetBufferPointer(pblob),pblob->lpVtbl->GetBufferSize(pblob),0,&pshader);
		device->lpVtbl->CreateInputLayout(device,desc,ARRAYSIZE(desc),vblob->lpVtbl->GetBufferPointer(vblob),vblob->lpVtbl->GetBufferSize(vblob),&layout);

		pblob->lpVtbl->Release(pblob);
		vblob->lpVtbl->Release(vblob);
	}

	ID3D11Buffer* ubuffer;
	{
		D3D11_BUFFER_DESC desc =
		{
			// space for 4x4 float matrix (cbuffer0 from pixel shader)
			.ByteWidth = 4 * 4 * sizeof(float),
			.Usage = D3D11_USAGE_DYNAMIC,
			.BindFlags = D3D11_BIND_CONSTANT_BUFFER,
			.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
		};
		device->lpVtbl->CreateBuffer(device,&desc,0,&ubuffer);
	}

	ID3D11ShaderResourceView* textureView;
	ID3D11Texture2D* texture;
	{
		D3D11_TEXTURE2D_DESC desc =
		{
			.Width = GAME_WIDTH,
			.Height = GAME_HEIGHT,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_B8G8R8A8_UNORM,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DYNAMIC,
			.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE,
		};

		device->lpVtbl->CreateTexture2D(device,&desc,0,&texture);
		device->lpVtbl->CreateShaderResourceView(device,texture,0,&textureView);
		texture->lpVtbl->Release(texture);

		D3D11_MAPPED_SUBRESOURCE mapped;
		context->lpVtbl->Map(context,texture,0,D3D11_MAP_WRITE_DISCARD,0,&mapped);
		Image framebuffer = {
			.width = GAME_WIDTH,
			.height = GAME_HEIGHT,
			.rowPitch = mapped.RowPitch,
			.pixels = mapped.pData
		};
		Blit(&background,0,0,GAME_WIDTH,GAME_HEIGHT,&framebuffer,0,0,0);
		context->lpVtbl->Unmap(context,texture,0);
	}

	ID3D11SamplerState* sampler;
	{
		D3D11_SAMPLER_DESC desc =
		{
			.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
			.AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
			.AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
			.AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
		};

		device->lpVtbl->CreateSamplerState(device,&desc,&sampler);
	}

	ID3D11BlendState* blendState;
	{
		// enable alpha blending
		D3D11_BLEND_DESC desc =
		{
			.RenderTarget[0] =
			{
				.BlendEnable = 1,
				.SrcBlend = D3D11_BLEND_SRC_ALPHA,
				.DestBlend = D3D11_BLEND_INV_SRC_ALPHA,
				.BlendOp = D3D11_BLEND_OP_ADD,
				.SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA,
				.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA,
				.BlendOpAlpha = D3D11_BLEND_OP_ADD,
				.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL,
		},
		};
		device->lpVtbl->CreateBlendState(device,&desc,&blendState);
	}

	ID3D11RasterizerState* rasterizerState;
	{
		// disable culling
		D3D11_RASTERIZER_DESC desc =
		{
			.FillMode = D3D11_FILL_SOLID,
			.CullMode = D3D11_CULL_NONE,
		};
		device->lpVtbl->CreateRasterizerState(device,&desc,&rasterizerState);
	}

	ID3D11DepthStencilState* depthState;
	{
		// disable depth & stencil test
		D3D11_DEPTH_STENCIL_DESC desc =
		{
			.DepthEnable = FALSE,
			.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL,
			.DepthFunc = D3D11_COMPARISON_LESS,
			.StencilEnable = FALSE,
			.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK,
			.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK,
			// .FrontFace = ... 
			// .BackFace = ...
		};
		device->lpVtbl->CreateDepthStencilState(device,&desc,&depthState);
	}

	ID3D11RenderTargetView* rtView = 0;
	ID3D11DepthStencilView* dsView = 0;

	ShowWindow(window,SW_SHOWDEFAULT);

	LARGE_INTEGER freq, c1;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&c1);

	float angle = 0;
	DWORD currentWidth = 0;
	DWORD currentHeight = 0;

	i32 framecount = 0;
	SpawnApple();

	while(1){
		MSG msg;
		while (PeekMessageW(&msg,0,0,0,PM_REMOVE)){
			if (msg.message == WM_QUIT) end(0);
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}

		RECT rect;
		GetClientRect(window, &rect);
		width = rect.right - rect.left;
		height = rect.bottom - rect.top;

		if (rtView == 0 || width != currentWidth || height != currentHeight)
		{
			if (rtView)
			{
				context->lpVtbl->ClearState(context);
				rtView->lpVtbl->Release(rtView);
				dsView->lpVtbl->Release(dsView);
				rtView = 0;
			}

			// resize to new size for non-zero size
			if (width != 0 && height != 0)
			{
				hr = swapChain->lpVtbl->ResizeBuffers(swapChain,0,width,height,DXGI_FORMAT_UNKNOWN,0);

				// create RenderTarget view for new backbuffer texture
				ID3D11Texture2D* backbuffer;
				swapChain->lpVtbl->GetBuffer(swapChain,0,&IID_ID3D11Texture2D,&backbuffer);
				device->lpVtbl->CreateRenderTargetView(device,backbuffer,0,&rtView);
				backbuffer->lpVtbl->Release(backbuffer);

				D3D11_TEXTURE2D_DESC depthDesc =
				{
					.Width = width,
					.Height = height,
					.MipLevels = 1,
					.ArraySize = 1,
					.Format = DXGI_FORMAT_D32_FLOAT, // or use DXGI_FORMAT_D32_FLOAT_S8X24_UINT if you need stencil
					.SampleDesc = { 1, 0 },
					.Usage = D3D11_USAGE_DEFAULT,
					.BindFlags = D3D11_BIND_DEPTH_STENCIL,
				};

				// create new depth stencil texture & DepthStencil view
				ID3D11Texture2D* depth;
				device->lpVtbl->CreateTexture2D(device,&depthDesc,0,&depth);
				device->lpVtbl->CreateDepthStencilView(device,depth,0,&dsView);
				depth->lpVtbl->Release(depth);
			}

			currentWidth = width;
			currentHeight = height;
		}

		// can render only if window size is non-zero - we must have backbuffer & RenderTarget view created
		if (rtView)
		{
			LARGE_INTEGER c2;
			QueryPerformanceCounter(&c2);
			float delta = (float)((double)(c2.QuadPart - c1.QuadPart) / freq.QuadPart);
			c1 = c2;

			// output viewport covering all client area of window
			D3D11_VIEWPORT viewport =
			{
				.TopLeftX = 0,
				.TopLeftY = 0,
				.Width = (FLOAT)width,
				.Height = (FLOAT)height,
				.MinDepth = 0,
				.MaxDepth = 1,
			};

			// clear screen
			FLOAT color[] = { 0.392f, 0.584f, 0.929f, 1.f };
			context->lpVtbl->ClearRenderTargetView(context,rtView,color);
			context->lpVtbl->ClearDepthStencilView(context,dsView,D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1.f,0);

			// setup 4x4c rotation matrix in uniform
			{
				angle += delta * 2.0f * (float)M_PI / 20.0f; // full rotation in 20 seconds
				angle = fmodf(angle, 2.0f * (float)M_PI);

				float aspect = (float)height / width;
				float matrix[16] =
				{
					1, 0, 0, 0,
					0, 1, 0, 0,
					0, 0, 1, 0,
					0, 0, 0, 1,
				};

				D3D11_MAPPED_SUBRESOURCE mapped;
				context->lpVtbl->Map(context,ubuffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped);
				memcpy(mapped.pData,matrix,sizeof(matrix));
				context->lpVtbl->Unmap(context,ubuffer,0);
				
				GetKeyboardState(keys);
				//i got an idea. each WM_KEYDOWN message adds a message onto a queue, and you just read one valid message off that queue each game tick, skipping any invalid moves.s
				if (framecount < 0 && (keys[VK_SPACE] & KEYMASK_DOWN)){
					hx = 0;
					hy = 0;
					tx = 0;
					ty = 0;
					hdir = RIGHT;
					memset(board,0,sizeof(board));
					SpawnApple();
					framecount = 0;
					kbused = 0;
				}

				if (framecount >= 0){
					framecount++;
					if (framecount == 5){
						framecount = 0;
						if (kbused){
							i32 i = 0;
							for (; i < kbused; i++){
								u8 m = kbmessages[i];
								if (m=='A' && hdir!=LEFT && hdir!=RIGHT){hdir = LEFT; break;}
								if (m=='D' && hdir!=LEFT && hdir!=RIGHT){hdir = RIGHT; break;}
								if (m=='S' && hdir!=DOWN && hdir!=UP){hdir = DOWN; break;}
								if (m=='W' && hdir!=DOWN && hdir!=UP){hdir = UP; break;}
							}
							if (i == kbused){
								kbused = 0;
							} else {
								kbused -= i+1;
								memmove(kbmessages,kbmessages+i+1,kbused);
							}
						}
						i32 oldhx = hx, oldhy = hy;
						switch(hdir){
							case LEFT: hx--; break;
							case RIGHT: hx++; break;
							case DOWN: hy--; break;
							case UP: hy++; break;
						}
						i32 v = board[hy*BOARD_WIDTH+hx].val;
						if (hx < 0 || hx >= BOARD_WIDTH || hy < 0 || hy >= BOARD_WIDTH || v > 1){
							framecount = -1;
							hx = oldhx;
							hy = oldhy;
						} else {
							if ((lastdir==DOWN && hdir==LEFT) || (lastdir==RIGHT && hdir==UP)) board[oldhy*BOARD_WIDTH+oldhx].val = 4;
							else if ((lastdir==DOWN && hdir==RIGHT) || (lastdir==LEFT && hdir==UP)) board[oldhy*BOARD_WIDTH+oldhx].val = 5;
							else if ((lastdir==UP && hdir==RIGHT) || (lastdir==LEFT && hdir==DOWN)) board[oldhy*BOARD_WIDTH+oldhx].val = 6;
							else if ((lastdir==UP && hdir==LEFT) || (lastdir==RIGHT && hdir==DOWN)) board[oldhy*BOARD_WIDTH+oldhx].val = 7;
							else board[oldhy*BOARD_WIDTH+oldhx].val = hdir < DOWN ? 2 : 3;
							board[oldhy*BOARD_WIDTH+oldhx].dir = hdir;
							lastdir = hdir;

							if (v != 1){
								i32 tdir = board[ty*BOARD_WIDTH+tx].dir;
								board[ty*BOARD_WIDTH+tx].val = 0;
								switch(tdir){
									case LEFT: tx--; break;
									case RIGHT: tx++; break;
									case DOWN: ty--; break;
									case UP: ty++; break;
								}
							} else SpawnApple();
						}
						context->lpVtbl->Map(context,texture,0,D3D11_MAP_WRITE_DISCARD,0,&mapped);
						Image framebuffer = {
							.width = GAME_WIDTH,
							.height = GAME_HEIGHT,
							.rowPitch = mapped.RowPitch,
							.pixels = mapped.pData
						};
						if (framecount >= 0){
							Blit(&background,0,0,GAME_WIDTH,GAME_HEIGHT,&framebuffer,0,0,0);
							for (i32 y = 0; y < BOARD_WIDTH; y++){
								for (i32 x = 0; x < BOARD_WIDTH; x++){
									if (x==tx && y==ty && !(hx==tx && hy==ty)){
										u8 d = board[y*BOARD_WIDTH+x].dir;
										Blit(&tiles,(7+d)*16,16,16,16,&framebuffer,x*16,y*16,0);
									} else {
										u8 v = board[y*BOARD_WIDTH+x].val;
										if (v) Blit(&tiles,(v-1)*16,0,16,16,&framebuffer,x*16,y*16,0);
									}
								}
							}
							Blit(&tiles,(7+hdir)*16,0,16,16,&framebuffer,hx*16,hy*16,0);
						} else {
							DrawString("You dead. mf. Press space to restart.",&framebuffer,25,20,0xffff0000);
						}
						context->lpVtbl->Unmap(context,texture,0);
					}
				}
			}

			context->lpVtbl->IASetInputLayout(context,layout);
			context->lpVtbl->IASetPrimitiveTopology(context,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			UINT stride = sizeof(struct Vertex);
			UINT offset = 0;
			context->lpVtbl->IASetVertexBuffers(context,0,1,&vbuffer,&stride,&offset);

			context->lpVtbl->VSSetConstantBuffers(context,0,1,&ubuffer);
			context->lpVtbl->VSSetShader(context,vshader,0,0);

			context->lpVtbl->RSSetViewports(context,1,&viewport);
			context->lpVtbl->RSSetState(context,rasterizerState);

			context->lpVtbl->PSSetSamplers(context,0,1,&sampler);
			context->lpVtbl->PSSetShaderResources(context,0,1,&textureView);
			context->lpVtbl->PSSetShader(context,pshader,0,0);

			context->lpVtbl->OMSetBlendState(context,blendState,0,~0U);
			context->lpVtbl->OMSetDepthStencilState(context,depthState,0);
			context->lpVtbl->OMSetRenderTargets(context,1,&rtView,dsView);

			context->lpVtbl->Draw(context,6,0);
		}

		if (swapChain->lpVtbl->Present(swapChain,1,0) == DXGI_STATUS_OCCLUDED) Sleep(10);// window is minimized, cannot vsync - instead sleep a bit
	}
}