// LobbyInputFilter.h
//
// 로비 UI 입력창(닉네임/방 이름/최대 인원) 문자 필터.
// UE에는 "영어 IME만 허용" 옵션이 없어서, OnTextChanged마다 허용 문자 외에는 지우는 방식으로 처리한다.
// 한글 IME가 켜진 채로 치면 조합 중인 글자가 잠깐 보였다가 바로 지워짐 (= 사실상 입력 불가).
// 붙여넣기 등 예외 대비로 버튼을 눌러 값을 읽는 쪽에서도 한 번 더 거친다.

#pragma once

#include "CoreMinimal.h"
#include "Components/EditableTextBox.h"

namespace LobbyInputFilter
{
	// 영문 대소문자(A-Z, a-z) + 공백만 남김
	inline FString KeepEnglishOnly(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (const TCHAR C : In)
		{
			if ((C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('a') && C <= TEXT('z')) || C == TEXT(' '))
			{
				Out.AppendChar(C);
			}
		}
		return Out;
	}

	// 영문 대소문자(A-Z, a-z) + 숫자(0-9) + 공백만 남김 (닉네임 입력용)
	inline FString KeepEnglishAndDigits(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (const TCHAR C : In)
		{
			if ((C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('0') && C <= TEXT('9')) || C == TEXT(' '))
			{
				Out.AppendChar(C);
			}
		}
		return Out;
	}

	// 숫자(0-9)만 남김 (최대 인원 입력용)
	inline FString KeepDigitsOnly(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (const TCHAR C : In)
		{
			if (C >= TEXT('0') && C <= TEXT('9'))
			{
				Out.AppendChar(C);
			}
		}
		return Out;
	}

	// 필터 결과가 원본과 다를 때만 SetText (같을 때 SetText하면 커서가 튀고 이벤트가 반복됨)
	inline void Apply(UEditableTextBox* Box, const FText& Text, FString (*Filter)(const FString&))
	{
		if (!Box)
		{
			return;
		}
		const FString Source = Text.ToString();
		const FString Filtered = Filter(Source);
		if (Filtered != Source)
		{
			Box->SetText(FText::FromString(Filtered));
		}
	}
}
