#include "Realm.h"
#include "GameCharacter.h"
#include "UnrealNetwork.h"
#include "RealmMoveController.h"
#include "PlayerHUD.h"
#include "PlayerCharacter.h"
#include "Projectile.h"
#include "RealmPlayerController.h"
#include "RealmGameMode.h"
#include "RealmCharacterMovementComponent.h"
#include "RealmLaneMinionAI.h"
#include "RealmFogofWarManager.h"

AGameCharacter::AGameCharacter(const FObjectInitializer& objectInitializer)
:Super(objectInitializer.SetDefaultSubobjectClass<URealmCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	AIControllerClass = AAIController::StaticClass();
	bAlwaysRelevant = true;

	level = 1;
	experienceAmount = 0;
	skillPoints = 0;
	baseExpReward = 21;
	experienceRewardRange = 1500.f;
	sightRadius = 850.f;
}

void AGameCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (Role == ROLE_Authority)
	{
		statsManager = GetWorld()->SpawnActor<AStatsManager>(GetActorLocation(), GetActorRotation());
		autoAttackManager = GetWorld()->SpawnActor<AAutoAttackManager>(GetActorLocation(), GetActorRotation());
		skillManager = GetWorld()->SpawnActor<ASkillManager>(GetActorLocation(), GetActorRotation());

		//initialize stats
		if (characterData)
			statsManager->InitializeStats(characterData->GetDefaultObject<UGameCharacterData>()->GetCharacterBaseStats(), this);
	
		autoAttackManager->InitializeManager(autoAttacks, statsManager);
		autoAttackManager->SetOwner(playerController);
		statsManager->AttachRootComponentToActor(this);
		autoAttackManager->AttachRootComponentToActor(this);
		skillManager->AttachRootComponentToActor(this);

		statsManager->SetMaxHealth();
		statsManager->SetMaxFlare();

		for (int32 i = 0; i < skillClasses.Num(); i++)
		{
			ASkill* newSkill = GetWorld()->SpawnActor<ASkill>(skillClasses[i], GetActorLocation(), GetActorRotation());
			if (newSkill)
			{
				newSkill->InitializeSkill(this);
				skillManager->AddSkill(newSkill);
			}
		}
	}

	for (TActorIterator<AHUD> objItr(GetWorld()); objItr; ++objItr)
	{
		APlayerHUD* hud = Cast<APlayerHUD>((*objItr));
		if (hud)
			hud->AddPostRenderedActor(this);
	}
}

void AGameCharacter::Destroy(bool bNetForce /* = false */, bool bShouldModifyLevel /* = true */)
{
	autoAttackManager->Destroy();
	statsManager->Destroy();
	skillManager->Destroy();

	Super::Destroy(bNetForce, bShouldModifyLevel);
}

void AGameCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (Role == ROLE_Authority)
	{
		GetCharacterMovement()->MaxWalkSpeed = GetCurrentValueForStat(EStat::ES_Move);

		if (IsValid(GetStatsManager()) && IsValid(GetAutoAttackManager()))
			GetStatsManager()->baseStats[(uint8)EStat::ES_AARange] = GetAutoAttackManager()->GetCurrentAutoAttackRange();

		if (IsAlive())
		{
			if (GetHealth() < GetCurrentValueForStat(EStat::ES_HP))
			{
				if (GetWorldTimerManager().GetTimerRemaining(healthRegen) <= 0.f)
					GetWorldTimerManager().SetTimer(healthRegen, this, &AGameCharacter::HealthRegen, 0.25f, true);
			}

			if (GetFlare() < GetCurrentValueForStat(EStat::ES_Flare))
			{
				if (GetWorldTimerManager().GetTimerRemaining(flareRegen) <= 0.f)
					GetWorldTimerManager().SetTimer(flareRegen, this, &AGameCharacter::FlareRegen, 0.25f, true);
			}
		}
	}

	if (IsValid(currentTarget) && currentTarget->IsAlive() && IsAlive())
	{
		FRotator newRot = GetActorRotation();
		FRotator dir = (currentTarget->GetActorLocation() - GetActorLocation()).Rotation();
		newRot.Yaw = dir.Yaw;

		SetActorRotation(newRot);
	}
}

