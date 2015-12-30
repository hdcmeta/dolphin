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

extern inline void ResourceBarrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource, D3D12_RESOURCE_STATES state_before, D3D12_RESOURCE_STATES state_after, UINT subresource);

// Font creation flags
static const unsigned int s_d3dfont_bold = 0x0001;
static const unsigned int s_d3dfont_italic = 0x0002;

// Font rendering flags
static const unsigned int s_d3dfont_centered = 0x0001;

class CD3DFont
{
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

private:
	ID3D12Resource* m_texture12;
	D3D12_CPU_DESCRIPTOR_HANDLE m_texture12_cpu;
	D3D12_GPU_DESCRIPTOR_HANDLE m_texture12_gpu;

	ID3D12Resource* m_vb12;
	D3D12_VERTEX_BUFFER_VIEW m_vb12_view;
	void* m_vb12_data;
	unsigned int m_vb12_offset;

	D3D12_INPUT_LAYOUT_DESC m_input_layout12;
	D3D12_SHADER_BYTECODE m_pshader12;
	D3D12_SHADER_BYTECODE m_vshader12;
	D3D12_BLEND_DESC m_blendstate12;
	D3D12_RASTERIZER_DESC m_raststate12;
	ID3D12PipelineState* m_pso;

	const int m_tex_width;
	const int m_tex_height;
	unsigned int m_line_height;
	float m_tex_coords[128-32][4];
};

extern CD3DFont font;

void InitUtils();
void ShutdownUtils();

void SetPointCopySampler();
void SetLinearCopySampler();

void DrawShadedTexQuad(D3DTexture2D* texture,
					const D3D12_RECT* source,
					int source_width,
					int source_height,
					D3D12_SHADER_BYTECODE pshader12 = {},
					D3D12_SHADER_BYTECODE vshader12 = {},
					D3D12_INPUT_LAYOUT_DESC layout12 = {},
					D3D12_SHADER_BYTECODE gshader12 = {},
					float gamma = 1.0f,
					u32 slice = 0,
					DXGI_FORMAT rt_format = DXGI_FORMAT_R8G8B8A8_UNORM,
					bool inherit_srv_binding = false,
					bool rt_multisampled = false
					);

void DrawClearQuad(u32 Color, float z, D3D12_BLEND_DESC* blend_desc, D3D12_DEPTH_STENCIL_DESC* depth_stencil_desc, bool rt_multisampled);
void DrawColorQuad(u32 Color, float z, float x1, float y1, float x2, float y2, D3D12_BLEND_DESC* blend_desc, D3D12_DEPTH_STENCIL_DESC* depth_stencil_desc, bool rt_multisampled);
}

}
