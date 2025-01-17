// SPDX-FileCopyrightText: 2021 Jorrit Rouwe
// SPDX-License-Identifier: MIT

#pragma once

#include <Physics/Collision/Shape/Shape.h>

namespace JPH {

/// Class that constructs a DecoratedShape
class DecoratedShapeSettings : public ShapeSettings
{
	JPH_DECLARE_SERIALIZABLE_VIRTUAL(DecoratedShapeSettings)

	/// Default constructor for deserialization
									DecoratedShapeSettings() = default;

	/// Constructor that decorates another shape
									DecoratedShapeSettings(const ShapeSettings *inShape)	: mInnerShape(inShape) { }
									DecoratedShapeSettings(const Shape *inShape)			: mInnerShapePtr(inShape) { }

	RefConst<ShapeSettings>			mInnerShape;											///< Sub shape (either this or mShapePtr needs to be filled up)
	RefConst<Shape>					mInnerShapePtr;											///< Sub shape (either this or mShape needs to be filled up)
};

/// Base class for shapes that decorate another shape with extra functionality (e.g. scale, translation etc.)
class DecoratedShape : public Shape
{
public:
	JPH_DECLARE_RTTI_VIRTUAL(DecoratedShape)

	/// Constructor
									DecoratedShape() = default;
									DecoratedShape(const Shape *inInnerShape) : mInnerShape(inInnerShape) { }
									DecoratedShape(const DecoratedShapeSettings &inSettings, ShapeResult &outResult);

	/// Access to the decorated inner shape
	const Shape *					GetInnerShape() const									{ return mInnerShape; }

	// See Shape::MustBeStatic
	virtual bool					MustBeStatic() const override							{ return mInnerShape->MustBeStatic(); }

	// See Shape::GetSubShapeIDBitsRecursive
	virtual uint					GetSubShapeIDBitsRecursive() const override				{ return mInnerShape->GetSubShapeIDBitsRecursive(); }

	// See Shape::GetMaterial
	virtual const PhysicsMaterial *	GetMaterial(const SubShapeID &inSubShapeID) const override;

	// See Shape::GetSubShapeUserData
	virtual uint32					GetSubShapeUserData(const SubShapeID &inSubShapeID) const override;

	// See Shape
	virtual void					SaveSubShapeState(ShapeList &outSubShapes) const override;
	virtual void					RestoreSubShapeState(const ShapeList &inSubShapes) override;

	// See Shape::GetStatsRecursive
	virtual Stats					GetStatsRecursive(VisitedShapes &ioVisitedShapes) const override;

protected:
	RefConst<Shape>					mInnerShape;
};

} // JPH