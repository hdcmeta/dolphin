// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <map>

#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DBlob.h"

#include "VideoCommon/VertexShaderGen.h"

namespace DX12 {

class VertexShaderCache
{
public:
	static void Init();
	static void Clear();
	static void Shutdown();
	static bool SetShader(); // TODO: Should be renamed to LoadShader

	static D3D12_SHADER_BYTECODE GetActiveShader12() { return last_entry->shader12; }
	static VertexShaderUid GetActiveShaderUid12() { return last_uid; }
	static D3D12_SHADER_BYTECODE GetShaderFromUid(VertexShaderUid uid)
	{
		// This function is only called when repopulating the PSO cache from disk.
		// In this case, we know the shader already exists on disk, and has been loaded
		// into memory, thus we don't need to handle the failure case.

		VSCache::iterator iter;
		iter = vshaders.find(uid);
		if (iter != vshaders.end())
		{
			const VSCacheEntry &entry = iter->second;
			last_entry = &entry;

			return entry.shader12;
		}

		return D3D12_SHADER_BYTECODE();
	}

	static void GetConstantBuffer12();

	static bool VertexShaderCache::InsertByteCode(const VertexShaderUid &uid, D3DBlob* bcodeblob);

private:
	struct VSCacheEntry
	{
		D3D12_SHADER_BYTECODE shader12;

		D3DBlob* bytecode; // needed to initialize the input layout

		std::string code;

		VSCacheEntry() :
			bytecode(nullptr) {}
		void SetByteCode(D3DBlob* blob)
		{
			SAFE_RELEASE(bytecode);
			bytecode = blob;
			blob->AddRef();
		}
		void Destroy()
		{
			SAFE_RELEASE(bytecode);
		}
	};
	typedef std::map<VertexShaderUid, VSCacheEntry> VSCache;

	static VSCache vshaders;
	static const VSCacheEntry* last_entry;
	static VertexShaderUid last_uid;

	static UidChecker<VertexShaderUid,ShaderCode> vertex_uid_checker;
};

}  // namespace DX12
