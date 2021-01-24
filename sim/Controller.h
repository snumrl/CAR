#ifndef __DEEP_PHYSICS_CONTROLLER_H__
#define __DEEP_PHYSICS_CONTROLLER_H__
#include "dart/dart.hpp"
#include "BVH.h"
#include "CharacterConfigurations.h"
#include "SkeletonBuilder.h"
#include "Functions.h"
#include "ReferenceManager.h"
#include <tuple>
#include <queue>
namespace DPhy
{
/**
*
* @brief World class expresses individual virtual world which contains character and ground.
* @details Character and ground are agent and ground information respectively. Each world contains both of them and also able to interactive environment status with super level.
* 
*/
class Controller
{
public:
Controller(ReferenceManager* ref, bool adaptive=true, bool parametric=true, bool record=false, int id=0);

	void Step();
	void UpdateReward();
	void UpdateTerminalInfo();
	void Reset(bool RSI=true);
	int GetTerminationReason() {return terminationReason; }
	int GetNumState();
	int GetNumAction();
	Eigen::VectorXd GetEndEffectorStatePosAndVel(const Eigen::VectorXd pos, const Eigen::VectorXd vel);
	Eigen::VectorXd GetState();

	
	bool FollowBvh();

	bool IsTerminalState() {return this->mIsTerminal; }
	bool IsNanAtTerminal() {return this->mIsNanAtTerminal;}
	bool IsTimeEnd(){
		if(this->terminationReason == 8)
			return true;
		else
			return false;
	}

	bool CheckCollisionWithGround(std::string bodyName);
	bool CheckCollisionWithObject(std::string bodyName);
	void SetAction(const Eigen::VectorXd& action);
	double GetReward() {return mRewardParts[0]; }
	std::vector<double> GetRewardByParts() {return mRewardParts; }
	std::vector<std::string> GetRewardLabels() {return mRewardLabels; }
	const dart::simulation::WorldPtr& GetWorld() {return mWorld;}

	double GetTimeElapsed(){return this->mTimeElapsed;}
	double GetCurrentFrame(){return this->mCurrentFrame;}
	double GetCurrentLength() {return this->mCurrentFrame - this->mStartFrame; }
	double GetStartFrame(){ return this->mStartFrame; }

	const dart::dynamics::SkeletonPtr& GetSkeleton();

	void SaveDisplayedData(std::string directory, bool bvh=false);
	void SaveTimeData(std::string directory);
	void SaveStepInfo();
	void ClearRecord();

	// get record (for visualization)

	Eigen::VectorXd GetObjPositions(int idx, bool start=true) {if(start) return this->mRecordObjPosition_s[idx]; else return this->mRecordObjPosition_e[idx]; }
	Eigen::VectorXd GetPositions(int idx) { return this->mRecordPosition[idx]; }
	Eigen::Vector3d GetCOM(int idx) { return this->mRecordCOM[idx]; }
	Eigen::VectorXd GetVelocities(int idx) { return this->mRecordVelocity[idx]; }
	double GetPhase(int idx) { return this->mRecordPhase[idx]; }
	Eigen::VectorXd GetTargetPositions(int idx) { return this->mRecordTargetPosition[idx]; }
	Eigen::VectorXd GetBVHPositions(int idx) { return this->mRecordBVHPosition[idx]; }
	int GetRecordSize() { return this->mRecordPosition.size(); }
	std::pair<bool, bool> GetFootContact(int idx) { return this->mRecordFootContact[idx]; }

 	// functions related to adaptive motion retargeting
	void RescaleCharacter(double w0, double w1);	
	std::tuple<double, double, double> GetRescaleParameter() { return mRescaleParameter; }
	
	void UpdateAdaptiveReward();
	void UpdateRewardTrajectory();
	double GetParamReward();
	double GetSimilarityReward();
	std::vector<double> GetTrackingReward(Eigen::VectorXd position, Eigen::VectorXd position2, Eigen::VectorXd velocity, Eigen::VectorXd velocity2, std::vector<std::string> list, bool useVelocity);
	std::vector<std::pair<bool, Eigen::Vector3d>> GetContactInfo(Eigen::VectorXd pos, double base_height=0);

	void SetGoalParameters(Eigen::VectorXd tp);
	void SetSkeletonWeight(double mass);

	Eigen::Isometry3d getLocalSpaceTransform(const dart::dynamics::SkeletonPtr& Skel);

