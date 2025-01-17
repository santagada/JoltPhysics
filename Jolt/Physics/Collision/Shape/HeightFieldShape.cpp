// SPDX-FileCopyrightText: 2021 Jorrit Rouwe
// SPDX-License-Identifier: MIT

#include <Jolt.h>

#include <Physics/Collision/Shape/HeightFieldShape.h>
#include <Physics/Collision/Shape/ConvexShape.h>
#include <Physics/Collision/Shape/ScaleHelpers.h>
#include <Physics/Collision/RayCast.h>
#include <Physics/Collision/ShapeCast.h>
#include <Physics/Collision/CastResult.h>
#include <Physics/Collision/CollidePointResult.h>
#include <Physics/Collision/ShapeFilter.h>
#include <Physics/Collision/CastConvexVsTriangles.h>
#include <Physics/Collision/CollideConvexVsTriangles.h>
#include <Physics/Collision/TransformedShape.h>
#include <Physics/Collision/ActiveEdges.h>
#include <Core/Profiler.h>
#include <Core/StringTools.h>
#include <Core/StreamIn.h>
#include <Core/StreamOut.h>
#include <Geometry/AABox4.h>
#include <Geometry/RayTriangle.h>
#include <Geometry/RayAABox.h>
#include <Geometry/OrientedBox.h>
#include <ObjectStream/TypeDeclarations.h>

//#define JPH_DEBUG_HEIGHT_FIELD

