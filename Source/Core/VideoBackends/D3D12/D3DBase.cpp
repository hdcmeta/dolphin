// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Common/StringUtil.h"
#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DCommandListManager.h"
#include "VideoBackends/D3D12/D3DDescriptorHeapManager.h"
#include "VideoBackends/D3D12/D3DState.h"
#include "VideoBackends/D3D12/D3DTexture.h"
#include "VideoBackends/D3D12/VertexShaderCache.h"
#include "VideoCommon/VideoConfig.h"

#define SWAP_CHAIN_BUFFER_COUNT 4

namespace DX12
{

	HINSTANCE hD3DCompilerDll = nullptr;
	D3DREFLECT PD3DReflect = nullptr;
	pD3DCompile PD3DCompile = nullptr;
	int d3dcompiler_dll_ref = 0;

	CREATEDXGIFACTORY PCreateDXGIFactory = nullptr;
	HINSTANCE hDXGIDll = nullptr;
	int dxgi_dll_ref = 0;

	D3D12CREATEDEVICE PD3D12CreateDevice = nullptr;
	D3D12SERIALIZEROOTSIGNATURE PD3D12SerializeRootSignature = nullptr;
	D3D12GETDEBUGINTERFACE PD3D12GetDebugInterface = nullptr;

	HINSTANCE hD3DDll12 = nullptr;
	int d3d12_dll_ref = 0;

	namespace D3D
	{
		ID3D12Device* device12 = nullptr;

		ID3D12CommandQueue* commandQueue = nullptr;
		D3DCommandListManager* commandListMgr = nullptr;
		ID3D12GraphicsCommandList* currentCommandList = nullptr;
		ID3D12RootSignature* defaultRootSignature = nullptr;

		static IDXGISwapChain* swapchain = nullptr;
		static ID3D12DebugDevice* debug12 = nullptr;

		D3D12_CPU_DESCRIPTOR_HANDLE nullSRVCPU = {};
		D3D12_CPU_DESCRIPTOR_HANDLE nullSRVCPUshadow = {};

		unsigned int resourceDescriptorSize = 0;
		unsigned int samplerDescriptorSize = 0;
		D3DDescriptorHeapManager* gpuDescriptorHeapMgr = nullptr;
		D3DDescriptorHeapManager* samplerDescriptorHeapMgr = nullptr;
		D3DDescriptorHeapManager* dsvDescriptorHeapMgr = nullptr;
		D3DDescriptorHeapManager* rtvDescriptorHeapMgr = nullptr;
		ID3D12DescriptorHeap* gpuVisibleDescriptorHeaps[2];

		D3D_FEATURE_LEVEL featlevel;
		D3DTexture2D* backbuf[SWAP_CHAIN_BUFFER_COUNT];
		UINT currentBackbuf = 0;

		HWND hWnd;

		std::vector<DXGI_SAMPLE_DESC> aa_modes; // supported AA modes of the current adapter

		bool bgra_textures_supported;

#define NUM_SUPPORTED_FEATURE_LEVELS 1
		const D3D_FEATURE_LEVEL supported_feature_levels[NUM_SUPPORTED_FEATURE_LEVELS] = {
			D3D_FEATURE_LEVEL_11_0
		};

		unsigned int xres, yres;

		bool bFrameInProgress = false;

		HRESULT LoadDXGI()
		{
			if (dxgi_dll_ref++ > 0) return S_OK;

			if (hDXGIDll) return S_OK;
			hDXGIDll = LoadLibraryA("dxgi.dll");
			if (!hDXGIDll)
			{
				MessageBoxA(nullptr, "Failed to load dxgi.dll", "Critical error", MB_OK | MB_ICONERROR);
				--dxgi_dll_ref;
				return E_FAIL;
			}
			PCreateDXGIFactory = (CREATEDXGIFACTORY)GetProcAddress(hDXGIDll, "CreateDXGIFactory");
			if (PCreateDXGIFactory == nullptr) MessageBoxA(nullptr, "GetProcAddress failed for CreateDXGIFactory!", "Critical error", MB_OK | MB_ICONERROR);

			return S_OK;
		}

