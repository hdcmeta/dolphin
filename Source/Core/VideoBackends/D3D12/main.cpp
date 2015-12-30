// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included. 

#include <string>

#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/StringUtil.h"
#include "Common/Logging/LogManager.h"

#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/Host.h"


#include "VideoBackends/D3D12/BoundingBox.h"
#include "VideoBackends/D3D12/D3DCommandListManager.h"
#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DUtil.h"
#include "VideoBackends/D3D12/PerfQuery.h"
#include "VideoBackends/D3D12/ShaderCache.h"
#include "VideoBackends/D3D12/ShaderConstantsManager.h"
#include "VideoBackends/D3D12/StaticShaderCache.h"
#include "VideoBackends/D3D12/TextureCache.h"
#include "VideoBackends/D3D12/VertexManager.h"
#include "VideoBackends/D3D12/VideoBackend.h"

#include "VideoBackends/D3D12/main.h"

#include "VideoCommon/BPStructs.h"
#include "VideoCommon/CommandProcessor.h"
#include "VideoCommon/Fifo.h"
#include "VideoCommon/GeometryShaderManager.h"
#include "VideoCommon/IndexGenerator.h"
#include "VideoCommon/OnScreenDisplay.h"
#include "VideoCommon/OpcodeDecoding.h"
#include "VideoCommon/PixelEngine.h"
#include "VideoCommon/PixelShaderManager.h"
#include "VideoCommon/VertexLoaderManager.h"
#include "VideoCommon/VertexShaderManager.h"
#include "VideoCommon/VideoConfig.h"