namespace JPH {

#ifdef JPH_DEBUG_RENDERER
bool HeightFieldShape::sDrawTriangleOutlines = false;
#endif // JPH_DEBUG_RENDERER

using namespace HeightFieldShapeConstants;

JPH_IMPLEMENT_SERIALIZABLE_VIRTUAL(HeightFieldShapeSettings)
{
	JPH_ADD_BASE_CLASS(HeightFieldShapeSettings, ShapeSettings)

	JPH_ADD_ATTRIBUTE(HeightFieldShapeSettings, mHeightSamples)
	JPH_ADD_ATTRIBUTE(HeightFieldShapeSettings, mOffset)
	JPH_ADD_ATTRIBUTE(HeightFieldShapeSettings, mScale)
	JPH_ADD_ATTRIBUTE(HeightFieldShapeSettings, mSampleCount)
	JPH_ADD_ATTRIBUTE(HeightFieldShapeSettings, mMaterialIndices)
	JPH_ADD_ATTRIBUTE(HeightFieldShapeSettings, mMaterials)
}

JPH_IMPLEMENT_RTTI_VIRTUAL(HeightFieldShape)
{
	JPH_ADD_BASE_CLASS(HeightFieldShape, Shape)
}

const uint HeightFieldShape::sGridOffsets[] = 
{
	0,			// level:  0, max x/y:     0, offset: 0
	1,			// level:  1, max x/y:     1, offset: 1
	5,			// level:  2, max x/y:     3, offset: 1 + 4
	21,			// level:  3, max x/y:     7, offset: 1 + 4 + 16
	85,			// level:  4, max x/y:    15, offset: 1 + 4 + 64
	341,		// level:  5, max x/y:    31, offset: 1 + 4 + 64 + 256
	1365,		// level:  6, max x/y:    63, offset: 1 + 4 + 64 + 256 + 1024
	5461,		// level:  7, max x/y:   127, offset: 1 + 4 + 64 + 256 + 1024 + 4096
	21845,		// level:  8, max x/y:   255, offset: 1 + 4 + 64 + 256 + 1024 + 4096 + ...
	87381,		// level:  9, max x/y:  1023, offset: 1 + 4 + 64 + 256 + 1024 + 4096 + ...
	349525,		// level: 10, max x/y:  2047, offset: 1 + 4 + 64 + 256 + 1024 + 4096 + ...
	1398101,	// level: 11, max x/y:  4095, offset: 1 + 4 + 64 + 256 + 1024 + 4096 + ...
	5592405,	// level: 12, max x/y:  8191, offset: 1 + 4 + 64 + 256 + 1024 + 4096 + ...
	22369621,	// level: 13, max x/y: 16383, offset: 1 + 4 + 64 + 256 + 1024 + 4096 + ...
	89478485,	// level: 14, max x/y: 32767, offset: 1 + 4 + 64 + 256 + 1024 + 4096 + ...
};

HeightFieldShapeSettings::HeightFieldShapeSettings(const float *inSamples, Vec3Arg inOffset, Vec3Arg inScale, uint32 inSampleCount, const uint8 *inMaterialIndices, const PhysicsMaterialList &inMaterialList) :
	mOffset(inOffset),
	mScale(inScale),
	mSampleCount(inSampleCount)
{
	mHeightSamples.resize(inSampleCount * inSampleCount);
	memcpy(&mHeightSamples[0], inSamples, inSampleCount * inSampleCount * sizeof(float));

	if (!inMaterialList.empty() && inMaterialIndices != nullptr)
	{
		mMaterialIndices.resize(Square(inSampleCount - 1));
		memcpy(&mMaterialIndices[0], inMaterialIndices, Square(inSampleCount - 1) * sizeof(uint8));
		mMaterials = inMaterialList;
	}
	else
	{
		JPH_ASSERT(inMaterialList.empty());
		JPH_ASSERT(inMaterialIndices == nullptr);
	}
}

ShapeSettings::ShapeResult HeightFieldShapeSettings::Create() const
{
	if (mCachedResult.IsEmpty())
		Ref<Shape> shape = new HeightFieldShape(*this, mCachedResult); 
	return mCachedResult;
}

HeightFieldShape::HeightFieldShape(const HeightFieldShapeSettings &inSettings, ShapeResult &outResult) :
	Shape(inSettings, outResult),
	mOffset(inSettings.mOffset),
	mScale(inSettings.mScale),
	mMaterials(inSettings.mMaterials),
	mSampleCount(inSettings.mSampleCount)
{
	// Required to be power of two to allow creating a hierarchical grid
	if (!IsPowerOf2(mSampleCount))
	{
		outResult.SetError("HeightFieldShape: Sample count must be power of 2!");
		return;
	}

	// We want at least 1 grid layer
	if (mSampleCount < cBlockSize * 2)
	{
		outResult.SetError("HeightFieldShape: Sample count too low!");
		return;
	}

	// Check that we don't overflow our 32 bit 'properties'
	if (mSampleCount > cBlockSize * (1 << cNumBitsXY))
	{
		outResult.SetError("HeightFieldShape: Sample count too high!");
		return;
	}

	// Check if we're not exceeding the amount of sub shape id bits
	if (GetSubShapeIDBitsRecursive() > SubShapeID::MaxBits)
	{
		outResult.SetError("HeightFieldShape: Size exceeds the amount of available sub shape ID bits!");
		return;
	}

	if (!mMaterials.empty())
	{
		// Validate materials
		if (mMaterials.size() > 256)
		{
			outResult.SetError("Supporting max 256 materials per height field");
			return;
		}
		for (uint8 s : inSettings.mMaterialIndices)
			if (s >= mMaterials.size())
			{
				outResult.SetError(StringFormat("Material %u is beyond material list (size: %u)", s, (uint)mMaterials.size()));
				return;
			}
	}
	else
	{
		// No materials assigned, validate that no materials have been specified
		if (!inSettings.mMaterialIndices.empty())
		{
			outResult.SetError("No materials present, mHeightSamples should be empty");
			return;
		}
	}

	// Determine range
	float min_value = FLT_MAX, max_value = -FLT_MAX;
	for (float h : inSettings.mHeightSamples)
		if (h != cNoCollisionValue)
		{
			min_value = min(min_value, h);
			max_value = max(max_value, h);
		}

	// Quantize to uint16
	float scale = min_value < max_value? float(cMaxHeightValue16) / (max_value - min_value) : 1.0f; // Only when there was collision / we would not divide by 0
	vector<uint16> quantized_samples;
	quantized_samples.reserve(mSampleCount * mSampleCount);
	for (float h : inSettings.mHeightSamples)
		if (h == cNoCollisionValue)
		{
			quantized_samples.push_back(cNoCollisionValue16);
		}
		else
		{
			float quantized_height = round(scale * (h - min_value));
			JPH_ASSERT(quantized_height >= 0.0f && quantized_height <= cMaxHeightValue16);
			quantized_samples.push_back(uint16(quantized_height));
		}

	// Update offset and scale to account for the compression to uint16
	if (min_value <= max_value) // Only when there was collision
		mOffset.SetY(mOffset.GetY() + min_value);
	mScale.SetY(mScale.GetY() / scale);

	// We stop at cBlockSize x cBlockSize height sample blocks
	uint n = mSampleCount / cBlockSize;

	// Calculate amount of grids
	uint max_level = CountTrailingZeros(n);

	// Temporary data structure used during creating of a hierarchy of grids 
	struct Range
	{
		uint16	mMin;
		uint16	mMax;
	};

	// Reserve size for temporary range data + reserve 1 extra for a 1x1 grid that we won't store but use for calculating the bounding box
	vector<vector<Range>> ranges;
	ranges.resize(max_level + 1);

	// Calculate highest detail grid by combining cBlockSize x cBlockSize height samples
	vector<Range> *cur_range_vector = &ranges.back();
	cur_range_vector->resize(n * n);
	Range *range_dst = &cur_range_vector->front();
	for (uint y = 0; y < n; ++y)
		for (uint x = 0; x < n; ++x)
		{
			range_dst->mMin = 0xffff;
			range_dst->mMax = 0;
			uint max_bx = x == n - 1? cBlockSize : cBlockSize + 1; // for interior blocks take 1 more because the triangles connect to the next block so we must include their height too
			uint max_by = y == n - 1? cBlockSize : cBlockSize + 1;
			for (uint by = 0; by < max_by; ++by)
				for (uint bx = 0; bx < max_bx; ++bx)
				{
					uint16 h = quantized_samples[(y * cBlockSize + by) * mSampleCount + (x * cBlockSize + bx)];
					if (h != cNoCollisionValue16)
					{
						range_dst->mMax = max(range_dst->mMax, h);
						range_dst->mMin = min(range_dst->mMin, h);
					}
				}
			++range_dst;
		}
		
	// Calculate remaining grids
	while (n > 1)
	{
		// Get source buffer
		Range *range_src = &cur_range_vector->front();

		// Previous array element
		--cur_range_vector;

		// Make space for this grid
		n >>= 1;
		cur_range_vector->resize(n * n);

		// Get target buffer
		range_dst = &cur_range_vector->front();

		// Combine the results of 2x2 ranges
		for (uint y = 0; y < n; ++y)
			for (uint x = 0; x < n; ++x)
			{
				range_dst->mMin = 0xffff;
				range_dst->mMax = 0;
				for (uint by = 0; by < 2; ++by)
					for (uint bx = 0; bx < 2; ++bx)
					{
						Range &r = range_src[(y * 2 + by) * n * 2 + x * 2 + bx];
						range_dst->mMax = max(range_dst->mMax, r.mMax);
						range_dst->mMin = min(range_dst->mMin, r.mMin);
					}
				++range_dst;
			}
	}
	JPH_ASSERT(cur_range_vector == &ranges.front());

	// Store global range for bounding box calculation
	mMinSample = ranges[0][0].mMin;
	mMaxSample = ranges[0][0].mMax;

#ifdef JPH_ENABLE_ASSERTS
	// Validate that we did not lose range along the way
	uint16 minv = 0xffff, maxv = 0;
	for (uint16 v : quantized_samples)
		if (v != cNoCollisionValue16)
		{
			minv = min(minv, v);
			maxv = max(maxv, v);
		}
	JPH_ASSERT(mMinSample == minv && mMaxSample == maxv);
#endif

	// Now erase the first element, we need a 2x2 grid to start with
	ranges.erase(ranges.begin());

	// Create blocks
	mRangeBlocks.reserve(sGridOffsets[ranges.size()]);
	for (uint level = 0; level < ranges.size(); ++level)
	{
		JPH_ASSERT(mRangeBlocks.size() == sGridOffsets[level]);

		n = 1 << level;

		for (uint y = 0; y < n; ++y)
			for (uint x = 0; x < n; ++x)
			{
				// Convert from 2x2 Range structure to 1 RangeBlock structure
				RangeBlock rb;
				for (uint by = 0; by < 2; ++by)
					for (uint bx = 0; bx < 2; ++bx)
					{
						uint src_pos = (y * 2 + by) * n * 2 + (x * 2 + bx);
						uint dst_pos = by * 2 + bx;
						rb.mMin[dst_pos] = ranges[level][src_pos].mMin;
						rb.mMax[dst_pos] = ranges[level][src_pos].mMax;
					}

				// Add this block
				mRangeBlocks.push_back(rb);
			}
	}	
	JPH_ASSERT(mRangeBlocks.size() == sGridOffsets[ranges.size()]);

	// Quantize height samples
	mHeightSamples.reserve(mSampleCount * mSampleCount);
	for (uint y = 0; y < mSampleCount; ++y)
		for (uint x = 0; x < mSampleCount; ++x)
		{
			uint bx = x / cBlockSize;
			uint by = y / cBlockSize;
			uint16 h = quantized_samples[y * mSampleCount + x];
			const Range &range = ranges.back()[by * (mSampleCount / cBlockSize) + bx];
			if (h == cNoCollisionValue16)
			{
				// No collision
				mHeightSamples.push_back(cNoCollisionValue8);
			}
			else
			{
				// Quantize to 8 bits
				float quantized_height = range.mMax == range.mMin? 0.0f : round(float(h - range.mMin) * float(cMaxHeightValue8) / float(range.mMax - range.mMin));
				JPH_ASSERT(quantized_height >= 0.0f && quantized_height <= float(cMaxHeightValue8));
				mHeightSamples.push_back(uint8(quantized_height));
			}
		}

	// Store active edges. The triangles are organized like this:
	//  +       +
	//  | \ T1B | \ T2B 
	// e0   e2  |   \
	//  | T1A \ | T2A \
	//  +--e1---+-------+
	//  | \ T3B | \ T4B 
	//  |   \   |   \
	//  | T3A \ | T4A \
	//  +-------+-------+
	// We store active edges e0 .. e2 as bits 0 .. 2. 
	// We store triangles horizontally then vertically (order T1A, T2A, T3A and T4A).
	// The top edge and right edge of the heightfield are always active so we do not need to store them, 
	// therefore we only need to store (mSampleCount - 1)^2 * 3-bit
	// The triangles T1B, T2B, T3B and T4B do not need to be stored, their active edges can be constructed from adjacent triangles.
	// Add 1 byte padding so we can always read 1 uint16 to get the bits that cross an 8 bit boundary
	uint count_min_1 = mSampleCount - 1;
	uint count_min_1_sq = Square(count_min_1);
	mActiveEdges.resize((count_min_1_sq * 3 + 7) / 8 + 1); 
	memset(&mActiveEdges[0], 0, mActiveEdges.size());

	// Calculate triangle normals and make normals zero for triangles that are missing
	vector<Vec3> normals;
	normals.resize(2 * count_min_1_sq);
	memset(&normals[0], 0, normals.size() * sizeof(Vec3));
	for (uint y = 0; y < count_min_1; ++y)
		for (uint x = 0; x < count_min_1; ++x)
			if (!IsNoCollision(x, y) && !IsNoCollision(x + 1, y + 1))
			{
				Vec3 x1y1 = GetPosition(x, y);
				Vec3 x2y2 = GetPosition(x + 1, y + 1);

				uint offset = 2 * (count_min_1 * y + x);

				if (!IsNoCollision(x, y + 1))
				{
					Vec3 x1y2 = GetPosition(x, y + 1);
					normals[offset] = (x2y2 - x1y2).Cross(x1y1 - x1y2).Normalized();
				}

				if (!IsNoCollision(x + 1, y))
				{
					Vec3 x2y1 = GetPosition(x + 1, y);
					normals[offset + 1] = (x1y1 - x2y1).Cross(x2y2 - x2y1).Normalized();
				}
			}

	// Calculate active edges
	for (uint y = 0; y < count_min_1; ++y)
		for (uint x = 0; x < count_min_1; ++x)
		{
			// Calculate vertex positions. 
			// We don't check 'no colliding' since those normals will be zero and sIsEdgeActive will return true
			Vec3 x1y1 = GetPosition(x, y);
			Vec3 x1y2 = GetPosition(x, y + 1);
			Vec3 x2y2 = GetPosition(x + 1, y + 1);

			// Calculate the edge flags (3 bits)
			uint offset = 2 * (count_min_1 * y + x);
			bool edge0_active = x == 0 || ActiveEdges::IsEdgeActive(normals[offset], normals[offset - 1], x1y2 - x1y1);
			bool edge1_active = y == count_min_1 - 1 || ActiveEdges::IsEdgeActive(normals[offset], normals[offset + 2 * count_min_1 + 1], x2y2 - x1y2);
			bool edge2_active = ActiveEdges::IsEdgeActive(normals[offset], normals[offset + 1], x1y1 - x2y2);
			uint16 edge_flags = (edge0_active? 0b001 : 0) | (edge1_active? 0b010 : 0) | (edge2_active? 0b100 : 0);

			// Store the edge flags in the array
			uint bit_pos = 3 * (y * count_min_1 + x);
			uint byte_pos = bit_pos >> 3;
			bit_pos &= 0b111;
			edge_flags <<= bit_pos;
			mActiveEdges[byte_pos] |= uint8(edge_flags);
			mActiveEdges[byte_pos + 1] |= uint8(edge_flags >> 8);
		}

	// Compress material indices
	if (mMaterials.size() > 1)
	{
		mNumBitsPerMaterialIndex = 32 - CountLeadingZeros((uint32)mMaterials.size() - 1);
		mMaterialIndices.resize(((Square(count_min_1) * mNumBitsPerMaterialIndex + 7) >> 3) + 1); // Add 1 byte so we don't read out of bounds when reading an uint16

		for (uint y = 0; y < count_min_1; ++y)
			for (uint x = 0; x < count_min_1; ++x)
			{
				// Read material
				uint sample_pos = x + y * count_min_1;
				uint16 material_index = uint16(inSettings.mMaterialIndices[sample_pos]);

				// Calculate byte and bit position where the material index needs to go
				uint bit_pos = sample_pos * mNumBitsPerMaterialIndex;
				uint byte_pos = bit_pos >> 3;
				bit_pos &= 0b111;

				// Write the material index
				material_index <<= bit_pos;
				JPH_ASSERT(byte_pos + 1 < mMaterialIndices.size());
				mMaterialIndices[byte_pos] |= uint8(material_index);
				mMaterialIndices[byte_pos + 1] |= uint8(material_index >> 8);
			}
	}

	outResult.Set(this);
}

void HeightFieldShape::GetBlockOffsetAndScale(uint inX, uint inY, float &outBlockOffset, float &outBlockScale) const
{
	JPH_ASSERT(inX < mSampleCount); 
	JPH_ASSERT(inY < mSampleCount); 

	// Calculate amount of grids
	uint num_blocks = mSampleCount / cBlockSize;
	uint max_level = CountTrailingZeros(num_blocks);

	// Get block location
	uint bx = inX / cBlockSize;
	uint by = inY / cBlockSize;

	// Convert to location of range block
	uint rbx = bx >> 1;
	uint rby = by >> 1;
	uint n = ((by & 1) << 1) + (bx & 1);

	// Calculate offset and scale
	const RangeBlock &block = mRangeBlocks[sGridOffsets[max_level - 1] + rby * (num_blocks >> 1) + rbx];
	outBlockOffset = float(block.mMin[n]);
	outBlockScale = float(block.mMax[n] - block.mMin[n]) / float(cMaxHeightValue8);
}

const Vec3 HeightFieldShape::GetPosition(uint inX, uint inY, float inBlockOffset, float inBlockScale) const
{ 
	JPH_ASSERT(inX < mSampleCount); 
	JPH_ASSERT(inY < mSampleCount); 

	return mOffset + mScale * Vec3(float(inX), inBlockOffset + float(mHeightSamples[inY * mSampleCount + inX]) * inBlockScale, float(inY)); 
}

const Vec3 HeightFieldShape::GetPosition(uint inX, uint inY) const
{
	float offset, scale;
	GetBlockOffsetAndScale(inX, inY, offset, scale);
	return GetPosition(inX, inY, offset, scale);
}

bool HeightFieldShape::IsNoCollision(uint inX, uint inY) const
{ 
	JPH_ASSERT(inX < mSampleCount); 
	JPH_ASSERT(inY < mSampleCount); 
	
	return mHeightSamples[inY * mSampleCount + inX] == cNoCollisionValue8; 
}

bool HeightFieldShape::ProjectOntoSurface(Vec3Arg inLocalPosition, Vec3 &outSurfacePosition, SubShapeID &outSubShapeID) const
{
	// Convert coordinate to integer space
	Vec3 integer_space = (inLocalPosition - mOffset) / mScale;

	// Get x coordinate and fraction
	float x_frac = integer_space.GetX();
	if (x_frac < 0.0f || x_frac >= mSampleCount - 1)
		return false;
	uint x = (uint)floor(x_frac);
	x_frac -= x;

	// Get y coordinate and fraction
	float y_frac = integer_space.GetZ();
	if (y_frac < 0.0f || y_frac >= mSampleCount - 1)
		return false;
	uint y = (uint)floor(y_frac);
	y_frac -= y;

	// If one of the diagonal points doesn't have collision, we don't have a height at this location
	if (IsNoCollision(x, y) || IsNoCollision(x + 1, y + 1))
		return false;

	if (y_frac >= x_frac)
	{
		// Left bottom triangle, test the 3rd point
		if (IsNoCollision(x, y + 1))
			return false;

		// Interpolate height value
		Vec3 v1 = GetPosition(x, y);
		Vec3 v2 = GetPosition(x, y + 1);
		Vec3 v3 = GetPosition(x + 1, y + 1);
		outSurfacePosition = v1 + y_frac * (v2 - v1) + x_frac * (v3 - v2);
		SubShapeIDCreator creator;
		outSubShapeID = EncodeSubShapeID(creator, x, y, 0);
		return true;
	}
	else
	{
		// Right top triangle, test the third point
		if (IsNoCollision(x + 1, y))
			return false;

		// Interpolate height value
		Vec3 v1 = GetPosition((uint)x, (uint)y);
		Vec3 v2 = GetPosition((uint)x + 1, (uint)y + 1);
		Vec3 v3 = GetPosition((uint)x + 1, (uint)y);
		outSurfacePosition = v1 + y_frac * (v2 - v3) + x_frac * (v3 - v1);
		SubShapeIDCreator creator;
		outSubShapeID = EncodeSubShapeID(creator, x, y, 1);
		return true;
	}
}

MassProperties HeightFieldShape::GetMassProperties() const
{
	// Object should always be static, return default mass properties
	return MassProperties();
}

const PhysicsMaterial *HeightFieldShape::GetMaterial(uint inX, uint inY) const
{
	if (mMaterials.empty())
		return PhysicsMaterial::sDefault;
	if (mMaterials.size() == 1)
		return mMaterials[0];

	uint count_min_1 = mSampleCount - 1;
	JPH_ASSERT(inX < count_min_1);
	JPH_ASSERT(inY < count_min_1);

	// Calculate at which bit the material index starts
	uint bit_pos = (inX + inY * count_min_1) * mNumBitsPerMaterialIndex;
	uint byte_pos = bit_pos >> 3;
	bit_pos &= 0b111;

	// Read the material index
	JPH_ASSERT(byte_pos + 1 < mMaterialIndices.size());
	const uint8 *material_indices = mMaterialIndices.data() + byte_pos;
	uint16 material_index = uint16(material_indices[0]) + uint16(uint16(material_indices[1]) << 8);
	material_index >>= bit_pos;
	material_index &= (1 << mNumBitsPerMaterialIndex) - 1;

	// Return the material
	return mMaterials[material_index];
}

uint HeightFieldShape::GetSubShapeIDBits() const
{
	// Need to store X, Y and 1 extra bit to specify the triangle number in the quad
	return 2 * CountTrailingZeros(mSampleCount) + 1;
}

SubShapeID HeightFieldShape::EncodeSubShapeID(const SubShapeIDCreator &inCreator, uint inX, uint inY, uint inTriangle) const
{
	return inCreator.PushID((inX + inY * mSampleCount) * 2 + inTriangle, GetSubShapeIDBits()).GetID();
}

void HeightFieldShape::DecodeSubShapeID(const SubShapeID &inSubShapeID, uint &outX, uint &outY, uint &outTriangle) const
{
	// Decode sub shape id
	SubShapeID remainder;
	uint32 id = inSubShapeID.PopID(GetSubShapeIDBits(), remainder);
	JPH_ASSERT(remainder.IsEmpty(), "Invalid subshape ID");

	// Get triangle index
	outTriangle = id & 1;
	id >>= 1;

	// Fetch the x and y coordinate
	outX = id % mSampleCount;
	outY = id / mSampleCount;
}

const PhysicsMaterial *HeightFieldShape::GetMaterial(const SubShapeID &inSubShapeID) const
{
	// Decode ID
	uint x, y, triangle;
	DecodeSubShapeID(inSubShapeID, x, y, triangle);

	// Fetch the material
	return GetMaterial(x, y);
}

Vec3 HeightFieldShape::GetSurfaceNormal(const SubShapeID &inSubShapeID, Vec3Arg inLocalSurfacePosition) const 
{ 
	// Decode ID
	uint x, y, triangle;
	DecodeSubShapeID(inSubShapeID, x, y, triangle);

	// Fetch vertices that both triangles share
	Vec3 x1y1 = GetPosition(x, y);
	Vec3 x2y2 = GetPosition(x + 1, y + 1);

	// Get normal depending on which triangle was selected
	Vec3 normal;
	if (triangle == 0)
	{
		Vec3 x1y2 = GetPosition(x, y + 1);
		normal = (x2y2 - x1y2).Cross(x1y1 - x1y2);
	}
	else
	{
		Vec3 x2y1 = GetPosition(x + 1, y);
		normal = (x1y1 - x2y1).Cross(x2y2 - x2y1);
	}

	return normal.Normalized();
}

inline uint8 HeightFieldShape::GetEdgeFlags(uint inX, uint inY, uint inTriangle) const
{
	if (inTriangle == 0)
	{
		// The edge flags for this triangle are directly stored, find the right 3 bits
		uint bit_pos = 3 * (inX + inY * (mSampleCount - 1));
		uint byte_pos = bit_pos >> 3;
		bit_pos &= 0b111;
		JPH_ASSERT(byte_pos + 1 < mActiveEdges.size());
		const uint8 *active_edges = mActiveEdges.data() + byte_pos;
		uint16 edge_flags = uint16(active_edges[0]) + uint16(uint16(active_edges[1]) << 8);
		return uint8(edge_flags >> bit_pos) & 0b111;
	}
	else
	{
		// We don't store this triangle directly, we need to look at our three neighbours to construct the edge flags
		uint8 edge0 = (GetEdgeFlags(inX, inY, 0) & 0b100) != 0? 0b001 : 0; // Diagonal edge
		uint8 edge1 = inX == mSampleCount - 1 || (GetEdgeFlags(inX + 1, inY, 0) & 0b001) != 0? 0b010 : 0; // Vertical edge
		uint8 edge2 = inY == 0 || (GetEdgeFlags(inX, inY - 1, 0) & 0b010) != 0? 0b100 : 0; // Horizontal edge
		return edge0 | edge1 | edge2;
	}
}

AABox HeightFieldShape::GetLocalBounds() const
{
	if (mMinSample == cNoCollisionValue16)
	{
		// This whole height field shape doesn't have any collision, return the center point
		Vec3 center = mOffset + 0.5f * mScale * Vec3(float(mSampleCount - 1), 0.0f, float(mSampleCount - 1));
		return AABox(center, center);
	}
	else
	{
		// Bounding box based on min and max sample height
		Vec3 bmin = mOffset + mScale * Vec3(0.0f, float(mMinSample), 0.0f);
		Vec3 bmax = mOffset + mScale * Vec3(float(mSampleCount - 1), float(mMaxSample), float(mSampleCount - 1));
		return AABox(bmin, bmax);
	}
}

#ifdef JPH_DEBUG_RENDERER
void HeightFieldShape::Draw(DebugRenderer *inRenderer, Mat44Arg inCenterOfMassTransform, Vec3Arg inScale, ColorArg inColor, bool inUseMaterialColors, bool inDrawWireframe) const
{
	// Reset the batch if we switch coloring mode
	if (mCachedUseMaterialColors != inUseMaterialColors)
	{
		mGeometry.clear();
		mCachedUseMaterialColors = inUseMaterialColors;
	}

	if (mGeometry.empty())
	{
		// Divide terrain in triangle batches of max 64x64x2 triangles to allow better culling of the terrain
		uint32 block_size = min<uint32>(mSampleCount, 64);
		for (uint32 by = 0; by < mSampleCount; by += block_size)
			for (uint32 bx = 0; bx < mSampleCount; bx += block_size)
			{
				// Create vertices for a block
				vector<DebugRenderer::Triangle> triangles;
				triangles.resize(block_size * block_size * 2);
				DebugRenderer::Triangle *out_tri = &triangles[0];
				for (uint32 y = by, max_y = min(by + block_size, mSampleCount - 1); y < max_y; ++y)
					for (uint32 x = bx, max_x = min(bx + block_size, mSampleCount - 1); x < max_x; ++x)
						if (!IsNoCollision(x, y) && !IsNoCollision(x + 1, y + 1))
						{
							Vec3 x1y1 = GetPosition(x, y);
							Vec3 x2y2 = GetPosition(x + 1, y + 1);
							Color color = inUseMaterialColors? GetMaterial(x, y)->GetDebugColor() : Color::sWhite;

							if (!IsNoCollision(x, y + 1))
							{
								Vec3 x1y2 = GetPosition(x, y + 1);

								x1y1.StoreFloat3(&out_tri->mV[0].mPosition);
								x1y2.StoreFloat3(&out_tri->mV[1].mPosition);
								x2y2.StoreFloat3(&out_tri->mV[2].mPosition);

								Vec3 normal = (x2y2 - x1y2).Cross(x1y1 - x1y2).Normalized();
								for (int i = 0; i < 3; ++i)
								{
									out_tri->mV[i].mColor = color;
									out_tri->mV[i].mUV = Float2(0, 0);
									normal.StoreFloat3(&out_tri->mV[i].mNormal);
								}

								++out_tri;
							}

							if (!IsNoCollision(x + 1, y))
							{
								Vec3 x2y1 = GetPosition(x + 1, y);

								x1y1.StoreFloat3(&out_tri->mV[0].mPosition);
								x2y2.StoreFloat3(&out_tri->mV[1].mPosition);
								x2y1.StoreFloat3(&out_tri->mV[2].mPosition);

								Vec3 normal = (x1y1 - x2y1).Cross(x2y2 - x2y1).Normalized();
								for (int i = 0; i < 3; ++i)
								{
									out_tri->mV[i].mColor = color;
									out_tri->mV[i].mUV = Float2(0, 0);
									normal.StoreFloat3(&out_tri->mV[i].mNormal);
								}

								++out_tri;
							}
						}

				// Resize triangles array to actual amount of triangles written
				size_t num_triangles = out_tri - &triangles[0];
				triangles.resize(num_triangles);

				// Create batch
				if (num_triangles > 0)
					mGeometry.push_back(new DebugRenderer::Geometry(inRenderer->CreateTriangleBatch(triangles), DebugRenderer::sCalculateBounds(&triangles[0].mV[0], int(3 * num_triangles))));
			}
	}

	// Get transform including scale
	Mat44 transform = inCenterOfMassTransform * Mat44::sScale(inScale);

	// Test if the shape is scaled inside out
	DebugRenderer::ECullMode cull_mode = ScaleHelpers::IsInsideOut(inScale)? DebugRenderer::ECullMode::CullFrontFace : DebugRenderer::ECullMode::CullBackFace;

	// Determine the draw mode
	DebugRenderer::EDrawMode draw_mode = inDrawWireframe? DebugRenderer::EDrawMode::Wireframe : DebugRenderer::EDrawMode::Solid;

	// Draw the geometry
	for (const DebugRenderer::GeometryRef &b : mGeometry)
		inRenderer->DrawGeometry(transform, inColor, b, cull_mode, DebugRenderer::ECastShadow::On, draw_mode);

	if (sDrawTriangleOutlines)
	{
		struct Visitor
		{
					Visitor(const HeightFieldShape *inShape, DebugRenderer *inRenderer, Mat44Arg inTransform) :
				mShape(inShape),
				mRenderer(inRenderer),
				mTransform(inTransform)
			{
			}

			bool	ShouldAbort() const
			{
				return false;
			}

			bool	ShouldVisitRangeBlock(int inStackTop) const
			{
				return true;
			}

			int		VisitRangeBlock(Vec4Arg inBoundsMinX, Vec4Arg inBoundsMinY, Vec4Arg inBoundsMinZ, Vec4Arg inBoundsMaxX, Vec4Arg inBoundsMaxY, Vec4Arg inBoundsMaxZ, UVec4 &ioProperties, int inStackTop) 
			{
				UVec4 valid = UVec4::sOr(UVec4::sOr(Vec4::sLess(inBoundsMinX, inBoundsMaxX), Vec4::sLess(inBoundsMinY, inBoundsMaxY)), Vec4::sLess(inBoundsMinZ, inBoundsMaxZ));
				UVec4::sSort4True(valid, ioProperties);
				return valid.CountTrues();
			}

			void	VisitTriangle(uint inX, uint inY, uint inTriangle, Vec3Arg inV0, Vec3Arg inV1, Vec3Arg inV2) 
			{			
				// Determine active edges
				uint8 active_edges = mShape->GetEdgeFlags(inX, inY, inTriangle);

				// Loop through edges
				Vec3 v[] = { inV0, inV1, inV2 };
				for (uint edge_idx = 0; edge_idx < 3; ++edge_idx)
				{
					Vec3 v1 = mTransform * v[edge_idx];
					Vec3 v2 = mTransform * v[(edge_idx + 1) % 3];

					// Draw active edge as a green arrow, other edges as grey
					if (active_edges & (1 << edge_idx))
						mRenderer->DrawArrow(v1, v2, Color::sGreen, 0.01f);
					else
						mRenderer->DrawLine(v1, v2, Color::sGrey);
				}
			}

			const HeightFieldShape *mShape;
			DebugRenderer *			mRenderer;
			Mat44					mTransform;
		};

		Visitor visitor { this, inRenderer, inCenterOfMassTransform * Mat44::sScale(inScale) };
		WalkHeightField(visitor);
	}
}
#endif // JPH_DEBUG_RENDERER

class HeightFieldShape::DecodingContext
{
public:
								DecodingContext(const HeightFieldShape *inShape) :
		mShape(inShape)
	{
		static_assert(sizeof(sGridOffsets) / sizeof(uint) == cNumBitsXY + 1, "Offsets array is not long enough");

		// Calculate amount of grids
		mMaxLevel = CountTrailingZeros(inShape->mSampleCount / cBlockSize);

		// Construct root stack entry
		mPropertiesStack[0] = 0; // level: 0, x: 0, y: 0

		// Splat offset and scale
		mOX = inShape->mOffset.SplatX();
		mOY = inShape->mOffset.SplatY();
		mOZ = inShape->mOffset.SplatZ();
		mSX = inShape->mScale.SplatX();
		mSY = inShape->mScale.SplatY();
		mSZ = inShape->mScale.SplatZ();

		// Precalculate some values
		mSampleCountMinOne = UVec4::sReplicate(inShape->mSampleCount - 1);
	}

