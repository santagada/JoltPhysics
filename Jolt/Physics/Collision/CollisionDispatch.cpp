// SPDX-FileCopyrightText: 2021 Jorrit Rouwe
// SPDX-License-Identifier: MIT

#include <Jolt.h>

#include <Physics/Collision/CollisionDispatch.h>
#include <Physics/Collision/Shape/StaticCompoundShape.h>
#include <Physics/Collision/Shape/MutableCompoundShape.h>
#include <Physics/Collision/Shape/ConvexShape.h>
#include <Physics/Collision/Shape/MeshShape.h>
#include <Physics/Collision/Shape/HeightFieldShape.h>
#include <Physics/Collision/Shape/ScaledShape.h>
#include <Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Physics/Collision/RayCast.h>
#include <Physics/Collision/ShapeCast.h>
#include <Physics/Collision/CastResult.h>
#include <Physics/Collision/ShapeFilter.h>

namespace JPH {

void CollisionDispatch::sCollideShapeVsShape(const Shape *inShape1, const Shape *inShape2, Vec3Arg inScale1, Vec3Arg inScale2, Mat44Arg inCenterOfMassTransform1, Mat44Arg inCenterOfMassTransform2, const SubShapeIDCreator &inSubShapeIDCreator1, const SubShapeIDCreator &inSubShapeIDCreator2, const CollideShapeSettings &inCollideShapeSettings, CollideShapeCollector &ioCollector)
{
	switch (inShape1->GetType())
	{
	case EShapeType::Convex:
		switch (inShape2->GetType())
		{
		case EShapeType::Convex:
			ConvexShape::sCollideConvexVsConvex(static_cast<const ConvexShape *>(inShape1), static_cast<const ConvexShape *>(inShape2), inScale1, inScale2, inCenterOfMassTransform1, inCenterOfMassTransform2, inSubShapeIDCreator1, inSubShapeIDCreator2, inCollideShapeSettings, ioCollector);
			break;

		case EShapeType::Mesh:
			MeshShape::sCollideConvexVsMesh(static_cast<const ConvexShape *>(inShape1), static_cast<const MeshShape *>(inShape2), inScale1, inScale2, inCenterOfMassTransform1, inCenterOfMassTransform2, inSubShapeIDCreator1, inSubShapeIDCreator2, inCollideShapeSettings, ioCollector);
			break;

		case EShapeType::HeightField:
			HeightFieldShape::sCollideConvexVsHeightField(static_cast<const ConvexShape *>(inShape1), static_cast<const HeightFieldShape *>(inShape2), inScale1, inScale2, inCenterOfMassTransform1, inCenterOfMassTransform2, inSubShapeIDCreator1, inSubShapeIDCreator2, inCollideShapeSettings, ioCollector);
			break;

		case EShapeType::StaticCompound:
			StaticCompoundShape::sCollideShapeVsCompound(inShape1, static_cast<const StaticCompoundShape *>(inShape2), inScale1, inScale2, inCenterOfMassTransform1, inCenterOfMassTransform2, inSubShapeIDCreator1, inSubShapeIDCreator2, inCollideShapeSettings, ioCollector);
			break;

		case EShapeType::MutableCompound:
			MutableCompoundShape::sCollideShapeVsCompound(inShape1, static_cast<const MutableCompoundShape *>(inShape2), inScale1, inScale2, inCenterOfMassTransform1, inCenterOfMassTransform2, inSubShapeIDCreator1, inSubShapeIDCreator2, inCollideShapeSettings, ioCollector);
			break;

		case EShapeType::Scaled:
			{
				const ScaledShape *scaled_shape2 = static_cast<const ScaledShape *>(inShape2);
				CollisionDispatch::sCollideShapeVsShape(inShape1, scaled_shape2->GetInnerShape(), inScale1, inScale2 * scaled_shape2->GetScale(), inCenterOfMassTransform1, inCenterOfMassTransform2, inSubShapeIDCreator1, inSubShapeIDCreator2, inCollideShapeSettings, ioCollector);
			}
			break;

		case EShapeType::RotatedTranslated:
			RotatedTranslatedShape::sCollideShapeVsRotatedTranslated(inShape1, static_cast<const RotatedTranslatedShape *>(inShape2), inScale1, inScale2, inCenterOfMassTransform1, inCenterOfMassTransform2, inSubShapeIDCreator1, inSubShapeIDCreator2, inCollideShapeSettings, ioCollector);
			break;

		case EShapeType::OffsetCenterOfMass:
			OffsetCenterOfMassShape::sCollideShapeVsOffsetCenterOfMassShape(inShape1, static_cast<const OffsetCenterOfMassShape *>(inShape2), inScale1, inScale2, inCenterOfMassTransform1, inCenterOfMassTransform2, inSubShapeIDCreator1, inSubShapeIDCreator2, inCollideShapeSettings, ioCollector);
			break;
		}
		break;

	case EShapeType::Mesh:
	case EShapeType::HeightField:
		JPH_ASSERT(false, "Mesh / height field shapes cannot be dynamic, ignoring!");
		break;

	case EShapeType::StaticCompound:
		StaticCompoundShape::sCollideCompoundVsShape(static_cast<const StaticCompoundShape *>(inShape1), inShape2, inScale1, inScale2, inCenterOfMassTransform1, inCenterOfMassTransform2, inSubShapeIDCreator1, inSubShapeIDCreator2, inCollideShapeSettings, ioCollector);
		break;

	case EShapeType::MutableCompound:
		MutableCompoundShape::sCollideCompoundVsShape(static_cast<const MutableCompoundShape *>(inShape1), inShape2, inScale1, inScale2, inCenterOfMassTransform1, inCenterOfMassTransform2, inSubShapeIDCreator1, inSubShapeIDCreator2, inCollideShapeSettings, ioCollector);
		break;

	case EShapeType::Scaled:
		{
			const ScaledShape *scaled_shape1 = static_cast<const ScaledShape *>(inShape1);
			CollisionDispatch::sCollideShapeVsShape(scaled_shape1->GetInnerShape(), inShape2, inScale1 * scaled_shape1->GetScale(), inScale2, inCenterOfMassTransform1, inCenterOfMassTransform2, inSubShapeIDCreator1, inSubShapeIDCreator2, inCollideShapeSettings, ioCollector);
		}
		break;

	case EShapeType::RotatedTranslated:
		RotatedTranslatedShape::sCollideRotatedTranslatedVsShape(static_cast<const RotatedTranslatedShape *>(inShape1), inShape2, inScale1, inScale2, inCenterOfMassTransform1, inCenterOfMassTransform2, inSubShapeIDCreator1, inSubShapeIDCreator2, inCollideShapeSettings, ioCollector);
		break;

	case EShapeType::OffsetCenterOfMass:
		OffsetCenterOfMassShape::sCollideOffsetCenterOfMassShapeVsShape(static_cast<const OffsetCenterOfMassShape *>(inShape1), inShape2, inScale1, inScale2, inCenterOfMassTransform1, inCenterOfMassTransform2, inSubShapeIDCreator1, inSubShapeIDCreator2, inCollideShapeSettings, ioCollector);
		break;

	default:
		JPH_ASSERT(false);
		break;
	}
}

void CollisionDispatch::sCastShapeVsShape(const ShapeCast &inShapeCast, const ShapeCastSettings &inShapeCastSettings, const Shape *inShape, Vec3Arg inScale, const ShapeFilter &inShapeFilter, Mat44Arg inCenterOfMassTransform2, const SubShapeIDCreator &inSubShapeIDCreator1, const SubShapeIDCreator &inSubShapeIDCreator2, CastShapeCollector &ioCollector)
{
	// Only test shape if it passes the shape filter
	if (!inShapeFilter.ShouldCollide(inSubShapeIDCreator1.GetID(), inSubShapeIDCreator2.GetID()))
		return;

	switch (inShapeCast.mShape->GetType())
	{
	case EShapeType::StaticCompound:
	case EShapeType::MutableCompound:
		CompoundShape::sCastCompoundShapeVsShape(inShapeCast, inShapeCastSettings, inShape, inScale, inShapeFilter, inCenterOfMassTransform2, inSubShapeIDCreator1, inSubShapeIDCreator2, ioCollector);
		break;

	case EShapeType::Convex:
		inShape->CastShape(inShapeCast, inShapeCastSettings, inScale, inShapeFilter, inCenterOfMassTransform2, inSubShapeIDCreator1, inSubShapeIDCreator2, ioCollector);
		break;

	case EShapeType::Mesh:
	case EShapeType::HeightField:
		JPH_ASSERT(false, "Cannot cast a mesh / height field, ignoring!");
		break;

	case EShapeType::Scaled:
		{
			const ScaledShape *scaled_shape = static_cast<const ScaledShape *>(inShapeCast.mShape.GetPtr());
			ShapeCast scaled_cast(scaled_shape->GetInnerShape(), inShapeCast.mScale * scaled_shape->GetScale(), inShapeCast.mCenterOfMassStart, inShapeCast.mDirection);
			CollisionDispatch::sCastShapeVsShape(scaled_cast, inShapeCastSettings, inShape, inScale, inShapeFilter, inCenterOfMassTransform2, inSubShapeIDCreator1, inSubShapeIDCreator2, ioCollector);
		}
		break;

	case EShapeType::RotatedTranslated:
		RotatedTranslatedShape::sCastRotatedTranslatedShapeVsShape(inShapeCast, inShapeCastSettings, inShape, inScale, inShapeFilter, inCenterOfMassTransform2, inSubShapeIDCreator1, inSubShapeIDCreator2, ioCollector);
		break;

	case EShapeType::OffsetCenterOfMass:
		OffsetCenterOfMassShape::sCastOffsetCenterOfMassShapeVsShape(inShapeCast, inShapeCastSettings, inShape, inScale, inShapeFilter, inCenterOfMassTransform2, inSubShapeIDCreator1, inSubShapeIDCreator2, ioCollector);
		break;

	default:
		JPH_ASSERT(false);
		break;
	}
}

} // JPH