	Eigen::VectorXd GetGoalParameters(){return mParamGoal;}
protected:
	dart::simulation::WorldPtr mWorld;
	double w_p,w_v,w_com,w_ee;
	double mStartFrame;
	double mCurrentFrame; // for discrete ref motion
	double mTimeElapsed;
	int mControlHz;
	int mSimulationHz;
	int mSimPerCon;
	double mCurrentFrameOnPhase;
	int nTotalSteps;
	bool isAdaptive;
	bool isParametric;
	
	int id;
	double mPrevFrameOnPhase;
	double mParamRewardTrajectory;
	double mTrackingRewardTrajectory;

	Character* mCharacter;
	Character* mObject_start;
	Character* mObject_end;
	ReferenceManager* mReferenceManager;
	dart::dynamics::SkeletonPtr mGround;

	Eigen::VectorXd mTargetPositions;
	Eigen::VectorXd mTargetVelocities;

	Eigen::VectorXd mPDTargetPositions;
	Eigen::VectorXd mPDTargetVelocities;

	Eigen::VectorXd mActions;
	double mAdaptiveStep;

	std::vector<std::string> mInterestedBodies;
	std::vector<std::string> mRewardBodies;
	int mInterestedDof;
	int mRewardDof;

	std::vector<std::string> mEndEffectors;
	std::vector<std::string> mRewardLabels;
	std::vector<double> mRewardParts;
	std::vector<double> mRewardSimilarity;
	// for foot collision, left, right foot, ground
	std::unique_ptr<dart::collision::CollisionGroup> mCGEL, mCGER, mCGL, mCGR, mCGG, mCGHR, mCGHL, mCGOBJ_s, mCGOBJ_e; 

	std::vector<Eigen::VectorXd> mRecordPosition;
	std::vector<Eigen::VectorXd> mRecordVelocity;
	std::vector<Eigen::Vector3d> mRecordCOM;
	std::vector<Eigen::VectorXd> mRecordTargetPosition;
	std::vector<Eigen::VectorXd> mRecordBVHPosition;
	std::vector<Eigen::VectorXd> mRecordObjPosition_s;
	std::vector<Eigen::VectorXd> mRecordObjPosition_e;
	
	std::vector<std::pair<bool, bool>> mRecordFootContact;
	std::vector<double> mRecordPhase;

	bool mIsTerminal;
	bool mIsNanAtTerminal;
	bool mRecord;
	std::tuple<double, double, double> mRescaleParameter;
	std::vector<Eigen::Vector6d> mRecordCOMVelocity;
	std::vector<Eigen::Vector3d> mRecordCOMPositionRef;
	std::vector<std::string> mContacts;

	int mNumState, mNumAction;

	int terminationReason;

	Eigen::VectorXd mTlPrev;
	Eigen::VectorXd mTlPrev2;
	Eigen::VectorXd mPrevPositions;

	double mPrevFrame;
	double mPrevFrame2;
	Eigen::Vector6d mRootZero;
	Eigen::Vector6d mDefaultRootZero;

	Eigen::VectorXd mPrevTargetPositions;
	Eigen::VectorXd mControlFlag;

	Eigen::VectorXd mParamGoal;
	Eigen::VectorXd mParamCur;

	std::vector<std::pair<Eigen::VectorXd,double>> data_raw;

	int mCountParam;
	int mCountTracking;

	Eigen::Vector3d mGravity;
	double mMass;

	Eigen::Vector3d mBaseGravity;
	double mBaseMass;

	// double mPrevVelocity;
	// double mVelocity;
	// Eigen::Vector3d mEnergy;

	Eigen::Vector3d mStartRoot; //root 0th frame
	Eigen::Vector3d mRootZeroDiff; //root 0th frame
	Eigen::Vector3d mStartFoot; //middle of two feet at 0th frame
	std::vector<double> foot_diff;

	Eigen::Vector3d prevLeftToe;
	Eigen::Vector3d prevRightToe;
	bool stickFoot;

	Fitness mFitness;
	std::queue<Eigen::VectorXd> mPosQueue;
	std::queue<double> mTimeQueue;


	bool mLanded;
	double mean_land_foot;
	int land_foot_cnt;
	// double min_land_foot;

	bool gotParamReward;	
	bool gotParamReward_z_v;	

	bool placedObject; 


	int jump_phase; //0 stand, 1 jump (in the air) 2 landed

	double stickFoot_left_min;
	double stickFoot_left_max;
	double stickFoot_right_min;
	double stickFoot_right_max;

	Eigen::Vector3d stickLeftFoot; 
	Eigen::Vector3d stickRightFoot; 

	int v_count = 0;
	Eigen::Vector3d prev_com_v;
};
}
#endif
