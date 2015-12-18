// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <d3d11.h>
#include <map>

#include "VideoCommon/PixelShaderGen.h"

enum DSTALPHA_MODE;

namespace DX12
{

class PixelShaderCache
{
public:
	static void Init();
	static void Clear();
	static void Shutdown();
	static bool SetShader(DSTALPHA_MODE dstAlphaMode); // TODO: Should be renamed to LoadShader
	static bool InsertByteCode(const PixelShaderUid &uid, const void* bytecode, unsigned int bytecodelen);

	static D3D12_SHADER_BYTECODE GetActiveShader12() { return last_entry->shaderDesc; }
	static PixelShaderUid GetActiveShaderUid12() { return last_uid; }
	static D3D12_SHADER_BYTECODE GetShaderFromUid(PixelShaderUid uid)
	{
		// This function is only called when repopulating the PSO cache from disk.
		// In this case, we know the shader already exists on disk, and has been loaded
		// into memory, thus we don't need to handle the failure case.

		PSCache::iterator iter;
		iter = PixelShaders.find(uid);
		if (iter != PixelShaders.end())
		{
			const PSCacheEntry &entry = iter->second;
			last_entry = &entry;

			return entry.shaderDesc;
		}

		return D3D12_SHADER_BYTECODE();
	}

	static void GetConstantBuffer12(); // Does not return a buffer, but actually binds the constant data.

	static D3D12_SHADER_BYTECODE GetColorMatrixProgram12(bool multisampled);
	static D3D12_SHADER_BYTECODE GetColorCopyProgram12(bool multisampled);
	static D3D12_SHADER_BYTECODE GetDepthMatrixProgram12(bool multisampled);
	static D3D12_SHADER_BYTECODE GetClearProgram12();
	static D3D12_SHADER_BYTECODE GetAnaglyphProgram12();
	static D3D12_SHADER_BYTECODE ReinterpRGBA6ToRGB812(bool multisampled);
	static D3D12_SHADER_BYTECODE ReinterpRGB8ToRGBA612(bool multisampled);

	static void InvalidateMSAAShaders();

private:
	struct PSCacheEntry
	{
		D3D12_SHADER_BYTECODE shaderDesc;

		std::string code;

		PSCacheEntry() {}
		void Destroy() {
			SAFE_DELETE(shaderDesc.pShaderBytecode);
		}
	};

	typedef std::map<PixelShaderUid, PSCacheEntry> PSCache;

	static PSCache PixelShaders;
	static const PSCacheEntry* last_entry;
	static PixelShaderUid last_uid;

	static UidChecker<PixelShaderUid,ShaderCode> pixel_uid_checker;
};

}  // namespace DX12
