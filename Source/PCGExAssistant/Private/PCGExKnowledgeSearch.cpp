// Copyright 2026 Timothé Lapetite and contributors


#include "PCGExKnowledgeSearch.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace PCGExKnowledgeSearch
{
	enum class EField : uint8
	{
		Name,
		Alias,
		Category,
		Id,
		Outline,
		Situation,
		Purpose,
		Num
	};

	constexpr int32 NumFields = static_cast<int32>(EField::Num);

	// How strongly a token hit in each field speaks for the row, in EField order.
	constexpr double FieldWeights[NumFields] = {30.0, 20.0, 20.0, 12.0, 10.0, 10.0, 8.0};

	// A prefix hit (either token extends the other) counts this fraction of an exact hit.
	constexpr double PrefixFactor = 0.5;
	constexpr int32 PrefixMinLength = 4;

	// Query expansions match at this fraction of a direct hit.
	constexpr double ExpansionFactor = 0.6;

	struct FIndexedRow
	{
		TSharedPtr<FJsonValue> Row;
		TSet<FString> Fields[NumFields];
		FString NormalizedName;
	};

	struct FIndex
	{
		TWeakPtr<FJsonValue> Root;
		TArray<FIndexedRow> Rows;
		TMap<FString, int32> DocFrequency;
	};

	const TSet<FString>& Stopwords()
	{
		static const TSet<FString> Words = {
			TEXT("a"), TEXT("an"), TEXT("the"), TEXT("of"), TEXT("on"), TEXT("in"), TEXT("to"), TEXT("for"), TEXT("from"),
			TEXT("with"), TEXT("and"), TEXT("or"), TEXT("my"), TEXT("into"), TEXT("that"), TEXT("are"), TEXT("is"),
			TEXT("how"), TEXT("do"), TEXT("does"), TEXT("it"), TEXT("its"), TEXT("by"), TEXT("at"), TEXT("as"), TEXT("be"),
			TEXT("this"), TEXT("all"), TEXT("any"), TEXT("some"), TEXT("what"), TEXT("which"), TEXT("when"), TEXT("where"),
			TEXT("why"), TEXT("can"), TEXT("should"), TEXT("would"), TEXT("i"), TEXT("you"), TEXT("we"), TEXT("me"),
			TEXT("our"), TEXT("your"), TEXT("using"), TEXT("between"), TEXT("each"), TEXT("per"),
		};
		return Words;
	}

	// Tokens rewritten to the vocabulary the bundle uses.
	const TMap<FString, FString>& TokenSynonyms()
	{
		static const TMap<FString, FString> Synonyms = {
			{TEXT("vertex"), TEXT("vtx")}, {TEXT("vertices"), TEXT("vtx")}, {TEXT("vert"), TEXT("vtx")}, {TEXT("verts"), TEXT("vtx")},
		};
		return Synonyms;
	}

	// Query-side alternates tried at reduced weight, keyed by stemmed token.
	const TMap<FString, TArray<FString>>& QueryExpansions()
	{
		static const TMap<FString, TArray<FString>> Expansions = {
			{TEXT("spline"), {TEXT("path")}},
			{TEXT("path"), {TEXT("spline")}},
			{TEXT("graph"), {TEXT("cluster")}},
			{TEXT("node"), {TEXT("vtx")}},
			{TEXT("remove"), {TEXT("prune"), TEXT("delete")}},
			{TEXT("delete"), {TEXT("remove")}},
			{TEXT("collapse"), {TEXT("fuse"), TEXT("merge")}},
			{TEXT("overlap"), {TEXT("fuse")}},
			{TEXT("corner"), {TEXT("bevel")}},
			{TEXT("round"), {TEXT("bevel")}},
			{TEXT("chamfer"), {TEXT("bevel")}},
			{TEXT("shortest"), {TEXT("route"), TEXT("pathfind")}},
			{TEXT("dead"), {TEXT("leaf")}},
			{TEXT("end"), {TEXT("leaf")}},
		};
		return Expansions;
	}

	// Identifier fragments that carry no meaning once a class name is split.
	const TSet<FString>& IdentifierNoise()
	{
		static const TSet<FString> Noise = {TEXT("u"), TEXT("pcgex"), TEXT("pcg"), TEXT("ex"), TEXT("setting")};
		return Noise;
	}

	// Light English stemming; enough to fold plurals and verb forms onto the bundle's nouns.
	FString Stem(const FString& Token)
	{
		if (const FString* Synonym = TokenSynonyms().Find(Token)) { return *Synonym; }

		const int32 Len = Token.Len();
		if (Len > 4 && Token.EndsWith(TEXT("ies"))) { return Token.LeftChop(3) + TEXT("y"); }
		if (Len > 5 && Token.EndsWith(TEXT("ing"))) { return Token.LeftChop(3); }
		if (Len > 4 && Token.EndsWith(TEXT("ed"))) { return Token.LeftChop(2); }
		if (Len > 4 && Token.EndsWith(TEXT("es")))
		{
			const TCHAR C = Token[Len - 3];
			if (C == TEXT('s') || C == TEXT('x') || C == TEXT('z') || C == TEXT('h')) { return Token.LeftChop(2); }
		}
		if (Len > 3 && Token.EndsWith(TEXT("s")) && !Token.EndsWith(TEXT("ss"))) { return Token.LeftChop(1); }
		return Token;
	}

	// Splits on non-alphanumerics, and at CamelCase boundaries when asked, then lowercases,
	// drops stopwords and stems.
	void Tokenize(const FString& Text, const bool bSplitCamelCase, TArray<FString>& Out)
	{
		FString Current;
		auto Flush = [&Current, &Out]()
		{
			if (Current.IsEmpty()) { return; }
			const FString Lower = Current.ToLower();
			Current.Reset();
			if (Stopwords().Contains(Lower)) { return; }
			Out.Add(Stem(Lower));
		};

		const int32 Len = Text.Len();
		for (int32 i = 0; i < Len; i++)
		{
			const TCHAR C = Text[i];
			if (!FChar::IsAlnum(C))
			{
				Flush();
				continue;
			}

			if (bSplitCamelCase && !Current.IsEmpty() && FChar::IsUpper(C))
			{
				const TCHAR Prev = Text[i - 1];
				const bool bNextIsLower = (i + 1 < Len) && FChar::IsLower(Text[i + 1]);
				if (FChar::IsLower(Prev) || FChar::IsDigit(Prev) || (FChar::IsUpper(Prev) && bNextIsLower)) { Flush(); }
			}

			Current.AppendChar(C);
		}
		Flush();
	}

	FString NormalizeName(const FString& Name)
	{
		FString Out;
		bool bPendingSpace = false;
		for (const TCHAR C : Name)
		{
			if (FChar::IsAlnum(C))
			{
				if (bPendingSpace && !Out.IsEmpty()) { Out.AppendChar(TEXT(' ')); }
				bPendingSpace = false;
				Out.AppendChar(FChar::ToLower(C));
			}
			else
			{
				bPendingSpace = true;
			}
		}
		return Out;
	}

	void AddFieldTokens(FIndexedRow& Indexed, const EField Field, const FString& Text, const bool bSplitCamelCase = false)
	{
		TArray<FString> Tokens;
		Tokenize(Text, bSplitCamelCase, Tokens);
		Indexed.Fields[static_cast<int32>(Field)].Append(Tokens);
	}

	void AddFieldTokens(FIndexedRow& Indexed, const EField Field, const TSharedPtr<FJsonObject>& Row, const TCHAR* ArrayField)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Row->TryGetArrayField(ArrayField, Values)) { return; }
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString AsString;
			if (Value && Value->TryGetString(AsString)) { AddFieldTokens(Indexed, Field, AsString); }
		}
	}

	void Build(FIndex& Index, const TSharedPtr<FJsonValue>& Root, const TArray<TSharedPtr<FJsonValue>>& Rows)
	{
		Index.Root = Root;
		Index.Rows.Reset(Rows.Num());
		Index.DocFrequency.Reset();

		for (const TSharedPtr<FJsonValue>& RowValue : Rows)
		{
			const TSharedPtr<FJsonObject>* RowObj = nullptr;
			if (!RowValue || !RowValue->TryGetObject(RowObj)) { continue; }
			const TSharedPtr<FJsonObject>& Row = *RowObj;

			FIndexedRow& Indexed = Index.Rows.AddDefaulted_GetRef();
			Indexed.Row = RowValue;

			auto Field = [&Row](const TCHAR* Name)
			{
				FString Value;
				Row->TryGetStringField(Name, Value);
				return Value;
			};

			const FString Name = Field(TEXT("name"));
			Indexed.NormalizedName = NormalizeName(Name);
			AddFieldTokens(Indexed, EField::Name, Name);
			AddFieldTokens(Indexed, EField::Alias, Row, TEXT("aliases"));
			AddFieldTokens(Indexed, EField::Alias, Row, TEXT("presets"));
			AddFieldTokens(Indexed, EField::Category, Field(TEXT("category")));
			AddFieldTokens(Indexed, EField::Outline, Row, TEXT("outline"));
			AddFieldTokens(Indexed, EField::Situation, Field(TEXT("situation")));
			AddFieldTokens(Indexed, EField::Purpose, Field(TEXT("purpose")));

			// Entry ids are class names; concept ids are path slugs. Both split fine, only the
			// former carries identifier noise.
			const bool bIsEntry = Field(TEXT("kind")) == TEXT("entry");
			AddFieldTokens(Indexed, EField::Id, Field(TEXT("id")), bIsEntry);
			if (bIsEntry)
			{
				TSet<FString>& IdTokens = Indexed.Fields[static_cast<int32>(EField::Id)];
				for (const FString& Noise : IdentifierNoise()) { IdTokens.Remove(Noise); }
			}

			TSet<FString> RowTokens;
			for (const TSet<FString>& FieldTokens : Indexed.Fields) { RowTokens.Append(FieldTokens); }
			for (const FString& Token : RowTokens) { Index.DocFrequency.FindOrAdd(Token)++; }
		}
	}

	double Match(const FString& Token, const TSet<FString>& FieldTokens)
	{
		if (FieldTokens.Contains(Token)) { return 1.0; }
		if (Token.Len() < PrefixMinLength) { return 0.0; }
		for (const FString& Candidate : FieldTokens)
		{
			if (Candidate.Len() < PrefixMinLength) { continue; }
			if (Candidate.StartsWith(Token, ESearchCase::CaseSensitive) || Token.StartsWith(Candidate, ESearchCase::CaseSensitive)) { return PrefixFactor; }
		}
		return 0.0;
	}

	double BestFieldScore(const FString& Token, const FIndexedRow& Indexed)
	{
		double Best = 0.0;
		for (int32 f = 0; f < NumFields; f++)
		{
			const double M = Match(Token, Indexed.Fields[f]);
			if (M > 0.0) { Best = FMath::Max(Best, FieldWeights[f] * M); }
		}
		return Best;
	}

	double InverseDocumentFrequency(const FIndex& Index, const FString& Token)
	{
		const int32* Frequency = Index.DocFrequency.Find(Token);
		return FMath::Loge(1.0 + static_cast<double>(Index.Rows.Num()) / (1.0 + (Frequency ? *Frequency : 0)));
	}

	double Score(const FIndex& Index, const FIndexedRow& Indexed, const TArray<FString>& QueryTokens, const FString& NormalizedQuery)
	{
		double Total = 0.0;
		int32 Matched = 0;

		for (const FString& Token : QueryTokens)
		{
			double Best = BestFieldScore(Token, Indexed) * InverseDocumentFrequency(Index, Token);
			if (const TArray<FString>* Alternates = QueryExpansions().Find(Token))
			{
				for (const FString& Alternate : *Alternates)
				{
					Best = FMath::Max(Best, ExpansionFactor * BestFieldScore(Alternate, Indexed) * InverseDocumentFrequency(Index, Alternate));
				}
			}
			if (Best > 0.0) { Matched++; }
			Total += Best;
		}

		if (Matched == 0) { return 0.0; }

		// Rows matching every query token win over rows matching one common token strongly.
		const double Coverage = static_cast<double>(Matched) / QueryTokens.Num();
		double Result = Total * (0.4 + 0.6 * Coverage);

		if (!NormalizedQuery.IsEmpty() && NormalizedQuery == Indexed.NormalizedName) { Result += 100.0; }
		else if (NormalizedQuery.Len() >= 3 && Indexed.NormalizedName.Contains(NormalizedQuery, ESearchCase::CaseSensitive)) { Result += 40.0; }
		else
		{
			const TSet<FString>& NameTokens = Indexed.Fields[static_cast<int32>(EField::Name)];
			bool bAllInName = true;
			for (const FString& Token : QueryTokens)
			{
				if (!NameTokens.Contains(Token))
				{
					bAllInName = false;
					break;
				}
			}
			if (bAllInName) { Result += 20.0; }
		}

		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> Search(const TSharedPtr<FJsonValue>& Root, const FString& Query, const int32 MaxResults)
	{
		static FIndex Index;

		TArray<TSharedPtr<FJsonValue>> Results;

		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Root || !Root->TryGetArray(Rows)) { return Results; }

		if (Index.Root.Pin() != Root) { Build(Index, Root, *Rows); }

		TArray<FString> QueryTokens;
		Tokenize(Query, false, QueryTokens);
		if (QueryTokens.IsEmpty()) { return Results; }

		const FString NormalizedQuery = NormalizeName(Query);

		struct FScored
		{
			double Score;
			const FIndexedRow* Row;
		};
		TArray<FScored> Scored;

		for (const FIndexedRow& Indexed : Index.Rows)
		{
			if (const double S = Score(Index, Indexed, QueryTokens, NormalizedQuery); S > 0.0)
			{
				Scored.Add(FScored{S, &Indexed});
			}
		}

		Scored.Sort([](const FScored& A, const FScored& B)
		{
			if (A.Score != B.Score) { return A.Score > B.Score; }
			return A.Row->NormalizedName < B.Row->NormalizedName;
		});

		const int32 Count = FMath::Min(MaxResults, Scored.Num());
		Results.Reserve(Count);
		for (int32 i = 0; i < Count; i++) { Results.Add(Scored[i].Row->Row); }

		return Results;
	}
}