void AGameCharacter::ReplicateHit(float damage, struct FDamageEvent const& damageEvent, class APawn* instigatingPawn, class AActor* damageCauser, bool bKilled)
{
	const float timeoutTime = GetWorld()->GetTimeSeconds() + 0.5f;

	FDamageEvent lastDamageEvent = lastTakeHitInfo.GetDamageEvent();
	if ((instigatingPawn == lastTakeHitInfo.PawnInstigator) && (lastDamageEvent.DamageTypeClass == lastTakeHitInfo.DamageTypeClass) && (lastTakeHitTimeTimeout == timeoutTime))
	{
		// same frame damage
		if (bKilled && lastTakeHitInfo.bKilled)
		{
			// Redundant death take hit, just ignore it
			return;
		}

		// otherwise, accumulate damage done this frame
		damage += lastTakeHitInfo.ActualDamage;
	}

	lastTakeHitInfo.ActualDamage = damage;
	lastTakeHitInfo.PawnInstigator = Cast<AGameCharacter>(instigatingPawn);
	lastTakeHitInfo.DamageCauser = damageCauser;
	lastTakeHitInfo.SetDamageEvent(damageEvent);
	lastTakeHitInfo.bKilled = bKilled;
	lastTakeHitInfo.EnsureReplication();

	lastTakeHitTimeTimeout = timeoutTime;
}

void AGameCharacter::OnRep_LastTakeHitInfo()
{
	if (lastTakeHitInfo.bKilled)
	{
		OnDeath(lastTakeHitInfo.ActualDamage, lastTakeHitInfo.GetDamageEvent(), lastTakeHitInfo.PawnInstigator, lastTakeHitInfo.DamageCauser.Get());
	}
	else if (Role < ROLE_Authority)
	{
		PlayHit(lastTakeHitInfo.ActualDamage, lastTakeHitInfo.GetDamageEvent(), lastTakeHitInfo.PawnInstigator, lastTakeHitInfo.DamageCauser.Get());
	}
}

bool AGameCharacter::CanMove() const
{
	return (currentAilment.newAilment != EAilment::AL_Knockup && currentAilment.newAilment != EAilment::AL_Stun);
}

bool AGameCharacter::CanAutoAttack() const
{
	return currentAilment.newAilment == EAilment::AL_None && GetWorldTimerManager().GetTimerRemaining(actionTimer) <= 0.f;
}

bool AGameCharacter::CanPerformSkills() const
{
	return currentAilment.newAilment == EAilment::AL_None && GetWorldTimerManager().GetTimerRemaining(actionTimer) <= 0.f;
}

bool AGameCharacter::UseSkill_Validate(int32 index, FVector mouseHitLoc, AGameCharacter* unitTarget)
{
	return true;
}

void AGameCharacter::UseSkill_Implementation(int32 index, FVector mouseHitLoc, AGameCharacter* unitTarget)
{
	if (skillManager->GetSkill(index) == nullptr)
		return;

	if (!IsAlive())
		return;

	ESkillState skillState = skillManager->GetSkill(index)->GetSkillState();
	if (skillState == ESkillState::Disabled || skillState == ESkillState::OnCooldown || skillState == ESkillState::Performing)
		return;

	float curr = statsManager->GetFlare();
	float diff = curr - skillManager->GetSkill(index)->GetCost();
	if (diff < 0)
		return;

	if (skillManager->GetSkill(index)->bAutoPerform)
		skillManager->GetSkill(index)->SetSkillState(ESkillState::Performing);

	if (Role == ROLE_Authority)
		skillManager->ServerPerformSkill(index, mouseHitLoc, unitTarget);

	if (Role < ROLE_Authority || (GetNetMode() == NM_ListenServer || GetNetMode() == NM_Standalone))
		skillManager->ClientPerformSkill(index, mouseHitLoc, unitTarget);
}

void AGameCharacter::PlayHit(float DamageTaken, struct FDamageEvent const& DamageEvent, class AGameCharacter* PawnInstigator, class AActor* DamageCauser)
{
	if (!IsValid(this))
		return;

	if (Role == ROLE_Authority)
	{
		ReplicateHit(DamageTaken, DamageEvent, PawnInstigator, DamageCauser, false);
	}
	
	if (IsValid(PawnInstigator) && PawnInstigator->playerController == GetWorld()->GetFirstPlayerController())
	{
		if (IsValid(PawnInstigator->playerController))
		{
			AHUD* hud = PawnInstigator->playerController->GetHUD();
			APlayerHUD* InstigatorHUD = Cast<APlayerHUD>(hud);
			if (IsValid(InstigatorHUD))
			{
				InstigatorHUD->NewDamageEvent(lastTakeHitInfo, GetActorLocation());
			}
		}
	}

	/*AShooterPlayerController* MyPC = Cast<AShooterPlayerController>(Controller);
	AShooterHUD* MyHUD = MyPC ? Cast<AShooterHUD>(MyPC->GetHUD()) : NULL;
	if (MyHUD)
	{
		MyHUD->NotifyWeaponHit(DamageTaken, DamageEvent, PawnInstigator);
	}*/
}

