// Copyright 2016-2018 Chris Conway (Koderz). All Rights Reserved.

#include "RuntimeMeshComponentProxy.h"
#include "RuntimeMeshComponentPlugin.h"
#include "RuntimeMeshComponent.h"
#include "RuntimeMeshProxy.h"
#include "PhysicsEngine/BodySetup.h"
#include "TessellationRendering.h"
#include "Engine/Public/PrimitiveSceneProxy.h"
#include "Materials/Material.h"

FRuntimeMeshComponentSceneProxy::FRuntimeMeshComponentSceneProxy(URuntimeMeshComponent* Component) 
	: FPrimitiveSceneProxy(Component)
	, BodySetup(Component->GetBodySetup())
{

	const auto FeatureLevel = GetScene().GetFeatureLevel();
	// We always use local vertex factory, which gets its primitive data from GPUScene, so we can skip expensive primitive uniform buffer updates
	bVFRequiresPrimitiveUniformBuffer = !UseGPUScene(GMaxRHIShaderPlatform, FeatureLevel);
	bStaticElementsAlwaysUseProxyPrimitiveUniformBuffer = true;

	check(Component->GetRuntimeMesh() != nullptr);

	RuntimeMeshProxy = Component->GetRuntimeMesh()->EnsureProxyCreated(GetScene().GetFeatureLevel());

	// Setup our material map


	for (auto SectionId : Component->GetRuntimeMesh()->GetSectionIds())
	{
		UMaterialInterface* Mat = Component->GetMaterial(SectionId);
		if (Mat == nullptr)
		{
			Mat = UMaterial::GetDefaultMaterial(MD_Surface);
		}

		SectionRenderData.Add(SectionId, FRuntimeMeshSectionRenderData{ Mat, false });

		MaterialRelevance |= Mat->GetRelevance(GetScene().GetFeatureLevel());
	}
}

FRuntimeMeshComponentSceneProxy::~FRuntimeMeshComponentSceneProxy()
{

}

void FRuntimeMeshComponentSceneProxy::CreateRenderThreadResources()
{
	RuntimeMeshProxy->CalculateViewRelevance(bHasStaticSections, bHasDynamicSections, bHasShadowableSections);

	for (auto& Entry : SectionRenderData)
	{
		if (RuntimeMeshProxy->GetSections().Contains(Entry.Key))
		{
			FRuntimeMeshSectionProxyPtr Section = RuntimeMeshProxy->GetSections()[Entry.Key];

			auto& RenderData = SectionRenderData[Entry.Key];

			RenderData.bWantsAdjacencyInfo = RequiresAdjacencyInformation(RenderData.Material, RuntimeMeshProxy->GetSections()[Entry.Key]->GetVertexFactory()->GetType(), GetScene().GetFeatureLevel());
		}
	}

	FPrimitiveSceneProxy::CreateRenderThreadResources();
}

FPrimitiveViewRelevance FRuntimeMeshComponentSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	//FPrimitiveViewRelevance Result;
	//Result.bDrawRelevance = IsShown(View);
	//Result.bShadowRelevance = IsShadowCast(View);

	//bool bForceDynamicPath = !IsStaticPathAvailable() || IsRichView(*View->Family) || IsSelected() || View->Family->EngineShowFlags.Wireframe;
	//Result.bStaticRelevance = !bForceDynamicPath && bHasStaticSections;
	//Result.bDynamicRelevance = bForceDynamicPath || bHasDynamicSections;

	//Result.bRenderInMainPass = ShouldRenderInMainPass();
	//Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	//Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	//MaterialRelevance.SetPrimitiveViewRelevance(Result);
	//return Result;

	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bShadowRelevance = IsShadowCast(View);

	bool bForceDynamicPath = !IsStaticPathAvailable() || IsRichView(*View->Family) || IsSelected() || View->Family->EngineShowFlags.Wireframe;
	Result.bStaticRelevance = !bForceDynamicPath && bHasStaticSections;
	Result.bDynamicRelevance = bForceDynamicPath || bHasDynamicSections;

	Result.bRenderInMainPass = ShouldRenderInMainPass();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	MaterialRelevance.SetPrimitiveViewRelevance(Result);
	Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
#if ENGINE_MAJOR_VERSION >= 4 && ENGINE_MINOR_VERSION >= 25
	Result.bVelocityRelevance = IsMovable() && Result.bOpaque && Result.bRenderInMainPass;
#else
	Result.bVelocityRelevance = IsMovable() && Result.bOpaqueRelevance && Result.bRenderInMainPass;
#endif
	return Result;
}

