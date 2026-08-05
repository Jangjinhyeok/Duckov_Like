// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DuckovLike : ModuleRules
{
	public DuckovLike(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		// InventoryCore는 Model 계층이다. 의존은 이 방향(게임 -> Model)만 존재한다.
		// UMG·ModelViewViewModel은 M2까지 실사용이 없지만 지금 넣는다.
		// InventoryCore.Build.cs와의 대비 자체가 모듈 경계의 증거이기 때문이다.
		// GameplayAbilities 모듈은 M3에서 실제로 쓸 때 추가한다.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "InventoryCore", "UMG", "ModelViewViewModel" });

		PrivateDependencyModuleNames.AddRange(new string[] {  });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
