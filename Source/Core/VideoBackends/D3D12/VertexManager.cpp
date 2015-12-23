// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoBackends/D3D12/BoundingBox.h"
#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DCommandListManager.h"
#include "VideoBackends/D3D12/D3DState.h"
#include "VideoBackends/D3D12/FramebufferManager.h"
#include "VideoBackends/D3D12/GeometryShaderCache.h"
#include "VideoBackends/D3D12/PixelShaderCache.h"
#include "VideoBackends/D3D12/Render.h"
#include "VideoBackends/D3D12/VertexManager.h"
#include "VideoBackends/D3D12/VertexShaderCache.h"

#include "VideoCommon/BoundingBox.h"
#include "VideoCommon/Debugger.h"
#include "VideoCommon/IndexGenerator.h"
#include "VideoCommon/MainBase.h"
#include "VideoCommon/PixelShaderManager.h"
#include "VideoCommon/RenderBase.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/TextureCacheBase.h"
#include "VideoCommon/VertexLoaderManager.h"
#include "VideoCommon/VertexShaderManager.h"
#include "VideoCommon/VideoConfig.h"

namespace DX12
{

// TODO: Find sensible values for these two
const u32 MAX_IBUFFER_SIZE = VertexManager::MAXIBUFFERSIZE * sizeof(u16) * 16;
const u32 MAX_VBUFFER_SIZE = VertexManager::MAXVBUFFERSIZE * 4;

bool usingCPUOnlyBuffer = false;
u8* indexCpuBuffer;
u8* vertexCpuBuffer;

void VertexManager::SetIndexBuffer()
{
	D3D12_INDEX_BUFFER_VIEW ibView = {
		m_indexBuffer->GetGPUVirtualAddress(), // D3D12_GPU_VIRTUAL_ADDRESS BufferLocation;
		MAX_IBUFFER_SIZE,                      // UINT SizeInBytes;
		DXGI_FORMAT_R16_UINT                   // DXGI_FORMAT Format;
	};

	D3D::current_command_list->IASetIndexBuffer(&ibView);
}

void VertexManager::CreateDeviceObjects()
{
	m_vertexDrawOffset = 0;
	m_indexDrawOffset = 0;

	CheckHR(
		D3D::device12->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(MAX_VBUFFER_SIZE),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_vertexBuffer)
			)
		);

	D3D::SetDebugObjectName12(m_vertexBuffer, "Vertex Buffer of VertexManager");

	CheckHR(m_vertexBuffer->Map(0, nullptr, &m_vertexBufferData));

	CheckHR(
		D3D::device12->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(MAX_IBUFFER_SIZE),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_indexBuffer)
			)
		);

	D3D::SetDebugObjectName12(m_indexBuffer, "Index Buffer of VertexManager");

	CheckHR(m_indexBuffer->Map(0, nullptr, &m_indexBufferData));

	SetIndexBuffer();

	// Use CPU-only memory if the GPU won't be reading from the buffers,
	// since reading upload heaps on the CPU is slow..
	vertexCpuBuffer = new u8[MAXVBUFFERSIZE];
	indexCpuBuffer = new u8[MAXIBUFFERSIZE];
}

void VertexManager::DestroyDeviceObjects()
{
	D3D::command_list_mgr->DestroyResourceAfterCurrentCommandListExecuted(m_vertexBuffer);
	D3D::command_list_mgr->DestroyResourceAfterCurrentCommandListExecuted(m_indexBuffer);

	SAFE_DELETE(vertexCpuBuffer);
	SAFE_DELETE(indexCpuBuffer);
}

u8* s_pCurBufferPointerBeforeWrite = nullptr;
u8* s_pIndexBufferPointer = nullptr;
u32 s_lastIndexWriteSize = 0;

VertexManager::VertexManager()
{
	CreateDeviceObjects();

	s_pCurBufferPointer = s_pBaseBufferPointer = (u8*)m_vertexBufferData;
	s_pEndBufferPointer = s_pBaseBufferPointer + MAX_VBUFFER_SIZE;

	s_pIndexBufferPointer = (u8*)m_indexBufferData;
}

VertexManager::~VertexManager()
{
	DestroyDeviceObjects();
}

void VertexManager::PrepareDrawBuffers(u32 stride)
{
	u32 vertexBufferSize = u32(s_pCurBufferPointer - s_pBaseBufferPointer);
	s_lastIndexWriteSize = IndexGenerator::GetIndexLen() * sizeof(u16);

	m_vertexDrawOffset = (u32) (s_pCurBufferPointerBeforeWrite - s_pBaseBufferPointer);
	m_indexDrawOffset = (u32) (s_pIndexBufferPointer - (u8*) m_indexBufferData);

	ADDSTAT(stats.thisFrame.bytesVertexStreamed, vertexBufferSize);
	ADDSTAT(stats.thisFrame.bytesIndexStreamed, s_lastIndexWriteSize);
}

u32 oldStride = UINT_MAX;

