#include "CustomizationCatalog.h"

const TCHAR* FCustomizationCatalog::GetDefaultTablePath()
{
    return TEXT("/Game/Customization/DT_CustomizationParts.DT_CustomizationParts");
}

FCustomizationCatalog FCustomizationCatalog::LoadDefault()
{
    return FCustomizationCatalog(LoadObject<UDataTable>(nullptr, GetDefaultTablePath()));
}

FCustomizationCatalog::FCustomizationCatalog(UDataTable* InTable)
{
    if (InTable && InTable->GetRowStruct() == FCustomizationPartRow::StaticStruct()) { Table.Reset(InTable); }
}

const FCustomizationPartRow* FCustomizationCatalog::Find(FName PartId) const
{
    return Table.IsValid() && !PartId.IsNone()
        ? Table->FindRow<FCustomizationPartRow>(PartId, TEXT("CustomizationCatalog"), false) : nullptr;
}

TArray<FName> FCustomizationCatalog::GetPartIds(ECustomizationSlot Slot) const
{
    TArray<FName> Result;
    if (!Table.IsValid()) { return Result; }
    for (const FName RowName : Table->GetRowNames())
    {
        const FCustomizationPartRow* Row = Table->FindRow<FCustomizationPartRow>(RowName, TEXT("CustomizationCatalog"), false);
        if (Row && Row->Slot == Slot) { Result.Add(RowName); }
    }
    return Result;
}
