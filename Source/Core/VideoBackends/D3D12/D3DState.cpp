// Copyright 2014 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Common/BitSet.h"
#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/LinearDiskCache.h"
#include "Common/MsgHandler.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"

#include "Core/ConfigManager.h"

#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DState.h"
#include "VideoBackends/D3D12/D3DUtil.h"

#include "VideoBackends/D3D12/NativeVertexFormat.h"

#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/VertexLoaderManager.h"


namespace DX12
{

bool cacheIsCorrupted = false;
LinearDiskCache<SmallPsoDiskDesc, u8> g_pso_disk_cache;

class PipelineStateCacheInserter : public LinearDiskCacheReader<SmallPsoDiskDesc, u8>
{
public:
	void Read(const SmallPsoDiskDesc &key, const u8* value, u32 value_size)
	{
		if (cacheIsCorrupted)
			return;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature = D3D::defaultRootSignature;
		desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // This state changes in PSTextureEncoder::Encode.
		desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT; // This state changes in PSTextureEncoder::Encode.
		desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;
		desc.NumRenderTargets = 1;
		desc.SampleMask = UINT_MAX;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;

		desc.PS = PixelShaderCache::GetShaderFromUid(key.psUid);
		desc.VS = VertexShaderCache::GetShaderFromUid(key.vsUid);
		desc.GS = GeometryShaderCache::GetShaderFromUid(key.gsUid);

		if (!desc.PS.pShaderBytecode || !desc.VS.pShaderBytecode)
		{
			cacheIsCorrupted = true;
			return;
		}

		desc.BlendState = StateCache::GetDesc12(key.BlendState);
		desc.DepthStencilState = StateCache::GetDesc12(key.DepthStencilState);
		desc.RasterizerState = StateCache::GetDesc12(key.RasterizerState);
		desc.PrimitiveTopologyType = key.topology;

		// search for a cached native vertex format
		const PortableVertexDeclaration& native_vtx_decl = key.vertexDeclaration;
		std::unique_ptr<NativeVertexFormat>& native = (*VertexLoaderManager::GetNativeVertexFormatMap())[native_vtx_decl];

		if (!native)
		{
			native.reset(g_vertex_manager->CreateNativeVertexFormat(native_vtx_decl));
		}
		
		desc.InputLayout = ((D3DVertexFormat*)native.get())->GetActiveInputLayout12();

		desc.CachedPSO.CachedBlobSizeInBytes = value_size;
		desc.CachedPSO.pCachedBlob = value;

		ID3D12PipelineState *pPso = nullptr;
		HRESULT hr = D3D::device12->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pPso));

		if (FAILED(hr))
		{
			// Failure can occur if disk cache is corrupted, or a driver upgrade invalidates the existing blobs.
			// In this case, we need to clear the disk cache.

			cacheIsCorrupted = true;

			return;
		}

		SmallPsoDesc smallDesc = {};
		smallDesc.BlendState.packed = key.BlendState.packed;
		smallDesc.DepthStencilState.hex = key.DepthStencilState.hex;
		smallDesc.RasterizerState.packed = key.RasterizerState.packed;
		smallDesc.GS = desc.GS;
		smallDesc.VS = desc.VS;
		smallDesc.PS = desc.PS;
		smallDesc.InputLayout = reinterpret_cast<D3DVertexFormat*>(native.get());

		gx_state_cache._smallPsoMap[smallDesc] = pPso;
	}
};

StateCache::StateCache()
{
	m_currentPsoDesc = {};

	m_currentPsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // This state changes in PSTextureEncoder::Encode.
	m_currentPsoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT; // This state changes in PSTextureEncoder::Encode.
	m_currentPsoDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;
	m_currentPsoDesc.NumRenderTargets = 1;
	m_currentPsoDesc.SampleMask = UINT_MAX;
}

