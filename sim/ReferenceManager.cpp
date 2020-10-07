#include "ReferenceManager.h"
#include <tinyxml.h>
#include <fstream>
#include <stdlib.h>
#include <cmath>

using namespace dart::dynamics;
namespace DPhy
{
ReferenceManager::ReferenceManager(Character* character)
{
	mCharacter = character;
	mBlendingInterval = 10;
	
	mMotions_gen.clear();
	mMotions_raw.clear();
	mMotions_phase.clear();


	auto& skel = mCharacter->GetSkeleton();
	mDOF = skel->getPositions().rows();

	mTargetBase.resize(1);
	mTargetUnit.resize(1);
	mTargetGoal.resize(1);

}
void 
ReferenceManager::
SaveAdaptiveMotion(std::string postfix) {
	std::string path = mPath + std::string("adaptive") + postfix;
	std::cout << "save motion to:" << path << std::endl;

	std::ofstream ofs(path);

	for(int i = 0; i < mMotions_phase_adaptive.size(); i++) {
		ofs << mMotions_phase_adaptive[i]->GetPosition().transpose() << std::endl;
		ofs << mMotions_phase_adaptive[i]->GetVelocity().transpose() << std::endl;
		ofs << mTimeStep_adaptive[i] << std::endl;

	}
	ofs.close();
	
	path = mPath + std::string("cp") + postfix;
	ofs.open(path);
	ofs << mKnots.size() << std::endl;
	for(auto t: mKnots) {	
		ofs << t << std::endl;
	}
		
	for(auto t: mPrevCps) {	
		ofs << t.transpose() << std::endl;
	}
	ofs.close();

	path = mPath + std::string("time") + postfix;
	std::cout << "save results to" << path << std::endl;
	
	ofs.open(path);
	ofs << mPhaseLength << std::endl;

	for(int i = 0; i < mPhaseLength; i++) {
		ofs << i << " " << i << " " << mTimeStep_adaptive[i] << std::endl;
	}
	ofs.close();

}
void 
ReferenceManager::
LoadAdaptiveMotion(std::vector<Eigen::VectorXd> cps) {
	DPhy::MultilevelSpline* s = new DPhy::MultilevelSpline(1, mPhaseLength);

	s->SetKnots(0, mKnots);
	s->SetControlPoints(0, cps);

	std::vector<Eigen::VectorXd> newpos;
	std::vector<Eigen::VectorXd> new_displacement = s->ConvertSplineToMotion();
	this->AddDisplacementToBVH(new_displacement, newpos);
	std::vector<Eigen::VectorXd> newvel = this->GetVelocityFromPositions(newpos);

	for(int j = 0; j < mPhaseLength; j++) {
		mMotions_phase_adaptive[j]->SetPosition(newpos[j]);
		mMotions_phase_adaptive[j]->SetVelocity(newvel[j]);
	}

	this->GenerateMotionsFromSinglePhase(1000, false, mMotions_phase_adaptive, mMotions_gen_adaptive);

}
void 
ReferenceManager::
LoadAdaptiveMotion(std::string postfix) {
	std::string path = mPath + std::string("adaptive") + postfix;

	std::ifstream is(path);
	if(is.fail())
		return;
	std::cout << "load Motion from: " << path << std::endl;

	char buffer[256];
	for(int i = 0; i < mPhaseLength; i++) {
		Eigen::VectorXd pos(mDOF);
		Eigen::VectorXd vel(mDOF);
		for(int j = 0; j < mDOF; j++) 
		{
			is >> buffer;
			pos[j] = atof(buffer);
		}
		for(int j = 0; j < mDOF; j++) 
		{
			is >> buffer;
			vel[j] = atof(buffer);
		}
		mMotions_phase_adaptive[i]->SetPosition(pos);
		mMotions_phase_adaptive[i]->SetVelocity(vel);
		is >> buffer;
		mTimeStep_adaptive[i] = atof(buffer);
	}
	is.close();
	
	path = mPath + std::string("cp") + postfix;
	is.open(path);
	mKnots.clear();
	is >> buffer;
	int knot_size = atoi(buffer);
	for(int i = 0; i < knot_size; i++) {	
		is >> buffer;
		mKnots.push_back(atoi(buffer));
	}
	for(int i = 0; i < knot_size; i++) {	
		Eigen::VectorXd cps(mDOF);	
		for(int j = 0; j < mDOF; j++) {
			is >> buffer;
			cps[j] = atof(buffer);
		}
		mPrevCps[i] = cps;
	}
	is.close();

	this->GenerateMotionsFromSinglePhase(1000, false, mMotions_phase_adaptive, mMotions_gen_adaptive);

}
void 
ReferenceManager::
LoadMotionFromBVH(std::string filename)
{
	mMotions_raw.clear();
	mMotions_phase.clear();
	
	this->mCharacter->LoadBVHMap();

	BVH* bvh = new BVH();
	std::string path = std::string(CAR_DIR) + filename;
	bvh->Parse(path);
	std::cout << "load trained data from: " << path << std::endl;

	std::vector<std::string> contact;
	contact.clear();
	contact.push_back("RightToe");
	contact.push_back("RightFoot");
	contact.push_back("LeftToe");
	contact.push_back("LeftFoot");

	auto& skel = mCharacter->GetSkeleton();
	int dof = skel->getPositions().rows();
	std::map<std::string,std::string> bvhMap = mCharacter->GetBVHMap(); 
	for(const auto ss :bvhMap){
		bvh->AddMapping(ss.first,ss.second);
	}

	double t = 0;
	for(int i = 0; i < bvh->GetMaxFrame(); i++)
	{
		Eigen::VectorXd p = Eigen::VectorXd::Zero(dof);
		Eigen::VectorXd p1 = Eigen::VectorXd::Zero(dof);

		//Set p
		bvh->SetMotion(t);

		for(auto ss :bvhMap)
		{
			dart::dynamics::BodyNode* bn = skel->getBodyNode(ss.first);
			Eigen::Matrix3d R = bvh->Get(ss.first);

			dart::dynamics::Joint* jn = bn->getParentJoint();
			Eigen::Vector3d a = dart::dynamics::BallJoint::convertToPositions(R);
			a = QuaternionToDARTPosition(DARTPositionToQuaternion(a));
			// p.block<3,1>(jn->getIndexInSkeleton(0),0) = a;
			if(dynamic_cast<dart::dynamics::BallJoint*>(jn)!=nullptr
				|| dynamic_cast<dart::dynamics::FreeJoint*>(jn)!=nullptr){
				p.block<3,1>(jn->getIndexInSkeleton(0),0) = a;
			}
			else if(dynamic_cast<dart::dynamics::RevoluteJoint*>(jn)!=nullptr){
				if(ss.first.find("Arm") != std::string::npos)
					p[jn->getIndexInSkeleton(0)] = a[1];
				else	
					p[jn->getIndexInSkeleton(0)] = a[0];

				if(p[jn->getIndexInSkeleton(0)]>M_PI)
					p[jn->getIndexInSkeleton(0)] -= 2*M_PI;
				else if(p[jn->getIndexInSkeleton(0)]<-M_PI)
					p[jn->getIndexInSkeleton(0)] += 2*M_PI;
			}

		}

		p.block<3,1>(3,0) = bvh->GetRootCOM(); 
		Eigen::VectorXd v;

		if(t != 0)
		{
			v = skel->getPositionDifferences(p, mMotions_raw.back()->GetPosition()) / 0.033;
			for(auto& jn : skel->getJoints()){
				if(dynamic_cast<dart::dynamics::RevoluteJoint*>(jn)!=nullptr){
					double v_ = v[jn->getIndexInSkeleton(0)];
					if(v_ > M_PI){
						v_ -= 2*M_PI;
					}
					else if(v_ < -M_PI){
						v_ += 2*M_PI;
					}
					v[jn->getIndexInSkeleton(0)] = v_;
				}
			}
			mMotions_raw.back()->SetVelocity(v);
		}
		mMotions_raw.push_back(new Motion(p, Eigen::VectorXd(p.rows())));
		
		skel->setPositions(p);
		skel->computeForwardKinematics(true,false,false);

		std::vector<bool> c;
		for(int j = 0; j < contact.size(); j++) {
			Eigen::Vector3d p = skel->getBodyNode(contact[j])->getWorldTransform().translation();
			c.push_back(p[1] < 0.04);
		}
		mContacts.push_back(c);

		t += bvh->GetTimeStep();
	}

	mMotions_raw.back()->SetVelocity(mMotions_raw.front()->GetVelocity());

	mPhaseLength = mMotions_raw.size();
	mTimeStep = bvh->GetTimeStep();

	for(int i = 0; i < mPhaseLength; i++) {
		mMotions_phase.push_back(new Motion(mMotions_raw[i]));
		if(i != 0 && i != mPhaseLength - 1) {
			for(int j = 0; j < contact.size(); j++)
				if(mContacts[i-1][j] && mContacts[i+1][j] && !mContacts[i][j])
						mContacts[i][j] = true;
		}
	 }

	delete bvh;

	this->GenerateMotionsFromSinglePhase(1000, false, mMotions_phase, mMotions_gen);

	for(int i = 0; i < this->GetPhaseLength(); i++) {
		mMotions_phase_adaptive.push_back(new Motion(mMotions_phase[i]));
	}
	this->GenerateMotionsFromSinglePhase(1000, false, mMotions_phase_adaptive, mMotions_gen_adaptive);
}
std::vector<Eigen::VectorXd> 
ReferenceManager::
GetVelocityFromPositions(std::vector<Eigen::VectorXd> pos)
{
	std::vector<Eigen::VectorXd> vel;
	auto skel = mCharacter->GetSkeleton();
	for(int i = 0; i < pos.size() - 1; i++) {
		skel->setPositions(pos[i]);
		skel->computeForwardKinematics(true,false,false);
		Eigen::VectorXd v = skel->getPositionDifferences(pos[i + 1], pos[i]) / 0.033;
		for(auto& jn : skel->getJoints()){
			if(dynamic_cast<dart::dynamics::RevoluteJoint*>(jn)!=nullptr){
				double v_ = v[jn->getIndexInSkeleton(0)];
				if(v_ > M_PI){
					v_ -= 2*M_PI;
				}
				else if(v_ < -M_PI){
					v_ += 2*M_PI;
				}
				v[jn->getIndexInSkeleton(0)] = v_;
			}
		}
		vel.push_back(v);
	}
	vel.push_back(vel.front());

	return vel;
}

void 
ReferenceManager::
RescaleMotion(double w)
{
	mMotions_phase.clear();

	auto& skel = mCharacter->GetSkeleton();
	Eigen::VectorXd p_save = mCharacter->GetSkeleton()->getPositions();
	Eigen::VectorXd v_save = mCharacter->GetSkeleton()->getVelocities();

	skel->setPositions(mMotions_raw[0]->GetPosition());
	skel->setVelocities(mMotions_raw[0]->GetVelocity());
	skel->computeForwardKinematics(true,true,false);

	double minheight = 0.0;
	std::vector<std::string> contactList;
	contactList.push_back("FootR");
	contactList.push_back("FootL");
	contactList.push_back("FootEndR");
	contactList.push_back("FootEndL");
	contactList.push_back("HandR");
	contactList.push_back("HandL");
	
	for(int i = 0; i < contactList.size(); i++) 
	{
		double height = skel->getBodyNode(contactList[i])->getWorldTransform().translation()[1];
		if(i == 0 || height < minheight) minheight = height;
	}

	skel->setPositions(p_save);
	skel->setVelocities(v_save);
	skel->computeForwardKinematics(true,true,false);

	for(int i = 0; i < mPhaseLength; i++)
	{
		Eigen::VectorXd p = mMotions_raw[i]->GetPosition();
		p[4] -= minheight - 0.02;
		mMotions_phase[i]->SetPosition(p);
	}

//calculate contact infomation
	double heightLimit = 0.05;
	double velocityLimit = 6;
	Eigen::VectorXd prev_p;
	Eigen::VectorXd prev_v;
	for(int i = 0; i < mPhaseLength; i++)
	{
		if(i != 0) {
			Eigen::VectorXd cur_p = mMotions_raw[i]->GetPosition();
			Eigen::Vector3d d_p = cur_p.segment<3>(3) - prev_p.segment<3>(3);
			d_p *= w;
			prev_p = cur_p;
			cur_p.segment<3>(3) = mMotions_raw[i-1]->GetPosition().segment<3>(3) + d_p;
			mMotions_phase[i]->SetPosition(cur_p);

			Eigen::VectorXd cur_v = mMotions_raw[i]->GetVelocity();
			cur_v.segment<3>(3) = w * cur_v.segment<3>(3);

			mMotions_phase[i]->SetVelocity(cur_v);

		} else {
			prev_p = mMotions_raw[i]->GetPosition();
			mMotions_phase[i]->SetPosition(mMotions_raw[i]->GetPosition());
			mMotions_phase[i]->SetVelocity(mMotions_raw[i]->GetVelocity());
		}

	}
}
void 
ReferenceManager::
GenerateMotionsFromSinglePhase(int frames, bool blend, std::vector<Motion*>& p_phase, std::vector<Motion*>& p_gen)
{
	mLock.lock();
	while(!p_gen.empty()){
		Motion* m = p_gen.back();
		p_gen.pop_back();

		delete m;
	}		

	auto& skel = mCharacter->GetSkeleton();

	Eigen::VectorXd p_save = skel->getPositions();
	Eigen::VectorXd v_save = skel->getVelocities();
	
	skel->setPositions(p_phase[0]->GetPosition());
	skel->computeForwardKinematics(true,false,false);

	Eigen::Vector3d p0_footl = skel->getBodyNode("LeftFoot")->getWorldTransform().translation();
	Eigen::Vector3d p0_footr = skel->getBodyNode("RightFoot")->getWorldTransform().translation();


	Eigen::Isometry3d T0_phase = dart::dynamics::FreeJoint::convertToTransform(p_phase[0]->GetPosition().head<6>());
	Eigen::Isometry3d T1_phase = dart::dynamics::FreeJoint::convertToTransform(p_phase.back()->GetPosition().head<6>());

	Eigen::Isometry3d T0_gen = T0_phase;
	
	Eigen::Isometry3d T01 = T1_phase*T0_phase.inverse();

	Eigen::Vector3d p01 = dart::math::logMap(T01.linear());			
	T01.linear() = dart::math::expMapRot(DPhy::projectToXZ(p01));
	T01.translation()[1] = 0;

	for(int i = 0; i < frames; i++) {
		
		int phase = i % mPhaseLength;
		
		if(i < mPhaseLength) {
			p_gen.push_back(new Motion(p_phase[i]));
		} else {
			Eigen::VectorXd pos;
			if(phase == 0) {
				std::vector<std::tuple<std::string, Eigen::Vector3d, Eigen::Vector3d>> constraints;
	
				skel->setPositions(p_gen.back()->GetPosition());
				skel->computeForwardKinematics(true,false,false);

				Eigen::Vector3d p_footl = skel->getBodyNode("LeftFoot")->getWorldTransform().translation();
				Eigen::Vector3d p_footr = skel->getBodyNode("RightFoot")->getWorldTransform().translation();

				p_footl(1) = p0_footl(1);
				p_footr(1)= p0_footr(1);

				constraints.push_back(std::tuple<std::string, Eigen::Vector3d, Eigen::Vector3d>("LeftFoot", p_footl, Eigen::Vector3d(0, 0, 0)));
				constraints.push_back(std::tuple<std::string, Eigen::Vector3d, Eigen::Vector3d>("RightFoot", p_footr, Eigen::Vector3d(0, 0, 0)));

				Eigen::VectorXd p = p_phase[phase]->GetPosition();
				p.segment<3>(3) = p_gen.back()->GetPosition().segment<3>(3);

				skel->setPositions(p);
				skel->computeForwardKinematics(true,false,false);
				pos = solveMCIKRoot(skel, constraints);
				pos(4) = p_phase[phase]->GetPosition()(4);
				
				T0_gen = dart::dynamics::FreeJoint::convertToTransform(pos.head<6>());
			} else {
				pos = p_phase[phase]->GetPosition();
				Eigen::Isometry3d T_current = dart::dynamics::FreeJoint::convertToTransform(pos.head<6>());
				T_current = T0_phase.inverse()*T_current;
				T_current = T0_gen*T_current;
				pos.head<6>() = dart::dynamics::FreeJoint::convertToPositions(T_current);
			}

			Eigen::VectorXd vel = skel->getPositionDifferences(pos, p_gen.back()->GetPosition()) / 0.033;
			p_gen.back()->SetVelocity(vel);
			p_gen.push_back(new Motion(pos, vel));

			if(blend && phase == 0) {
				for(int j = mBlendingInterval; j > 0; j--) {
					double weight = 1.0 - j / (double)(mBlendingInterval+1);
					Eigen::VectorXd oldPos = p_gen[i - j]->GetPosition();
					p_gen[i - j]->SetPosition(DPhy::BlendPosition(oldPos, pos, weight));
					vel = skel->getPositionDifferences(p_gen[i - j]->GetPosition(), p_gen[i - j - 1]->GetPosition()) / 0.033;
			 		p_gen[i - j - 1]->SetVelocity(vel);
				}
			}
		}
	}
	mLock.unlock();

}
Eigen::VectorXd 
ReferenceManager::
GetPosition(double t , bool adaptive) 
{
	std::vector<Motion*>* p_gen;
	if(adaptive)
	{
		p_gen = &mMotions_gen_adaptive;
	}
	else {
		p_gen = &mMotions_gen;
	}

	auto& skel = mCharacter->GetSkeleton();

	if((*p_gen).size()-1 < t) {
	 	return (*p_gen).back()->GetPosition();
	}
	
	int k0 = (int) std::floor(t);
	int k1 = (int) std::ceil(t);	
	if (k0 == k1)
		return (*p_gen)[k0]->GetPosition();
	else
		return DPhy::BlendPosition((*p_gen)[k1]->GetPosition(), (*p_gen)[k0]->GetPosition(), 1 - (t-k0));	
}
Motion*
ReferenceManager::
GetMotion(double t, bool adaptive)
{
	std::vector<Motion*>* p_gen;
	if(adaptive)
	{
		p_gen = &mMotions_gen_adaptive;
	}
	else {
		p_gen = &mMotions_gen;
	}

	auto& skel = mCharacter->GetSkeleton();

	if(mMotions_gen.size()-1 < t) {
	 	return new Motion((*p_gen).back()->GetPosition(), (*p_gen).back()->GetVelocity());
	}
	
	int k0 = (int) std::floor(t);
	int k1 = (int) std::ceil(t);	

	if (k0 == k1)
		return new Motion((*p_gen)[k0]);
	else {
		return new Motion(DPhy::BlendPosition((*p_gen)[k1]->GetPosition(), (*p_gen)[k0]->GetPosition(), 1 - (t-k0)), 
				DPhy::BlendVelocity((*p_gen)[k1]->GetVelocity(), (*p_gen)[k0]->GetVelocity(), 1 - (t-k0)));		
	}
}
void
ReferenceManager::
InitOptimization(int nslaves, std::string save_path) {
	
	mKnots.push_back(0);
	// mKnots.push_back(5);
	mKnots.push_back(10);
	// mKnots.push_back(15);
//	mKnots.push_back(20);
	mKnots.push_back(18);
	mKnots.push_back(24);
	// mKnots.push_back(29);
	mKnots.push_back(34);
	// mKnots.push_back(36);
	mKnots.push_back(42);
	mKnots.push_back(48);
	mKnots.push_back(58);
	mKnots.push_back(64);

	for(int i = 0; i < mPhaseLength; i+= 2) {
		mKnots_t.push_back(i);
	}

	// gravity, mass, linear momentum
	mTargetBase.resize(5);
	mTargetBase << 1, 1, 0, 230, 0; //, 1.5;
	mTargetCurMean = mTargetBase;

	mTargetGoal.resize(5);
	// mTargetGoal<< 0.44773, 0.12624, -1.4252, 6; 2
	mTargetGoal <<  1, 1, 0, 240, 0;

	mTargetUnit.resize(3);
	mTargetUnit<< 0.05, 0.05, 0.1; //, 0.05;

	mRefUpdateMode = true;

	for(int i = 0; i < this->mKnots.size() + 3; i++) {
		mPrevCps.push_back(Eigen::VectorXd::Zero(mDOF));
	}
	
	for(int i = 0; i < this->mKnots_t.size() + 3; i++) {
		mPrevCps_t.push_back(Eigen::VectorXd::Zero(1));
	}

	mTimeStep_adaptive.clear();
	for(int i = 0; i < mPhaseLength; i++) {
		mTimeStep_adaptive.push_back(1.0);
	}
	nOp = 0;
	mPath = save_path;
	mPrevRewardTrajectory = 0.5;
	mPrevRewardTarget = 0.0;	
	mMeanTrackingReward = 0;
	mMeanTargetReward = 0;

	nET = 0;
	nT = 0;
	// for(int i = 0; i < 3; i++) {
	// 	nRejectedSamples.push_back(0);
	// }

	// std::vector<std::pair<Eigen::VectorXd,double>> pos;
	// for(int i = 0; i < mPhaseLength; i++) {
	// 	pos.push_back(std::pair<Eigen::VectorXd,double>(mMotions_phase[i]->GetPosition(), i));
	// }
	// MultilevelSpline* s = new MultilevelSpline(1, mPhaseLength);
	// s->SetKnots(0, mKnots);

	// s->ConvertMotionToSpline(pos);
	// std::string path = std::string(CAR_DIR) + std::string("/result/walk_base");
	
	// std::vector<Eigen::VectorXd> cps = s->GetControlPoints(0);

	// std::ofstream ofs(path);

	// ofs << mKnots.size() << std::endl;
	// for(auto t: mKnots) {	
	// 	ofs << t << std::endl;
	// }
	// for(int i = 0; i < cps.size(); i++) {
	// 	if(i >= 1 && i <= cps.size() - 3)	
	// 		ofs << cps[i].transpose() << std::endl;
	// }

	// ofs << pos.size() << std::endl;
	// for(auto t: pos) {	
	// 	ofs << t.second << std::endl;
	// 	ofs << t.first.transpose() << std::endl;
	// }
	// ofs.close();

}
std::vector<double> 
ReferenceManager::
GetContacts(double t)
{
	std::vector<double> result;
	int k0 = (int) std::floor(t);
	int k1 = (int) std::ceil(t);	

	if (k0 == k1) {
		int phase = k0 % mPhaseLength;
		std::vector<bool> contact = mContacts[phase];
		for(int i = 0; i < contact.size(); i++)
			result.push_back(contact[i]);
	} else {
		int phase0 = k0 % mPhaseLength;
		int phase1 = k1 % mPhaseLength;

		std::vector<bool> contact0 = mContacts[phase0];
		std::vector<bool> contact1 = mContacts[phase1];
		for(int i = 0; i < contact0.size(); i++) {
			if(contact0[i] == contact1[i])
				result.push_back(contact0[i]);
			else 
				result.push_back(0.5);
		}

	}
	return result;
}
std::vector<std::pair<bool, Eigen::Vector3d>> 
ReferenceManager::
GetContactInfo(Eigen::VectorXd pos) 
{
	auto& skel = this->mCharacter->GetSkeleton();
	Eigen::VectorXd p_save = skel->getPositions();
	Eigen::VectorXd v_save = skel->getVelocities();
	
	skel->setPositions(pos);
	skel->computeForwardKinematics(true,false,false);

	std::vector<std::string> contact;
	contact.clear();
	contact.push_back("RightFoot");
	contact.push_back("RightToe");
	contact.push_back("LeftFoot");
	contact.push_back("LeftToe");

	std::vector<std::pair<bool, Eigen::Vector3d>> result;
	result.clear();
	for(int i = 0; i < contact.size(); i++) {
		Eigen::Vector3d p = skel->getBodyNode(contact[i])->getWorldTransform().translation();
		if(p[1] < 0.07) {
			result.push_back(std::pair<bool, Eigen::Vector3d>(true, p));
		} else {
			result.push_back(std::pair<bool, Eigen::Vector3d>(false, p));
		}
	}

	skel->setPositions(p_save);
	skel->setVelocities(v_save);
	skel->computeForwardKinematics(true,true,false);

	return result;
}
double 
ReferenceManager::
GetTimeStep(double t, bool adaptive) {
	if(adaptive) {
		t = std::fmod(t, mPhaseLength);
		int k0 = (int) std::floor(t);
		int k1 = (int) std::ceil(t);	
		if (k0 == k1) {
			return mTimeStep_adaptive[k0];
		}
		else if(k1 >= mTimeStep_adaptive.size())
			return (1 - (t - k0)) * mTimeStep_adaptive[k0] + (t-k0) * mTimeStep_adaptive[0];
		else
			return (1 - (t - k0)) * mTimeStep_adaptive[k0] + (t-k0) * mTimeStep_adaptive[k1];
	} else 
		return 1.0;
}
void
ReferenceManager::
ReportEarlyTermination() {
	mLock_ET.lock();
	nET +=1;
	mLock_ET.unlock();
}
void 
ReferenceManager::
SaveTrajectories(std::vector<std::pair<Eigen::VectorXd,double>> data_spline, 
				 std::pair<double, double> rewards,
				 Eigen::VectorXd parameters) {
	// std::cout << (rewards.first / mPhaseLength) << std::endl;
	if(dart::math::isNan(rewards.first) || dart::math::isNan(rewards.second)) {
		mLock_ET.lock();
		nET +=1;
		mLock_ET.unlock();
		return;
	}
	
	mLock_ET.lock();
	nT += 1;
	mLock_ET.unlock();
	mMeanTrackingReward = 0.99 * mMeanTrackingReward + 0.01 * (rewards.first / mPhaseLength);
	mMeanTargetReward = 0.99 * mMeanTargetReward + 0.01 * rewards.second;
	std::vector<int> flag;
	if(mPrevRewardTarget == 0 && (rewards.first / mPhaseLength) < 0.83) {
		return;
	}

	if((rewards.first / mPhaseLength)  < 0.8) {
		flag.push_back(0);
	}
	else {
		flag.push_back(1);
	}
	
	if((rewards.first / mPhaseLength)  < 0.6 || rewards.second < mPrevRewardTarget)
		return;

	if(rewards.second < mPrevRewardTarget)
		flag.push_back(0);
	else
		flag.push_back(1);

	if(flag[0] == 0)
		return;

	std::vector<int> nc;
	nc.push_back(3);
	nc.push_back(5);

	MultilevelSpline* s = new MultilevelSpline(1, this->GetPhaseLength(), nc);
	s->SetKnots(0, mKnots);

	double start_phase = std::fmod(data_spline[0].second, mPhaseLength);
	std::vector<Eigen::VectorXd> trajectory;
	for(int i = 0; i < data_spline.size(); i++) {
		trajectory.push_back(data_spline[i].first);
	}
	trajectory = Align(trajectory, this->GetPosition(start_phase).segment<6>(0));

	std::vector<std::pair<Eigen::VectorXd,double>> displacement;
	for(int i = 0; i < data_spline.size(); i++) {
		data_spline[i].first = trajectory[i];
	}

	this->GetDisplacementWithBVH(data_spline, displacement);
	s->ConvertMotionToSpline(displacement);

	std::vector<Eigen::VectorXd> newpos;
	std::vector<Eigen::VectorXd> new_displacement = s->ConvertSplineToMotion();
	this->AddDisplacementToBVH(new_displacement, newpos);

	nc.clear();
	nc.push_back(0);
	MultilevelSpline* st = new MultilevelSpline(1, this->GetPhaseLength());
	st->SetKnots(0, mKnots_t);

	std::vector<std::pair<Eigen::VectorXd,double>> displacement_t;
	for(int i = 0; i < data_spline.size(); i++) {
		double phase = std::fmod(data_spline[i].second, mPhaseLength);
		if(i < data_spline.size() - 1) {
			Eigen::VectorXd ts(1);
			ts << data_spline[i+1].second - data_spline[i].second - 1;
			displacement_t.push_back(std::pair<Eigen::VectorXd,double>(ts, phase));
		}
		else
			displacement_t.push_back(std::pair<Eigen::VectorXd,double>(Eigen::VectorXd::Zero(1), phase));
	}

	st->ConvertMotionToSpline(displacement_t);

	double r_slide = 0;
	std::vector<std::vector<std::pair<bool, Eigen::Vector3d>>> c;
	for(int i = 0; i < newpos.size(); i++) {
		c.push_back(this->GetContactInfo(newpos[i]));
	}
	for(int i = 1; i < newpos.size(); i++) {
		if(i < newpos.size() - 1) {
			for(int j = 0; j < 4; j++) {
				if((c[i-1][j].first) && (c[i+1][j].first) && !(c[i][j].first)) 
					(c[i][j].first) = true;
			}
		}
		for(int j = 0; j < 2; j++) {
			bool c_prev_j = (c[i-1][2*j].first) && (c[i-1][2*j + 1].first);
			bool c_cur_j = (c[i][2*j].first) && (c[i][2*j + 1].first);
			if(c_prev_j && c_cur_j) {
				double d = ((c[i-1][2*j].second + c[i-1][2*j+1].second) - (c[i][2*j].second + c[i][2*j+1].second)).norm()*0.5; 
				r_slide += pow(d*4, 2);
			} 
		}
	}
	r_slide = exp(-r_slide);

	auto cps = s->GetControlPoints(0);
	double r_regul = 0;
	int n_bnodes = mCharacter->GetSkeleton()->getNumBodyNodes();

	for(int i = 0; i < cps.size(); i++) {
		for(int j = 0; j < n_bnodes; j++) {
			int idx = mCharacter->GetSkeleton()->getBodyNode(j)->getParentJoint()->getIndexInSkeleton(0);
			int dof = mCharacter->GetSkeleton()->getBodyNode(j)->getParentJoint()->getNumDofs();
			std::string b_name = mCharacter->GetSkeleton()->getBodyNode(j)->getName();
			if(dof == 6) {
				r_regul += 1 * cps[i].segment<3>(idx).norm();
				r_regul += 1 * cps[i].segment<3>(idx + 3).norm();
			} else if (dof == 3) {
				r_regul += 0.5 * cps[i].segment<3>(idx).norm();
			} 
		}
	}
	r_regul = exp(-pow(r_regul / cps.size(), 2)*0.1);
	double reward_trajectory = (0.4 * r_regul + 0.6 * r_slide) * (rewards.first / mPhaseLength); // r_regul * r_slide;
	mLock.lock();
	// if(reward_trajectory > 0.4) {
	// 	mRegressionSamples.push_back(std::tuple<std::vector<Eigen::VectorXd>, Eigen::VectorXd, double>
	// 								(cps, parameters, reward_trajectory));
	// }

	// if((flag[0] || (!flag[0] && reward_trajectory > mPrevRewardTrajectory)) && flag[1] && mRefUpdateMode) {
	if(flag[0] && flag[1] && mRefUpdateMode) {
		mSamples.push_back(std::tuple<std::pair<MultilevelSpline*, MultilevelSpline*>, 
							std::pair<double, double>,  
							double>(std::pair<MultilevelSpline*, MultilevelSpline*>(s, st), 
									std::pair<double, double>(reward_trajectory, r_slide), 
									rewards.second));
		mSampleTargets.push_back(parameters);
		std::string path = mPath + std::string("samples") + std::to_string(nOp);

		std::ofstream ofs;
		ofs.open(path, std::fstream::out | std::fstream::app);

		for(auto t: data_spline) {	
			ofs << t.first.transpose() << " " << t.second << " " << r_slide << " " << r_regul << " " << rewards.second << std::endl;
		}
		ofs.close();
	}

	mLock.unlock();


}
bool cmp(const std::tuple<std::pair<MultilevelSpline*, MultilevelSpline*>, std::pair<double, double>, double> &p1, const std::tuple<std::pair<MultilevelSpline*, MultilevelSpline*>, std::pair<double, double>, double> &p2){
    if(std::get<1>(p1).first > std::get<1>(p2).first){
        return true;
    }
    else{
        return false;
    }
}
void 
ReferenceManager::
AddDisplacementToBVH(std::vector<Eigen::VectorXd> displacement, std::vector<Eigen::VectorXd>& position) {
	position.clear();
	int n_bnodes = mCharacter->GetSkeleton()->getNumBodyNodes();

	for(int i = 0; i < displacement.size(); i++) {

		Eigen::VectorXd p_bvh = mMotions_phase[i]->GetPosition();
		Eigen::VectorXd d = displacement[i];
		Eigen::VectorXd p(mCharacter->GetSkeleton()->getNumDofs());

		for(int j = 0; j < n_bnodes; j++) {
			int idx = mCharacter->GetSkeleton()->getBodyNode(j)->getParentJoint()->getIndexInSkeleton(0);
			int dof = mCharacter->GetSkeleton()->getBodyNode(j)->getParentJoint()->getNumDofs();
			if(dof == 6) {
				p.segment<3>(idx) = Rotate3dVector(p_bvh.segment<3>(idx), d.segment<3>(idx));
				p.segment<3>(idx + 3) = d.segment<3>(idx + 3) + p_bvh.segment<3>(idx + 3);
			} else if (dof == 3) {
				p.segment<3>(idx) = Rotate3dVector(p_bvh.segment<3>(idx), d.segment<3>(idx));
			} else {
				p(idx) = d(idx) + p_bvh(idx);
			}
		}
		position.push_back(p);
	}
}
void
ReferenceManager::
GetDisplacementWithBVH(std::vector<std::pair<Eigen::VectorXd, double>> position, std::vector<std::pair<Eigen::VectorXd, double>>& displacement) {
	displacement.clear();
	int n_bnodes = mCharacter->GetSkeleton()->getNumBodyNodes();
	for(int i = 0; i < position.size(); i++) {
		double phase = std::fmod(position[i].second, mPhaseLength);
		
		Eigen::VectorXd p = position[i].first;
		Eigen::VectorXd p_bvh = this->GetPosition(phase);
		Eigen::VectorXd d(mCharacter->GetSkeleton()->getNumDofs());
		for(int j = 0; j < n_bnodes; j++) {
			int idx = mCharacter->GetSkeleton()->getBodyNode(j)->getParentJoint()->getIndexInSkeleton(0);
			int dof = mCharacter->GetSkeleton()->getBodyNode(j)->getParentJoint()->getNumDofs();
			
			if(dof == 6) {
				d.segment<3>(idx) = JointPositionDifferences(p.segment<3>(idx), p_bvh.segment<3>(idx));
				d.segment<3>(idx + 3) = p.segment<3>(idx + 3) -  p_bvh.segment<3>(idx + 3);
			} else if (dof == 3) {
				d.segment<3>(idx) = JointPositionDifferences(p.segment<3>(idx), p_bvh.segment<3>(idx));
			} else {
				d(idx) = p(idx) - p_bvh(idx);
			}
		}
		displacement.push_back(std::pair<Eigen::VectorXd,double>(d, phase));
	}
}
bool 
ReferenceManager::
Optimize() {
	if(!mRefUpdateMode)
		return false;

	double rewardTarget = 0;
	double rewardTrajectory = 0;
    int mu = 60;
    std::cout << "num sample: " << mSamples.size() << std::endl;
    if(mSamples.size() < 300) {
  //   	for(int i = 0; i < nRejectedSamples.size(); i++) {
		// 	std::cout << i << " " << nRejectedSamples[i] << std::endl;
		// }
    	return false;
    }

    std::stable_sort(mSamples.begin(), mSamples.end(), cmp);

	std::vector<int> nc;
	nc.push_back(3);
	nc.push_back(5);

	MultilevelSpline* mean_spline = new MultilevelSpline(1, this->GetPhaseLength(), nc); 
	mean_spline->SetKnots(0, mKnots);

	nc.clear();
	nc.push_back(0);
	MultilevelSpline* mean_spline_t = new MultilevelSpline(1, this->GetPhaseLength()); 
	mean_spline_t->SetKnots(0, mKnots_t);

	std::vector<Eigen::VectorXd> mean_cps;   
   	mean_cps.clear();

   	std::vector<Eigen::VectorXd> mean_cps_t;
   	mean_cps_t.clear();

   	int num_knot = mean_spline->GetKnots(0).size();
   	for(int i = 0; i < num_knot + 3; i++) {
		mean_cps.push_back(Eigen::VectorXd::Zero(mDOF));
	}
   	
   	int num_knot_t = mean_spline_t->GetKnots(0).size();
	for(int i = 0;i < num_knot_t + 3; i++) {
		mean_cps_t.push_back(Eigen::VectorXd::Zero(1));
	}

	double weight_sum = 0;

	std::string path = mPath + std::string("rewards");
	std::ofstream ofs;
	ofs.open(path, std::fstream::out | std::fstream::app);

	for(int i = 0; i < mu; i++) {
		double w = log(mu + 1) - log(i + 1);
	    weight_sum += w;
	    std::vector<Eigen::VectorXd> cps = std::get<0>(mSamples[i]).first->GetControlPoints(0);
	    for(int j = 0; j < num_knot + 3; j++) {
			mean_cps[j] += w * cps[j];
	    }
	    std::vector<Eigen::VectorXd> cps_t = std::get<0>(mSamples[i]).second->GetControlPoints(0);
	    for(int j = 0; j < num_knot_t + 3; j++) {
			mean_cps_t[j] += w * cps_t[j];
	    }
	    rewardTrajectory += w * std::get<1>(mSamples[i]).first;
	    rewardTarget += std::get<2>(mSamples[i]);
	    ofs << std::get<1>(mSamples[i]).second << " ";

	}
	ofs << std::endl;
	ofs.close();

	rewardTrajectory /= weight_sum;
	rewardTarget /= (double)mu;

	std::cout << "current avg elite similarity reward: " << rewardTrajectory << ", target reward: " << rewardTarget << ", cutline: " << mPrevRewardTrajectory << std::endl;
	

	for(int i = 0; i < num_knot + 3; i++) {
	    mean_cps[i] /= weight_sum;
	    mPrevCps[i] = mPrevCps[i] * 0.6 + mean_cps[i] * 0.4;
	}
	for(int i = 0; i < num_knot_t + 3; i++) {
	    mean_cps_t[i] /= weight_sum;
		mPrevCps_t[i] = mPrevCps_t[i] * 0.6 + mean_cps_t[i] * 0.4;
	}

	mPrevRewardTrajectory = rewardTrajectory;
	mPrevRewardTarget = rewardTarget;

	mean_spline->SetControlPoints(0, mPrevCps);
	std::vector<Eigen::VectorXd> new_displacement = mean_spline->ConvertSplineToMotion();
	std::vector<Eigen::VectorXd> newpos;
	this->AddDisplacementToBVH(new_displacement, newpos);

	mean_spline_t->SetControlPoints(0, mPrevCps_t);
	std::vector<Eigen::VectorXd> new_displacement_t = mean_spline_t->ConvertSplineToMotion();

	for(int i = 0; i < mPhaseLength; i++) {
		mTimeStep_adaptive[i] = 1 + new_displacement_t[i](0);
	}

	std::vector<Eigen::VectorXd> newvel = this->GetVelocityFromPositions(newpos);
	for(int i = 0; i < mMotions_phase_adaptive.size(); i++) {
		mMotions_phase_adaptive[i]->SetPosition(newpos[i]);
		mMotions_phase_adaptive[i]->SetVelocity(newvel[i]);
	}
	
	this->GenerateMotionsFromSinglePhase(1000, false, mMotions_phase_adaptive, mMotions_gen_adaptive);
	this->SaveAdaptiveMotion();
	this->SaveAdaptiveMotion(std::to_string(nOp));

	//save motion
	path =  mPath + std::string("motion") + std::to_string(nOp);
	ofs.open(path);

	for(auto t: newpos) {	
		ofs << t.transpose() << std::endl;
	}
	ofs.close();

	nOp += 1;
			
	while(!mSamples.empty()){
		MultilevelSpline* s = std::get<0>(mSamples.back()).first;
		MultilevelSpline* st = std::get<0>(mSamples.back()).second;

		mSamples.pop_back();

		delete s;
		delete st;

	}	

	for(int i = 0; i < nRejectedSamples.size(); i++) {
		std::cout << i << " " << nRejectedSamples[i] << std::endl;
		nRejectedSamples[i] = 0;
	}

	mTargetCurMean.setZero();
	for(int i = 0; i < mSampleTargets.size(); i++) {
		mTargetCurMean += mSampleTargets[i];
	}
	mTargetCurMean /= mSampleTargets.size();
	mSampleTargets.clear();
		
	return true;
}
bool
ReferenceManager::
UpdateExternalTarget() {

	double survival_ratio = (double)nT / (nET + nT);
	std::cout << "current mean tracking reward :" << mMeanTrackingReward  << ", survival ratio: " << survival_ratio << std::endl;
	nT = 0;
	nET = 0;
	if(survival_ratio > 0.8 && mMeanTrackingReward > 0.75) {
		return true;
	}
	return false;
}
int 
ReferenceManager::
NeedUpdateSigTarget() {
	std::cout << "current mean target reward : " << mMeanTargetReward << std::endl;
	if(mMeanTargetReward < 0.05 && mMeanTrackingReward > 0.75) {
		return 1;
	} else if (mMeanTargetReward > 0.8 && mMeanTrackingReward > 0.75) {
		return -1;
	}
	return 0;
}
void 
ReferenceManager::
UpdateTargetReward(double old_sig, double new_sig) {
	mPrevRewardTarget = exp(log(mPrevRewardTarget) / pow(new_sig, 2) * pow(old_sig, 2));
	std::cout << "updated mean elite target reward: " << mPrevRewardTarget << " new sig: " << new_sig << std::endl;

}

std::tuple<std::vector<Eigen::VectorXd>, std::vector<Eigen::VectorXd>, std::vector<double>> 
ReferenceManager::
GetRegressionSamples() {
	std::vector<Eigen::VectorXd> x;
	std::vector<Eigen::VectorXd> y;
	std::vector<double> r;

	for(int i = 0; i < mRegressionSamples.size(); i++) {
		std::tuple<std::vector<Eigen::VectorXd>, Eigen::VectorXd, double> s = mRegressionSamples[i];
		for(int j = 0; j < mKnots.size() + 3; j++) {
			Eigen::VectorXd knot_and_target;
			
			knot_and_target.resize(1 + std::get<1>(s).rows());
			knot_and_target << j, std::get<1>(s);
			x.push_back(knot_and_target);
			y.push_back(std::get<0>(s)[j]);
			r.push_back(std::get<2>(s));
		}
	}
	mRegressionSamples.clear();

	return std::tuple<std::vector<Eigen::VectorXd>, std::vector<Eigen::VectorXd>, std::vector<double>>(x, y, r);
}
};