void AGameCharacter::StartAutoAttack()
{
	if (!IsValid(currentTarget) || !IsValid(autoAttackManager) || !IsValid(statsManager) || !IsAlive())
		return;

	if (bAutoAttackLaunching) //bAutoAttackOnCooldown || bAutoAttackLaunching
		return;

	if (GetWorldTimerManager().GetTimerRemaining(aaRangeTimer) <= 0.f)
		GetWorldTimerManager().SetTimer(aaRangeTimer, this, &AGameCharacter::CheckAutoAttack, 1.f / 20.f, true);

	if (bAutoAttackOnCooldown)
		return;

	float distance = (GetActorLocation() - currentTarget->GetActorLocation()).Size2D();
	if (distance <= statsManager->GetCurrentValueForStat(EStat::ES_AARange))
	{
		ARealmMoveController* aicc = Cast<ARealmMoveController>(GetController());
		if (IsValid(aicc))
			aicc->CharacterInAttackRange();

		float scale = statsManager->GetCurrentValueForStat(EStat::ES_AtkSp) / statsManager->GetBaseValueForStat(EStat::ES_AtkSp);
		AllPlayAnimMontage(autoAttackManager->GetCurrentAttackAnimation(), scale);

		AAIController* aic = Cast<AAIController>(GetController());
		if (IsValid(aic))
			aic->StopMovement();

		GetWorldTimerManager().SetTimer(aaLaunchTimer, this, &AGameCharacter::LaunchAutoAttack, autoAttackManager->GetAutoAttackLaunchTime() / scale);

		bAutoAttackLaunching = true;
	}
}

void AGameCharacter::LaunchAutoAttack()
{
	//GetWorldTimerManager().ClearTimer(aaRangeTimer);

	if (!IsValid(currentTarget) || !IsValid(this) || !IsValid(GetController()) || !currentTarget->IsAlive())
	{
		SetCurrentTarget(nullptr);
		bAutoAttackLaunching = false;

		ARealmLaneMinionAI* aic = Cast<ARealmLaneMinionAI>(GetController());
		if (IsValid(aic))
			aic->NeedsNewCommand();

		return;
	}

	//calculate critcal hit
	int32 crit = FMath::RandRange(0, 100);
	float dmg = GetCurrentValueForStat(EStat::ES_Atk);

	if (GetCurrentValueForStat(EStat::ES_CritChance) > 0.f && crit <= (GetCurrentValueForStat(EStat::ES_CritChance) * 100))
		dmg += dmg * GetCurrentValueForStat(EStat::ES_CritRatio);

	if (autoAttackManager->IsCurrentAttackProjectile())
	{
		//launch a projectile
		FVector spawnPos = GetMesh()->GetSocketLocation(autoAttackManager->GetCurrentAutoAttackProjectileSocket());
		FRotator dir = (currentTarget->GetActorLocation() - GetActorLocation()).Rotation();
		AProjectile* attackProjectile = GetWorld()->SpawnActor<AProjectile>(autoAttackManager->GetCurrentAutoAttackProjectileClass(), spawnPos, dir);

		if (IsValid(attackProjectile))
		{
			attackProjectile->bAutoAttackProjectile = true;
			attackProjectile->InitializeProjectile(dir.Vector(), dmg, UPhysicalDamage::StaticClass(), this, currentTarget);
		}
	}
	else
	{
		//instant damage
		float dmg = statsManager->GetCurrentValueForStat(EStat::ES_Atk);
		FDamageEvent de(UPhysicalDamage::StaticClass());

		currentTarget->TakeDamage(dmg, de, GetRealmController(), this);

		DamagedOtherCharacter(currentTarget, dmg, true);
	}

	bAutoAttackLaunching = false;

	float aaTime = 1.f / statsManager->GetCurrentValueForStat(EStat::ES_AtkSp);
	GetWorldTimerManager().SetTimer(aaTimer, this, &AGameCharacter::OnFinishAATimer, aaTime);
	bAutoAttackOnCooldown = true;

	GetWorldTimerManager().ClearTimer(aaRangeTimer);
}

void AGameCharacter::CheckAutoAttack()
{
	if (!IsValid(currentTarget) || !IsValid(GetController()) || !currentTarget->IsAlive())
	{
		AllStopAnimMontage(autoAttackManager->GetCurrentAttackAnimation());

		SetCurrentTarget(nullptr);
		GetWorldTimerManager().ClearTimer(aaLaunchTimer);
		bAutoAttackLaunching = false;

		ARealmLaneMinionAI* aic = Cast<ARealmLaneMinionAI>(GetController());
		if (IsValid(aic))
			aic->NeedsNewCommand();

		return;
	}

	float distance = (GetActorLocation() - currentTarget->GetActorLocation()).Size2D();
	if (distance > statsManager->GetCurrentValueForStat(EStat::ES_AARange))
	{
		AllStopAnimMontage(autoAttackManager->GetCurrentAttackAnimation());
		GetWorldTimerManager().ClearTimer(aaLaunchTimer);
		bAutoAttackLaunching = false;

		//@todo: check to see if the unit is still visible
		AAIController* aic = Cast<AAIController>(GetController());
		if (IsValid(aic))
			aic->MoveToActor(currentTarget, statsManager->GetCurrentValueForStat(EStat::ES_AARange));
	}
	else if (!bAutoAttackLaunching && !bAutoAttackOnCooldown)
	{
		float scale = statsManager->GetCurrentValueForStat(EStat::ES_AtkSp) / statsManager->GetBaseValueForStat(EStat::ES_AtkSp);
		AllPlayAnimMontage(autoAttackManager->GetCurrentAttackAnimation(), scale);

		AAIController* aic = Cast<AAIController>(GetController());
		if (IsValid(aic))
			aic->StopMovement();

		ARealmMoveController* aicc = Cast<ARealmMoveController>(GetController());
		if (IsValid(aicc))
			aicc->CharacterInAttackRange();

		GetWorldTimerManager().SetTimer(aaLaunchTimer, this, &AGameCharacter::LaunchAutoAttack, autoAttackManager->GetAutoAttackLaunchTime() / scale);

		bAutoAttackLaunching = true;
	}
}

