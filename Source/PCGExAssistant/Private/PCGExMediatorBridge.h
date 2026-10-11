// Copyright 2026 Timothé Lapetite and contributors


#pragma once

#include "CoreMinimal.h"

/**
 * PCGExtendedToolkit's UPCGExMediatorLibrary, reached by name through UFunction: this plugin ships alone and never
 * links it. Every parameter and the return value are FStrings, so no shared header is needed.
 */
namespace PCGExAssistant::MediatorBridge
{
	/** False with OutError when the library, the function or a parameter is missing; OutResult is the function's JSON text otherwise. */
	bool Call(const TCHAR* FunctionName, const TMap<FName, FString>& Args, FString& OutResult, FString& OutError);

	/** True when Result is { "ok": false, ... }, its diagnostics joined into OutMessage. */
	bool IsFailure(const FString& Result, FString& OutMessage);

	/** Call, with any failure raised as a script error; "{}" when the call itself failed. */
	FString Run(const TCHAR* FunctionName, const TMap<FName, FString>& Args);
}
