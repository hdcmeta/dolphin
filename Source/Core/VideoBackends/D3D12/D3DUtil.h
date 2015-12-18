// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <d3d11.h>
#include <string>

#include "Common/MathUtil.h"
#include "VideoBackends/D3D12/D3DState.h"

namespace DX12
{

	extern StateCache gx_state_cache;

namespace D3D
{
	extern inline void ResourceBarrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter, UINT subresource);

	// Font creation flags
	#define D3DFONT_BOLD        0x0001
	#define D3DFONT_ITALIC      0x0002

	// Font rendering flags
	#define D3DFONT_CENTERED    0x0001

	class CD3DFont
	{
		ID3D12Resource* m_pTexture12;
		D3D12_CPU_DESCRIPTOR_HANDLE m_pTexture12cpu;
		D3D12_GPU_DESCRIPTOR_HANDLE m_pTexture12gpu;

		ID3D12Resource* m_pVB12;
		D3D12_VERTEX_BUFFER_VIEW m_pVB12view;
		void* m_pVB12data;
		unsigned int m_pVB12offset;
		D3D12_CPU_DESCRIPTOR_HANDLE m_pVB12vertexbuffer;
		D3D12_INPUT_LAYOUT_DESC m_InputLayout12;
		D3D12_SHADER_BYTECODE m_pshader12;
		D3D12_SHADER_BYTECODE m_vshader12;
		D3D12_BLEND_DESC m_blendstate12;
		D3D12_RASTERIZER_DESC m_raststate12;
		ID3D12PipelineState* m_pPso;

		const int m_dwTexWidth;
		const int m_dwTexHeight;
		unsigned int m_LineHeight;
		float m_fTexCoords[128-32][4];

	public:
		CD3DFont();
		// 2D text drawing function
		// Initializing and destroying device-dependent objects
		int Init();
		int Shutdown();
		int DrawTextScaled(float x, float y,
		                   float size,
		                   float spacing, u32 dwColor,
		                   const std::string& text);
	};

	extern CD3DFont font;

	void InitUtils();
	void ShutdownUtils();

	void SetPointCopySampler();
	void SetLinearCopySampler();

	void drawShadedTexQuad(D3DTexture2D* texture,
						const D3D11_RECT* rSource,
						int SourceWidth,
						int SourceHeight,
						D3D12_SHADER_BYTECODE PShader12 = {},
						D3D12_SHADER_BYTECODE VShader12 = {},
						D3D12_INPUT_LAYOUT_DESC layout12 = {},
						D3D12_SHADER_BYTECODE GShader12 = {},
						float Gamma = 1.0f,
						u32 slice = 0,
						DXGI_FORMAT rtFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
						bool inheritSRVbinding = false);
	void drawClearQuad(u32 Color, float z, D3D12_BLEND_DESC *pBlendDesc, D3D12_DEPTH_STENCIL_DESC *pDepthStencilDesc);
	void drawColorQuad(u32 Color, float z, float x1, float y1, float x2, float y2, D3D12_BLEND_DESC *pBlendDesc, D3D12_DEPTH_STENCIL_DESC *pDepthStencilDesc);
}

}