void AGameCharacter::ResetAutoAttack()
{
	OnFinishAATimer();
}

void AGameCharacter::OnFinishAATimer()
{
	bAutoAttackOnCooldown = false;
	bAutoAttackLaunching = false;

	if (IsValid(currentTarget))
		StartAutoAttack();
}

void AGameCharacter::StopAutoAttack()
{
	SetCurrentTarget(nullptr);
	AllStopAnimMontage(autoAttackManager->GetCurrentAttackAnimation());
	GetWorldTimerManager().ClearTimer(aaLaunchTimer);
	GetWorldTimerManager().ClearTimer(aaRangeTimer);
	bAutoAttackLaunching = false;
}

bool AGameCharacter::AllPlayAnimMontage_Validate(class UAnimMontage* AnimMontage, float InPlayRate)
{
	return true;
}

bool AGameCharacter::AllStopAnimMontage_Validate(class UAnimMontage* AnimMontage)
{
	return true;
}

void AGameCharacter::AllPlayAnimMontage_Implementation(class UAnimMontage* AnimMontage, float InPlayRate)
{
	USkeletalMeshComponent* UseMesh = GetMesh();
	if (AnimMontage && UseMesh && UseMesh->AnimScriptInstance)
	{
		UseMesh->AnimScriptInstance->Montage_Play(AnimMontage, InPlayRate);
	}
}

void AGameCharacter::AllStopAnimMontage_Implementation(class UAnimMontage* AnimMontage)
{
	USkeletalMeshComponent* UseMesh = GetMesh();
	if (AnimMontage && UseMesh && UseMesh->AnimScriptInstance &&
		UseMesh->AnimScriptInstance->Montage_IsPlaying(AnimMontage))
	{
		UseMesh->AnimScriptInstance->Montage_Stop(AnimMontage->BlendOutTime);
	}
}

AGameCharacter* AGameCharacter::GetCurrentTarget() const
{
	return currentTarget;
}

void AGameCharacter::SetCurrentTarget(AGameCharacter* newTarget)
{
	currentTarget = newTarget;
}

AStatsManager* AGameCharacter::GetStatsManager() const
{
	return statsManager;
}

AAutoAttackManager* AGameCharacter::GetAutoAttackManager() const
{
	return autoAttackManager;
}

ASkillManager* AGameCharacter::GetSkillManager() const
{
	return skillManager;
}

float AGameCharacter::GetCurrentValueForStat(EStat stat) const
{
	if (statsManager)
		return statsManager->GetCurrentValueForStat(stat);
	else
		return -1.f;
}

float AGameCharacter::GetBaseValueForStat(EStat stat) const
{
	if (statsManager)
		return statsManager->GetBaseValueForStat(stat);
	else
		return -1.f;
}

float AGameCharacter::GetUnaffectedValueForStat(EStat stat) const
{
	if (statsManager)
		return statsManager->GetUnaffectedValueForStat(stat);
	else
		return -1.f;
}

AEffect* AGameCharacter::AddEffect(const FString& effectName, const FString& effectDescription, const TArray<TEnumAsByte<EStat> >& stats, const TArray<float>& amounts, float effectDuration /* = 0.f */, FString keyName, bool bStacking /*= false*/, bool bMultipleInfliction)
{
	if (Role == ROLE_Authority && statsManager)
		return statsManager->AddEffect(effectName, effectDescription, stats, amounts, effectDuration, keyName, bStacking, bMultipleInfliction);
	else
		return nullptr;
}

void AGameCharacter::AddEffectStacks(const FString& effectKey, int32 stackAmount)
{
	if (Role == ROLE_Authority && IsValid(statsManager))
		statsManager->AddEffectStacks(effectKey, stackAmount);
}

void AGameCharacter::EndEffect(const FString& effectKey)
{
	if (Role == ROLE_Authority && IsValid(statsManager))
		statsManager->EffectFinished(effectKey);
}