void StateCache::Init()
{
	// Root signature isn't available at time of StateCache construction, so fill it in now.
	gx_state_cache.m_currentPsoDesc.pRootSignature = D3D::defaultRootSignature;

	// Multi-sample configuration isn't available at time of StateCache construction, so fille it in now.
	gx_state_cache.m_currentPsoDesc.SampleDesc.Count = g_ActiveConfig.iMultisamples;
	gx_state_cache.m_currentPsoDesc.SampleDesc.Quality = 0;

	if (!File::Exists(File::GetUserPath(D_SHADERCACHE_IDX)))
		File::CreateDir(File::GetUserPath(D_SHADERCACHE_IDX));

	std::string cache_filename = StringFromFormat("%sdx12-%s-pso.cache", File::GetUserPath(D_SHADERCACHE_IDX).c_str(),
		SConfig::GetInstance().m_strUniqueID.c_str());

	PipelineStateCacheInserter inserter;
	g_pso_disk_cache.OpenAndRead(cache_filename, inserter);

	if (cacheIsCorrupted)
	{
		// If a PSO fails to create, that means either:
		// - The file itself is corrupt.
		// - A driver/HW change has occured, causing the existing cache blobs to be invalid.
		// 
		// In either case, we want to re-create the disk cache. This should not be a frequent occurence.

		g_pso_disk_cache.Close();

		for (auto it : gx_state_cache._smallPsoMap)
		{
			SAFE_RELEASE(it.second);
		}
		gx_state_cache._smallPsoMap.clear();

		File::Delete(cache_filename);

		g_pso_disk_cache.OpenAndRead(cache_filename, inserter);
	
		cacheIsCorrupted = false;
	}
}

D3D12_SAMPLER_DESC StateCache::GetDesc12(SamplerState state)
{
	const unsigned int d3dMipFilters[4] =
	{
		TexMode0::TEXF_NONE,
		TexMode0::TEXF_POINT,
		TexMode0::TEXF_LINEAR,
		TexMode0::TEXF_NONE, //reserved
	};
	const D3D12_TEXTURE_ADDRESS_MODE d3dClamps[4] =
	{
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP //reserved
	};

	D3D12_SAMPLER_DESC sampdc;

	unsigned int mip = d3dMipFilters[state.min_filter & 3];

	sampdc.MaxAnisotropy = 1;
	if (g_ActiveConfig.iMaxAnisotropy > 1)
	{
		sampdc.Filter = D3D12_FILTER_ANISOTROPIC;
		sampdc.MaxAnisotropy = 1 << g_ActiveConfig.iMaxAnisotropy;
	}
	else if (state.min_filter & 4) // linear min filter
	{
		if (state.mag_filter) // linear mag filter
		{
			if (mip == TexMode0::TEXF_NONE)
				sampdc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
			else if (mip == TexMode0::TEXF_POINT)
				sampdc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
			else if (mip == TexMode0::TEXF_LINEAR)
				sampdc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		}
		else // point mag filter
		{
			if (mip == TexMode0::TEXF_NONE)
				sampdc.Filter = D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
			else if (mip == TexMode0::TEXF_POINT)
				sampdc.Filter = D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
			else if (mip == TexMode0::TEXF_LINEAR)
				sampdc.Filter = D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
		}
	}
	else // point min filter
	{
		if (state.mag_filter) // linear mag filter
		{
			if (mip == TexMode0::TEXF_NONE)
				sampdc.Filter = D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
			else if (mip == TexMode0::TEXF_POINT)
				sampdc.Filter = D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
			else if (mip == TexMode0::TEXF_LINEAR)
				sampdc.Filter = D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR;
		}
		else // point mag filter
		{
			if (mip == TexMode0::TEXF_NONE)
				sampdc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
			else if (mip == TexMode0::TEXF_POINT)
				sampdc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
			else if (mip == TexMode0::TEXF_LINEAR)
				sampdc.Filter = D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
		}
	}

	sampdc.AddressU = d3dClamps[state.wrap_s];
	sampdc.AddressV = d3dClamps[state.wrap_t];
	sampdc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

	sampdc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;

	sampdc.BorderColor[0] = sampdc.BorderColor[1] = sampdc.BorderColor[2] = sampdc.BorderColor[3] = 1.0f;

	sampdc.MaxLOD = (mip == TexMode0::TEXF_NONE) ? 0.0f : (float)state.max_lod / 16.f;
	sampdc.MinLOD = (float)state.min_lod / 16.f;
	sampdc.MipLODBias = (s32)state.lod_bias / 32.0f;

	return sampdc;
}

