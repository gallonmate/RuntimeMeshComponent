// Copyright 2016-2018 Chris Conway (Koderz). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RuntimeMeshData.h"
#include "RuntimeMeshSectionProxy.h"
#include "PrimitiveSceneProxy.h"

class UBodySetup;
class URuntimeMeshComponent;

/** Runtime mesh scene proxy */
class FRuntimeMeshComponentSceneProxy : public FPrimitiveSceneProxy
{
private:
	struct FRuntimeMeshSectionRenderData
	{
		UMaterialInterface* Material;
		bool bWantsAdjacencyInfo;
	};


	FRuntimeMeshProxyPtr RuntimeMeshProxy;

	TMap<int32, FRuntimeMeshSectionRenderData> SectionRenderData;


	// Reference all the in-use buffers so that as long as this proxy is around these buffers will be too. 
	// This is meant only for statically drawn sections. Dynamically drawn sections can update safely in place.
	// Static sections get new buffers on each update.
	TArray<TSharedPtr<FRuntimeMeshSectionProxy>> InUseBuffers;


	// Reference to the body setup for rendering.
	UBodySetup* BodySetup;

	FMaterialRelevance MaterialRelevance;

	bool bHasStaticSections;
	bool bHasDynamicSections;
	bool bHasShadowableSections;

public:

	/*Constructor, copies the whole mesh data to feed to UE */
	FRuntimeMeshComponentSceneProxy(URuntimeMeshComponent* Component);

	virtual ~FRuntimeMeshComponentSceneProxy();

	void CreateRenderThreadResources() override;

	virtual bool CanBeOccluded() const override
	{
		return !MaterialRelevance.bDisableDepthTest;
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;

	void CreateMeshBatch(FMeshBatch& MeshBatch, const FRuntimeMeshSectionProxyPtr& Section, const FRuntimeMeshSectionRenderData& RenderData, FMaterialRenderProxy* Material, FMaterialRenderProxy* WireframeMaterial) const;

	virtual void DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) override;

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;

	virtual uint32 GetMemoryFootprint(void) const
	{
		return(sizeof(*this) + GetAllocatedSize());
	}

	uint32 GetAllocatedSize(void) const
	{
		return(FPrimitiveSceneProxy::GetAllocatedSize());
	}

#if ENGINE_MAJOR_VERSION >= 4 && ENGINE_MINOR_VERSION >= 19
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}
#endif
};