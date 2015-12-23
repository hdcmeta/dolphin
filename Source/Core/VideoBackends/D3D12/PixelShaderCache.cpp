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

PixelShaderCache::PSCache PixelShaderCache::PixelShaders;
const PixelShaderCache::PSCacheEntry* PixelShaderCache::last_entry;
PixelShaderUid PixelShaderCache::last_uid = {};
UidChecker<PixelShaderUid,ShaderCode> PixelShaderCache::pixel_uid_checker;

LinearDiskCache<PixelShaderUid, u8> g_ps_disk_cache;

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

// this class will load the precompiled shaders into our cache
class PixelShaderCacheInserter : public LinearDiskCacheReader<PixelShaderUid, u8>
{
public:
	void Read(const PixelShaderUid &key, const u8* value, u32 value_size)
	{
		PixelShaderCache::InsertByteCode(key, value, value_size);
	}
};

void PixelShaderCache::Init()
{
	unsigned int pscbuf12sizeInBytes = pscbuf12paddedSize * pscbuf12Slots;
	CheckHR(D3D::device12->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(pscbuf12sizeInBytes), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&pscbuf12)));
	D3D::SetDebugObjectName12(pscbuf12, "pixel shader constant buffer used to emulate the GX pipeline");

	// Obtain persistent CPU pointer to PS Constant Buffer
	CheckHR(pscbuf12->Map(0, nullptr, &pscbuf12data));

	// Obtain GPU VA for buffer, used at binding time.
	pscbuf12GPUVA = pscbuf12->GetGPUVirtualAddress();

	Clear();

	if (!File::Exists(File::GetUserPath(D_SHADERCACHE_IDX)))
		File::CreateDir(File::GetUserPath(D_SHADERCACHE_IDX));

	SETSTAT(stats.numPixelShadersCreated, 0);
	SETSTAT(stats.numPixelShadersAlive, 0);

	// Intentionally share the same cache as DX11, as the shaders are identical. Reduces recompilation when switching APIs.
	std::string cache_filename = StringFromFormat("%sdx11-%s-ps.cache", File::GetUserPath(D_SHADERCACHE_IDX).c_str(),
			SConfig::GetInstance().m_strUniqueID.c_str());
	PixelShaderCacheInserter inserter;
	g_ps_disk_cache.OpenAndRead(cache_filename, inserter);

	if (g_Config.bEnableShaderDebugging)
		Clear();

	last_entry = nullptr;
	last_uid = {};
}

// ONLY to be used during shutdown.
void PixelShaderCache::Clear()
{
	for (auto& iter : PixelShaders)
	{
		iter.second.Destroy();
		delete iter.second.shaderDesc.pShaderBytecode;
	}

	PixelShaders.clear();
	pixel_uid_checker.Invalidate();

	last_entry = nullptr;
}

void PixelShaderCache::Shutdown()
{
	D3D::command_list_mgr->DestroyResourceAfterCurrentCommandListExecuted(pscbuf12);

	Clear();
	g_ps_disk_cache.Sync();
	g_ps_disk_cache.Close();
}

bool PixelShaderCache::SetShader(DSTALPHA_MODE dstAlphaMode)
{
	PixelShaderUid uid = GetPixelShaderUid(dstAlphaMode, API_D3D);

	// Check if the shader is already set
	if (uid == last_uid)
	{
		GFX_DEBUGGER_PAUSE_AT(NEXT_PIXEL_SHADER_CHANGE,true);
		return (last_entry->shaderDesc.pShaderBytecode != nullptr);
	}

	last_uid = uid;
	D3D::command_list_mgr->m_dirty_pso = true;

	if (g_ActiveConfig.bEnableShaderDebugging)
	{
		ShaderCode code = GeneratePixelShaderCode(dstAlphaMode, API_D3D);
		pixel_uid_checker.AddToIndexAndCheck(code, uid, "Pixel", "p");
	}

	// Check if the shader is already in the cache
	PSCache::iterator iter;
	iter = PixelShaders.find(uid);
	if (iter != PixelShaders.end())
	{
		const PSCacheEntry &entry = iter->second;
		last_entry = &entry;

		GFX_DEBUGGER_PAUSE_AT(NEXT_PIXEL_SHADER_CHANGE,true);
		return (entry.shaderDesc.pShaderBytecode != nullptr);
	}

	// Need to compile a new shader
	ShaderCode code = GeneratePixelShaderCode(dstAlphaMode, API_D3D);

	D3DBlob* pbytecode;
	if (!D3D::CompilePixelShader(code.GetBuffer(), &pbytecode))
	{
		GFX_DEBUGGER_PAUSE_AT(NEXT_ERROR, true);
		return false;
	}

	// Insert the bytecode into the caches
	g_ps_disk_cache.Append(uid, pbytecode->Data(), pbytecode->Size());

	bool success = InsertByteCode(uid, pbytecode->Data(), pbytecode->Size());
	pbytecode->Release();

	if (g_ActiveConfig.bEnableShaderDebugging && success)
	{
		PixelShaders[uid].code = code.GetBuffer();
	}

	GFX_DEBUGGER_PAUSE_AT(NEXT_PIXEL_SHADER_CHANGE, true);
	return success;
}

bool PixelShaderCache::InsertByteCode(const PixelShaderUid &uid, const void* bytecode, unsigned int bytecodelen)
{
	// Make an entry in the table
	PSCacheEntry newentry;

	// In D3D12, shader bytecode is needed at Pipeline State creation time, so make a copy (as LinearDiskCache frees original after load).
	newentry.shaderDesc.BytecodeLength = bytecodelen;
	newentry.shaderDesc.pShaderBytecode = new u8[bytecodelen];
	memcpy(const_cast<void*>(newentry.shaderDesc.pShaderBytecode), bytecode, bytecodelen);

	PixelShaders[uid] = newentry;
	last_entry = &PixelShaders[uid];

	if (!bytecode)
	{
		// INCSTAT(stats.numPixelShadersFailed);
		return false;
	}

	INCSTAT(stats.numPixelShadersCreated);
	SETSTAT(stats.numPixelShadersAlive, PixelShaders.size());
	return true;
}

}  // DX12