void VertexManager::Draw(u32 stride)
{
	u32 indices = IndexGenerator::GetIndexLen();

	if (D3D::command_list_mgr->m_dirty_vertex_buffer || oldStride != stride)
	{
		D3D12_VERTEX_BUFFER_VIEW vbView = {
			m_vertexBuffer->GetGPUVirtualAddress(), // D3D12_GPU_VIRTUAL_ADDRESS BufferLocation;
			MAX_VBUFFER_SIZE,                       // UINT SizeInBytes;
			stride                                  // UINT StrideInBytes;
		};

		D3D::current_command_list->IASetVertexBuffers(0, 1, &vbView);

		D3D::command_list_mgr->m_dirty_vertex_buffer = false;
		oldStride = stride;
	}

	u32 baseVertex = m_vertexDrawOffset / stride;
	u32 startIndex = m_indexDrawOffset / sizeof(u16);

	D3D_PRIMITIVE_TOPOLOGY d3dPrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;

	switch (current_primitive_type)
	{
		case PRIMITIVE_POINTS:
			d3dPrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
			break;
		case PRIMITIVE_LINES:
			d3dPrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
			break;
	}

	if (D3D::command_list_mgr->m_current_topology != d3dPrimitiveTopology)
	{
		D3D::current_command_list->IASetPrimitiveTopology(d3dPrimitiveTopology);
		D3D::command_list_mgr->m_current_topology = d3dPrimitiveTopology;
	}

	D3D::current_command_list->DrawIndexedInstanced(indices, 1, startIndex, baseVertex, 0);

	INCSTAT(stats.thisFrame.numDrawCalls);
}

void VertexManager::vFlush(bool useDstAlpha)
{
	if (!PixelShaderCache::SetShader(
		useDstAlpha ? DSTALPHA_DUAL_SOURCE_BLEND : DSTALPHA_NONE))
	{
		GFX_DEBUGGER_PAUSE_LOG_AT(NEXT_ERROR,true,{printf("Fail to set pixel shader\n");});
		return;
	}

	if (!VertexShaderCache::SetShader())
	{
		GFX_DEBUGGER_PAUSE_LOG_AT(NEXT_ERROR,true,{printf("Fail to set vertex shader\n");});
		return;
	}

	if (!GeometryShaderCache::SetShader(current_primitive_type))
	{
		GFX_DEBUGGER_PAUSE_LOG_AT(NEXT_ERROR, true, { printf("Fail to set geometry shader\n"); });
		return;
	}

	if (g_ActiveConfig.backend_info.bSupportsBBox && BoundingBox::active)
	{
		// D3D12TODO: Support GPU-side bounding box.
		// D3D::context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr, 2, 1, &BBox::GetUAV(), nullptr);
	}

	u32 stride = VertexLoaderManager::GetCurrentVertexFormat()->GetVertexStride();

	PrepareDrawBuffers(stride);

	g_renderer->ApplyState(useDstAlpha);

	Draw(stride);

	D3D::command_list_mgr->m_draws_since_last_execution++;

	// Many Gamecube/Wii titles read from the EFB each frame to determine what new rendering work to submit, e.g. where sun rays are
	// occluded and where they aren't. When the CPU wants to read this data (done in Renderer::AccessEFB), it requires that the GPU
	// finish all oustanding work. As an optimization, when we detect that the CPU is likely to read back data this frame, we break
	// up the rendering work and submit it more frequently to the GPU (via ExecuteCommandList). Thus, when the CPU finally needs the
	// the GPU to finish all of its work, there is (hopefully) less work outstanding to wait on at that moment.

	// D3D12TODO: Decide right threshold for drawCountSinceAsyncFlush at runtime depending on 
	// amount of stall measured in AccessEFB's Flushcommand_listAndWait call.

	if (D3D::command_list_mgr->m_draws_since_last_execution > 100 && D3D::command_list_mgr->m_cpu_access_last_frame)
	{
		D3D::command_list_mgr->m_draws_since_last_execution = 0;

		D3D::command_list_mgr->ExecuteQueuedWork();

		g_renderer->SetViewport();

		D3D::current_command_list->OMSetRenderTargets(1, &FramebufferManager::GetEFBColorTexture()->GetRTV12(), FALSE, &FramebufferManager::GetEFBDepthTexture()->GetDSV12());
	}
}

u8* lastGpuVertexBufferLocation = nullptr;
UINT lastStride = 0;

void VertexManager::ResetBuffer(u32 stride)
{
	if (s_cull_all)
	{
		if (!usingCPUOnlyBuffer)
		{
			lastGpuVertexBufferLocation = s_pCurBufferPointer;
		}

		usingCPUOnlyBuffer = true;

		s_pCurBufferPointer = vertexCpuBuffer;
		s_pBaseBufferPointer = vertexCpuBuffer;
		s_pEndBufferPointer = vertexCpuBuffer + sizeof(vertexCpuBuffer);

		IndexGenerator::Start((u16*)indexCpuBuffer);
	}
	else
	{
		if (usingCPUOnlyBuffer)
		{
			s_pBaseBufferPointer = (u8*) m_vertexBufferData;
			s_pEndBufferPointer = (u8*) m_vertexBufferData + MAX_VBUFFER_SIZE;
			s_pCurBufferPointer = lastGpuVertexBufferLocation;

			usingCPUOnlyBuffer = false;
		}

		if (stride != lastStride)
		{
			lastStride = stride;
			u32 padding = (s_pCurBufferPointer - s_pBaseBufferPointer) % stride;
			if (padding)
			{
				s_pCurBufferPointer += stride - padding;
			}
		}

		if ((s_pCurBufferPointer - s_pBaseBufferPointer) + MAXVBUFFERSIZE > MAX_VBUFFER_SIZE)
		{
			s_pCurBufferPointer = s_pBaseBufferPointer;
			lastStride = 0;
		}

		s_pCurBufferPointerBeforeWrite = s_pCurBufferPointer;

		s_pIndexBufferPointer += s_lastIndexWriteSize;

		if ((s_pIndexBufferPointer - (u8*)m_indexBufferData) + MAXIBUFFERSIZE > MAX_IBUFFER_SIZE)
		{
			s_pIndexBufferPointer = (u8*)m_indexBufferData;
		}

		IndexGenerator::Start((u16*)s_pIndexBufferPointer);
	}
}

}  // namespace
