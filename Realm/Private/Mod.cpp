#include "Realm.h"
#include "Mod.h"
#include "PlayerCharacter.h"

AMod::AMod(const FObjectInitializer& objectInitializer)
: Super(objectInitializer)
{
	statsDesc = FText::GetEmpty();

	bReplicates = true;
	NetUpdateFrequency = 15.f;
}

int32 AMod::GetCost(bool bNeededCost /* = false */, APlayerCharacter* buyer /* = nullptr */)
{
	if (!bNeededCost)
		return cost;
	else
	{
		if (!IsValid(buyer))
			return cost;

		//array of mods we need for this mod
		TArray<TSubclassOf<AMod> > recipeMods;
		GetRecipe(recipeMods);

		//get an array of mods that the character has
		TArray<AMod*> mods = buyer->GetMods();

		//see if they have the correct recipe and enough credits
		int32 neededCredits = cost;
		for (int32 j = 0; j < mods.Num(); j++)
		{
			if (recipeMods.Contains(mods[j]->GetClass()))
				neededCredits -= mods[j]->cost;
		}

		return neededCredits;
	}
}

FText AMod::GetStatsDescription()
{
	statsDesc = FText::GetEmpty();

	for (int32 i = 0; i < (int32)EStat::ES_Max; i++)
	{
		FText currentLine = FText::GetEmpty();
		if (deltaStats[i] != 0.f)
		{
			switch (i)
			{
			case EStat::ES_Atk:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modatkdesc", "+{0} Attack\n"), FText::AsNumber(FMath::RoundToInt(deltaStats[i])));
				break;
			case EStat::ES_Def:
				currentLine = FText::Format(NSLOCTEXT("Realm", "moddefdesc", "+{0} Defense\n"), FText::AsNumber(FMath::RoundToInt(deltaStats[i])));
				break;
			case EStat::ES_AtkPL:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modatkpldesc", "+{0} Attack per Level\n"), FText::AsNumber(deltaStats[i]));
				break;
			case EStat::ES_DefPL:
				currentLine = FText::Format(NSLOCTEXT("Realm", "moddefpldesc", "+{0} Defense per Level\n"), FText::AsNumber(deltaStats[i]));
				break;
			case EStat::ES_SpAtk:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modspatkdesc", "+{0} Special Attack\n"), FText::AsNumber(FMath::RoundToInt(deltaStats[i])));
				break;
			case EStat::ES_SpDef:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modspdefdesc", "+{0} Special Defense\n"), FText::AsNumber(FMath::RoundToInt(deltaStats[i])));
				break;
			case EStat::ES_SpAtkPL:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modspatkpldesc", "+{0} Special Attack per Level\n"), FText::AsNumber(deltaStats[i]));
				break;
			case EStat::ES_SpDefPL:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modspdefpldesc", "+{0} Special Defense per Level\n"), FText::AsNumber(deltaStats[i]));
				break;
			case EStat::ES_AARange:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modaarangedesc", "+{0} Auto Attack Range\n"), FText::AsNumber(FMath::RoundToInt(deltaStats[i])));
				break;
			case EStat::ES_AtkSp:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modatkspdesc", "+{0} % Attack Speed\n"), FText::AsNumber(FMath::RoundToInt(deltaStats[i])));
				break;
			case EStat::ES_AtkSpPL:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modatksppldesc", "+{0} Attack Speed per Level\n"), FText::AsNumber(deltaStats[i]));
				break;
			case EStat::ES_Move:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modmovespdesc", "+{0} Movement Speed\n"), FText::AsNumber(FMath::RoundToInt(deltaStats[i])));
				break;
			case EStat::ES_CDR:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modcdreddesc", "+{0} % Cooldown Reduction\n"), FText::AsNumber(FMath::RoundToInt(deltaStats[i])));
				break;
			case EStat::ES_HP:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modhpdesc", "+{0} Health\n"), FText::AsNumber(FMath::RoundToInt(deltaStats[i])));
				break;
			case EStat::ES_HPPL:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modhppldesc", "+{0} Health per Level\n"), FText::AsNumber(deltaStats[i]));
				break;
			case EStat::ES_HPRegen:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modhpregendesc", "+{0} Health Regen per Second\n"), FText::AsNumber(deltaStats[i]));
				break;
			case EStat::ES_HPRegenPL:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modhpregenpldesc", "+{0} Health Regen per Second per Level\n"), FText::AsNumber(deltaStats[i]));
				break;
			case EStat::ES_Flare:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modflaredesc", "+{0} Flare\n"), FText::AsNumber(FMath::RoundToInt(deltaStats[i])));
				break;
			case EStat::ES_FlarePL:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modflarepldesc", "+{0} Flare per Level\n"), FText::AsNumber(deltaStats[i]));
				break;
			case EStat::ES_FlareRegen:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modflareregendesc", "+{0} Flare Regen per Second\n"), FText::AsNumber(deltaStats[i]));
				break;
			case EStat::ES_FlareRegenPL:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modflareregenpldesc", "+{0} Flare Regen per Second per Level\n"), FText::AsNumber(deltaStats[i]));
				break;
			case EStat::ES_CritChance:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modcritdesc", "+{0} % Critical Hit Chance\n"), FText::AsNumber(FMath::RoundToInt(deltaStats[i])));
				break;
			case EStat::ES_CritRatio:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modcritratiodesc", "+{0} % Critical Hit Damage\n"), FText::AsNumber(FMath::RoundToInt(deltaStats[i])));
				break;
			case EStat::ES_HPDrain:
				currentLine = FText::Format(NSLOCTEXT("Realm", "modhpdraindesc", "+{0} % Health Drain\n"), FText::AsNumber(FMath::RoundToInt(deltaStats[i])));
				break;
			}
			statsDesc = FText::Format(NSLOCTEXT("Realm", "moditemdesc", "{0}{1}"), statsDesc, currentLine);
		}
	}

	return statsDesc;
}