		HRESULT LoadD3D()
		{
			if (d3d12_dll_ref++ > 0) return S_OK;

			hD3DDll12 = LoadLibraryA("d3d12.dll");
			if (!hD3DDll12)
			{
				MessageBoxA(nullptr, "Failed to load d3d12.dll", "Critical error", MB_OK | MB_ICONERROR);
				--d3d12_dll_ref;
				return E_FAIL;
			}

			PD3D12CreateDevice = (D3D12CREATEDEVICE)GetProcAddress(hD3DDll12, "D3D12CreateDevice");
			if (PD3D12CreateDevice == nullptr)
			{
				MessageBoxA(nullptr, "GetProcAddress failed for D3D12CreateDevice!", "Critical error", MB_OK | MB_ICONERROR);
				return E_FAIL;
			}

			PD3D12SerializeRootSignature = (D3D12SERIALIZEROOTSIGNATURE)GetProcAddress(hD3DDll12, "D3D12SerializeRootSignature");
			if (PD3D12SerializeRootSignature == nullptr)
			{
				MessageBoxA(nullptr, "GetProcAddress failed for D3D12SerializeRootSignature!", "Critical error", MB_OK | MB_ICONERROR);
				return E_FAIL;
			}

			PD3D12GetDebugInterface = (D3D12GETDEBUGINTERFACE)GetProcAddress(hD3DDll12, "D3D12GetDebugInterface");
			if (PD3D12SerializeRootSignature == nullptr)
			{
				MessageBoxA(nullptr, "GetProcAddress failed for D3D12GetDebugInterface!", "Critical error", MB_OK | MB_ICONERROR);
				return E_FAIL;
			}

			return S_OK;
		}

		HRESULT LoadD3DCompiler()
		{
			if (d3dcompiler_dll_ref++ > 0) return S_OK;
			if (hD3DCompilerDll) return S_OK;

			// try to load D3DCompiler first to check whether we have proper runtime support
			// try to use the dll the backend was compiled against first - don't bother about debug runtimes
			hD3DCompilerDll = LoadLibraryA(D3DCOMPILER_DLL_A);
			if (!hD3DCompilerDll)
			{
				// if that fails, use the dll which should be available in every SDK which officially supports DX12.
				hD3DCompilerDll = LoadLibraryA("D3DCompiler_42.dll");
				if (!hD3DCompilerDll)
				{
					MessageBoxA(nullptr, "Failed to load D3DCompiler_42.dll, update your DX12 runtime, please", "Critical error", MB_OK | MB_ICONERROR);
					return E_FAIL;
				}
				else
				{
					NOTICE_LOG(VIDEO, "Successfully loaded D3DCompiler_42.dll. If you're having trouble, try updating your DX runtime first.");
				}
			}

			PD3DReflect = (D3DREFLECT)GetProcAddress(hD3DCompilerDll, "D3DReflect");
			if (PD3DReflect == nullptr) MessageBoxA(nullptr, "GetProcAddress failed for D3DReflect!", "Critical error", MB_OK | MB_ICONERROR);
			PD3DCompile = (pD3DCompile)GetProcAddress(hD3DCompilerDll, "D3DCompile");
			if (PD3DCompile == nullptr) MessageBoxA(nullptr, "GetProcAddress failed for D3DCompile!", "Critical error", MB_OK | MB_ICONERROR);

			return S_OK;
		}

		void UnloadDXGI()
		{
			if (!dxgi_dll_ref) return;
			if (--dxgi_dll_ref != 0) return;

			if (hDXGIDll) FreeLibrary(hDXGIDll);
			hDXGIDll = nullptr;
			PCreateDXGIFactory = nullptr;
		}

		void UnloadD3D()
		{
			if (!d3d12_dll_ref) return;
			if (--d3d12_dll_ref != 0) return;

			if (hD3DDll12) FreeLibrary(hD3DDll12);
			hD3DDll12 = nullptr;
			PD3D12CreateDevice = nullptr;
			PD3D12SerializeRootSignature = nullptr;
		}

		void UnloadD3DCompiler()
		{
			if (!d3dcompiler_dll_ref) return;
			if (--d3dcompiler_dll_ref != 0) return;

			if (hD3DCompilerDll) FreeLibrary(hD3DCompilerDll);
			hD3DCompilerDll = nullptr;
			PD3DReflect = nullptr;
		}

		std::vector<DXGI_SAMPLE_DESC> EnumAAModes(IDXGIAdapter* adapter)
		{
			std::vector<DXGI_SAMPLE_DESC> _aa_modes;

			ID3D12Device *_device12;
			PD3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&_device12));