float AGameCharacter::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, class AActor* DamageCauser)
{
	if (Role < ROLE_Authority)
		return 0.f;

	if (!IsValid(statsManager) || !IsAlive())
	{
		return 0.f;
	}

	ARealmPlayerController* pc = Cast<ARealmPlayerController>(EventInstigator);
	ARealmMoveController* aipc = Cast<ARealmMoveController>(EventInstigator);
	AGameCharacter* damageCausingGC = NULL;

	if (!GetWorld()->GetAuthGameMode<ARealmGameMode>()->CanDamageFriendlies() && ((pc && pc->GetPlayerCharacter()->GetTeamIndex() == teamIndex) || (damageCausingGC && damageCausingGC->GetTeamIndex() == teamIndex)))
		return 0.f;

	if (aipc)
	{
		damageCausingGC = Cast<AGameCharacter>(aipc->GetCharacter());
		aipc->CharacterDamaged(this);
	}
	else if (pc)
	{
		damageCausingGC = pc->GetPlayerCharacter();
		pc->GetMoveController()->CharacterDamaged(this);
	}

	if (!damageCausingGC)
		return 0.f;

	float ActualDamage = Damage;

	if (DamageEvent.DamageTypeClass == UPhysicalDamage::StaticClass() && statsManager->GetCurrentValueForStat(EStat::ES_Def) >= 0)
		ActualDamage -= statsManager->GetCurrentValueForStat(EStat::ES_Def);
	else if (DamageEvent.DamageTypeClass == USpecialDamage::StaticClass() && statsManager->GetCurrentValueForStat(EStat::ES_SpDef) >= 0)
		ActualDamage -= statsManager->GetCurrentValueForStat(EStat::ES_SpDef);

	ActualDamage = FMath::Max(ActualDamage, 0.f);

	if (ActualDamage > 0.f)
	{
		if (GetHealth() - ActualDamage > 0)
		{
			statsManager->RemoveHealth(ActualDamage);
			PlayHit(ActualDamage, DamageEvent, damageCausingGC, DamageCauser);
		}
		else
		{
			statsManager->RemoveHealth(GetHealth());
			Die(ActualDamage, DamageEvent, damageCausingGC, DamageCauser);
		}

		CharacterDamaged(ActualDamage, DamageEvent.DamageTypeClass, damageCausingGC, DamageCauser);
		MakeNoise(1.0f, EventInstigator ? EventInstigator->GetPawn() : this);

		if (!GetWorldTimerManager().IsTimerActive(healthRegen) && IsAlive())
			GetWorldTimerManager().SetTimer(healthRegen, this, &AGameCharacter::HealthRegen, 0.25f, true);
	}

	return ActualDamage;
}

float AGameCharacter::GetHealth() const
{
	if (IsValid(statsManager))
		return statsManager->GetHealth();
	else
		return -1.f;
}

float AGameCharacter::GetFlare() const
{
	if (IsValid(statsManager))
		return statsManager->GetFlare();
	else
		return -1.f;
}

void AGameCharacter::UseFlare(float amount)
{
	if (IsValid(statsManager))
		statsManager->RemoveFlare(amount);

	if (!GetWorldTimerManager().IsTimerActive(healthRegen))
		GetWorldTimerManager().SetTimer(flareRegen, this, &AGameCharacter::FlareRegen, 0.25f, true);
}

void AGameCharacter::HealthRegen()
{
	float hr = statsManager->GetCurrentValueForStat(EStat::ES_HPRegen) / 4.f;
	if (GetHealth() + hr < statsManager->GetCurrentValueForStat(EStat::ES_HP))
		statsManager->RemoveHealth(hr * -1);
	else
	{
		statsManager->SetMaxHealth();
		GetWorldTimerManager().ClearTimer(healthRegen);
	}
}

void AGameCharacter::FlareRegen()
{
	float hr = statsManager->GetCurrentValueForStat(EStat::ES_FlareRegen) / 4.f;
	if (GetFlare() + hr < statsManager->GetCurrentValueForStat(EStat::ES_Flare))
		statsManager->RemoveFlare(hr * -1);
	else
	{
		statsManager->SetMaxFlare();
		GetWorldTimerManager().ClearTimer(flareRegen);
	}
}

bool AGameCharacter::IsAlive() const
{
	return !bIsDying;
}

bool AGameCharacter::CanDie(float KillingDamage, FDamageEvent const& DamageEvent, APawn* Killer, AActor* DamageCauser) const
{
	if (bIsDying										// already dying
		|| IsPendingKill()								// already destroyed
		|| Role != ROLE_Authority						// not authority
		|| GetWorld()->GetAuthGameMode() == NULL
		|| GetWorld()->GetAuthGameMode()->GetMatchState() == MatchState::LeavingMap)	// level transition occurring
	{
		return false;
	}

	return true;
}