	template <class Visitor>
	void						WalkHeightField(Visitor &ioVisitor)
	{
		do
		{
			// Decode properties
			uint32 properties_top = mPropertiesStack[mTop];
			uint32 x = properties_top & cMaskBitsXY;
			uint32 y = (properties_top >> cNumBitsXY) & cMaskBitsXY;
			uint32 level = properties_top >> cLevelShift;

			if (level >= mMaxLevel)
			{
				// Determine actual range of samples
				uint32 min_x = x * cBlockSize;
				uint32 max_x = min(min_x + cBlockSize + 1, mShape->mSampleCount);
				uint32 num_x = max_x - min_x;
				uint32 min_y = y * cBlockSize;
				uint32 max_y = min(min_y + cBlockSize + 1, mShape->mSampleCount);

				// Decompress vertices
				constexpr int array_size = Square(cBlockSize + 1);
				bool no_collision[array_size];
				bool *dst_no_collision = no_collision;
				Vec3 vertices[array_size];
				Vec3 *dst_vertex = vertices;
				for (uint32 v_y = min_y; v_y < max_y; ++v_y)
					for (uint32 v_x = min_x; v_x < max_x; ++v_x)
					{
						*dst_no_collision++ = mShape->IsNoCollision(v_x, v_y);
						*dst_vertex++ = mShape->GetPosition(v_x, v_y);
					}

				// Loop triangles
				max_x--;
				max_y--;
				for (uint32 v_y = min_y; v_y < max_y; ++v_y)
					for (uint32 v_x = min_x; v_x < max_x; ++v_x)
					{
						// Get first vertex
						const int offset = (v_y - min_y) * num_x + (v_x - min_x);
						const Vec3 *start_vertex = vertices + offset;
						const bool *start_no_collision = no_collision + offset;

						// Check if vertices shared by both triangles have collision
						if (!start_no_collision[0] && !start_no_collision[num_x + 1])
						{
							// Loop 2 triangles
							for (uint t = 0; t < 2; ++t)
							{
								// Determine triangle vertices
								Vec3 v0, v1, v2;
								if (t == 0)
								{
									// Check third vertex
									if (start_no_collision[num_x])
										continue;

									// Get vertices for triangle
									v0 = start_vertex[0];
									v1 = start_vertex[num_x];
									v2 = start_vertex[num_x + 1];
								}
								else
								{
									// Check third vertex
									if (start_no_collision[1])
										continue;

									// Get vertices for triangle
									v0 = start_vertex[0];
									v1 = start_vertex[num_x + 1];
									v2 = start_vertex[1];
								}

								// Call visitor
								ioVisitor.VisitTriangle(v_x, v_y, t, v0, v1, v2);
							}
						}
					}
			}
			else
			{
				// Visit child grid
				uint32 offset = sGridOffsets[level] + (1 << level) * y + x;

				// Decode min/max height
				UVec4 block = UVec4::sLoadInt4Aligned(reinterpret_cast<const uint32 *>(&mShape->mRangeBlocks[offset]));
				Vec4 bounds_miny = mOY + mSY * block.Expand4Uint16Lo().ToFloat();
				Vec4 bounds_maxy = mOY + mSY * block.Expand4Uint16Hi().ToFloat();

				// Calculate size of one cell at this grid level
				UVec4 internal_cell_size = UVec4::sReplicate(cBlockSize << (mMaxLevel - level - 1)); // subtract 1 from level because we have an internal grid of 2x2

				// Calculate min/max x and z
				UVec4 two_x = UVec4::sReplicate(2 * x); // multiply by two because we have an internal grid of 2x2
				Vec4 bounds_minx = mOX + mSX * (internal_cell_size * (two_x + UVec4(0, 1, 0, 1))).ToFloat();
				Vec4 bounds_maxx = mOX + mSX * UVec4::sMin(internal_cell_size * (two_x + UVec4(1, 2, 1, 2)), mSampleCountMinOne).ToFloat();

				UVec4 two_y = UVec4::sReplicate(2 * y);
				Vec4 bounds_minz = mOZ + mSZ * (internal_cell_size * (two_y + UVec4(0, 0, 1, 1))).ToFloat();
				Vec4 bounds_maxz = mOZ + mSZ * UVec4::sMin(internal_cell_size * (two_y + UVec4(1, 1, 2, 2)), mSampleCountMinOne).ToFloat();

				// Calculate properties of child blocks
				UVec4 properties = UVec4::sReplicate(((level + 1) << cLevelShift) + (y << (cNumBitsXY + 1)) + (x << 1)) + UVec4(0, 1, 1 << cNumBitsXY, (1 << cNumBitsXY) + 1);

			#ifdef JPH_DEBUG_HEIGHT_FIELD
				// Draw boxes
				for (int i = 0; i < 4; ++i)
				{
					AABox b(Vec3(bounds_minx[i], bounds_miny[i], bounds_minz[i]), Vec3(bounds_maxx[i], bounds_maxy[i], bounds_maxz[i]));
					if (b.IsValid())
						DebugRenderer::sInstance->DrawWireBox(b, Color::sGreen);
				}
			#endif

				// Check which sub nodes to visit
				int num_results = ioVisitor.VisitRangeBlock(bounds_minx, bounds_miny, bounds_minz, bounds_maxx, bounds_maxy, bounds_maxz, properties, mTop);

				// Push them onto the stack
				JPH_ASSERT(mTop + 4 < cStackSize);
				properties.StoreInt4(&mPropertiesStack[mTop]);
				mTop += num_results;		
			}

			// Check if we're done
			if (ioVisitor.ShouldAbort())
				break;

			// Fetch next node until we find one that the visitor wants to see
			do 
				--mTop;
			while (mTop >= 0 && !ioVisitor.ShouldVisitRangeBlock(mTop));
		}
		while (mTop >= 0);
	}