			for (int samples = 0; samples < D3D12_MAX_MULTISAMPLE_SAMPLE_COUNT; ++samples)
			{
				D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS multisample_quality_levels = {};
				multisample_quality_levels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				multisample_quality_levels.SampleCount = samples;
				
				_device12->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &multisample_quality_levels, sizeof(multisample_quality_levels));

				DXGI_SAMPLE_DESC desc;
				desc.Count = samples;
				desc.Quality = 0;

				if (multisample_quality_levels.NumQualityLevels > 0)
				{
					_aa_modes.push_back(desc);
				}
			}

			_device12->Release();

			return _aa_modes;
		}

		D3D_FEATURE_LEVEL GetFeatureLevel(IDXGIAdapter* adapter)
		{
			return D3D_FEATURE_LEVEL_11_0;
		}

		DXGI_SAMPLE_DESC GetAAMode(int index)
		{
			return aa_modes[index];
		}

		HRESULT Create(HWND wnd)
		{
			hWnd = wnd;
			HRESULT hr;

			RECT client;
			GetClientRect(hWnd, &client);
			xres = client.right - client.left;
			yres = client.bottom - client.top;

			hr = LoadDXGI();
			if (SUCCEEDED(hr)) hr = LoadD3D();
			if (SUCCEEDED(hr)) hr = LoadD3DCompiler();
			if (FAILED(hr))
			{
				UnloadDXGI();
				UnloadD3D();
				UnloadD3DCompiler();
				return hr;
			}

			IDXGIFactory* factory;
			IDXGIAdapter* adapter;
			IDXGIOutput* output;
			hr = PCreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&factory);
			if (FAILED(hr)) MessageBox(wnd, _T("Failed to create IDXGIFactory object"), _T("Dolphin Direct3D 11 backend"), MB_OK | MB_ICONERROR);

			hr = factory->EnumAdapters(g_ActiveConfig.iAdapter, &adapter);
			if (FAILED(hr))
			{
				// try using the first one
				hr = factory->EnumAdapters(0, &adapter);
				if (FAILED(hr)) MessageBox(wnd, _T("Failed to enumerate adapters"), _T("Dolphin Direct3D 11 backend"), MB_OK | MB_ICONERROR);
			}

			// TODO: Make this configurable
			hr = adapter->EnumOutputs(0, &output);
			if (FAILED(hr))
			{
				// try using the first one
				IDXGIAdapter* firstadapter;
				hr = factory->EnumAdapters(0, &firstadapter);
				if (!FAILED(hr))
					hr = firstadapter->EnumOutputs(0, &output);
				if (FAILED(hr)) MessageBox(wnd,
					_T("Failed to enumerate outputs!\n")
					_T("This usually happens when you've set your video adapter to the Nvidia GPU in an Optimus-equipped system.\n")
					_T("Set Dolphin to use the high-performance graphics in Nvidia's drivers instead and leave Dolphin's video adapter set to the Intel GPU."),
					_T("Dolphin Direct3D 11 backend"), MB_OK | MB_ICONERROR);
				SAFE_RELEASE(firstadapter);
			}

			// get supported AA modes
			aa_modes = EnumAAModes(adapter);
			if (g_Config.iMultisampleMode >= (int)aa_modes.size())
			{
				g_Config.iMultisampleMode = 0;
				UpdateActiveConfig();
			}

			DXGI_SWAP_CHAIN_DESC swap_chain_desc = {};
			swap_chain_desc.BufferCount = SWAP_CHAIN_BUFFER_COUNT;
			swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swap_chain_desc.OutputWindow = wnd;
			swap_chain_desc.SampleDesc.Count = 1;
			swap_chain_desc.SampleDesc.Quality = 0;
			swap_chain_desc.Windowed = !g_Config.bFullscreen;
			swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

			DXGI_OUTPUT_DESC out_desc = {};
			output->GetDesc(&out_desc);

			DXGI_MODE_DESC mode_desc = {};
			mode_desc.Width = out_desc.DesktopCoordinates.right - out_desc.DesktopCoordinates.left;
			mode_desc.Height = out_desc.DesktopCoordinates.bottom - out_desc.DesktopCoordinates.top;
			mode_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			mode_desc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
			hr = output->FindClosestMatchingMode(&mode_desc, &swap_chain_desc.BufferDesc, nullptr);
			if (FAILED(hr)) MessageBox(wnd, _T("Failed to find a supported video mode"), _T("Dolphin Direct3D 11 backend"), MB_OK | MB_ICONERROR);

			if (swap_chain_desc.Windowed)
			{
				// forcing buffer resolution to xres and yres..
				// this is not a problem as long as we're in windowed mode
				swap_chain_desc.BufferDesc.Width = xres;
				swap_chain_desc.BufferDesc.Height = yres;
			}

