// Copyright 2014 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <d3d11.h>
#include <map>

#include "VideoCommon/GeometryShaderGen.h"

namespace DX12
{

class GeometryShaderCache
{
public:
	static void Init();
	static void Clear();
	static void Shutdown();
	static bool SetShader(u32 primitive_type); // TODO: Should be renamed to LoadShader
	static bool InsertByteCode(const GeometryShaderUid &uid, const void* bytecode, unsigned int bytecodelen);

	static D3D12_SHADER_BYTECODE GetActiveShader12() { return last_entry->shader12; }
	static GeometryShaderUid GetActiveShaderUid12() { return last_uid; }
	static D3D12_SHADER_BYTECODE GetShaderFromUid(GeometryShaderUid uid)
	{
		// This function is only called when repopulating the PSO cache from disk.
		// In this case, we know the shader already exists on disk, and has been loaded
		// into memory, thus we don't need to handle the failure case.

		GSCache::iterator iter;
		iter = GeometryShaders.find(uid);
		if (iter != GeometryShaders.end())
		{
			const GSCacheEntry &entry = iter->second;
			last_entry = &entry;

			return entry.shader12;
		}

		return D3D12_SHADER_BYTECODE();
	}

	static void GetConstantBuffer12(); // This call on D3D12 actually sets the constant buffer, no need to return it.

	static D3D12_PRIMITIVE_TOPOLOGY_TYPE GetCurrentPrimitiveTopology();

private:
	struct GSCacheEntry
	{
		D3D12_SHADER_BYTECODE shader12;

		std::string code;

		GSCacheEntry() : shader12({}) {}
		void Destroy() { SAFE_DELETE(shader12.pShaderBytecode); }
	};

	typedef std::map<GeometryShaderUid, GSCacheEntry> GSCache;

	static GSCache GeometryShaders;
	static const GSCacheEntry* last_entry;
	static GeometryShaderUid last_uid;
	static const GSCacheEntry pass_entry;

	static UidChecker<GeometryShaderUid, ShaderCode> geometry_uid_checker;
};

}  // namespace DX12
