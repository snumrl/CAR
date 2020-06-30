#ifndef __DEEP_PHYSICS_REFERENCE_MANAGER_H__
#define __DEEP_PHYSICS_REFERENCE_MANAGER_H__

#include "Functions.h"
#include "Character.h"
#include "CharacterConfigurations.h"
#include "BVH.h"
#include "MultilevelSpline.h"
#include <tuple>
#include <mutex>

namespace DPhy
{
class Motion
{
public:
	Motion(Motion* m) {
		position = m->position;
		velocity = m->velocity;
		// rightContact = m->rightContact;
		// leftContact = m->leftContact;
	}
	Motion(Eigen::VectorXd pos) {
		position = pos;
	}
	Motion(Eigen::VectorXd pos, Eigen::VectorXd vel) {
		position = pos;
		velocity = vel;
	}
	void SetPosition(Eigen::VectorXd pos) { position = pos; }
	void SetVelocity(Eigen::VectorXd vel) { velocity = vel; }
	// void SetLeftContact(bool contact) { leftContact = contact; }
	// void SetRightContact(bool contact) { rightContact = contact; }

	Eigen::VectorXd GetPosition() { return position; }
	Eigen::VectorXd GetVelocity() { return velocity; }
	// bool GetLeftContact() { return leftContact; }
	// bool GetRightContact() { return rightContact; }

protected:
	Eigen::VectorXd position;
	Eigen::VectorXd velocity;
	// bool rightContact;
	// bool leftContact;
};
class ReferenceManager
{
public:
	ReferenceManager(Character* character=nullptr);
	void SaveAdaptiveMotion();
	void LoadAdaptiveMotion();
	void LoadMotionFromBVH(std::string filename);
	void GenerateMotionsFromSinglePhase(int frames, bool blend, bool adaptive=false);
	void RescaleMotion(double w);
	Motion* GetMotion(double t, bool adaptive=false);
	std::vector<Eigen::VectorXd> GetVelocityFromPositions(std::vector<Eigen::VectorXd> pos); 
	Eigen::VectorXd GetPosition(double t, bool adaptive=false);
	double GetTimeStep() {return mTimeStep; }
	int GetPhaseLength() {return mPhaseLength; }
	void ComputeDeviation();
	Eigen::VectorXd GetAxis(double t);
	Eigen::VectorXd GetDev(double t);

	void SaveTrajectories(std::vector<std::pair<Eigen::VectorXd,double>> data_spline, double rewards);
	void InitOptimization(std::string save_path);
	void Optimize();
protected:
	Character* mCharacter;
	double mTimeStep;
	int mBlendingInterval;
	int mPhaseLength;
	std::vector<bool> mFootSliding;
	std::vector<Motion*> mMotions_raw;
	std::vector<Motion*> mMotions_phase;
	std::vector<Motion*> mMotions_phase_adaptive;
	std::vector<Motion*> mMotions_gen;
	std::vector<Motion*> mMotions_gen_adaptive;
	std::vector<double> mIdxs;
	
	std::vector<Eigen::VectorXd> mAxis_BVH;
	std::vector<Eigen::VectorXd> mDev_BVH;

	std::vector<std::pair<MultilevelSpline*, double>> mSamples;
	std::vector<Eigen::VectorXd> mDisplacement;
	std::vector<double> mKnots;
	std::vector<std::string> mInterestedBodies;

	double mSlaves;
	std::mutex mLock;
	bool mSaveTrajectory;
	std::string mPath;
	double mDOF;
};
}

#endif