#if defined(_DEBUG) || defined(DEBUGFAST)
			// Creating debug devices can sometimes fail if the user doesn't have the correct
			// version of the DirectX SDK. If it does, simply fallback to a non-debug device.
	{

		if (SUCCEEDED(hr))
		{
			ID3D12Debug* debugController;
			hr = PD3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
			if (SUCCEEDED(hr))
			{
				debugController->EnableDebugLayer();
				debugController->Release();
			}
			else
			{
				MessageBox(wnd, _T("Failed to initialize Direct3D debug layer."), _T("Dolphin Direct3D 12 backend"), MB_OK | MB_ICONERROR);
			}

			hr = PD3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device12));

			featlevel = D3D_FEATURE_LEVEL_11_0;
		}
	}

	if (FAILED(hr))
#endif
	{
		if (SUCCEEDED(hr))
		{
			hr = PD3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device12));

			featlevel = D3D_FEATURE_LEVEL_11_0;
		}
	}

	if (SUCCEEDED(hr))
	{
		D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {
			D3D12_COMMAND_LIST_TYPE_DIRECT,	// D3D12_COMMAND_LIST_TYPE Type;
			0,								// INT Priority;
			D3D12_COMMAND_QUEUE_FLAG_NONE,	// D3D12_COMMAND_QUEUE_FLAG Flags;
			0								// UINT NodeMask;
		};

		CheckHR(device12->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue)));

		IDXGIFactory *factory = nullptr;
		adapter->GetParent(IID_PPV_ARGS(&factory));

		CheckHR(factory->CreateSwapChain(commandQueue, &swap_chain_desc, &swapchain));

		currentBackbuf = 0;

		factory->Release();
	}

	if (FAILED(hr))
	{
		MessageBox(wnd, _T("Failed to initialize Direct3D.\nMake sure your video card supports Direct3D 12."), _T("Dolphin Direct3D 12 backend"), MB_OK | MB_ICONERROR);
		SAFE_RELEASE(swapchain);
		return E_FAIL;
	}
	
	ID3D12InfoQueue* pInfoQueue = nullptr;
	if (SUCCEEDED(device12->QueryInterface(&pInfoQueue)))
	{
		CheckHR(pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE));
		CheckHR(pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE));

		D3D12_INFO_QUEUE_FILTER filter = {};
		D3D12_MESSAGE_ID idList[] = {
			D3D12_MESSAGE_ID_CREATEGRAPHICSPIPELINESTATE_DEPTHSTENCILVIEW_NOT_SET, // Benign.
			D3D12_MESSAGE_ID_CREATEGRAPHICSPIPELINESTATE_RENDERTARGETVIEW_NOT_SET, // Benign.
			D3D12_MESSAGE_ID_CREATEINPUTLAYOUT_TYPE_MISMATCH, // Benign.
			D3D12_MESSAGE_ID_DRAW_EMPTY_SCISSOR_RECTANGLE, // Benign. Probably.
			D3D12_MESSAGE_ID_INVALID_SUBRESOURCE_STATE,
			D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE, // Benign.
			D3D12_MESSAGE_ID_EXECUTECOMMANDLISTS_GPU_WRITTEN_READBACK_RESOURCE_MAPPED, // Benign.
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_BEFORE_AFTER_MISMATCH // Benign. Probably.
		};
		filter.DenyList.NumIDs = ARRAYSIZE(idList);
		filter.DenyList.pIDList = idList;
		pInfoQueue->PushStorageFilter(&filter);

		pInfoQueue->Release();

		// Used at Close time to report live objects.
		CheckHR(device12->QueryInterface(&debug12));
	}


	// prevent DXGI from responding to Alt+Enter, unfortunately DXGI_MWA_NO_ALT_ENTER
	// does not work so we disable all monitoring of window messages. However this
	// may make it more difficult for DXGI to handle display mode changes.
	hr = factory->MakeWindowAssociation(wnd, DXGI_MWA_NO_WINDOW_CHANGES);
	if (FAILED(hr)) MessageBox(wnd, _T("Failed to associate the window"), _T("Dolphin Direct3D 11 backend"), MB_OK | MB_ICONERROR);

	SAFE_RELEASE(factory);
	SAFE_RELEASE(output);
	SAFE_RELEASE(adapter)

	CreateDescriptorHeaps();
	CreateRootSignatures();

	commandListMgr = new D3DCommandListManager(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		device12,
		commandQueue,
		gpuVisibleDescriptorHeaps,
		ARRAYSIZE(gpuVisibleDescriptorHeaps),
		defaultRootSignature
		);

	commandListMgr->GetCommandList(&currentCommandList);
	commandListMgr->SetInitialCommandListState();

	for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
	{
		ID3D12Resource* buf12;
		hr = swapchain->GetBuffer(i, IID_PPV_ARGS(&buf12));

		backbuf[i] = new D3DTexture2D(buf12,
			D3D11_BIND_RENDER_TARGET,
			DXGI_FORMAT_UNKNOWN,
			DXGI_FORMAT_UNKNOWN,
			DXGI_FORMAT_UNKNOWN,
			false,
			D3D12_RESOURCE_STATE_PRESENT // Swap Chain back buffers start out in D3D12_RESOURCE_STATE_PRESENT.
			);

		CHECK(backbuf != nullptr, "Create back buffer texture");

		SAFE_RELEASE(buf12);
		SetDebugObjectName12(backbuf[i]->GetTex12(), "backbuffer texture");
	}	

	backbuf[currentBackbuf]->TransitionToResourceState(currentCommandList, D3D12_RESOURCE_STATE_RENDER_TARGET);
	currentCommandList->OMSetRenderTargets(1, &backbuf[currentBackbuf]->GetRTV12(), FALSE, nullptr);

	// BGRA textures are easier to deal with in TextureCache, but might not be supported by the hardware. But are always supported on D3D12.
	bgra_textures_supported = true;

	return S_OK;
}

