#include "GameSessionSubsystem.h"

#include "CustomizationModel.h"

UCustomizationModel* UGameSessionSubsystem::GetCustomizationModel()
{
    if (!CustomizationModel) { CustomizationModel = NewObject<UCustomizationModel>(this); }
    return CustomizationModel;
}
