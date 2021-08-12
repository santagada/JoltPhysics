// SPDX-FileCopyrightText: 2021 Jorrit Rouwe
// SPDX-License-Identifier: MIT

#pragma once

#include <Tests/Test.h>
#include <Physics/Ragdoll/Ragdoll.h>

// Test that contains a sensor that will apply forces to bodies inside the sensor
class SensorTest : public Test, public ContactListener
{
public:
	JPH_DECLARE_RTTI_VIRTUAL(SensorTest)

	virtual				~SensorTest() override;

	// Number used to scale the terrain and camera movement to the scene
	virtual float		GetWorldScale() const override		{ return 0.2f; }

	// See: Test
	virtual void		Initialize() override;
	virtual void		PrePhysicsUpdate(const PreUpdateParams &inParams) override;

	// If this test implements a contact listener, it should be returned here
	virtual ContactListener *GetContactListener() override	{ return this; }

	// See: ContactListener
	virtual void		OnContactAdded(const Body &inBody1, const Body &inBody2, const ContactManifold &inManifold, ContactSettings &ioSettings) override;
	virtual void		OnContactRemoved(const SubShapeIDPair &inSubShapePair) override;
	
	// Saving / restoring state for replay
	virtual void		SaveState(StateRecorder &inStream) const override;
	virtual void		RestoreState(StateRecorder &inStream) override;

private:
	BodyID				mSensorID;							// Body ID of the sensor

	Ref<Ragdoll>		mRagdoll;

	Mutex				mMutex;								// Mutex that protects mBodiesInSensor

	// Structure that keeps track of how many contact point each body has with the sensor
	struct BodyAndCount
	{
		BodyID			mBodyID;
		int				mCount;

		bool			operator < (const BodyAndCount &inRHS) const { return mBodyID < inRHS.mBodyID; }
	};

	using BodiesInSensor = vector<BodyAndCount>;
	BodiesInSensor		mBodiesInSensor;					// Bodies that are currently inside the sensor
};