namespace DX12
{

unsigned int VideoBackend::PeekMessages()
{
	MSG msg;
	while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
	{
		if (msg.message == WM_QUIT)
			return FALSE;
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return TRUE;
}

std::string VideoBackend::GetName() const
{
	return "D3D12";
}

std::string VideoBackend::GetDisplayName() const
{
	return "Direct3D 12";
}

std::string VideoBackend::GetConfigName() const
{
	return "gfx_dx12";
}

void InitBackendInfo()
{
	HRESULT hr = DX12::D3D::LoadDXGI();
	if (SUCCEEDED(hr)) hr = DX12::D3D::LoadD3D();
	if (FAILED(hr))
	{
		DX12::D3D::UnloadDXGI();
		return;
	}

	g_Config.backend_info.APIType = API_D3D;
	g_Config.backend_info.bSupportsExclusiveFullscreen = false;
	g_Config.backend_info.bSupportsDualSourceBlend = true;
	g_Config.backend_info.bSupportsPrimitiveRestart = true;
	g_Config.backend_info.bSupportsOversizedViewports = false;
	g_Config.backend_info.bSupportsGeometryShaders = true;
	g_Config.backend_info.bSupports3DVision = true;
	g_Config.backend_info.bSupportsPostProcessing = false;
	g_Config.backend_info.bSupportsPaletteConversion = true;
	g_Config.backend_info.bSupportsClipControl = true;

	IDXGIFactory* factory;
	IDXGIAdapter* ad;
	hr = DX12::create_dxgi_factory(__uuidof(IDXGIFactory), (void**)&factory);
	if (FAILED(hr))
		PanicAlert("Failed to create IDXGIFactory object");

	// adapters
	g_Config.backend_info.Adapters.clear();
	g_Config.backend_info.AAModes.clear();
	while (factory->EnumAdapters((UINT)g_Config.backend_info.Adapters.size(), &ad) != DXGI_ERROR_NOT_FOUND)
	{
		const size_t adapter_index = g_Config.backend_info.Adapters.size();

		DXGI_ADAPTER_DESC desc;
		ad->GetDesc(&desc);

		// TODO: These don't get updated on adapter change, yet
		if (adapter_index == g_Config.iAdapter)
		{
			std::string samples;
			std::vector<DXGI_SAMPLE_DESC> modes = DX12::D3D::EnumAAModes(ad);
			// First iteration will be 1. This equals no AA.
			for (unsigned int i = 0; i < modes.size(); ++i)
			{
				g_Config.backend_info.AAModes.push_back(modes[i].Count);
			}

			bool shader_model_5_supported = (DX12::D3D::GetFeatureLevel(ad) >= D3D_FEATURE_LEVEL_11_0);

			// Requires the earlydepthstencil attribute (only available in shader model 5)
			g_Config.backend_info.bSupportsEarlyZ = shader_model_5_supported;

			// Requires full UAV functionality (only available in shader model 5)
			g_Config.backend_info.bSupportsBBox = false; // D3D12TODO: Implement GPU-side bounding box;

			// Requires the instance attribute (only available in shader model 5)
			g_Config.backend_info.bSupportsGSInstancing = shader_model_5_supported;

			// Sample shading requires shader model 5
			g_Config.backend_info.bSupportsSSAA = shader_model_5_supported;
		}
		g_Config.backend_info.Adapters.push_back(UTF16ToUTF8(desc.Description));
		ad->Release();
	}
	factory->Release();

	// Clear ppshaders string vector
	g_Config.backend_info.PPShaders.clear();
	g_Config.backend_info.AnaglyphShaders.clear();

	DX12::D3D::UnloadDXGI();
	DX12::D3D::UnloadD3D();
}

void VideoBackend::ShowConfig(void *hParent)
{
	InitBackendInfo();
	Host_ShowVideoConfig(hParent, GetDisplayName(), GetConfigName());
}

bool VideoBackend::Initialize(void *window_handle)
{
	if (window_handle == nullptr)
		return false;

	InitializeShared();
	InitBackendInfo();

	frameCount = 0;

	g_Config.Load(File::GetUserPath(D_CONFIG_IDX) + GetConfigName() + ".ini");
	g_Config.GameIniLoad();
	g_Config.UpdateProjectionHack();
	g_Config.VerifyValidity();
	UpdateActiveConfig();

	m_window_handle = window_handle;

	s_BackendInitialized = true;

	return true;
}

void VideoBackend::Video_Prepare()
{
	// internal interfaces
	g_renderer = new Renderer(m_window_handle);
	g_texture_cache = new TextureCache;
	g_vertex_manager = new VertexManager;
	g_perf_query = new PerfQuery;
	ShaderCache::Init();
	ShaderConstantsManager::Init();
	StaticShaderCache::Init();
	StateCache::Init(); // PSO cache is populated here, after constituent shaders are loaded.
	D3D::InitUtils();

	// VideoCommon
	BPInit();
	Fifo_Init();
	IndexGenerator::Init();
	VertexLoaderManager::Init();
	OpcodeDecoder_Init();
	VertexShaderManager::Init();
	PixelShaderManager::Init();
	GeometryShaderManager::Init();
	CommandProcessor::Init();
	PixelEngine::Init();
	BBox::Init();

	// Tell the host that the window is ready
	Host_Message(WM_USER_CREATE);
}

void VideoBackend::Shutdown()
{
	s_BackendInitialized = false;

	// TODO: should be in Video_Cleanup
	if (g_renderer)
	{
		// Immediately stop app from submitting work to GPU, and wait for all submitted work to complete. D3D12TODO: Check this.
		D3D::command_list_mgr->ExecuteQueuedWork(true);

		// VideoCommon
		Fifo_Shutdown();
		CommandProcessor::Shutdown();
		GeometryShaderManager::Shutdown();
		PixelShaderManager::Shutdown();
		VertexShaderManager::Shutdown();
		OpcodeDecoder_Shutdown();
		VertexLoaderManager::Shutdown();

		// internal interfaces
		D3D::ShutdownUtils();
		ShaderCache::Shutdown();
		ShaderConstantsManager::Shutdown();
		StaticShaderCache::Shutdown();
		BBox::Shutdown();

		delete g_perf_query;
		delete g_vertex_manager;
		delete g_texture_cache;
		delete g_renderer;
		g_renderer = nullptr;
		g_texture_cache = nullptr;
		g_vertex_manager = nullptr;
		g_perf_query = nullptr;
	}
}

void VideoBackend::Video_Cleanup()
{
}

}