	// This can be used to have the visitor early out (ioVisitor.ShouldAbort() returns true) and later continue again (call WalkHeightField() again)
	bool						IsDoneWalking() const
	{
		return mTop < 0;
	}

private:
	const HeightFieldShape *	mShape;
	uint						mMaxLevel;
	uint32						mPropertiesStack[cStackSize];
	Vec4						mOX;
	Vec4						mOY;
	Vec4						mOZ;
	Vec4						mSX;
	Vec4						mSY;
	Vec4						mSZ;
	UVec4						mSampleCountMinOne;
	int							mTop = 0;
};

template <class Visitor>
void HeightFieldShape::WalkHeightField(Visitor &ioVisitor) const
{
	DecodingContext ctx(this);
	ctx.WalkHeightField(ioVisitor);
}

bool HeightFieldShape::CastRay(const RayCast &inRay, const SubShapeIDCreator &inSubShapeIDCreator, RayCastResult &ioHit) const
{
	JPH_PROFILE_FUNCTION();

	struct Visitor
	{
				Visitor(RayCastResult &ioHit) : 
			mHit(ioHit) 
		{ 
		}

		bool	ShouldAbort() const
		{
			return mHit.mFraction <= 0.0f;
		}

		bool	ShouldVisitRangeBlock(int inStackTop) const
		{
			return mDistanceStack[inStackTop] < mHit.mFraction;
		}

		int		VisitRangeBlock(Vec4Arg inBoundsMinX, Vec4Arg inBoundsMinY, Vec4Arg inBoundsMinZ, Vec4Arg inBoundsMaxX, Vec4Arg inBoundsMaxY, Vec4Arg inBoundsMaxZ, UVec4 &ioProperties, int inStackTop) 
		{
			// Test bounds of 4 children
			Vec4 distance = RayAABox4(mRayOrigin, mRayInvDirection, inBoundsMinX, inBoundsMinY, inBoundsMinZ, inBoundsMaxX, inBoundsMaxY, inBoundsMaxZ);
	
			// Sort so that highest values are first (we want to first process closer hits and we process stack top to bottom)
			Vec4::sSort4Reverse(distance, ioProperties);

			// Count how many results are closer
			UVec4 closer = Vec4::sLess(distance, Vec4::sReplicate(mHit.mFraction));
			int num_results = closer.CountTrues();

			// Shift the results so that only the closer ones remain
			distance = distance.ReinterpretAsInt().ShiftComponents4Minus(num_results).ReinterpretAsFloat();
			ioProperties = ioProperties.ShiftComponents4Minus(num_results);

			distance.StoreFloat4((Float4 *)&mDistanceStack[inStackTop]);
			return num_results;
		}

		void	VisitTriangle(uint inX, uint inY, uint inTriangle, Vec3Arg inV0, Vec3Arg inV1, Vec3Arg inV2) 
		{			
		#ifdef JPH_DEBUG_HEIGHT_FIELD
			float old_fraction = mHit.mFraction;
		#endif

			float fraction = RayTriangle(mRayOrigin, mRayDirection, inV0, inV1, inV2);
			if (fraction < mHit.mFraction)
			{
				// It's a closer hit
				mHit.mFraction = fraction;
				mHit.mSubShapeID2 = mShape->EncodeSubShapeID(mSubShapeIDCreator, inX, inY, inTriangle);
				mReturnValue = true;
			}
			
		#ifdef JPH_DEBUG_HEIGHT_FIELD
			DebugRenderer::sInstance->DrawWireTriangle(inV0, inV1, inV2, old_fraction > mHit.mFraction? Color::sRed : Color::sCyan);
		#endif
		}

		RayCastResult &				mHit;
		Vec3						mRayOrigin;
		Vec3						mRayDirection;
		RayInvDirection				mRayInvDirection;
		const HeightFieldShape *	mShape;
		SubShapeIDCreator			mSubShapeIDCreator;
		bool						mReturnValue = false;
		float						mDistanceStack[cStackSize];
	};

	Visitor visitor(ioHit);
	visitor.mRayOrigin = inRay.mOrigin;
	visitor.mRayDirection = inRay.mDirection;
	visitor.mRayInvDirection.Set(inRay.mDirection);
	visitor.mShape = this;
	visitor.mSubShapeIDCreator = inSubShapeIDCreator;

	WalkHeightField(visitor);

	return visitor.mReturnValue;
}

void HeightFieldShape::CastRay(const RayCast &inRay, const RayCastSettings &inRayCastSettings, const SubShapeIDCreator &inSubShapeIDCreator, CastRayCollector &ioCollector) const
{
	JPH_PROFILE_FUNCTION();

	struct Visitor
	{
				Visitor(CastRayCollector &ioCollector) : 
			mCollector(ioCollector) 
		{ 
		}

		bool	ShouldAbort() const
		{
			return mCollector.ShouldEarlyOut();
		}

		bool	ShouldVisitRangeBlock(int inStackTop) const
		{
			return mDistanceStack[inStackTop] < mCollector.GetEarlyOutFraction();
		}

		int		VisitRangeBlock(Vec4Arg inBoundsMinX, Vec4Arg inBoundsMinY, Vec4Arg inBoundsMinZ, Vec4Arg inBoundsMaxX, Vec4Arg inBoundsMaxY, Vec4Arg inBoundsMaxZ, UVec4 &ioProperties, int inStackTop) 
		{
			// Test bounds of 4 children
			Vec4 distance = RayAABox4(mRayOrigin, mRayInvDirection, inBoundsMinX, inBoundsMinY, inBoundsMinZ, inBoundsMaxX, inBoundsMaxY, inBoundsMaxZ);
	
			// Sort so that highest values are first (we want to first process closer hits and we process stack top to bottom)
			Vec4::sSort4Reverse(distance, ioProperties);

			// Count how many results are closer
			UVec4 closer = Vec4::sLess(distance, Vec4::sReplicate(mCollector.GetEarlyOutFraction()));
			int num_results = closer.CountTrues();

			// Shift the results so that only the closer ones remain
			distance = distance.ReinterpretAsInt().ShiftComponents4Minus(num_results).ReinterpretAsFloat();
			ioProperties = ioProperties.ShiftComponents4Minus(num_results);

			distance.StoreFloat4((Float4 *)&mDistanceStack[inStackTop]);
			return num_results;
		}

		void	VisitTriangle(uint inX, uint inY, uint inTriangle, Vec3Arg inV0, Vec3Arg inV1, Vec3Arg inV2) 
		{			
			// Back facing check
			if (mBackFaceMode == EBackFaceMode::IgnoreBackFaces && (inV2 - inV0).Cross(inV1 - inV0).Dot(mRayDirection) < 0)
				return;

			// Check the triangle
			float fraction = RayTriangle(mRayOrigin, mRayDirection, inV0, inV1, inV2);
			if (fraction < mCollector.GetEarlyOutFraction())
			{
				RayCastResult hit;
				hit.mBodyID = TransformedShape::sGetBodyID(mCollector.GetContext());
				hit.mFraction = fraction;
				hit.mSubShapeID2 = mShape->EncodeSubShapeID(mSubShapeIDCreator, inX, inY, inTriangle);
				mCollector.AddHit(hit);
			}
		}
		
		CastRayCollector &			mCollector;
		Vec3						mRayOrigin;
		Vec3						mRayDirection;
		RayInvDirection				mRayInvDirection;
		EBackFaceMode				mBackFaceMode;
		const HeightFieldShape *	mShape;
		SubShapeIDCreator			mSubShapeIDCreator;
		float						mDistanceStack[cStackSize];
	};

	Visitor visitor(ioCollector);
	visitor.mRayOrigin = inRay.mOrigin;
	visitor.mRayDirection = inRay.mDirection;
	visitor.mRayInvDirection.Set(inRay.mDirection);
	visitor.mBackFaceMode = inRayCastSettings.mBackFaceMode;
	visitor.mShape = this;
	visitor.mSubShapeIDCreator = inSubShapeIDCreator;

	WalkHeightField(visitor);
}

void HeightFieldShape::CollidePoint(Vec3Arg inPoint, const SubShapeIDCreator &inSubShapeIDCreator, CollidePointCollector &ioCollector) const
{
	// First test if we're inside our bounding box
	AABox bounds = GetLocalBounds();
	if (bounds.Contains(inPoint))
	{
		// Cast a ray that's 10% longer than the heigth of our bounding box downwards to see if we hit the surface
		RayCastResult result;
		if (!CastRay(RayCast { inPoint, -1.1f * bounds.GetSize().GetY() * Vec3::sAxisY() }, inSubShapeIDCreator, result))
			ioCollector.AddHit({ TransformedShape::sGetBodyID(ioCollector.GetContext()), inSubShapeIDCreator.GetID() });
	}
}

void HeightFieldShape::CastShape(const ShapeCast &inShapeCast, const ShapeCastSettings &inShapeCastSettings, Vec3Arg inScale, const ShapeFilter &inShapeFilter, Mat44Arg inCenterOfMassTransform2, const SubShapeIDCreator &inSubShapeIDCreator1, const SubShapeIDCreator &inSubShapeIDCreator2, CastShapeCollector &ioCollector) const 
{
	JPH_PROFILE_FUNCTION();

	struct Visitor : public CastConvexVsTriangles
	{
		using CastConvexVsTriangles::CastConvexVsTriangles;

		bool		ShouldAbort() const
		{
			return mCollector.ShouldEarlyOut();
		}

		bool		ShouldVisitRangeBlock(int inStackTop) const
		{
			return mDistanceStack[inStackTop] < mCollector.GetEarlyOutFraction();
		}

		int			VisitRangeBlock(Vec4Arg inBoundsMinX, Vec4Arg inBoundsMinY, Vec4Arg inBoundsMinZ, Vec4Arg inBoundsMaxX, Vec4Arg inBoundsMaxY, Vec4Arg inBoundsMaxZ, UVec4 &ioProperties, int inStackTop) 
		{
			// Scale the bounding boxes of this node 
			Vec4 bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z;
			AABox4Scale(mScale, inBoundsMinX, inBoundsMinY, inBoundsMinZ, inBoundsMaxX, inBoundsMaxY, inBoundsMaxZ, bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z);

			// Enlarge them by the casted shape's box extents
			AABox4EnlargeWithExtent(mBoxExtent, bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z);

			// Test bounds of 4 children
			Vec4 distance = RayAABox4(mBoxCenter, mInvDirection, bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z);
	
			// Sort so that highest values are first (we want to first process closer hits and we process stack top to bottom)
			Vec4::sSort4Reverse(distance, ioProperties);

			// Count how many results are closer
			UVec4 closer = Vec4::sLess(distance, Vec4::sReplicate(mCollector.GetEarlyOutFraction()));
			int num_results = closer.CountTrues();

			// Shift the results so that only the closer ones remain
			distance = distance.ReinterpretAsInt().ShiftComponents4Minus(num_results).ReinterpretAsFloat();
			ioProperties = ioProperties.ShiftComponents4Minus(num_results);

			distance.StoreFloat4((Float4 *)&mDistanceStack[inStackTop]);
			return num_results;
		}

		void	VisitTriangle(uint inX, uint inY, uint inTriangle, Vec3Arg inV0, Vec3Arg inV1, Vec3Arg inV2) 
		{			
			// Create sub shape id for this part
			SubShapeID triangle_sub_shape_id = mShape2->EncodeSubShapeID(mSubShapeIDCreator2, inX, inY, inTriangle);

			// Determine active edges
			uint8 active_edges = mShape2->GetEdgeFlags(inX, inY, inTriangle);

			Cast(inV0, inV1, inV2, active_edges, triangle_sub_shape_id);
		}

		const HeightFieldShape *	mShape2;
		RayInvDirection				mInvDirection;
		Vec3						mBoxCenter;
		Vec3						mBoxExtent;
		SubShapeIDCreator			mSubShapeIDCreator2;
		float						mDistanceStack[cStackSize];
	};

	Visitor visitor(inShapeCast, inShapeCastSettings, inScale, inShapeFilter, inCenterOfMassTransform2, inSubShapeIDCreator1, ioCollector);
	visitor.mShape2 = this;
	visitor.mInvDirection.Set(inShapeCast.mDirection);
	visitor.mBoxCenter = inShapeCast.mShapeWorldBounds.GetCenter();
	visitor.mBoxExtent = inShapeCast.mShapeWorldBounds.GetExtent();
	visitor.mSubShapeIDCreator2 = inSubShapeIDCreator2;
	WalkHeightField(visitor);
}

struct HeightFieldShape::HSGetTrianglesContext
{
			HSGetTrianglesContext(const HeightFieldShape *inShape, const AABox &inBox, Vec3Arg inPositionCOM, QuatArg inRotation, Vec3Arg inScale) : 
		mDecodeCtx(inShape),
		mShape(inShape),
		mLocalBox(Mat44::sInverseRotationTranslation(inRotation, inPositionCOM), inBox),
		mHeightFieldScale(inScale),
		mLocalToWorld(Mat44::sRotationTranslation(inRotation, inPositionCOM) * Mat44::sScale(inScale)),
		mIsInsideOut(ScaleHelpers::IsInsideOut(inScale))
	{
	}

