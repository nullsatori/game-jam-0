#include "OSGasRender.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphUtils.h"
#include "SceneTexturesConfig.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "ShaderCore.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"

class FMindKoreShadersModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// /MindKoreShaders/OSGas.usf is the project's Shaders/OSGas.usf.
		AddShaderSourceDirectoryMapping(TEXT("/MindKoreShaders"), FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders")));
	}
};

IMPLEMENT_MODULE(FMindKoreShadersModule, MindKoreShaders)

BEGIN_SHADER_PARAMETER_STRUCT(FOSGasCommonParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepth)
	SHADER_PARAMETER_TEXTURE(Texture2D<float4>, GasData)
	SHADER_PARAMETER_TEXTURE(Texture2D<float4>, GasTunnels)
	SHADER_PARAMETER_TEXTURE(Texture2D, GasGrid0)
	SHADER_PARAMETER_TEXTURE(Texture2D, GasGrid1)
	SHADER_PARAMETER_TEXTURE(Texture2D, GasGrid2)
	SHADER_PARAMETER_TEXTURE(Texture2D, GasGrid3)
	SHADER_PARAMETER_TEXTURE(Texture2D, GasGrid4)
	SHADER_PARAMETER_TEXTURE(Texture2D, GasGrid5)
	SHADER_PARAMETER_TEXTURE(Texture2D, GasGrid6)
	SHADER_PARAMETER_TEXTURE(Texture2D, GasGrid7)
	SHADER_PARAMETER_TEXTURE(Texture2D, GasGrid8)
	SHADER_PARAMETER_TEXTURE(Texture2D, GasGrid9)
	SHADER_PARAMETER_SAMPLER(SamplerState, GasSampler)
	SHADER_PARAMETER(float, GasCloudCount)
	SHADER_PARAMETER(float, GasTunnelCount)
	SHADER_PARAMETER(float, GasBlockPx)
	SHADER_PARAMETER(float, GasEyeClear)
	SHADER_PARAMETER(float, GasDebugAlpha)
	SHADER_PARAMETER(FVector3f, GasCamera)
	SHADER_PARAMETER(FIntPoint, OutMin)
	SHADER_PARAMETER(FIntPoint, OutSize)
	SHADER_PARAMETER(FIntPoint, DepthMin)
	SHADER_PARAMETER(FVector2f, DepthScale)
END_SHADER_PARAMETER_STRUCT()

class FOSGasBlockCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FOSGasBlockCS);
	SHADER_USE_PARAMETER_STRUCT(FOSGasBlockCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FOSGasCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutBlock)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutEdge)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FOSGasBlockCS, "/MindKoreShaders/OSGas.usf", "MainBlockCS", SF_Compute);

class FOSGasCompositePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FOSGasCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FOSGasCompositePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FOSGasCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColor)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, BlockResult)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BlockEdge)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FOSGasCompositePS, "/MindKoreShaders/OSGas.usf", "MainCompositePS", SF_Pixel);

