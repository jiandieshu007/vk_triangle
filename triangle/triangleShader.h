#pragma once

#include "CoreMinimal.h"

#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"


class FTriangleVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTriangleVS);

	SHADER_USE_PARAMETER_STRUCT(FTriangleVS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FShaderVS)
	SHADER_PARAMETER(FMatrix44f, MVP)
	RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
};

class FTrianglePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTrianglePS);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
};

IMPLEMENT_GLOBAL_SHADER(FTriangleVS, "../shader/triangle.usf", "VS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FTrianglePS, "../shader/triangle.usf", "PS", SF_Pixel);