	bool	ShouldAbort() const
	{
		return mShouldAbort;
	}

	bool	ShouldVisitRangeBlock(int inStackTop) const
	{
		return true;
	}

	int		VisitRangeBlock(Vec4Arg inBoundsMinX, Vec4Arg inBoundsMinY, Vec4Arg inBoundsMinZ, Vec4Arg inBoundsMaxX, Vec4Arg inBoundsMaxY, Vec4Arg inBoundsMaxZ, UVec4 &ioProperties, int inStackTop) 
	{
		// Scale the bounding boxes of this node 
		Vec4 bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z;
		AABox4Scale(mHeightFieldScale, inBoundsMinX, inBoundsMinY, inBoundsMinZ, inBoundsMaxX, inBoundsMaxY, inBoundsMaxZ, bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z);

		// Test which nodes collide
		UVec4 collides = AABox4VsBox(mLocalBox, bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z);

		// Sort so the colliding ones go first
		UVec4::sSort4True(collides, ioProperties);

		// Return number of hits
		return collides.CountTrues();
	}

	void	VisitTriangle(uint inX, uint inY, uint inTriangle, Vec3Arg inV0, Vec3Arg inV1, Vec3Arg inV2) 
	{			
		// When the buffer is full and we cannot process the triangles, abort the height field walk. The next time GetTrianglesNext is called we will continue here.
		if (mNumTrianglesFound + 1 > mMaxTrianglesRequested)
		{
			mShouldAbort = true;
			return;
		}

		// Store vertices as Float3
		if (mIsInsideOut)
		{
			// Reverse vertices
			(mLocalToWorld * inV0).StoreFloat3(mTriangleVertices++);
			(mLocalToWorld * inV2).StoreFloat3(mTriangleVertices++);
			(mLocalToWorld * inV1).StoreFloat3(mTriangleVertices++);
		}
		else
		{
			// Normal scale
			(mLocalToWorld * inV0).StoreFloat3(mTriangleVertices++);
			(mLocalToWorld * inV1).StoreFloat3(mTriangleVertices++);
			(mLocalToWorld * inV2).StoreFloat3(mTriangleVertices++);
		}

		// Decode material
		if (mMaterials != nullptr)
			*mMaterials++ = mShape->GetMaterial(inX, inY);

		// Accumulate triangles found
		mNumTrianglesFound++;
	}

