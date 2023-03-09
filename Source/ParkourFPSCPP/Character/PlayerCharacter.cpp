// Fill out your copyright notice in the Description page of Project Settings.


#include "PlayerCharacter.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "Components/InputComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "ParkourFPSCPP/Weapon/Weapon.h"
#include "ParkourFPSCPP/ParkourFPSComponent/CombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "PlayerAnimInstance.h"

// Sets default values
APlayerCharacter::APlayerCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetMesh());
	CameraBoom->TargetArmLength = 600.f;
	CameraBoom->bUsePawnControlRotation = true;

	ThirdPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ThirdPersonCamera"));
	ThirdPersonCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	ThirdPersonCamera->bUsePawnControlRotation = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;

	OverheadWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("OverheadWidget"));
	OverheadWidget->SetupAttachment(RootComponent);

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetMesh(), TEXT("head"));
	FirstPersonCamera->bUsePawnControlRotation = true;

	ThirdPersonCamera->Activate();
	ThirdPersonCamera->Activate();

	Combat = CreateDefaultSubobject<UCombatComponent>(TEXT("CombatComponent"));
	Combat->SetIsReplicated(true);

	GetCharacterMovement()->NavAgentProps.bCanCrouch = true;

	GetCapsuleComponent()->SetCollisionResponseToChannel(ECollisionChannel::ECC_Camera, ECollisionResponse::ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECollisionChannel::ECC_Camera, ECollisionResponse::ECR_Ignore);

	bUseControllerRotationYaw = true;
	bFirstPerson = false;
	bFreeLook = false;

	TurningInPlace = ETurningInPlace::ETIP_NotTurning;

	NetUpdateFrequency = 66.f;
	MinNetUpdateFrequency = 33.f;
}

// Called when the game starts or when spawned
void APlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	if (APlayerController* PlayerController = Cast<APlayerController>(GetController())) {
		
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer())) {
			Subsystem->AddMappingContext(PlayerInputMappingContext, 0);
		}
	}
}

// Called every frame
void APlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	AimOffset(DeltaTime);
}

void APlayerCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();

	if (!GetController()) return;

	const FRotator Rotation = GetController()->GetControlRotation();
	const FRotator YawRotation(0.f, Rotation.Yaw, 0.f);
	
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	AddMovementInput(ForwardDirection, MovementVector.Y);

	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	AddMovementInput(RightDirection, MovementVector.X);

}

void APlayerCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D LookAxisValue = Value.Get<FVector2D>();
	if (GetController()) {
		AddControllerYawInput(LookAxisValue.X);
		AddControllerPitchInput(LookAxisValue.Y);
	}
}

void APlayerCharacter::SwitchCamera(const FInputActionValue& Value)
{
	if (ThirdPersonCamera && ThirdPersonCamera->IsActive()) {
		ThirdPersonCamera->Deactivate();
		FirstPersonCamera->Activate();
		//bUseControllerRotationYaw = true;
		bFirstPerson = true;
	}

	else if (FirstPersonCamera && FirstPersonCamera->IsActive()) {
		FirstPersonCamera->Deactivate();
		ThirdPersonCamera->Activate();
		//if(IsWeaponEquipped()) bUseControllerRotationYaw = false;
		bFirstPerson = false;
	}
}

void APlayerCharacter::Equip(const FInputActionValue& Value) {
	if (Combat) {
		if (HasAuthority()) {
			Combat->EquipWeapon(OverlappingWeapon);
		}
		
		else {
			ServerEquip();
		}
	}
}

void APlayerCharacter::Jump()
{
	if (bIsCrouched) {
		UnCrouch();
	}

	else Super::Jump();
}

void APlayerCharacter::StopJumping()
{
	Super::StopJumping();
}

void APlayerCharacter::Crouch()
{
	if (bIsCrouched) UnCrouch();
	else Super::Crouch();
}

void APlayerCharacter::Aim(const FInputActionValue& Value)
{
	if (Combat) {
		Combat->SetAiming(true);
	}
}

void APlayerCharacter::StopAiming(const FInputActionValue& Value)
{
	if (Combat) {
		Combat->SetAiming(false);
	}
}

void APlayerCharacter::AimOffset(float DeltaTime)
{
	if (Combat && Combat->EquippedWeapon == nullptr) return;

	FVector Velocity = GetVelocity();
	Velocity.Z = 0.f;
	float Speed = Velocity.Size();
	bool bIsInAir = GetCharacterMovement()->IsFalling();

	if (Speed == 0.f && !bIsInAir) {
		FRotator CurrentAimRotation = FRotator(0.f, GetBaseAimRotation().Yaw, 0.f);
		FRotator DeltaAimRotation = UKismetMathLibrary::NormalizedDeltaRotator(CurrentAimRotation, StartingAimRotation);
		AO_Yaw = DeltaAimRotation.Yaw;
		if (TurningInPlace == ETurningInPlace::ETIP_NotTurning) {
			InterpAO_Yaw = AO_Yaw;
		}

		bUseControllerRotationYaw = true;
		TurnInPlace(DeltaTime);
	}

	if (Speed > 0.f || bIsInAir) {
		StartingAimRotation = FRotator(0.f, GetBaseAimRotation().Yaw, 0.f);
		AO_Yaw = 0.f;
		TurningInPlace = ETurningInPlace::ETIP_NotTurning;
		//bUseControllerRotationYaw = true;
	}

	AO_Pitch = GetBaseAimRotation().Pitch;

	if (AO_Pitch > 90.f && !IsLocallyControlled()) {
		FVector2D InRange(270.f, 360.f);
		FVector2D OutRange(-90.f, 0.f);
		AO_Pitch = FMath::GetMappedRangeValueClamped(InRange, OutRange, AO_Pitch);
	}
}