void CreateDescriptorHeaps()
{
	// Create D3D12 GPU and CPU descriptor heaps.

	{
		D3D12_DESCRIPTOR_HEAP_DESC gpuDescriptorHeapDesc = {};
		gpuDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		gpuDescriptorHeapDesc.NumDescriptors = 500000;
		gpuDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

		gpuDescriptorHeapMgr = new D3DDescriptorHeapManager(&gpuDescriptorHeapDesc, device12, 50000);

		gpuVisibleDescriptorHeaps[0] = gpuDescriptorHeapMgr->GetDescriptorHeap();

		D3D12_CPU_DESCRIPTOR_HANDLE descriptorHeapCPUBase = gpuDescriptorHeapMgr->GetDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();

		resourceDescriptorSize = device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		samplerDescriptorSize = device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

		D3D12_GPU_DESCRIPTOR_HANDLE nullSRVGPU = {};
		gpuDescriptorHeapMgr->Allocate(&nullSRVCPU, &nullSRVGPU, &nullSRVCPUshadow);

		D3D12_SHADER_RESOURCE_VIEW_DESC nullSRVDesc = {};
		nullSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		nullSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		nullSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		device12->CreateShaderResourceView(NULL, &nullSRVDesc, nullSRVCPU);

		for (UINT i = 0; i < 500000; i++)
		{
			// D3D12TODO: Make paving of descriptor heap optional.

			D3D12_CPU_DESCRIPTOR_HANDLE destinationDescriptor = {};
			destinationDescriptor.ptr = descriptorHeapCPUBase.ptr + i * resourceDescriptorSize;

			device12->CreateShaderResourceView(NULL, &nullSRVDesc, destinationDescriptor);
		}
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC samplerDescriptorHeapDesc = {};
		samplerDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		samplerDescriptorHeapDesc.NumDescriptors = 2000;
		samplerDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;

		samplerDescriptorHeapMgr = new D3DDescriptorHeapManager(&samplerDescriptorHeapDesc, device12);

		gpuVisibleDescriptorHeaps[1] = samplerDescriptorHeapMgr->GetDescriptorHeap();
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC dsvDescriptorHeapDesc = {};
		dsvDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		dsvDescriptorHeapDesc.NumDescriptors = 2000;
		dsvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

		dsvDescriptorHeapMgr = new D3DDescriptorHeapManager(&dsvDescriptorHeapDesc, device12);
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc = {};
		rtvDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		rtvDescriptorHeapDesc.NumDescriptors = 2000;
		rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

		rtvDescriptorHeapMgr = new D3DDescriptorHeapManager(&rtvDescriptorHeapDesc, device12);
	}
}