D3D12_BLEND_DESC StateCache::GetDesc12(BlendState state)
{
	if (!state.blend_enable)
	{
		state.src_blend = D3D12_BLEND_ONE;
		state.dst_blend = D3D12_BLEND_ZERO;
		state.blend_op = D3D12_BLEND_OP_ADD;
		state.use_dst_alpha = false;
	}

	D3D12_BLEND_DESC blenddc = {
		FALSE, // BOOL AlphaToCoverageEnable;
	    FALSE, // BOOL IndependentBlendEnable;
	    {
			state.blend_enable,   // BOOL BlendEnable;
		    FALSE,                // BOOL LogicOpEnable;
		    state.src_blend,      // D3D12_BLEND SrcBlend;
		    state.dst_blend,      // D3D12_BLEND DestBlend;
		    state.blend_op,       // D3D12_BLEND_OP BlendOp;
		    state.src_blend,      // D3D12_BLEND SrcBlendAlpha;
		    state.dst_blend,      // D3D12_BLEND DestBlendAlpha;
		    state.blend_op,       // D3D12_BLEND_OP BlendOpAlpha;
		    D3D12_LOGIC_OP_NOOP,  // D3D12_LOGIC_OP LogicOp
		    state.write_mask      // UINT8 RenderTargetWriteMask;
	    }
	};

	if (blenddc.RenderTarget[0].SrcBlend == D3D12_BLEND_SRC_COLOR)
		blenddc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
	else if (blenddc.RenderTarget[0].SrcBlend == D3D12_BLEND_INV_SRC_COLOR)
		blenddc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	else if (blenddc.RenderTarget[0].SrcBlend == D3D12_BLEND_DEST_COLOR)
		blenddc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_DEST_ALPHA;
	else if (blenddc.RenderTarget[0].SrcBlend == D3D12_BLEND_INV_DEST_COLOR)
		blenddc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_INV_DEST_ALPHA;
	else
		blenddc.RenderTarget[0].SrcBlendAlpha = blenddc.RenderTarget[0].SrcBlend;

	if (blenddc.RenderTarget[0].DestBlend == D3D12_BLEND_SRC_COLOR)
		blenddc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_SRC_ALPHA;
	else if (blenddc.RenderTarget[0].DestBlend == D3D12_BLEND_INV_SRC_COLOR)
		blenddc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	else if (blenddc.RenderTarget[0].DestBlend == D3D12_BLEND_DEST_COLOR)
		blenddc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_DEST_ALPHA;
	else if (blenddc.RenderTarget[0].DestBlend == D3D12_BLEND_INV_DEST_COLOR)
		blenddc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_DEST_ALPHA;
	else
		blenddc.RenderTarget[0].DestBlendAlpha = blenddc.RenderTarget[0].DestBlend;

	if (state.use_dst_alpha)
	{
		// Colors should blend against SRC1_ALPHA
		if (blenddc.RenderTarget[0].SrcBlend == D3D12_BLEND_SRC_ALPHA)
			blenddc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC1_ALPHA;
		else if (blenddc.RenderTarget[0].SrcBlend == D3D12_BLEND_INV_SRC_ALPHA)
			blenddc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_SRC1_ALPHA;

		// Colors should blend against SRC1_ALPHA
		if (blenddc.RenderTarget[0].DestBlend == D3D12_BLEND_SRC_ALPHA)
			blenddc.RenderTarget[0].DestBlend = D3D12_BLEND_SRC1_ALPHA;
		else if (blenddc.RenderTarget[0].DestBlend == D3D12_BLEND_INV_SRC_ALPHA)
			blenddc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC1_ALPHA;

		blenddc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		blenddc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		blenddc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	}

	return blenddc;
}

D3D12_RASTERIZER_DESC StateCache::GetDesc12(RasterizerState state)
{
	D3D12_RASTERIZER_DESC rastdc = {
		D3D12_FILL_MODE_SOLID,
		state.cull_mode,
		false,
		0,
		0.f,
		0,
		true,
		true,
		false,
		0,
		D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
	};

	return rastdc;
}