void FRuntimeMeshComponentSceneProxy::CreateMeshBatch(FMeshBatch& MeshBatch, const FRuntimeMeshSectionProxyPtr& Section, const FRuntimeMeshSectionRenderData& RenderData, FMaterialRenderProxy* Material, FMaterialRenderProxy* WireframeMaterial) const
{
	bool bRenderWireframe = WireframeMaterial != nullptr;
	bool bWantsAdjacency = !bRenderWireframe && RenderData.bWantsAdjacencyInfo;
	bool bForRayTracing = false;


	Section->CreateMeshBatch(MeshBatch, bWantsAdjacency);

	//gallon fixes lol
	const FRuntimeMeshIndexBuffer* CurrentIndexBuffer =
		(bWantsAdjacency ?
			Section->GetAdjacencyIndexBuffer() :
			Section->GetIndexBuffer());


	int32 NumIndicesPerTriangle = bWantsAdjacency ? 12 : 3;
	int32 NumPrimitives = CurrentIndexBuffer->Num() / NumIndicesPerTriangle;
	MeshBatch.VertexFactory = Section->GetVertexFactory();


	MeshBatch.bWireframe = WireframeMaterial != nullptr;

	MeshBatch.MaterialRenderProxy = MeshBatch.bWireframe ? WireframeMaterial : Material;
	MeshBatch.ReverseCulling = IsLocalToWorldDeterminantNegative();

	MeshBatch.DepthPriorityGroup = SDPG_World;
	MeshBatch.bCanApplyViewModeOverrides = true;


	//timur-losev fixes starting at 4.23
	FMeshBatchElement& BatchElement = MeshBatch.Elements[0];
	BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
	BatchElement.IndexBuffer = CurrentIndexBuffer;
	BatchElement.FirstIndex = 0;
	BatchElement.NumPrimitives = NumPrimitives;
	BatchElement.MinVertexIndex = 0;
	BatchElement.MaxVertexIndex = Section->GetPositionBuffer()->Num() - 1;

	//if (!bForRayTracing)
	//{
	//	BatchElement.MaxScreenSize = 1.0f;
	//	BatchElement.MinScreenSize = .01f;
	//}
}

void FRuntimeMeshComponentSceneProxy::DrawStaticElements(FStaticPrimitiveDrawInterface* PDI)
{
	for (const auto& SectionEntry : RuntimeMeshProxy->GetSections())
	{
		FRuntimeMeshSectionProxyPtr Section = SectionEntry.Value;
		if (SectionRenderData.Contains(SectionEntry.Key) && Section.IsValid() && Section->ShouldRender() && Section->WantsToRenderInStaticPath())
		{
			const FRuntimeMeshSectionRenderData& RenderData = SectionRenderData[SectionEntry.Key];
			FMaterialRenderProxy* Material = RenderData.Material->GetRenderProxy();

			FMeshBatch MeshBatch;
			MeshBatch.LODIndex = 0;
			MeshBatch.SegmentIndex = SectionEntry.Key;

			CreateMeshBatch(MeshBatch, Section, RenderData, Material, nullptr);
			PDI->DrawMesh(MeshBatch, 1.0f);

			// Here we add a reference to the buffers so that we can guarantee these stay around for the life of this proxy
			InUseBuffers.Add(Section);
		}
	}
}

void FRuntimeMeshComponentSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	// Set up wireframe material (if needed)
	const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;

	FColoredMaterialRenderProxy* WireframeMaterialInstance = nullptr;
	if (bWireframe)
	{
		WireframeMaterialInstance = new FColoredMaterialRenderProxy(
			GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr,
			FLinearColor(0, 0.5f, 1.f)
		);

		Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);
	}

	// Iterate over sections
	for (const auto& SectionEntry : RuntimeMeshProxy->GetSections())
	{
		FRuntimeMeshSectionProxyPtr Section = SectionEntry.Value;
		if (SectionRenderData.Contains(SectionEntry.Key) && Section.IsValid() && Section->ShouldRender())
		{
			//fix - for null RHI resource crash at Colector.Addmesh because section index buffer has 0 items
			if (Section->CanRender())
			{
				// Add the mesh batch to every view it's visible in
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					if (VisibilityMap & (1 << ViewIndex))
					{
						bool bForceDynamicPath = IsRichView(*Views[ViewIndex]->Family) || Views[ViewIndex]->Family->EngineShowFlags.Wireframe || IsSelected() || !IsStaticPathAvailable();

						if (bForceDynamicPath || !Section->WantsToRenderInStaticPath())
						{
							const FRuntimeMeshSectionRenderData& RenderData = SectionRenderData[SectionEntry.Key];
							FMaterialRenderProxy* Material = RenderData.Material->GetRenderProxy();

							FMeshBatch& MeshBatch = Collector.AllocateMesh();
							CreateMeshBatch(MeshBatch, Section, RenderData, Material, WireframeMaterialInstance);

							Collector.AddMesh(ViewIndex, MeshBatch);
						}
					}
				}
			}
		}
	}

	// Draw bounds
#if RUNTIMEMESH_ENABLE_DEBUG_RENDERING
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			// Draw simple collision as wireframe if 'show collision', and collision is enabled, and we are not using the complex as the simple
			if (ViewFamily.EngineShowFlags.Collision && IsCollisionEnabled() && BodySetup && BodySetup->GetCollisionTraceFlag() != ECollisionTraceFlag::CTF_UseComplexAsSimple)
			{
				FTransform GeomTransform(GetLocalToWorld());
				bool bDrawsVelocity = false;
				BodySetup->AggGeom.GetAggGeom(GeomTransform, GetSelectionColor(FColor(157, 149, 223, 255), IsSelected(), IsHovered()).ToFColor(true), NULL, false, false, bDrawsVelocity, ViewIndex, Collector);
			}

			// Render bounds
			RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
		}
	}
#endif
}