void CreateRootSignatures()
{
	D3D12_DESCRIPTOR_RANGE descRangeSRV = {
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,          // D3D12_DESCRIPTOR_RANGE_TYPE RangeType;
		8,                                   // UINT NumDescriptors;
		0,                                   // UINT BaseShaderRegister;
		0,                                   // UINT RegisterSpace;
		D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND // UINT OffsetInDescriptorsFromTableStart;
	};

	D3D12_DESCRIPTOR_RANGE descRangeSampler = {
		D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,      // D3D12_DESCRIPTOR_RANGE_TYPE RangeType;
		8,                                   // UINT NumDescriptors;
		0,                                   // UINT BaseShaderRegister;
		0,                                   // UINT RegisterSpace;
		D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND // UINT OffsetInDescriptorsFromTableStart;
	};

	D3D12_ROOT_PARAMETER rootParameters[6];

	rootParameters[DESCRIPTOR_TABLE_PS_SRV].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[DESCRIPTOR_TABLE_PS_SRV].DescriptorTable.NumDescriptorRanges = 1;
	rootParameters[DESCRIPTOR_TABLE_PS_SRV].DescriptorTable.pDescriptorRanges = &descRangeSRV;
	rootParameters[DESCRIPTOR_TABLE_PS_SRV].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	
	rootParameters[DESCRIPTOR_TABLE_PS_SAMPLER].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[DESCRIPTOR_TABLE_PS_SAMPLER].DescriptorTable.NumDescriptorRanges = 1;
	rootParameters[DESCRIPTOR_TABLE_PS_SAMPLER].DescriptorTable.pDescriptorRanges = &descRangeSampler;
	rootParameters[DESCRIPTOR_TABLE_PS_SAMPLER].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	
	rootParameters[DESCRIPTOR_TABLE_GS_CBV].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[DESCRIPTOR_TABLE_GS_CBV].Descriptor.RegisterSpace = 0;
	rootParameters[DESCRIPTOR_TABLE_GS_CBV].Descriptor.ShaderRegister = 0;
	rootParameters[DESCRIPTOR_TABLE_GS_CBV].ShaderVisibility = D3D12_SHADER_VISIBILITY_GEOMETRY;

	rootParameters[DESCRIPTOR_TABLE_VS_CBV].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[DESCRIPTOR_TABLE_VS_CBV].Descriptor.RegisterSpace = 0;
	rootParameters[DESCRIPTOR_TABLE_VS_CBV].Descriptor.ShaderRegister = 0;
	rootParameters[DESCRIPTOR_TABLE_VS_CBV].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	rootParameters[DESCRIPTOR_TABLE_PS_CBVONE].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[DESCRIPTOR_TABLE_PS_CBVONE].Descriptor.RegisterSpace = 0;
	rootParameters[DESCRIPTOR_TABLE_PS_CBVONE].Descriptor.ShaderRegister = 0;
	rootParameters[DESCRIPTOR_TABLE_PS_CBVONE].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	if (g_ActiveConfig.bEnablePixelLighting)
	{
		rootParameters[DESCRIPTOR_TABLE_PS_CBVTWO].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[DESCRIPTOR_TABLE_PS_CBVTWO].Descriptor.RegisterSpace = 1;
		rootParameters[DESCRIPTOR_TABLE_PS_CBVTWO].Descriptor.ShaderRegister = 0;
		rootParameters[DESCRIPTOR_TABLE_PS_CBVTWO].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	}

	// D3D12TODO: Add bounding box UAV to root signature.


	D3D12_ROOT_SIGNATURE_DESC textRootSignatureDesc = {};
	textRootSignatureDesc.pParameters = rootParameters;
	textRootSignatureDesc.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

	textRootSignatureDesc.NumParameters = ARRAYSIZE(rootParameters);

	if (!g_ActiveConfig.bEnablePixelLighting)
		textRootSignatureDesc.NumParameters--;

	ID3DBlob* textRootSignatureBlob;
	ID3DBlob* textRootSignatureErrorBlob;

	CheckHR(PD3D12SerializeRootSignature(&textRootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &textRootSignatureBlob, &textRootSignatureErrorBlob));

	CheckHR(D3D::device12->CreateRootSignature(0, textRootSignatureBlob->GetBufferPointer(), textRootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&defaultRootSignature)));
}