inline D3D12_DEPTH_STENCIL_DESC StateCache::GetDesc12(ZMode state)
{
	D3D12_DEPTH_STENCIL_DESC depthdc;

	depthdc.StencilEnable = FALSE;
	depthdc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
	depthdc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;

	D3D12_DEPTH_STENCILOP_DESC defaultStencilOp = { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
	depthdc.FrontFace = defaultStencilOp;
	depthdc.BackFace = defaultStencilOp;

	const D3D12_COMPARISON_FUNC d3dCmpFuncs[8] =
	{
		D3D12_COMPARISON_FUNC_NEVER,
		D3D12_COMPARISON_FUNC_GREATER,
		D3D12_COMPARISON_FUNC_EQUAL,
		D3D12_COMPARISON_FUNC_GREATER_EQUAL,
		D3D12_COMPARISON_FUNC_LESS,
		D3D12_COMPARISON_FUNC_NOT_EQUAL,
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_COMPARISON_FUNC_ALWAYS
	};

	if (state.testenable)
	{
		depthdc.DepthEnable = TRUE;
		depthdc.DepthWriteMask = state.updateenable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
		depthdc.DepthFunc = d3dCmpFuncs[state.func];
	}
	else
	{
		// if the test is disabled write is disabled too
		depthdc.DepthEnable = FALSE;
		depthdc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	}

	return depthdc;
}

HRESULT StateCache::GetPipelineStateObjectFromCache(D3D12_GRAPHICS_PIPELINE_STATE_DESC* psoDesc, ID3D12PipelineState** pso)
{
	auto it = _psoMap.find(*psoDesc);

	if (it == _psoMap.end())
	{
		// Not found, create new PSO.

		ID3D12PipelineState *pNewPso = nullptr;
		HRESULT hr = D3D::device12->CreateGraphicsPipelineState(psoDesc, IID_PPV_ARGS(&pNewPso));

		if (FAILED(hr))
		{
			return hr;
		}

		_psoMap[*psoDesc] = pNewPso;
		*pso = pNewPso;
	}
	else
	{
		*pso = it->second;
	}

	return S_OK;
}

HRESULT StateCache::GetPipelineStateObjectFromCache(SmallPsoDesc* psoDesc, ID3D12PipelineState** pso, D3D12_PRIMITIVE_TOPOLOGY_TYPE topology, PixelShaderUid psUid, VertexShaderUid vsUid, GeometryShaderUid gsUid)
{
	auto it = _smallPsoMap.find(*psoDesc);

	if (it == _smallPsoMap.end())
	{
		// Not found, create new PSO.

		// RootSignature, SampleMask, SampleDesc, NumRenderTargets, RTVFormats, DSVFormat
		// never change so they are set in constructor and forgotten.
		m_currentPsoDesc.PS = psoDesc->PS;
		m_currentPsoDesc.VS = psoDesc->VS;
		m_currentPsoDesc.GS = psoDesc->GS;
		m_currentPsoDesc.BlendState = GetDesc12(psoDesc->BlendState);
		m_currentPsoDesc.DepthStencilState = GetDesc12(psoDesc->DepthStencilState);
		m_currentPsoDesc.RasterizerState = GetDesc12(psoDesc->RasterizerState);
		m_currentPsoDesc.PrimitiveTopologyType = topology;
		m_currentPsoDesc.InputLayout = psoDesc->InputLayout->GetActiveInputLayout12();

		ID3D12PipelineState *pNewPso = nullptr;
		HRESULT hr = D3D::device12->CreateGraphicsPipelineState(&m_currentPsoDesc, IID_PPV_ARGS(&pNewPso));

		if (FAILED(hr))
		{
			return hr;
		}

		_smallPsoMap[*psoDesc] = pNewPso;
		*pso = pNewPso;

		// This contains all of the information needed to reconstruct a PSO at startup.
		SmallPsoDiskDesc diskDesc = {};
		diskDesc.BlendState.packed = psoDesc->BlendState.packed;
		diskDesc.DepthStencilState.hex = psoDesc->DepthStencilState.hex;
		diskDesc.RasterizerState.packed = psoDesc->RasterizerState.packed;
		diskDesc.psUid = psUid;
		diskDesc.vsUid = vsUid;
		diskDesc.gsUid = gsUid;
		diskDesc.vertexDeclaration = psoDesc->InputLayout->GetVertexDeclaration();
		diskDesc.topology = topology;
		
		// This shouldn't fail.. but if it does, don't cache to disk.
		ID3DBlob* psoBlob = nullptr;
		hr = pNewPso->GetCachedBlob(&psoBlob);
		
		if (SUCCEEDED(hr))
		{
			g_pso_disk_cache.Append(diskDesc, reinterpret_cast<const u8*>(psoBlob->GetBufferPointer()), static_cast<u32>(psoBlob->GetBufferSize()));
			psoBlob->Release();
		}
	}
	else
	{
		*pso = it->second;
	}

	return S_OK;
}

void StateCache::Clear()
{
	for (auto it : m_depth)
	{
		SAFE_RELEASE(it.second);
	}
	m_depth.clear();

	for (auto it : m_raster)
	{
		SAFE_RELEASE(it.second);
	}
	m_raster.clear();

	for (auto it : m_blend)
	{
		SAFE_RELEASE(it.second);
	}
	m_blend.clear();

	for (auto it : m_sampler)
	{
		SAFE_RELEASE(it.second);
	}
	m_sampler.clear();

	for (auto it : _psoMap)
	{
		SAFE_RELEASE(it.second);
	}
	_psoMap.clear();

	for (auto it : _smallPsoMap)
	{
		SAFE_RELEASE(it.second);
	}
	_smallPsoMap.clear();

	g_pso_disk_cache.Sync();
	g_pso_disk_cache.Close();
}

}  // namespace DX12
