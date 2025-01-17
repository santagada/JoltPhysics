// SPDX-FileCopyrightText: 2021 Jorrit Rouwe
// SPDX-License-Identifier: MIT

#include <Jolt.h>

#include <Physics/Collision/Shape/ScaledShape.h>
#include <Physics/Collision/RayCast.h>
#include <Physics/Collision/TransformedShape.h>
#include <ObjectStream/TypeDeclarations.h>
#include <Core/StreamIn.h>
#include <Core/StreamOut.h>

namespace JPH {

JPH_IMPLEMENT_SERIALIZABLE_VIRTUAL(ScaledShapeSettings)
{
	JPH_ADD_BASE_CLASS(ScaledShapeSettings, DecoratedShapeSettings)

	JPH_ADD_ATTRIBUTE(ScaledShapeSettings, mScale)
}

JPH_IMPLEMENT_RTTI_VIRTUAL(ScaledShape)
{
	JPH_ADD_BASE_CLASS(ScaledShape, DecoratedShape)
}

ShapeSettings::ShapeResult ScaledShapeSettings::Create() const
{ 
	if (mCachedResult.IsEmpty())
		Ref<Shape> shape = new ScaledShape(*this, mCachedResult); 
	return mCachedResult;
}

ScaledShape::ScaledShape(const ScaledShapeSettings &inSettings, ShapeResult &outResult) :
	DecoratedShape(inSettings, outResult),
	mScale(inSettings.mScale)
{
	if (outResult.HasError())
		return;

	outResult.Set(this);
}

MassProperties ScaledShape::GetMassProperties() const
{
	MassProperties p = mInnerShape->GetMassProperties();
	p.Scale(mScale);
	return p;
}

AABox ScaledShape::GetLocalBounds() const
{ 
	return mInnerShape->GetLocalBounds().Scaled(mScale);
}

AABox ScaledShape::GetWorldSpaceBounds(Mat44Arg inCenterOfMassTransform, Vec3Arg inScale) const
{ 
	return mInnerShape->GetWorldSpaceBounds(inCenterOfMassTransform, inScale * mScale);
}

TransformedShape ScaledShape::GetSubShapeTransformedShape(const SubShapeID &inSubShapeID, Vec3Arg inPositionCOM, QuatArg inRotation, Vec3Arg inScale, SubShapeID &outRemainder) const
{
	// We don't use any bits in the sub shape ID
	outRemainder = inSubShapeID;

	TransformedShape ts(inPositionCOM, inRotation, mInnerShape, BodyID());
	ts.SetShapeScale(inScale * mScale);
	return ts;
}

Vec3 ScaledShape::GetSurfaceNormal(const SubShapeID &inSubShapeID, Vec3Arg inLocalSurfacePosition) const 
{ 
	// Transform the surface point to local space and pass the query on
	Vec3 normal = mInnerShape->GetSurfaceNormal(inSubShapeID, inLocalSurfacePosition / mScale);

	// Need to transform the plane normals using inScale
	// Transforming a direction with matrix M is done through multiplying by (M^-1)^T
	// In this case M is a diagonal matrix with the scale vector, so we need to multiply our normal by 1 / scale and renormalize afterwards
	return (normal / mScale).Normalized();
}

void ScaledShape::GetSubmergedVolume(Mat44Arg inCenterOfMassTransform, Vec3Arg inScale, const Plane &inSurface, float &outTotalVolume, float &outSubmergedVolume, Vec3 &outCenterOfBuoyancy) const
{
	mInnerShape->GetSubmergedVolume(inCenterOfMassTransform, inScale * mScale, inSurface, outTotalVolume, outSubmergedVolume, outCenterOfBuoyancy);
}

#ifdef JPH_DEBUG_RENDERER
void ScaledShape::Draw(DebugRenderer *inRenderer, Mat44Arg inCenterOfMassTransform, Vec3Arg inScale, ColorArg inColor, bool inUseMaterialColors, bool inDrawWireframe) const
{
	mInnerShape->Draw(inRenderer, inCenterOfMassTransform, inScale * mScale, inColor, inUseMaterialColors, inDrawWireframe);
}

void ScaledShape::DrawGetSupportFunction(DebugRenderer *inRenderer, Mat44Arg inCenterOfMassTransform, Vec3Arg inScale, ColorArg inColor, bool inDrawSupportDirection) const
{
	mInnerShape->DrawGetSupportFunction(inRenderer, inCenterOfMassTransform, inScale * mScale, inColor, inDrawSupportDirection);
}

void ScaledShape::DrawGetSupportingFace(DebugRenderer *inRenderer, Mat44Arg inCenterOfMassTransform, Vec3Arg inScale) const 
{ 
	mInnerShape->DrawGetSupportingFace(inRenderer, inCenterOfMassTransform, inScale * mScale);
}
#endif // JPH_DEBUG_RENDERER

bool ScaledShape::CastRay(const RayCast &inRay, const SubShapeIDCreator &inSubShapeIDCreator, RayCastResult &ioHit) const
{
	Vec3 inv_scale = mScale.Reciprocal();
	RayCast scaled_ray { inv_scale * inRay.mOrigin, inv_scale * inRay.mDirection };
	return mInnerShape->CastRay(scaled_ray, inSubShapeIDCreator, ioHit);
}

void ScaledShape::CastRay(const RayCast &inRay, const RayCastSettings &inRayCastSettings, const SubShapeIDCreator &inSubShapeIDCreator, CastRayCollector &ioCollector) const
{
	Vec3 inv_scale = mScale.Reciprocal();
	RayCast scaled_ray { inv_scale * inRay.mOrigin, inv_scale * inRay.mDirection };
	return mInnerShape->CastRay(scaled_ray, inRayCastSettings, inSubShapeIDCreator, ioCollector);
}

void ScaledShape::CollidePoint(Vec3Arg inPoint, const SubShapeIDCreator &inSubShapeIDCreator, CollidePointCollector &ioCollector) const
{
	Vec3 inv_scale = mScale.Reciprocal();
	mInnerShape->CollidePoint(inv_scale * inPoint, inSubShapeIDCreator, ioCollector);
}

void ScaledShape::CastShape(const ShapeCast &inShapeCast, const ShapeCastSettings &inShapeCastSettings, Vec3Arg inScale, const ShapeFilter &inShapeFilter, Mat44Arg inCenterOfMassTransform2, const SubShapeIDCreator &inSubShapeIDCreator1, const SubShapeIDCreator &inSubShapeIDCreator2, CastShapeCollector &ioCollector) const 
{
	mInnerShape->CastShape(inShapeCast, inShapeCastSettings, inScale * mScale, inShapeFilter, inCenterOfMassTransform2, inSubShapeIDCreator1, inSubShapeIDCreator2, ioCollector);
}

void ScaledShape::CollectTransformedShapes(const AABox &inBox, Vec3Arg inPositionCOM, QuatArg inRotation, Vec3Arg inScale, const SubShapeIDCreator &inSubShapeIDCreator, TransformedShapeCollector &ioCollector) const 
{
	mInnerShape->CollectTransformedShapes(inBox, inPositionCOM, inRotation, inScale * mScale, inSubShapeIDCreator, ioCollector);
}

void ScaledShape::TransformShape(Mat44Arg inCenterOfMassTransform, TransformedShapeCollector &ioCollector) const
{
	mInnerShape->TransformShape(inCenterOfMassTransform * Mat44::sScale(mScale), ioCollector);
}

void ScaledShape::SaveBinaryState(StreamOut &inStream) const
{
	DecoratedShape::SaveBinaryState(inStream);

	inStream.Write(mScale);
}

void ScaledShape::RestoreBinaryState(StreamIn &inStream)
{
	DecoratedShape::RestoreBinaryState(inStream);

	inStream.Read(mScale);
}

float ScaledShape::GetVolume() const
{
	return abs(mScale.GetX() * mScale.GetY() * mScale.GetZ()) * mInnerShape->GetVolume();
}

bool ScaledShape::IsValidScale(Vec3Arg inScale) const
{
	return mInnerShape->IsValidScale(inScale * mScale);
}

} // JPH