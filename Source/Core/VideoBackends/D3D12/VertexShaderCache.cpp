// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <string>

#include "Common/FileUtil.h"
#include "Common/LinearDiskCache.h"
#include "Common/StringUtil.h"

#include "Core/ConfigManager.h"

#include "VideoBackends/D3D12/D3DCommandListManager.h"
#include "VideoBackends/D3D12/D3DShader.h"
#include "VideoBackends/D3D12/D3DUtil.h"
#include "VideoBackends/D3D12/VertexShaderCache.h"

#include "VideoCommon/Debugger.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/VertexShaderGen.h"
#include "VideoCommon/VertexShaderManager.h"

namespace DX12 {

ID3D12Resource* vscbuf12 = nullptr;
D3D12_GPU_VIRTUAL_ADDRESS vscbuf12GPUVA = {};

void* vscbuf12data = nullptr;
const UINT vscbuf12paddedSize = (sizeof(VertexShaderConstants) + 0xff) & ~0xff;

#define vscbuf12Slots 10000
UINT current_vsc_buf12 = 0; // 0 - vscbuf12Slots;

void VertexShaderCache::GetConstantBuffer12()
{
	if (VertexShaderManager::dirty)
	{
		// Overflow handled in D3DCommandListManager.
		current_vsc_buf12 += vscbuf12paddedSize;

		memcpy((u8*)vscbuf12data + current_vsc_buf12, &VertexShaderManager::constants, sizeof(VertexShaderConstants));

		VertexShaderManager::dirty = false;

		ADDSTAT(stats.thisFrame.bytesUniformStreamed, sizeof(VertexShaderConstants));

		D3D::current_command_list->SetGraphicsRootConstantBufferView(
			DESCRIPTOR_TABLE_VS_CBV,
			vscbuf12GPUVA + current_vsc_buf12
			);

		if (g_ActiveConfig.bEnablePixelLighting)
			D3D::current_command_list->SetGraphicsRootConstantBufferView(
			DESCRIPTOR_TABLE_PS_CBVTWO,
			vscbuf12GPUVA + current_vsc_buf12
			);

		D3D::command_list_mgr->m_dirty_vs_cbv = false;
	}
	else if (D3D::command_list_mgr->m_dirty_vs_cbv)
	{
		D3D::current_command_list->SetGraphicsRootConstantBufferView(
			DESCRIPTOR_TABLE_VS_CBV,
			vscbuf12GPUVA + current_vsc_buf12
			);

		if (g_ActiveConfig.bEnablePixelLighting)
			D3D::current_command_list->SetGraphicsRootConstantBufferView(
				DESCRIPTOR_TABLE_PS_CBVTWO,
				vscbuf12GPUVA + current_vsc_buf12
				);

		D3D::command_list_mgr->m_dirty_vs_cbv = false;
	}
}

void VertexShaderCache::Init()
{
	unsigned int vscbuf12sizeInBytes = vscbuf12paddedSize * vscbuf12Slots;

	CheckHR(
		D3D::device12->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(vscbuf12sizeInBytes),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&vscbuf12)
			)
		);

	D3D::SetDebugObjectName12(vscbuf12, "vertex shader constant buffer used to emulate the GX pipeline");

	// Obtain persistent CPU pointer to PS Constant Buffer
	CheckHR(vscbuf12->Map(0, nullptr, &vscbuf12data));

	// Obtain GPU VA for buffer, used at binding time.
	vscbuf12GPUVA = vscbuf12->GetGPUVirtualAddress();
}

void VertexShaderCache::Shutdown()
{
	D3D::command_list_mgr->DestroyResourceAfterCurrentCommandListExecuted(vscbuf12);
}

}  // namespace DX12
