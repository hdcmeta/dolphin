// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <string>

#include "Common/FileUtil.h"
#include "Common/LinearDiskCache.h"
#include "Common/StringUtil.h"

#include "Core/ConfigManager.h"

#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DCommandListManager.h"
#include "VideoBackends/D3D12/D3DDescriptorHeapManager.h"
#include "VideoBackends/D3D12/D3DShader.h"
#include "VideoBackends/D3D12/PixelShaderCache.h"

#include "VideoCommon/Debugger.h"
#include "VideoCommon/PixelShaderGen.h"
#include "VideoCommon/PixelShaderManager.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/VideoConfig.h"

namespace DX12
{
ID3D12Resource* pscbuf12 = nullptr;
D3D12_GPU_VIRTUAL_ADDRESS pscbuf12GPUVA = {};
void* pscbuf12data = nullptr;
const UINT pscbuf12paddedSize = (sizeof(PixelShaderConstants) + 0xff) & ~0xff;

#define pscbuf12Slots 10000
unsigned int current_psc_buf12 = 0; // 0 - pscbuf12Slots;

void PixelShaderCache::GetConstantBuffer12()
{
	if (PixelShaderManager::dirty)
	{
		//current_psc_buf12 = (current_psc_buf12 + 1) % pscbuf12Slots;
		current_psc_buf12 += pscbuf12paddedSize;

		memcpy((u8*)pscbuf12data + current_psc_buf12, &PixelShaderManager::constants, sizeof(PixelShaderConstants));

		PixelShaderManager::dirty = false;

		ADDSTAT(stats.thisFrame.bytesUniformStreamed, sizeof(PixelShaderConstants));

		D3D::command_list_mgr->m_dirty_ps_cbv = true;
	}

	if (D3D::command_list_mgr->m_dirty_ps_cbv)
	{
		D3D::current_command_list->SetGraphicsRootConstantBufferView(
			DESCRIPTOR_TABLE_PS_CBVONE,
			pscbuf12GPUVA + current_psc_buf12
			);

		D3D::command_list_mgr->m_dirty_ps_cbv = false;
	}
}

void PixelShaderCache::Init()
{
	unsigned int pscbuf12sizeInBytes = pscbuf12paddedSize * pscbuf12Slots;
	CheckHR(D3D::device12->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(pscbuf12sizeInBytes), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&pscbuf12)));
	D3D::SetDebugObjectName12(pscbuf12, "pixel shader constant buffer used to emulate the GX pipeline");

	// Obtain persistent CPU pointer to PS Constant Buffer
	CheckHR(pscbuf12->Map(0, nullptr, &pscbuf12data));

	// Obtain GPU VA for buffer, used at binding time.
	pscbuf12GPUVA = pscbuf12->GetGPUVirtualAddress();
}

void PixelShaderCache::Shutdown()
{
	D3D::command_list_mgr->DestroyResourceAfterCurrentCommandListExecuted(pscbuf12);
}

}  // DX12