AMod* AMod::GetDefaultModObject(TSubclassOf<AMod> modClass)
{
	return modClass->GetDefaultObject<AMod>();
}

bool AMod::CanCharacterBuyThisMod(APlayerCharacter* buyer)
{
	if (!IsValid(buyer))
		return false;

	//get an array of mods that the character has
	TArray<AMod*> mods;
	for (int32 i = 0; i < buyer->GetMods().Num(); i++)
		mods.Add(buyer->GetMods()[i]);
	
	//get an array of classes for the mods
	TArray<TSubclassOf<AMod> > classList;
	GetRecipe(classList);

	//see if they have the correct recipe and enough credits
	int32 neededCredits = cost;
	int32 removeCount = 0;
	for (int32 i = 0; i < classList.Num(); i++)
	{
		for (int32 j = 0; j < mods.Num(); j++)
		{
			if (mods[j]->GetClass() == classList[i])
			{
				neededCredits -= mods[j]->GetCost(false);
				mods.RemoveAt(j);
				classList.RemoveAt(i);
				removeCount++;
			}
		}
	}

	if ((buyer->GetModCount() - removeCount) + 1 > 5)
		return false;

	if (buyer->GetCredits() >= cost)
		return true;

	return buyer->GetCredits() >= neededCredits;
}

void AMod::CharacterPurchasedMod(APlayerCharacter* buyer)
{
	if (!IsValid(buyer))
		return;

	buyer->ChangeCredits(-1 * GetCost(true, buyer));

	TArray<TSubclassOf<AMod> > classList;
	GetRecipe(classList);

	TArray<AMod*> mods = buyer->GetMods();
	for (int32 i = 0; i < classList.Num(); i++)
	{
		for (int32 j = 0; j < mods.Num(); j++)
		{
			if (mods[j]->GetClass() == classList[i])
			{
				buyer->RemoveMod(j);
				mods.RemoveAt(j);
				classList.RemoveAt(i);
			}
		}
	}
}

void AMod::GetRecipe(TArray<TSubclassOf<AMod> >& recipeClasses)
{
	for (int32 i = 0; i < recipe.Num(); i++)
	{
		recipeClasses.Add(recipe[i]);
		Cast<AMod>(recipe[i]->GetDefaultObject())->GetRecipe(recipeClasses);
	}
}

void AMod::StartCooldown_Implementation(float cooldownTime)
{
	GetWorldTimerManager().SetTimer(cooldownTimer, cooldownTime, false);
}

float AMod::GetCooldownRemaining()
{
	return GetWorldTimerManager().GetTimerRemaining(cooldownTimer);
}

float AMod::GetCooldownProgressPercent()
{
	if (GetWorldTimerManager().GetTimerRemaining(cooldownTimer) <= 0.f)
		return 0.f;

	float timeElapsed = GetWorldTimerManager().GetTimerElapsed(cooldownTimer);
	float totalTime = timeElapsed + GetWorldTimerManager().GetTimerRemaining(cooldownTimer);

	return timeElapsed / totalTime;
}

void AMod::GetUIRecipeForMod(TSubclassOf<AMod> modClass, TArray<TSubclassOf<AMod> >& recipeArray)
{
	AMod* defaultMod = Cast<AMod>(modClass->GetDefaultObject());
	if (IsValid(defaultMod))
	{
		for (TSubclassOf<AMod> recipeItem : defaultMod->recipe)
			recipeArray.Add(recipeItem);
	}
}

int32 AMod::GetUIRecipeCost(TSubclassOf<AMod> modClass)
{
	int32 cost = 0;
	AMod* defaultMod = Cast<AMod>(modClass->GetDefaultObject());
	if (IsValid(defaultMod))
	{
		cost = defaultMod->GetCost(false);
		for (TSubclassOf<AMod> recipeItem : defaultMod->recipe)
		{
			AMod* recipeMod = Cast<AMod>(recipeItem->GetDefaultObject());
			if (IsValid(recipeMod))
				cost -= recipeMod->GetCost(false);
		}
	}

	return cost;
}