void AGameCharacter::KilledBy(APawn* EventInstigator)
{
	if (Role == ROLE_Authority && !bIsDying)
	{
		AController* Killer = NULL;
		AGameCharacter* gc = Cast<AGameCharacter>(EventInstigator);
		if (gc != NULL)
		{
			Killer = gc->GetPlayerController();
			LastHitBy = NULL;
		}

		Die(GetHealth(), FDamageEvent(UDamageType::StaticClass()), Killer->GetPawn(), NULL);
	}
}

void AGameCharacter::Suicide()
{
	KilledBy(this);
}

bool AGameCharacter::Die(float KillingDamage, FDamageEvent const& DamageEvent, APawn* Killer, AActor* DamageCauser)
{
	if (!CanDie(KillingDamage, DamageEvent, Killer, DamageCauser))
	{
		return false;
	}

	if (IsValid(statsManager))
		statsManager->RemoveAllEffects();

	// if this is an environmental death then refer to the previous killer so that they receive credit (knocked into lava pits, etc)
	UDamageType const* const DamageType = DamageEvent.DamageTypeClass ? DamageEvent.DamageTypeClass->GetDefaultObject<UDamageType>() : GetDefault<UDamageType>();
	Killer = GetDamageInstigator(Killer->GetController(), *DamageType)->GetPawn();

	ARealmPlayerController* const KilledPlayer = (Controller != NULL) ? Cast<ARealmPlayerController>(Controller) : Cast<ARealmPlayerController>(GetOwner());
	//GetWorld()->GetAuthGameMode<AShooterGameMode>()->Killed(Killer, KilledPlayer, this, DamageType);

	NetUpdateFrequency = GetDefault<AGameCharacter>()->NetUpdateFrequency;
	GetCharacterMovement()->ForceReplicationUpdate();

	GetWorldTimerManager().ClearAllTimersForObject(this);
	OnDeath(KillingDamage, DamageEvent, Killer, DamageCauser);

	return true;
}

void AGameCharacter::OnDeath(float KillingDamage, struct FDamageEvent const& DamageEvent, class APawn* PawnInstigator, class AActor* DamageCauser)
{
	if (bIsDying)
	{
		return;
	}

	//bReplicateMovement = false;
	bIsDying = true;

	if (Role == ROLE_Authority)
	{
		ReplicateHit(KillingDamage, DamageEvent, PawnInstigator, DamageCauser, true);

		TArray<APlayerCharacter*> gcs;
		for (TActorIterator<APlayerCharacter> gcitr(GetWorld()); gcitr; ++gcitr)
		{
			APlayerCharacter* gc = (*gcitr);
			if (!IsValid(gc))
				continue;

			float distsq = (gc->GetActorLocation() - GetActorLocation()).SizeSquared2D();
			if (distsq <= FMath::Square(experienceRewardRange) && GetTeamIndex() != gc->GetTeamIndex())
				gcs.AddUnique(gc);
		}

		for (APlayerCharacter* gcc : gcs)
		{
			if (gcc->level == 1)
				gcc->GiveCharacterExperience(baseExpReward / gcs.Num());
			else
				gcc->GiveCharacterExperience((baseExpReward + (level * 2.45f)) / gcs.Num());
		}
	}

	GetWorldTimerManager().ClearTimer(flareRegen);
	GetWorldTimerManager().ClearTimer(healthRegen);

	AGameCharacter* gc = Cast<AGameCharacter>(PawnInstigator);

	if (IsValid(gc) && gc->playerController == GetWorld()->GetFirstPlayerController())
	{
		if (IsValid(gc->playerController))
		{
			AHUD* hud = gc->playerController->GetHUD();
			APlayerHUD* InstigatorHUD = Cast<APlayerHUD>(hud);
			if (IsValid(InstigatorHUD))
			{
				InstigatorHUD->NewDamageEvent(lastTakeHitInfo, GetActorLocation());
			}
		}
	}

	// cannot use IsLocallyControlled here, because even local client's controller may be NULL here
	/*if (GetNetMode() != NM_DedicatedServer && deathSound && GetPawnMesh() && GetPawnMesh()->IsVisible())
	{
		UGameplayStatics::PlaySoundAtLocation(this, deathSound, GetActorLocation());
	}*/

	//DetachFromControllerPendingDestroy();
	StopAnimMontage();
	StopAutoAttack();

	GetCharacterMovement()->SetMovementMode(MOVE_None);

	// disable collisions on capsule
	GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Overlap);

	GetMesh()->SetCollisionResponseToAllChannels(ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	GetMesh()->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);

	/*if (GetMesh())
	{
		static FName CollisionProfileName(TEXT("Ragdoll"));
		GetMesh()->SetCollisionProfileName(CollisionProfileName);
	}

	SetActorEnableCollision(true);

	// Death anim
	//float DeathAnimDuration = PlayAnimMontage(deathAnim);

	// Ragdoll
	/*if (DeathAnimDuration > 0.f)
	{
		FTimerHandle th;
		GetWorldTimerManager().SetTimer(th, this, &AFissureCharacter::SetRagdollPhysics, FMath::Min(0.1f, DeathAnimDuration), false);
	}
	else
	{
		SetRagdollPhysics();
	}

	SetRagdollPhysics();*/
}

