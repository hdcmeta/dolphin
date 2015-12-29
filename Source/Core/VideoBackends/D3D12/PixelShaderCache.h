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
	static void Shutdown();

	static void GetConstantBuffer12(); // Does not return a buffer, but actually binds the constant data.
};

}  // namespace DX12
