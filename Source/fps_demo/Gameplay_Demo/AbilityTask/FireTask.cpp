// Fill out your copyright notice in the Description page of Project Settings.


#include "FireTask.h"

UFireTask* UFireTask::CreateFireTask(UGameplayAbility* OwningAbility, float SomeParameter)
{
	// 使用 NewAbilityTask 模板来实例化，千万不要用 NewObject
	UFireTask* MyObj = NewAbilityTask<UFireTask>(OwningAbility);
	
	// 缓存蓝图传进来的参数
	MyObj->InternalParameter = SomeParameter;
	
	return MyObj;
}

void UFireTask::Activate()
{
	Super::Activate();
}