FScreenPassTexture OSAddGasPasses(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs, const FOSGasRenderData& Data)
{
	const FSceneTextureUniformParameters* SceneTextures = Inputs.SceneTextures.SceneTextures ? Inputs.SceneTextures.SceneTextures->GetContents() : nullptr;
	// No clouds, nothing to draw - unless a debug view, which shows its (empty) opacity, not the scene.
	if (!SceneTextures || !SceneTextures->SceneDepthTexture || (Data.CloudCount <= 0 && Data.DebugMode <= 0.0f) || !Data.Data || !Data.Tunnels)
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	if (!SceneColor.IsValid())
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}
	RDG_EVENT_SCOPE(GraphBuilder, "OSGas");
	FRHITexture* Black = GBlackTexture->TextureRHI;
	const auto RHIOf = [Black](FTextureResource* Resource) -> FRHITexture* { return Resource && Resource->TextureRHI ? Resource->TextureRHI.GetReference() : Black; };

	// The output rect (post-upscale) and the depth's (the view's render rect).
	const FIntRect OutRect = SceneColor.ViewRect;
	// ponytail: the depth taken at the output's rect - right with DLAA (the project's: render = output resolution);
	// an upscaler below 100% would need the renderer's view rect (FViewInfo, private), the DepthScale is ready for it.
	const FIntRect DepthRect = OutRect;
	const float Block = FMath::Max(Data.BlockPx, 1.0f);
	const FIntPoint Blocks(FMath::CeilToInt(OutRect.Width() / Block) + 1, FMath::CeilToInt(OutRect.Height() / Block) + 1);

	const auto FillCommon = [&](FOSGasCommonParameters& C)
	{
		C.View = View.ViewUniformBuffer;
		C.SceneDepth = SceneTextures->SceneDepthTexture;
		C.GasData = RHIOf(Data.Data);
		C.GasTunnels = RHIOf(Data.Tunnels);
		FRHITexture** Grids[] = { &C.GasGrid0, &C.GasGrid1, &C.GasGrid2, &C.GasGrid3, &C.GasGrid4, &C.GasGrid5, &C.GasGrid6, &C.GasGrid7, &C.GasGrid8, &C.GasGrid9 };
		for (int32 Index = 0; Index < FOSGasRenderData::MaxGrids; ++Index)
		{
			*Grids[Index] = RHIOf(Data.Grids[Index]);
		}
		C.GasSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		C.GasCloudCount = float(Data.CloudCount);
		C.GasTunnelCount = float(Data.TunnelCount);
		C.GasBlockPx = Block;
		C.GasEyeClear = Data.EyeClear;
		C.GasDebugAlpha = Data.DebugMode;
		C.GasCamera = FVector3f(View.ViewMatrices.GetViewOrigin());
		C.OutMin = OutRect.Min;
		C.OutSize = OutRect.Size();
		C.DepthMin = DepthRect.Min;
		C.DepthScale = FVector2f(float(DepthRect.Width()) / FMath::Max(OutRect.Width(), 1), float(DepthRect.Height()) / FMath::Max(OutRect.Height(), 1));
	};

	FRDGTextureRef BlockResult = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Blocks, PF_FloatRGBA, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("OSGas.Block"));
	FRDGTextureRef BlockEdge = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Blocks, PF_R32_FLOAT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("OSGas.Edge"));
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	{
		FOSGasBlockCS::FParameters* P = GraphBuilder.AllocParameters<FOSGasBlockCS::FParameters>();
		FillCommon(P->Common);
		P->OutBlock = GraphBuilder.CreateUAV(BlockResult);
		P->OutEdge = GraphBuilder.CreateUAV(BlockEdge);
		TShaderMapRef<FOSGasBlockCS> Shader(ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("OSGas.Blocks %dx%d", Blocks.X, Blocks.Y), Shader, P, FComputeShaderUtils::GetGroupCount(Blocks, FIntPoint(8, 8)));
	}
	FScreenPassRenderTarget Output = Inputs.OverrideOutput;
	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, SceneColor, View.GetOverwriteLoadAction(), TEXT("OSGas.Output"));
	}
	{
		FOSGasCompositePS::FParameters* P = GraphBuilder.AllocParameters<FOSGasCompositePS::FParameters>();
		FillCommon(P->Common);
		P->SceneColor = SceneColor.Texture;
		P->SceneColorSampler = TStaticSamplerState<SF_Point>::GetRHI();
		P->BlockResult = BlockResult;
		P->BlockEdge = BlockEdge;
		P->RenderTargets[0] = Output.GetRenderTargetBinding();
		TShaderMapRef<FScreenPassVS> VS(ShaderMap);
		TShaderMapRef<FOSGasCompositePS> PS(ShaderMap);
		AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("OSGas.Composite"), FScreenPassViewInfo(View), FScreenPassTextureViewport(Output), FScreenPassTextureViewport(SceneColor), VS, PS, P);
	}
	return FScreenPassTexture(Output);
}
