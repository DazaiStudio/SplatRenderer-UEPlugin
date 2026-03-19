#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "CSCropBoxComponent.generated.h"

DECLARE_DELEGATE(FOnCropBoxExtentChanged);

UCLASS()
class SPLATRENDERER_API UCSCropBoxComponent : public UBoxComponent
{
    GENERATED_BODY()

public:
    FOnCropBoxExtentChanged OnExtentChanged;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override
    {
        Super::PostEditChangeProperty(PropertyChangedEvent);
        OnExtentChanged.ExecuteIfBound();
    }

    virtual bool CanEditChange(const FProperty* InProperty) const override
    {
        if (!Super::CanEditChange(InProperty))
            return false;
        return true;
    }
#endif
};