void AGameCharacter::SetRagdollPhysics()
{
	bool bInRagdoll = false;

	if (IsPendingKill())
	{
		bInRagdoll = false;
	}
	else if (!GetMesh() || !GetMesh()->GetPhysicsAsset())
	{
		bInRagdoll = false;
	}
	else
	{
		// initialize physics/etc
		GetMesh()->SetAllBodiesSimulatePhysics(true);
		GetMesh()->SetSimulatePhysics(true);
		GetMesh()->WakeAllRigidBodies();
		GetMesh()->bBlendPhysics = true;

		bInRagdoll = true;
	}

	GetCharacterMovement()->StopMovementImmediately();
	//GetCharacterMovement()->DisableMovement();
}

void AGameCharacter::StopRagdollPhysics()
{
	if (IsPendingKill())
	{
		return;
	}
	else if (!GetMesh() || !GetMesh()->GetPhysicsAsset())
	{
		return;
	}

	GetMesh()->PutAllRigidBodiesToSleep();
	GetMesh()->SetSimulatePhysics(false);
	GetMesh()->SetAllBodiesSimulatePhysics(false);
	//GetMesh()->SetAllBodiesPhysicsBlendWeight(0.f);
	GetMesh()->bBlendPhysics = false;
	GetMesh()->AttachParent = GetRootComponent();

	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Block);

	if (GetMesh())
	{
		static FName CollisionProfileName(TEXT("BlockAll"));
		GetMesh()->SetCollisionProfileName(CollisionProfileName);
	}
}

int32 AGameCharacter::GetTeamIndex() const
{
	return teamIndex;
}

void AGameCharacter::SetTeamIndex(int32 newTeam)
{
	teamIndex = newTeam;
}

void AGameCharacter::PostRenderFor(class APlayerController* PC, class UCanvas* Canvas, FVector CameraPosition, FVector CameraDir)
{
	if (!IsValid(Cast<ARealmPlayerController>(PC)) || !IsValid(Cast<ARealmPlayerController>(PC)->GetPlayerCharacter()))
		return;

	int32 otherTeam = Cast<ARealmPlayerController>(PC)->GetPlayerCharacter()->GetTeamIndex();
	otherTeam == teamIndex ? Canvas->SetDrawColor(FColor::Blue) : Canvas->SetDrawColor(FColor::Red);
}

void AGameCharacter::AddMod(AMod* newMod)
{
	if (IsValid(newMod))
		mods.Add(newMod);

	statsManager->UpdateModStats(mods);
}

void AGameCharacter::RemoveMod(int32 index)
{
	if (index < 0 || index >= mods.Num())
		return;

	mods.RemoveAt(index);
	statsManager->UpdateModStats(mods);
}

int32 AGameCharacter::GetModCount()
{
	return mods.Num();
}

void AGameCharacter::ReceiveCallForHelp(AGameCharacter* distressedUnit, AGameCharacter* enemyTarget)
{

}

AController* AGameCharacter::GetRealmController() const
{
	if (IsValid(GetPlayerController()))
		return GetPlayerController();
	else
		return GetController();
}

void AGameCharacter::CharacterDash(FVector dashEndLocation)
{
	//UE_LOG(LogCharacter, Verbose, TEXT("ACharacter::LaunchCharacter '%s' (%f,%f,%f)"), *GetName(), LaunchVelocity.X, LaunchVelocity.Y, LaunchVelocity.Z);

	URealmCharacterMovementComponent* rmc = Cast<URealmCharacterMovementComponent>(GetCharacterMovement());
	if (rmc)
	{
		rmc->DashLaunch(dashEndLocation);
		CharacterDashStarted();
	}
}

void AGameCharacter::EndCharacterDash()
{
	URealmCharacterMovementComponent* rmc = Cast<URealmCharacterMovementComponent>(GetCharacterMovement());
	if (rmc)
		rmc->EndDash();
}

int32 AGameCharacter::GetNextLevelExperience() const
{
	return level + 1 <= MAX_LEVEL ? FMath::Square(level + 1) / FMath::Square(EXP_CONST) : 0;
}

void AGameCharacter::GiveCharacterExperience(int32 amount)
{
	if (level < MAX_LEVEL)
	{
		experienceAmount += amount;
		if (experienceAmount >= GetNextLevelExperience())
			LevelUp();
	}
	else
		experienceAmount = EXP_CONST;
}