void WaitForOutstandingRenderingToComplete()
{
	commandListMgr->ClearQueueAndWaitForCompletionOfInflightWork();
}

void Close()
{
	// we can't release the swapchain while in fullscreen.
	swapchain->SetFullscreenState(false, nullptr);

	// Release all back buffer references
	for (UINT i = 0; i < ARRAYSIZE(backbuf); i++)
	{
		SAFE_RELEASE(backbuf[i]);
	}

	commandListMgr->ImmediatelyDestroyAllResourcesScheduledForDestruction();

	SAFE_RELEASE(swapchain);

	commandListMgr->Release();
	commandQueue->Release();

	defaultRootSignature->Release();

	gpuDescriptorHeapMgr->Release();
	samplerDescriptorHeapMgr->Release();
	rtvDescriptorHeapMgr->Release();
	dsvDescriptorHeapMgr->Release();

	D3D::CleanupPersistentD3DTextureResources();

	ULONG references12 = device12->Release();
	if ((!debug12 && references12) || (debug12 && references12 > 1))
	{
		ERROR_LOG(VIDEO, "Unreleased D3D12 references: %i.", references12);
	}
	else
	{
		NOTICE_LOG(VIDEO, "Successfully released all D3D12 device references!");
	}

#if defined(_DEBUG) || defined(DEBUGFAST)
	if (debug12)
	{
		--references12; // the debug interface increases the refcount of the device, subtract that.
		if (references12)
		{
			// print out alive objects, but only if we actually have pending references
			// note this will also print out internal live objects to the debug console
			debug12->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL);
		}
		SAFE_RELEASE(debug12);
	}
#endif

	device12 = nullptr;
	currentCommandList = nullptr;

	// unload DLLs
	UnloadDXGI();
	UnloadD3DCompiler();
	UnloadD3D();
}

const char* VertexShaderVersionString()
{
	if (featlevel == D3D_FEATURE_LEVEL_11_0) return "vs_5_0";
	else if (featlevel == D3D_FEATURE_LEVEL_10_1) return "vs_4_1";
	else /*if(featlevel == D3D_FEATURE_LEVEL_10_0)*/ return "vs_4_0";
}

const char* GeometryShaderVersionString()
{
	if (featlevel == D3D_FEATURE_LEVEL_11_0) return "gs_5_0";
	else if (featlevel == D3D_FEATURE_LEVEL_10_1) return "gs_4_1";
	else /*if(featlevel == D3D_FEATURE_LEVEL_10_0)*/ return "gs_4_0";
}

const char* PixelShaderVersionString()
{
	if (featlevel == D3D_FEATURE_LEVEL_11_0) return "ps_5_0";
	else if (featlevel == D3D_FEATURE_LEVEL_10_1) return "ps_4_1";
	else /*if(featlevel == D3D_FEATURE_LEVEL_10_0)*/ return "ps_4_0";
}

D3DTexture2D* &GetBackBuffer() { return backbuf[currentBackbuf]; }
unsigned int GetBackBufferWidth() { return xres; }
unsigned int GetBackBufferHeight() { return yres; }

bool BGRATexturesSupported() { return bgra_textures_supported; }

// Returns the maximum width/height of a texture. This value only depends upon the feature level in DX12
unsigned int GetMaxTextureSize()
{
	switch (featlevel)
	{
		case D3D_FEATURE_LEVEL_11_0:
			return D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;

		case D3D_FEATURE_LEVEL_10_1:
		case D3D_FEATURE_LEVEL_10_0:
			return D3D10_REQ_TEXTURE2D_U_OR_V_DIMENSION;

		case D3D_FEATURE_LEVEL_9_3:
			return 4096;

		case D3D_FEATURE_LEVEL_9_2:
		case D3D_FEATURE_LEVEL_9_1:
			return 2048;

		default:
			return 0;
	}
}

