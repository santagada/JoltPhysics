// SPDX-FileCopyrightText: 2021 Jorrit Rouwe
// SPDX-License-Identifier: MIT

#include <Jolt.h>

#include <Physics/Collision/NarrowPhaseQuery.h>
#include <Physics/Collision/CollisionDispatch.h>
#include <Physics/Collision/RayCast.h>
#include <Physics/Collision/AABoxCast.h>
#include <Physics/Collision/ShapeCast.h>
#include <Physics/Collision/CollideShape.h>
#include <Physics/Collision/CollisionCollectorImpl.h>
#include <Physics/Collision/CastResult.h>

namespace JPH {

bool NarrowPhaseQuery::CastRay(const RayCast &inRay, RayCastResult &ioHit, const BroadPhaseLayerFilter &inBroadPhaseLayerFilter, const ObjectLayerFilter &inObjectLayerFilter, const BodyFilter &inBodyFilter) const
{
	JPH_PROFILE_FUNCTION();

	class MyCollector : public RayCastBodyCollector
	{
	public:
							MyCollector(const RayCast &inRay, RayCastResult &ioHit, const BodyInterface &inBodyInterface, const BodyFilter &inBodyFilter) :
			mRay(inRay),
			mHit(ioHit),
			mBodyInterface(inBodyInterface),
			mBodyFilter(inBodyFilter)
		{
			UpdateEarlyOutFraction(ioHit.mFraction);
		}

		virtual void		AddHit(const ResultType &inResult) override
		{
			JPH_ASSERT(inResult.mFraction < mHit.mFraction, "This hit should not have been passed on to the collector");

			// Only test shape if it passes the body filter
			if (mBodyFilter.ShouldCollide(inResult.mBodyID))
			{
				// Collect the transformed shape
				TransformedShape ts = mBodyInterface.GetTransformedShape(inResult.mBodyID);

				// Do narrow phase collision check
				if (ts.CastRay(mRay, mHit))
				{
					// Test that we didn't find a further hit by accident
					JPH_ASSERT(mHit.mFraction >= 0.0f && mHit.mFraction < GetEarlyOutFraction());

					// Update early out fraction based on narrow phase collector
					UpdateEarlyOutFraction(mHit.mFraction);
				}
			}
		}

		RayCast						mRay;
		RayCastResult &				mHit;
		const BodyInterface &		mBodyInterface;
		const BodyFilter &			mBodyFilter;
	};
	
	// Do broadphase test
	MyCollector collector(inRay, ioHit, *mBodyInterface, inBodyFilter);
	mBroadPhase->CastRay(inRay, collector, inBroadPhaseLayerFilter, inObjectLayerFilter);
	return ioHit.mFraction <= 1.0f;
}

void NarrowPhaseQuery::CastRay(const RayCast &inRay, const RayCastSettings &inRayCastSettings, CastRayCollector &ioCollector, const BroadPhaseLayerFilter &inBroadPhaseLayerFilter, const ObjectLayerFilter &inObjectLayerFilter, const BodyFilter &inBodyFilter) const
{
	JPH_PROFILE_FUNCTION();

	class MyCollector : public RayCastBodyCollector
	{
	public:
							MyCollector(const RayCast &inRay, const RayCastSettings &inRayCastSettings, CastRayCollector &ioCollector, const BodyInterface &inBodyInterface, const BodyFilter &inBodyFilter) :
			mRay(inRay),
			mRayCastSettings(inRayCastSettings),
			mCollector(ioCollector),
			mBodyInterface(inBodyInterface),
			mBodyFilter(inBodyFilter)
		{
			UpdateEarlyOutFraction(ioCollector.GetEarlyOutFraction());
		}

		virtual void		AddHit(const ResultType &inResult) override
		{
			JPH_ASSERT(inResult.mFraction < mCollector.GetEarlyOutFraction(), "This hit should not have been passed on to the collector");

			// Only test shape if it passes the body filter
			if (mBodyFilter.ShouldCollide(inResult.mBodyID))
			{
				// Collect the transformed shape
				TransformedShape ts = mBodyInterface.GetTransformedShape(inResult.mBodyID);

				// Do narrow phase collision check
				ts.CastRay(mRay, mRayCastSettings, mCollector);

				// Update early out fraction based on narrow phase collector
				UpdateEarlyOutFraction(mCollector.GetEarlyOutFraction());
			}
		}

		RayCast						mRay;
		RayCastSettings				mRayCastSettings;
		CastRayCollector &			mCollector;
		const BodyInterface &		mBodyInterface;
		const BodyFilter &			mBodyFilter;
	};

	// Do broadphase test
	MyCollector collector(inRay, inRayCastSettings, ioCollector, *mBodyInterface, inBodyFilter);
	mBroadPhase->CastRay(inRay, collector, inBroadPhaseLayerFilter, inObjectLayerFilter);
}

void NarrowPhaseQuery::CollidePoint(Vec3Arg inPoint, CollidePointCollector &ioCollector, const BroadPhaseLayerFilter &inBroadPhaseLayerFilter, const ObjectLayerFilter &inObjectLayerFilter, const BodyFilter &inBodyFilter) const
{
	JPH_PROFILE_FUNCTION();

	class MyCollector : public CollideShapeBodyCollector
	{
	public:
							MyCollector(Vec3Arg inPoint, CollidePointCollector &ioCollector, const BodyInterface &inBodyInterface, const BodyFilter &inBodyFilter) :
			mPoint(inPoint),
			mCollector(ioCollector),
			mBodyInterface(inBodyInterface),
			mBodyFilter(inBodyFilter)
		{
		}

		virtual void		AddHit(const ResultType &inResult) override
		{
			// Only test shape if it passes the body filter
			if (mBodyFilter.ShouldCollide(inResult))
			{
				// Collect the transformed shape
				TransformedShape ts = mBodyInterface.GetTransformedShape(inResult);

				// Do narrow phase collision check
				ts.CollidePoint(mPoint, mCollector);

				// Update early out fraction based on narrow phase collector
				UpdateEarlyOutFraction(mCollector.GetEarlyOutFraction());
			}
		}

		Vec3							mPoint;
		CollidePointCollector &			mCollector;
		const BodyInterface &			mBodyInterface;
		const BodyFilter &				mBodyFilter;
	};

	// Do broadphase test
	MyCollector collector(inPoint, ioCollector, *mBodyInterface, inBodyFilter);
	mBroadPhase->CollidePoint(inPoint, collector, inBroadPhaseLayerFilter, inObjectLayerFilter);
}

void NarrowPhaseQuery::CollideShape(const Shape *inShape, Vec3Arg inShapeScale, Mat44Arg inCenterOfMassTransform, const CollideShapeSettings &inCollideShapeSettings, CollideShapeCollector &ioCollector, const BroadPhaseLayerFilter &inBroadPhaseLayerFilter, const ObjectLayerFilter &inObjectLayerFilter, const BodyFilter &inBodyFilter) const
{
	JPH_PROFILE_FUNCTION();

	class MyCollector : public CollideShapeBodyCollector
	{
	public:
							MyCollector(const Shape *inShape, Vec3Arg inShapeScale, Mat44Arg inCenterOfMassTransform, const CollideShapeSettings &inCollideShapeSettings, CollideShapeCollector &ioCollector, const BodyInterface &inBodyInterface, const BodyFilter &inBodyFilter) :
			mShape(inShape),
			mShapeScale(inShapeScale),
			mCenterOfMassTransform(inCenterOfMassTransform),
			mCollideShapeSettings(inCollideShapeSettings),
			mCollector(ioCollector),
			mBodyInterface(inBodyInterface),
			mBodyFilter(inBodyFilter)
		{
		}

		virtual void		AddHit(const ResultType &inResult) override
		{
			// Only test shape if it passes the body filter
			if (mBodyFilter.ShouldCollide(inResult))
			{
				// Collect the transformed shape
				TransformedShape ts = mBodyInterface.GetTransformedShape(inResult);

				// Do narrow phase collision check
				ts.CollideShape(mShape, mShapeScale, mCenterOfMassTransform, mCollideShapeSettings, mCollector);

				// Update early out fraction based on narrow phase collector
				UpdateEarlyOutFraction(mCollector.GetEarlyOutFraction());
			}
		}

		const Shape *					mShape;
		Vec3							mShapeScale;
		Mat44							mCenterOfMassTransform;
		const CollideShapeSettings &	mCollideShapeSettings;
		CollideShapeCollector &			mCollector;
		const BodyInterface &			mBodyInterface;
		const BodyFilter &				mBodyFilter;
	};

	// Calculate bounds for shape and expand by max separation distance
	AABox bounds = inShape->GetWorldSpaceBounds(inCenterOfMassTransform, inShapeScale);
	bounds.ExpandBy(Vec3::sReplicate(inCollideShapeSettings.mMaxSeparationDistance));

	// Do broadphase test
	MyCollector collector(inShape, inShapeScale, inCenterOfMassTransform, inCollideShapeSettings, ioCollector, *mBodyInterface, inBodyFilter);
	mBroadPhase->CollideAABox(bounds, collector, inBroadPhaseLayerFilter, inObjectLayerFilter);
}

void NarrowPhaseQuery::CastShape(const ShapeCast &inShapeCast, const ShapeCastSettings &inShapeCastSettings, CastShapeCollector &ioCollector, const BroadPhaseLayerFilter &inBroadPhaseLayerFilter, const ObjectLayerFilter &inObjectLayerFilter, const BodyFilter &inBodyFilter, const ShapeFilter &inShapeFilter) const
{
	JPH_PROFILE_FUNCTION();

	class MyCollector : public CastShapeBodyCollector
	{
	private:
			/// Update early out fraction based on narrow phase collector
			inline void		PropagateEarlyOutFraction()
			{
				// The CastShapeCollector uses negative values for penetration depth so we want to clamp to the smallest positive number to keep receiving deeper hits
				if (mCollector.ShouldEarlyOut())
					ForceEarlyOut();
				else
					UpdateEarlyOutFraction(max(FLT_MIN, mCollector.GetEarlyOutFraction()));
			}

	public:
							MyCollector(const ShapeCast &inShapeCast, const ShapeCastSettings &inShapeCastSettings, CastShapeCollector &ioCollector, const BodyInterface &inBodyInterface, const BodyFilter &inBodyFilter, const ShapeFilter &inShapeFilter) :
			mShapeCast(inShapeCast),
			mShapeCastSettings(inShapeCastSettings),
			mCollector(ioCollector),
			mBodyInterface(inBodyInterface),
			mBodyFilter(inBodyFilter),
			mShapeFilter(inShapeFilter)
		{
			PropagateEarlyOutFraction();
		}

		virtual void		AddHit(const ResultType &inResult) override
		{
			JPH_ASSERT(inResult.mFraction <= max(0.0f, mCollector.GetEarlyOutFraction()), "This hit should not have been passed on to the collector");

			// Only test shape if it passes the body filter
			if (mBodyFilter.ShouldCollide(inResult.mBodyID))
			{
				// Collect the transformed shape
				TransformedShape ts = mBodyInterface.GetTransformedShape(inResult.mBodyID);

				// Do narrow phase collision check
				ts.CastShape(mShapeCast, mShapeCastSettings, mCollector, mShapeFilter);

				// Update early out fraction based on narrow phase collector
				PropagateEarlyOutFraction();
			}
		}

		ShapeCast					mShapeCast;
		const ShapeCastSettings &	mShapeCastSettings;
		CastShapeCollector &		mCollector;
		const BodyInterface &		mBodyInterface;
		const BodyFilter &			mBodyFilter;
		const ShapeFilter &			mShapeFilter;
	};

	// Do broadphase test
	MyCollector collector(inShapeCast, inShapeCastSettings, ioCollector, *mBodyInterface, inBodyFilter, inShapeFilter);
	mBroadPhase->CastAABox({ inShapeCast.mShapeWorldBounds, inShapeCast.mDirection }, collector, inBroadPhaseLayerFilter, inObjectLayerFilter);
}

void NarrowPhaseQuery::CollectTransformedShapes(const AABox &inBox, TransformedShapeCollector &ioCollector, const BroadPhaseLayerFilter &inBroadPhaseLayerFilter, const ObjectLayerFilter &inObjectLayerFilter, const BodyFilter &inBodyFilter) const
{
	class MyCollector : public CollideShapeBodyCollector
	{
	public:
							MyCollector(const AABox &inBox, TransformedShapeCollector &ioCollector, const BodyInterface &inBodyInterface, const BodyFilter &inBodyFilter) :
			mBox(inBox),
			mCollector(ioCollector),
			mBodyInterface(inBodyInterface),
			mBodyFilter(inBodyFilter)
		{
		}

		virtual void		AddHit(const ResultType &inResult) override
		{
			// Only test shape if it passes the body filter
			if (mBodyFilter.ShouldCollide(inResult))
			{
				// Collect the transformed shape
				TransformedShape ts = mBodyInterface.GetTransformedShape(inResult);

				// Do narrow phase collision check
				ts.CollectTransformedShapes(mBox, mCollector);

				// Update early out fraction based on narrow phase collector
				UpdateEarlyOutFraction(mCollector.GetEarlyOutFraction());
			}
		}

		const AABox &					mBox;
		TransformedShapeCollector &		mCollector;
		const BodyInterface &			mBodyInterface;
		const BodyFilter &				mBodyFilter;
	};

	// Do broadphase test
	MyCollector collector(inBox, ioCollector, *mBodyInterface, inBodyFilter);
	mBroadPhase->CollideAABox(inBox, collector, inBroadPhaseLayerFilter, inObjectLayerFilter);
}

} // JPH