void AGameCharacter::LevelUp()
{
	level++;
	skillPoints++;

	if (IsValid(statsManager))
		statsManager->CharacterLevelUp();
}

void AGameCharacter::EffectsUpdated()
{
	for (TActorIterator<APlayerHUD> huds(GetWorld()); huds; ++huds)
	{
		APlayerHUD* ph = (*huds);
		ph->CharacterEffectsUpdated(this);
	}
}

void AGameCharacter::OnRepAilment()
{

}

void AGameCharacter::GiveCharacterAilment(FAilmentInfo info)
{
	if (Role < ROLE_Authority)
		return;

	if (currentAilment.newAilment != EAilment::AL_None)
	{
		ailmentQueue.Enqueue(info);
		return;
	}

	currentAilment = info;

	URealmCharacterMovementComponent* mc = Cast<URealmCharacterMovementComponent>(GetCharacterMovement());
	if (!IsValid(mc))
		return;

	switch (currentAilment.newAilment)
	{
	case EAilment::AL_Knockup: //a knockup is a stun that displaces the character a certain distance.
		mc->AddImpulse(currentAilment.ailmentDir);
		break;

	case EAilment::AL_Stun:
		mc->IgnoreMovementForDuration(currentAilment.ailmentDuration);
		break;
	}

	GetWorldTimerManager().SetTimer(ailmentTimer, this, &AGameCharacter::CurrentAilmentFinished, currentAilment.ailmentDuration);
}

FAilmentInfo AGameCharacter::GetCharacterAilment() const
{
	return currentAilment;
}

void AGameCharacter::CurrentAilmentFinished()
{
	GetWorldTimerManager().ClearTimer(ailmentTimer);

	currentAilment.newAilment = EAilment::AL_None;

	if (!ailmentQueue.IsEmpty())
	{
		FAilmentInfo newInfo;
		ailmentQueue.Dequeue(newInfo);

		if (newInfo.newAilment != EAilment::AL_None)
			GiveCharacterAilment(newInfo);
	}
}

FAilmentInfo AGameCharacter::MakeAilmentInfo(EAilment ailment, FString ailmentString, float ailmentDuration, FVector ailmentDir)
{
	FAilmentInfo info;

	info.newAilment = ailment;
	info.ailmentText = ailmentString;
	info.ailmentDuration = ailmentDuration;
	info.ailmentDir = ailmentDir;

	return info;
}

void AGameCharacter::CalculateVisibility(TArray<AGameCharacter*>& seenCharacters)
{
	for (TActorIterator<AGameCharacter> gamechr(GetWorld()); gamechr; ++gamechr)
	{
		AGameCharacter* gc = *gamechr;
		if (!IsValid(gc))
			continue;

		if (gc->GetTeamIndex() == GetTeamIndex())
		{
			seenCharacters.AddUnique(gc);
			continue;
		}

		if (gc->CanEnemyAbsolutelySeeThisUnit())
		{
			seenCharacters.AddUnique(gc);
			continue;
		}

		float distsq = (gc->GetActorLocation() - GetActorLocation()).SizeSquared2D();
		if (distsq <= FMath::Square(sightRadius))
		{
			TArray<FHitResult> hits;
			FVector start = GetActorLocation();
			FVector end = gc->GetActorLocation();

			GetWorld()->LineTraceMultiByChannel(hits, start, end, ECC_Visibility);
			for (int32 i = 0; i < hits.Num(); i++)
			{
				if (hits[i].GetActor() == gc)
					seenCharacters.AddUnique(gc);
			}
		}
	}
}

bool AGameCharacter::CanEnemyAbsolutelySeeThisUnit() const
{
	return false;
}

void AGameCharacter::ApplyCharacterAction_Implementation(const FString& actionName, float actionDuration, bool bReverseProgressBar /* = false */)
{
	currentActionName = actionName;
	GetWorldTimerManager().SetTimer(actionTimer, actionDuration, false);
}

void AGameCharacter::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Only replicate this property for a short duration after it changes so join in progress players don't get spammed with fx when joining late
	DOREPLIFETIME_ACTIVE_OVERRIDE(AGameCharacter, lastTakeHitInfo, GetWorld() && GetWorld()->GetTimeSeconds() < lastTakeHitTimeTimeout);
}

void AGameCharacter::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGameCharacter, statsManager);
	DOREPLIFETIME(AGameCharacter, autoAttackManager);
	DOREPLIFETIME(AGameCharacter, skillManager);
	DOREPLIFETIME(AGameCharacter, teamIndex);
	DOREPLIFETIME(AGameCharacter, currentAilment);
	DOREPLIFETIME(AGameCharacter, currentTarget);
	DOREPLIFETIME_CONDITION(AGameCharacter, lastTakeHitInfo, COND_Custom);
}