void Reset()
{
	commandListMgr->ExecuteQueuedWork(true);

	// release all back buffer references
	for (UINT i = 0; i < ARRAYSIZE(backbuf); i++)
	{
		SAFE_RELEASE(backbuf[i]);
	}

	D3D::commandListMgr->ImmediatelyDestroyAllResourcesScheduledForDestruction();

	// resize swapchain buffers
	RECT client;
	GetClientRect(hWnd, &client);
	xres = client.right - client.left;
	yres = client.bottom - client.top;

	CheckHR(D3D::swapchain->ResizeBuffers(SWAP_CHAIN_BUFFER_COUNT, xres, yres, DXGI_FORMAT_R8G8B8A8_UNORM, 0));

	// recreate back buffer textures

	HRESULT hr = S_OK;
	ID3D12Resource* buf12 = nullptr;

	for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
	{
		ID3D12Resource* buf12;
		hr = swapchain->GetBuffer(i, IID_PPV_ARGS(&buf12));

		CHECK(SUCCEEDED(hr), "Create back buffer texture");

		backbuf[i] = new D3DTexture2D(buf12,
			D3D11_BIND_RENDER_TARGET,
			DXGI_FORMAT_UNKNOWN,
			DXGI_FORMAT_UNKNOWN,
			DXGI_FORMAT_UNKNOWN,
			false,
			D3D12_RESOURCE_STATE_PRESENT
			);

		CHECK(backbuf != nullptr, "Create back buffer texture");

		SAFE_RELEASE(buf12);
		SetDebugObjectName12(backbuf[i]->GetTex12(), "backbuffer texture");
	}

	currentBackbuf = 0;

	backbuf[currentBackbuf]->TransitionToResourceState(currentCommandList, D3D12_RESOURCE_STATE_RENDER_TARGET);
}

bool BeginFrame()
{
	if (bFrameInProgress)
	{
		PanicAlert("BeginFrame called although a frame is already in progress");
		return false;
	}
	bFrameInProgress = true;
	return (device12 != nullptr);
}

void EndFrame()
{
	if (!bFrameInProgress)
	{
		PanicAlert("EndFrame called although no frame is in progress");
		return;
	}
	bFrameInProgress = false;
}

LARGE_INTEGER lastPresent;
LARGE_INTEGER frequency;

UINT syncRefreshCount = 0;

void Present()
{
	UINT presentFlags = DXGI_PRESENT_TEST;

	LARGE_INTEGER currentTimestamp;
	QueryPerformanceCounter(&currentTimestamp);
	QueryPerformanceFrequency(&frequency);

	// Only present at most two times per vblank interval. If the application exhausts available back buffers, the
	// the Present call will block until the next vblank.
	
	if (((currentTimestamp.QuadPart - lastPresent.QuadPart) * 1000) / frequency.QuadPart >= (16.667 / 2))
	{
		lastPresent = currentTimestamp;

		backbuf[currentBackbuf]->TransitionToResourceState(currentCommandList, D3D12_RESOURCE_STATE_PRESENT);
		presentFlags = 0;

		currentBackbuf = (currentBackbuf + 1) % SWAP_CHAIN_BUFFER_COUNT;
	}

	commandListMgr->ExecuteQueuedWorkAndPresent(swapchain, (UINT)g_ActiveConfig.IsVSync(), presentFlags);

	backbuf[currentBackbuf]->TransitionToResourceState(currentCommandList, D3D12_RESOURCE_STATE_RENDER_TARGET);

#ifdef USE_D3D12_FREQUENT_EXECUTION
	commandListMgr->_cpuAccessLastFrame = commandListMgr->_cpuAccessThisFrame;
	commandListMgr->_cpuAccessThisFrame = false;
	commandListMgr->_drawsSinceLastExecution = 0;
#endif
}

HRESULT SetFullscreenState(bool enable_fullscreen)
{
	return swapchain->SetFullscreenState(enable_fullscreen, nullptr);
}

HRESULT GetFullscreenState(bool* fullscreen_state)
{
	if (fullscreen_state == nullptr)
	{
		return E_POINTER;
	}

	BOOL state;
	HRESULT hr = swapchain->GetFullscreenState(&state, nullptr);
	*fullscreen_state = !!state;
	return hr;
}

}  // namespace D3D

}  // namespace DX12
