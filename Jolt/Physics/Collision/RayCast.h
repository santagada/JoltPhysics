// SPDX-FileCopyrightText: 2021 Jorrit Rouwe
// SPDX-License-Identifier: MIT

#pragma once

#include <Physics/Collision/BackFaceMode.h>

namespace JPH {

/// Structure that holds a single ray cast
struct RayCast
{
	/// Transform this ray using inTransform
	RayCast						Transformed(Mat44Arg inTransform) const
	{
		Vec3 ray_origin = inTransform * mOrigin;
		Vec3 ray_direction = inTransform * (mOrigin + mDirection) - ray_origin;
		return { ray_origin, ray_direction };
	}

	Vec3						mOrigin;					///< Origin of the ray
	Vec3						mDirection;					///< Direction and length of the ray (anything beyond this length will not be reported as a hit)
};

/// Settings to be passed with a ray cast
class RayCastSettings
{
public:
	/// How backfacing triangles should be treated
	EBackFaceMode				mBackFaceMode				= EBackFaceMode::IgnoreBackFaces;

	/// If convex shapes should be treated as solid. When true, a ray starting inside a convex shape will generate a hit at fraction 0.
	bool						mTreatConvexAsSolid			= true;
};

} // JPH