	DecodingContext				mDecodeCtx;
	const HeightFieldShape *	mShape;
	OrientedBox					mLocalBox;
	Vec3						mHeightFieldScale;
	Mat44						mLocalToWorld;
	int							mMaxTrianglesRequested;
	Float3 *					mTriangleVertices;
	int							mNumTrianglesFound;
	const PhysicsMaterial **	mMaterials;
	bool						mShouldAbort;
	bool						mIsInsideOut;
};

void HeightFieldShape::GetTrianglesStart(GetTrianglesContext &ioContext, const AABox &inBox, Vec3Arg inPositionCOM, QuatArg inRotation, Vec3Arg inScale) const
{
	static_assert(sizeof(HSGetTrianglesContext) <= sizeof(GetTrianglesContext), "GetTrianglesContext too small");
	JPH_ASSERT(IsAligned(&ioContext, alignof(HSGetTrianglesContext)));

	new (&ioContext) HSGetTrianglesContext(this, inBox, inPositionCOM, inRotation, inScale);
}

int HeightFieldShape::GetTrianglesNext(GetTrianglesContext &ioContext, int inMaxTrianglesRequested, Float3 *outTriangleVertices, const PhysicsMaterial **outMaterials) const
{
	static_assert(cGetTrianglesMinTrianglesRequested >= 1, "cGetTrianglesMinTrianglesRequested is too small");
	JPH_ASSERT(inMaxTrianglesRequested >= cGetTrianglesMinTrianglesRequested);

	// Check if we're done
	HSGetTrianglesContext &context = (HSGetTrianglesContext &)ioContext;
	if (context.mDecodeCtx.IsDoneWalking())
		return 0;

	// Store parameters on context
	context.mMaxTrianglesRequested = inMaxTrianglesRequested;
	context.mTriangleVertices = outTriangleVertices;
	context.mMaterials = outMaterials;
	context.mShouldAbort = false; // Reset the abort flag
	context.mNumTrianglesFound = 0;
	
	// Continue (or start) walking the height field
	context.mDecodeCtx.WalkHeightField(context);
	return context.mNumTrianglesFound;
}

void HeightFieldShape::sCollideConvexVsHeightField(const ConvexShape *inShape1, const HeightFieldShape *inShape2, Vec3Arg inScale1, Vec3Arg inScale2, Mat44Arg inCenterOfMassTransform1, Mat44Arg inCenterOfMassTransform2, const SubShapeIDCreator &inSubShapeIDCreator1, const SubShapeIDCreator &inSubShapeIDCreator2, const CollideShapeSettings &inCollideShapeSettings, CollideShapeCollector &ioCollector)
{
	JPH_PROFILE_FUNCTION();

	struct Visitor : public CollideConvexVsTriangles
	{
		using CollideConvexVsTriangles::CollideConvexVsTriangles;

		bool	ShouldAbort() const
		{
			return mCollector.ShouldEarlyOut();
		}

		bool	ShouldVisitRangeBlock(int inStackTop) const
		{
			return true;
		}

		int		VisitRangeBlock(Vec4Arg inBoundsMinX, Vec4Arg inBoundsMinY, Vec4Arg inBoundsMinZ, Vec4Arg inBoundsMaxX, Vec4Arg inBoundsMaxY, Vec4Arg inBoundsMaxZ, UVec4 &ioProperties, int inStackTop) 
		{
			// Scale the bounding boxes of this node
			Vec4 bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z;
			AABox4Scale(mScale2, inBoundsMinX, inBoundsMinY, inBoundsMinZ, inBoundsMaxX, inBoundsMaxY, inBoundsMaxZ, bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z);

			// Test which nodes collide
			UVec4 collides = AABox4VsBox(mBoundsOf1InSpaceOf2, bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z);

			// Sort so the colliding ones go first
			UVec4::sSort4True(collides, ioProperties);

			// Return number of hits
			return collides.CountTrues();
		}

		void	VisitTriangle(uint inX, uint inY, uint inTriangle, Vec3Arg inV0, Vec3Arg inV1, Vec3Arg inV2) 
		{			
			// Create ID for triangle
			SubShapeID triangle_sub_shape_id = mShape2->EncodeSubShapeID(mSubShapeIDCreator2, inX, inY, inTriangle);

			// Determine active edges
			uint8 active_edges = mShape2->GetEdgeFlags(inX, inY, inTriangle);

			Collide(inV0, inV1, inV2, active_edges, triangle_sub_shape_id);
		}

		const HeightFieldShape *		mShape2;
		SubShapeIDCreator				mSubShapeIDCreator2;
	};

	Visitor visitor(inShape1, inScale1, inScale2, inCenterOfMassTransform1, inCenterOfMassTransform2, inSubShapeIDCreator1.GetID(), inCollideShapeSettings, ioCollector);
	visitor.mShape2 = inShape2;
	visitor.mSubShapeIDCreator2 = inSubShapeIDCreator2;
	inShape2->WalkHeightField(visitor);
}

void HeightFieldShape::SaveBinaryState(StreamOut &inStream) const
{
	Shape::SaveBinaryState(inStream);

	inStream.Write(mOffset);
	inStream.Write(mScale);
	inStream.Write(mSampleCount);
	inStream.Write(mMinSample);
	inStream.Write(mMaxSample);
	inStream.Write(mRangeBlocks);
	inStream.Write(mHeightSamples);
	inStream.Write(mActiveEdges);
	inStream.Write(mMaterialIndices);
	inStream.Write(mNumBitsPerMaterialIndex);
}

void HeightFieldShape::RestoreBinaryState(StreamIn &inStream)
{
	Shape::RestoreBinaryState(inStream);

	inStream.Read(mOffset);
	inStream.Read(mScale);
	inStream.Read(mSampleCount);
	inStream.Read(mMinSample);
	inStream.Read(mMaxSample);
	inStream.Read(mRangeBlocks);
	inStream.Read(mHeightSamples);
	inStream.Read(mActiveEdges);
	inStream.Read(mMaterialIndices);
	inStream.Read(mNumBitsPerMaterialIndex);
}

void HeightFieldShape::SaveMaterialState(PhysicsMaterialList &outMaterials) const
{ 
	outMaterials = mMaterials;
}

void HeightFieldShape::RestoreMaterialState(const PhysicsMaterialList &inMaterials) 
{ 
	mMaterials = inMaterials;
}

Shape::Stats HeightFieldShape::GetStats() const 
{ 
	return Stats(
		sizeof(*this) 
			+ mMaterials.size() * sizeof(Ref<PhysicsMaterial>) 
			+ mRangeBlocks.size() * sizeof(RangeBlock) 
			+ mHeightSamples.size() * sizeof(uint8) 
			+ mActiveEdges.size() * sizeof(uint8) 
			+ mMaterialIndices.size() * sizeof(uint8), 
		Square(mSampleCount - 1) * 2); 
}

} // JPH