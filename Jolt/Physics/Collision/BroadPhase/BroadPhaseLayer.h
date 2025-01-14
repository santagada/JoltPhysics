// SPDX-FileCopyrightText: 2021 Jorrit Rouwe
// SPDX-License-Identifier: MIT

#pragma once

#include <Core/NonCopyable.h>

namespace JPH {

/// An object layer can be mapped to a broadphase layer. Objects with the same broadphase layer will end up in the same sub structure (usually a tree) of the broadphase. 
/// When there are many layers, this reduces the total amount of sub structures the broad phase needs to manage. Usually you want objects that don't collide with each other 
/// in different broad phase layers, but there could be exceptions if objects layers only contain a minor amount of objects so it is not beneficial to give each layer its 
/// own sub structure in the broadphase.
/// Note: This class requires explicit casting from and to Type to avoid confusion with ObjectLayer
class BroadPhaseLayer
{
public:
	using Type = uint8;

	JPH_INLINE 						BroadPhaseLayer() = default;
	JPH_INLINE explicit constexpr	BroadPhaseLayer(Type inValue) : mValue(inValue) { }
	JPH_INLINE constexpr			BroadPhaseLayer(const BroadPhaseLayer &inRHS) : mValue(inRHS.mValue) { }

	JPH_INLINE BroadPhaseLayer &	operator = (const BroadPhaseLayer &inRHS)
	{
		mValue = inRHS.mValue;
		return *this;
	}

	JPH_INLINE constexpr bool		operator == (const BroadPhaseLayer &inRHS) const
	{
		return mValue == inRHS.mValue;
	}

	JPH_INLINE constexpr bool		operator != (const BroadPhaseLayer &inRHS) const
	{
		return mValue != inRHS.mValue;
	}

	JPH_INLINE constexpr bool		operator < (const BroadPhaseLayer &inRHS) const
	{
		return mValue < inRHS.mValue;
	}

	JPH_INLINE explicit constexpr	operator Type() const
	{
		return mValue;
	}

private:
	Type							mValue;
};

/// Constant value used to indicate an invalid broad phase layer
static constexpr BroadPhaseLayer cBroadPhaseLayerInvalid(0xff);

/// An array whose length corresponds to the max amount of object layers that should be supported.
/// To map these to a broadphase layer you'd do vector[BroadPhaseLayer]. The broadphase layers should be tightly 
/// packed, i.e. the lowest value should be 0 and the amount of sub structures that are created in the broadphase is max(inObjectToBroadPhaseLayer).
using ObjectToBroadPhaseLayer = vector<BroadPhaseLayer>;

/// Function to test if two objects can collide based on their object layer. Used while finding collision pairs.
using BroadPhaseLayerPairFilter = bool (*)(BroadPhaseLayer inLayer1, BroadPhaseLayer inLayer2);

/// Filter class for broadphase layers
class BroadPhaseLayerFilter : public NonCopyable
{
public:
	/// Destructor
	virtual							~BroadPhaseLayerFilter() { }

	/// Function to filter out broadphase layers when doing collision query test (return true to allow testing against objects with this layer)
	virtual bool					ShouldCollide(BroadPhaseLayer inLayer) const
	{
		return true;
	}
};

/// Default filter class that uses the pair filter in combination with a specified layer to filter layers
class DefaultBroadPhaseLayerFilter : public BroadPhaseLayerFilter
{
public:
	/// Constructor
									DefaultBroadPhaseLayerFilter(BroadPhaseLayerPairFilter inObjectLayerPairFilter, BroadPhaseLayer inLayer) :
		mBroadPhaseLayerPairFilter(inObjectLayerPairFilter),
		mLayer(inLayer)
	{
	}

	/// Copy constructor
									DefaultBroadPhaseLayerFilter(const DefaultBroadPhaseLayerFilter &inRHS) :
		mBroadPhaseLayerPairFilter(inRHS.mBroadPhaseLayerPairFilter),
		mLayer(inRHS.mLayer)
	{
	}

	// See BroadPhaseLayerFilter::ShouldCollide
	virtual bool					ShouldCollide(BroadPhaseLayer inLayer) const override
	{
		return mBroadPhaseLayerPairFilter(mLayer, inLayer);
	}

private:
	BroadPhaseLayerPairFilter		mBroadPhaseLayerPairFilter;
	BroadPhaseLayer					mLayer;
};

/// Allows objects from a specific broad phase layer only
class SpecifiedBroadPhaseLayerFilter : public BroadPhaseLayerFilter
{
public:
	/// Constructor
									SpecifiedBroadPhaseLayerFilter(BroadPhaseLayer inLayer) :
		mLayer(inLayer)
	{
	}

	// See BroadPhaseLayerFilter::ShouldCollide
	virtual bool					ShouldCollide(BroadPhaseLayer inLayer) const override
	{
		return mLayer == inLayer;
	}

private:
	BroadPhaseLayer					mLayer;
};

} // JPH