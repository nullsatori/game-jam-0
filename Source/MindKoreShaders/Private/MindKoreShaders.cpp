#include "OSGasRender.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphUtils.h"
#include "SystemTextures.h"
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
	SHADER_PARAMETER(float, GasLight)
	SHADER_PARAMETER(float, GasAmbientScale)
	SHADER_PARAMETER(float, GasLightScale)
	SHADER_PARAMETER(float, GasAlbedo)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GasAmbient)
	SHADER_PARAMETER_SAMPLER(SamplerState, GasAmbientSampler)
	SHADER_PARAMETER(float, GasForward)
	SHADER_PARAMETER(float, GasFilmSlope)
	SHADER_PARAMETER(float, GasFilmToe)
	SHADER_PARAMETER(float, GasFilmShoulder)
	SHADER_PARAMETER(float, GasFilmBlackClip)
	SHADER_PARAMETER(float, GasFilmWhiteClip)
END_SHADER_PARAMETER_STRUCT()

/** The room's light (stage 6): the frame before the gas to 1/32, weighted by depth. */
class FOSGasAmbientDownCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FOSGasAmbientDownCS);
	SHADER_USE_PARAMETER_STRUCT(FOSGasAmbientDownCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AmbientSceneColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AmbientDepth)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, AmbientOut)
		SHADER_PARAMETER(FIntPoint, AmbientSize)
		SHADER_PARAMETER(FIntPoint, OutMin)
		SHADER_PARAMETER(FIntPoint, OutSize)
		SHADER_PARAMETER(FIntPoint, DepthMin)
		SHADER_PARAMETER(FVector2f, DepthScale)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FOSGasAmbientDownCS, "/MindKoreShaders/OSGas.usf", "AmbientDownCS", SF_Compute);

/** A blur one way; the second into the last frame's (AmbientBlend >= 0). */
class FOSGasAmbientBlurCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FOSGasAmbientBlurCS);
	SHADER_USE_PARAMETER_STRUCT(FOSGasAmbientBlurCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, AmbientIn)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, AmbientHistory)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, AmbientOut)
		SHADER_PARAMETER(FIntPoint, AmbientSize)
		SHADER_PARAMETER(FIntPoint, AmbientDir)
		SHADER_PARAMETER(float, AmbientBlend)
		SHADER_PARAMETER(float, AmbientFloor)
		SHADER_PARAMETER(float, AmbientHasHistory)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FOSGasAmbientBlurCS, "/MindKoreShaders/OSGas.usf", "AmbientBlurCS", SF_Compute);

class FOSGasBlockCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FOSGasBlockCS);
	SHADER_USE_PARAMETER_STRUCT(FOSGasBlockCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FOSGasCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutBlock)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutEdge)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutLamp)
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
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, BlockLamp)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FOSGasCompositePS, "/MindKoreShaders/OSGas.usf", "MainCompositePS", SF_Pixel);

