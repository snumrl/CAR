#ifndef __DEEP_PHYSICS_REFERENCE_MANAGER_H__
#define __DEEP_PHYSICS_REFERENCE_MANAGER_H__

#include "Functions.h"
#include "Character.h"
#include "CharacterConfigurations.h"
#include "BVH.h"

#include <tuple>


namespace DPhy
{
class Motion
{
public:
	Motion(Motion* m) {
		position = m->position;
		velocity = m->velocity;
	}
	Motion(Eigen::VectorXd pos, Eigen::VectorXd vel) {
		position = pos;
		velocity = vel;
	}
	void SetPosition(Eigen::VectorXd pos) { position = pos; }
	void SetVelocity(Eigen::VectorXd vel) { velocity = vel; }
	Eigen::VectorXd GetPosition() { return position; }
	Eigen::VectorXd GetVelocity() { return velocity; }

protected:
	Eigen::VectorXd position;
	Eigen::VectorXd velocity;
};
class ReferenceManager
{
public:
	ReferenceManager(Character* character=nullptr);
	void LoadMotionFromBVH(std::string filename);
	void LoadWorkFromStats(std::string filename);
	void LoadContactInfoFromBVHData(std::string filename);
	void GenerateMotionsFromSinglePhase(int frames, bool blend);
	void RescaleMotion(double w);
	Motion* GetMotion(double t);
	double GetTimeStep() {return mTimeStep; }
	int GetPhaseLength() {return mPhaseLength; }
	std::pair<bool, bool> CalculateContactInfo(Eigen::VectorXd p, Eigen::VectorXd v);

protected:
	Character* mCharacter;
	double mTimeStep;
	int mBlendingInterval;
	int mPhaseLength;
	std::vector<Motion*> mMotions_raw;
	std::vector<Motion*> mMotions_phase;
	std::vector<Motion*> mMotions_gen;

};
}

#endif