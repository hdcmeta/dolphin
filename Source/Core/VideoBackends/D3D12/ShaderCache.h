// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "VideoCommon/GeometryShaderGen.h"
#include "VideoCommon/PixelShaderGen.h"
#include "VideoCommon/VertexShaderGen.h"

namespace DX12
{

enum SHADER_STAGE
{
	GEOMETRY_SHADER = 0,
	PIXEL_SHADER = 1,
	VERTEX_SHADER = 2,
	SHADER_STAGE_COUNT = 3
};

class ShaderCache final
{
public:
	static void Init();
	static void Clear();
	static void Shutdown();

	static bool LoadAndSetAsActiveShader(DSTALPHA_MODE dstAlphaMode);
	static bool InsertByteCode(const ShaderGeneratorInterface* uid, SHADER_STAGE stage, const void* bytecode, unsigned int bytecodelen);

	static const D3D12_SHADER_BYTECODE* GetActiveShaderBytecode(SHADER_STAGE stage);

	// The various uid flavors inherit from ShaderGeneratorInterface.
	static const ShaderGeneratorInterface* GetActiveShaderUid(SHADER_STAGE stage);
	static const D3D12_SHADER_BYTECODE* GetShaderFromUid(SHADER_STAGE stage, ShaderGeneratorInterface* uid);

private:
	typedef std::map<GeometryShaderUid, D3D12_SHADER_BYTECODE> GsBytecodeCache;
	typedef std::map<PixelShaderUid, D3D12_SHADER_BYTECODE> PsBytecodeCache;
	typedef std::map<VertexShaderUid, D3D12_SHADER_BYTECODE> VsBytecodeCache;
	static GsBytecodeCache gs_bytecode_cache;
	static PsBytecodeCache ps_bytecode_cache;
	static VsBytecodeCache vs_bytecode_cache;

	// Only used for shader debugging..
	typedef std::map<GeometryShaderUid, std::string> GsHlslCache;
	typedef std::map<PixelShaderUid, std::string> PsHlslCache;
	typedef std::map<VertexShaderUid, std::string> VsHlslCache;
	static GsHlslCache gs_hlsl_cache;
	static PsHlslCache ps_hlsl_cache;
	static VsHlslCache vs_hlsl_cache;
	
	static UidChecker<GeometryShaderUid, ShaderCode> geometry_uid_checker;
	static UidChecker<PixelShaderUid, ShaderCode> pixel_uid_checker;
	static UidChecker<VertexShaderUid, ShaderCode> vertex_uid_checker;

	static const D3D12_SHADER_BYTECODE* last_bytecode[SHADER_STAGE::SHADER_STAGE_COUNT];
	static ShaderGeneratorInterface* last_uid[SHADER_STAGE::SHADER_STAGE_COUNT];
};

}
