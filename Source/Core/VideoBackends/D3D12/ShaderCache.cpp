// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.


#include "VideoBackends/D3D12/D3DShader.h"
#include "VideoBackends/D3D12/ShaderCache.h"

#include "VideoCommon/GeometryShaderGen.h"
#include "VideoCommon/PixelShaderGen.h"
#include "VideoCommon/VertexShaderGen.h"

namespace DX12
{
	void ShaderCache::Init()
	{

	}

	void ShaderCache::Clear()
	{

	}

	void ShaderCache::Shutdown()
	{

	}

	bool ShaderCache::LoadAndSetAsActiveShader(DSTALPHA_MODE dstAlphaMode)
	{

		return true;
	}

	bool ShaderCache::InsertByteCode(const ShaderGeneratorInterface* uid, SHADER_STAGE stage, const void* bytecode, unsigned int bytecodelen)
	{

		return true;
	}

	const D3D12_SHADER_BYTECODE* ShaderCache::GetActiveShaderBytecode(SHADER_STAGE stage)
	{
		return last_bytecode[stage];
	}

	const ShaderGeneratorInterface* ShaderCache::GetActiveShaderUid(SHADER_STAGE stage)
	{
		return last_uid[stage];
	}

	const D3D12_SHADER_BYTECODE* ShaderCache::GetShaderFromUid(SHADER_STAGE stage, ShaderGeneratorInterface* uid)
	{
		switch (stage)
		{
		case SHADER_STAGE::GEOMETRY_SHADER:
			return &gs_bytecode_cache[*reinterpret_cast<GeometryShaderUid*>(uid)];
		case SHADER_STAGE::PIXEL_SHADER:
			return &ps_bytecode_cache[*reinterpret_cast<PixelShaderUid*>(uid)];
		case SHADER_STAGE::VERTEX_SHADER:
			return &vs_bytecode_cache[*reinterpret_cast<VertexShaderUid*>(uid)];
		default:
			CHECK(0, "Invalid shader stage specified.");
			return nullptr;
		}
	}
}