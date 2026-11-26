#include "ZombieAnimInstance.h"
#include "Zombie.h"
#include "HeadLookComponent.h"
#include "Components/SkeletalMeshComponent.h"

void UZombieAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const AZombie* Zombie = Cast<AZombie>(TryGetPawnOwner());
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	if (!Zombie || !Mesh)
	{
		return;
	}

	WalkAmount = Zombie->GetWalkAmount();
	WalkCycleTime = Zombie->GetWalkCycleTime();
	bAggressive = Zombie->IsAggressive();
	bDead = Zombie->IsDead();

	const UHeadLookComponent* HeadLook = Zombie->HeadLook;
	HeadAlpha = HeadLook ? HeadLook->HeadLookAlpha : 0.0f;
	if (!HeadLook)
	{
		HeadRotation = FRotator::ZeroRotator;
		return;
	}

	// HeadLook daje yaw/pitch u odnosu na tijelo u svijetu. Smjerove prebacimo
	// u prostor mesha i slozimo yaw (oko osi gore) pa pitch (oko bocne osi
	// zakrenute glave), pa ne ovisimo o tome kamo gleda mesh ni o konvenciji
	// predznaka FRotatora.
	const FTransform& MeshTransform = Mesh->GetComponentTransform();
	const FRotator BodyRotation = Zombie->GetActorRotation();
	const FVector Forward = MeshTransform.InverseTransformVectorNoScale(BodyRotation.Vector());
	const FVector YawOnly = MeshTransform.InverseTransformVectorNoScale(
		(BodyRotation + FRotator(0.0f, HeadLook->HeadLookYaw, 0.0f)).Vector());
	const FVector Look = MeshTransform.InverseTransformVectorNoScale(
		(BodyRotation + FRotator(HeadLook->HeadLookPitch, HeadLook->HeadLookYaw, 0.0f)).Vector());

	const FQuat Yaw = FQuat::FindBetweenNormals(Forward, YawOnly);
	const FQuat Pitch = FQuat::FindBetweenNormals(YawOnly, Look);
	HeadRotation = (Pitch * Yaw).Rotator();
}