FScreenPassTexture OSAddGasPasses(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs, const FOSGasRenderData& Data,
	TRefCountPtr<IPooledRenderTarget>& AmbientHistory)
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

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	// The room's light (os.Gas.Light 1): the frame before the gas, 1/32, blurred each way, into the last frame's.
	const FIntPoint AmbientSize(FMath::Max(1, FMath::DivideAndRoundUp(OutRect.Width(), 32)), FMath::Max(1, FMath::DivideAndRoundUp(OutRect.Height(), 32)));
	FRDGTextureRef Ambient = GSystemTextures.GetBlackDummy(GraphBuilder);
	if (Data.Light > 0.5f)
	{
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(AmbientSize, PF_FloatRGBA, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef Down = GraphBuilder.CreateTexture(Desc, TEXT("OSGas.AmbientDown"));
		FRDGTextureRef Across = GraphBuilder.CreateTexture(Desc, TEXT("OSGas.AmbientAcross"));
		Ambient = GraphBuilder.CreateTexture(Desc, TEXT("OSGas.Ambient"));
		{
			FOSGasAmbientDownCS::FParameters* P = GraphBuilder.AllocParameters<FOSGasAmbientDownCS::FParameters>();
			P->View = View.ViewUniformBuffer;
			P->AmbientSceneColor = SceneColor.Texture;
			P->AmbientDepth = SceneTextures->SceneDepthTexture;
			P->AmbientOut = GraphBuilder.CreateUAV(Down);
			P->AmbientSize = AmbientSize;
			P->OutMin = OutRect.Min;
			P->OutSize = OutRect.Size();
			P->DepthMin = DepthRect.Min;
			P->DepthScale = FVector2f(float(DepthRect.Width()) / FMath::Max(OutRect.Width(), 1), float(DepthRect.Height()) / FMath::Max(OutRect.Height(), 1));
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("OSGas.AmbientDown %dx%d", AmbientSize.X, AmbientSize.Y), TShaderMapRef<FOSGasAmbientDownCS>(ShaderMap), P,
				FComputeShaderUtils::GetGroupCount(AmbientSize, FIntPoint(8, 8)));
		}
		const bool bHistory = AmbientHistory.IsValid() && AmbientHistory->GetDesc().Extent == AmbientSize;
		FRDGTextureRef History = bHistory ? GraphBuilder.RegisterExternalTexture(AmbientHistory) : GSystemTextures.GetBlackDummy(GraphBuilder);
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			FOSGasAmbientBlurCS::FParameters* P = GraphBuilder.AllocParameters<FOSGasAmbientBlurCS::FParameters>();
			P->AmbientIn = Pass == 0 ? Down : Across;
			P->AmbientHistory = History;
			P->AmbientOut = GraphBuilder.CreateUAV(Pass == 0 ? Across : Ambient);
			P->AmbientSize = AmbientSize;
			P->AmbientDir = Pass == 0 ? FIntPoint(1, 0) : FIntPoint(0, 1);
			P->AmbientBlend = Pass == 0 ? -1.0f : 1.0f - FMath::Exp(-FMath::Max(Data.DeltaSeconds, 0.0f) / 0.1f);
			P->AmbientFloor = 0.02f;
			P->AmbientHasHistory = bHistory ? 1.0f : 0.0f;
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("OSGas.AmbientBlur"), TShaderMapRef<FOSGasAmbientBlurCS>(ShaderMap), P,
				FComputeShaderUtils::GetGroupCount(AmbientSize, FIntPoint(8, 8)));
		}
		GraphBuilder.QueueTextureExtraction(Ambient, &AmbientHistory);
	}

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
		C.GasLight = Data.Light;
		C.GasAmbientScale = Data.AmbientScale;
		C.GasLightScale = Data.LightScale;
		C.GasAlbedo = Data.Albedo;
		C.GasAmbient = Ambient;
		C.GasAmbientSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		C.GasForward = Data.Forward;
		const FFinalPostProcessSettings& Film = View.FinalPostProcessSettings;
		C.GasFilmSlope = Film.FilmSlope;
		C.GasFilmToe = Film.FilmToe;
		C.GasFilmShoulder = Film.FilmShoulder;
		C.GasFilmBlackClip = Film.FilmBlackClip;
		C.GasFilmWhiteClip = Film.FilmWhiteClip;
	};

	FRDGTextureRef BlockResult = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Blocks, PF_FloatRGBA, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("OSGas.Block"));
	FRDGTextureRef BlockLamp = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Blocks, PF_FloatRGBA, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("OSGas.BlockLamp"));
	FRDGTextureRef BlockEdge = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Blocks, PF_R32_FLOAT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("OSGas.Edge"));
	{
		FOSGasBlockCS::FParameters* P = GraphBuilder.AllocParameters<FOSGasBlockCS::FParameters>();
		FillCommon(P->Common);
		P->OutBlock = GraphBuilder.CreateUAV(BlockResult);
		P->OutEdge = GraphBuilder.CreateUAV(BlockEdge);
		P->OutLamp = GraphBuilder.CreateUAV(BlockLamp);
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
		P->BlockLamp = BlockLamp;
		P->RenderTargets[0] = Output.GetRenderTargetBinding();
		TShaderMapRef<FScreenPassVS> VS(ShaderMap);
		TShaderMapRef<FOSGasCompositePS> PS(ShaderMap);
		AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("OSGas.Composite"), FScreenPassViewInfo(View), FScreenPassTextureViewport(Output), FScreenPassTextureViewport(SceneColor), VS, PS, P);
	}
	return FScreenPassTexture(Output);
}
