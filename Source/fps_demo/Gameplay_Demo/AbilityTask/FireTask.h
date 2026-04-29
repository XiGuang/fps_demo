// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"

#include "FireTask.generated.h"

// 1. 声明多播委托。这里声明的每一个委托，都会变成蓝图节点右侧的“执行输出引脚”
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFireTaskDelegate);

/**
 * 
 */
UCLASS()
class FPS_DEMO_API UFireTask : public UAbilityTask
{
	GENERATED_BODY()

public:
	// 2. 将委托作为 UPROPERTY 暴露。
	UPROPERTY(BlueprintAssignable)
	FFireTaskDelegate OnCompleted;

	UPROPERTY(BlueprintAssignable)
	FFireTaskDelegate OnFailed;

	// 3. 静态工厂函数。这是你在蓝图右键菜单里搜索到的那个节点！
	// 关键宏：BlueprintInternalUseOnly = "TRUE"，引擎会把这个静态函数转换成异步节点样式。
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
	static UFireTask* CreateFireTask(UGameplayAbility* OwningAbility, float SomeParameter);

	// 4. 重写 Activate 函数，Task 被激活时从这里开始执行
	virtual void Activate() override;

protected:
	float InternalParameter;
	

	// 负责结束任务的自定义函数
	void FinishTask(bool bSuccess);
};