void APlayerCharacter::FreeLook(const FInputActionValue& Value)
{
	GetCharacterMovement()->bOrientRotationToMovement = true;
	if (bFirstPerson) return;

	bFreeLook = !bFreeLook;

	if (bFreeLook) {
		bUseControllerRotationYaw = false;
	}

	else {
		bUseControllerRotationYaw = true;
	}
}

void APlayerCharacter::FirePressed(const FInputActionValue& Value)
{
	if (Combat) {
		Combat->FireButtonPressed(true);
	}
}

void APlayerCharacter::FireReleased(const FInputActionValue& Value)
{
	if (Combat) {
		Combat->FireButtonPressed(false);
	}
}

void APlayerCharacter::ADS(const FInputActionValue& Value)
{
}


void APlayerCharacter::OnRep_OverlappingWeapon(AWeapon* LastWeapon)
{
	if (OverlappingWeapon) {
		OverlappingWeapon->ShowPickupWidget(true);
	}

	if (LastWeapon) {
		LastWeapon->ShowPickupWidget(false);
	}
}

void APlayerCharacter::ServerEquip_Implementation()
{
	if (Combat) {
		Combat->EquipWeapon(OverlappingWeapon);
	}
}

void APlayerCharacter::TurnInPlace(float DeltaTime)
{
	//if (bFirstPerson) return;

	if (AO_Yaw > 90.f) {
		TurningInPlace = ETurningInPlace::ETIP_Right;
	}

	else if (AO_Yaw < -90.f) {
		TurningInPlace = ETurningInPlace::ETIP_Left;
	}

	if (TurningInPlace != ETurningInPlace::ETIP_NotTurning) {
		InterpAO_Yaw = FMath::FInterpTo(InterpAO_Yaw, 0.f, DeltaTime, 4.f);
		AO_Yaw = InterpAO_Yaw;

		if (FMath::Abs(AO_Yaw) < 15.f) {
			TurningInPlace = ETurningInPlace::ETIP_NotTurning;
			StartingAimRotation = FRotator(0.f, GetBaseAimRotation().Yaw, 0.f);
		}
	}
}

void APlayerCharacter::SetOverlappingWeapon(AWeapon* Weapon)
{
	if (OverlappingWeapon) {
		OverlappingWeapon->ShowPickupWidget(false);
	}

	OverlappingWeapon = Weapon;

	if (IsLocallyControlled()) {

		if (OverlappingWeapon) {
			OverlappingWeapon->ShowPickupWidget(true);
		}
	}
}

bool APlayerCharacter::IsWeaponEquipped()
{
	return (Combat && Combat->EquippedWeapon);
}

bool APlayerCharacter::IsAiming()
{
	return (Combat && Combat->bAiming);
}

bool APlayerCharacter::IsFirstPerson()
{
	return bFirstPerson;
}

bool APlayerCharacter::IsFreeLook()
{
	return bFreeLook;
}

AWeapon* APlayerCharacter::GetEquippedWeapon()
{
	if (Combat == nullptr) return nullptr;

	return Combat->EquippedWeapon;
}

// Called to bind functionality to input
void APlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	if (UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(PlayerInputComponent)) {
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Move);
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Look);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &APlayerCharacter::StopJumping);
		EnhancedInputComponent->BindAction(SwitchCameraAction, ETriggerEvent::Completed, this, &APlayerCharacter::SwitchCamera);
		EnhancedInputComponent->BindAction(EquipAction, ETriggerEvent::Completed, this, &APlayerCharacter::Equip);
		EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Completed, this, &APlayerCharacter::Crouch);
		EnhancedInputComponent->BindAction(AimAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Aim);
		EnhancedInputComponent->BindAction(AimAction, ETriggerEvent::Completed, this, &APlayerCharacter::StopAiming);
		EnhancedInputComponent->BindAction(FreeLookAction, ETriggerEvent::Completed, this, &APlayerCharacter::FreeLook);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Triggered, this, &APlayerCharacter::FirePressed);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Completed, this, &APlayerCharacter::FireReleased);

	}
}

void APlayerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(APlayerCharacter, OverlappingWeapon, COND_OwnerOnly);
}

void APlayerCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (Combat) {
		Combat->Character = this;
	}
}

void APlayerCharacter::PlayFireMontage(bool bAiming)
{
	if (Combat == nullptr || Combat->EquippedWeapon == nullptr) return;

	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();

	if (AnimInstance && FireWeaponMontage) {
		AnimInstance->Montage_Play(FireWeaponMontage);
		FName SectionName;
		
		SectionName = bAiming ? FName("RifleAim") : FName("RifleHip");

		AnimInstance->Montage_JumpToSection(SectionName);
	}
}

