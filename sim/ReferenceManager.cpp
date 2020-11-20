#include "ReferenceManager.h"
#include <tinyxml.h>
#include <fstream>
#include <stdlib.h>
#include <cmath>

using namespace dart::dynamics;
namespace DPhy
{
ReferenceManager::ReferenceManager(Character* character) 
:mRD(), mMT(mRD()), mUniform(0.0, 1.0)
{
	mCharacter = character;
	mBlendingInterval = 10;
	
	mMotions_gen.clear();
	mMotions_raw.clear();
	mMotions_phase.clear();

	auto& skel = mCharacter->GetSkeleton();
	mDOF = skel->getPositions().rows();

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

}
void 
ReferenceManager::
LoadAdaptiveMotion(std::vector<Eigen::VectorXd> displacement) {

	std::vector<Eigen::VectorXd> d_space;
	std::vector<Eigen::VectorXd> d_time;

	for(int i = 0 ; i < displacement.size(); i++) {
		d_space.push_back(displacement[i].head(displacement[i].rows()-1));
		d_time.push_back(displacement[i].tail(1));
	}

	std::vector<Eigen::VectorXd> newpos;
	this->AddDisplacementToBVH(d_space, newpos);
	std::vector<Eigen::VectorXd> newvel = this->GetVelocityFromPositions(newpos);

	for(int j = 0; j < mPhaseLength; j++) {
		mMotions_phase_adaptive[j]->SetPosition(newpos[j]);
		mMotions_phase_adaptive[j]->SetVelocity(newvel[j]);
	}



	for(int i = 0; i < mPhaseLength; i++) {
		mTimeStep_adaptive[i] = exp(d_time[i](0));
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

	int smooth_time = 10;
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

				//// rotate "root" to seamlessly stitch foot
				pos = solveMCIKRoot(skel, constraints);
				pos(4) = p_phase[phase]->GetPosition()(4);
				T0_gen = dart::dynamics::FreeJoint::convertToTransform(pos.head<6>());

				//// same root as T0, seamlessly stitch foot 
				// pos = solveMCIK_woRoot(skel, constraints);
				// pos(4) = p_phase[phase]->GetPosition()(4);
				// T0_gen = dart::dynamics::FreeJoint::convertToTransform(pos.head<6>());
			} else {
				pos = p_phase[phase]->GetPosition();
				Eigen::Isometry3d T_current = dart::dynamics::FreeJoint::convertToTransform(pos.head<6>());
				// T_current = T0_phase.inverse()*T_current;
				// T_current = T0_gen*T_current;
				Eigen::Isometry3d T0_phase_gen = T0_gen* T0_phase.inverse();
				
				if(phase < smooth_time){
					Eigen::Quaterniond Q0_phase_gen(T0_phase_gen.linear());
					double slerp_t = (double)phase/smooth_time; 
					slerp_t = 0.5*(1-cos(M_PI*slerp_t)); //smooth slerp t [0,1]
					
					Eigen::Quaterniond Q_blend = Q0_phase_gen.slerp(slerp_t, Eigen::Quaterniond::Identity());
					T0_phase_gen.linear() = Eigen::Matrix3d(Q_blend);
					T_current = T0_phase_gen* T_current;
				}else{
					T0_phase_gen.linear() = Eigen::Matrix3d::Identity();
					T_current = T0_phase_gen* T_current;
					// T_current = T0_phase.inverse()*T_current;
					// T_current = T0_gen*T_current;					
				}

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
ResetOptimizationParameters(bool reset_displacement) {
	if(reset_displacement) {
		mTimeStep_adaptive.clear();
		for(int i = 0; i < mPhaseLength; i++) {
			mTimeStep_adaptive.push_back(1.0);
		}

		mMotions_phase_adaptive.clear();
		for(int i = 0; i < this->GetPhaseLength(); i++) {
			mMotions_phase_adaptive.push_back(new Motion(mMotions_phase[i]));
		}
		this->GenerateMotionsFromSinglePhase(1000, false, mMotions_phase_adaptive, mMotions_gen_adaptive);

	}
	
	if(isParametric) {
		mRegressionMemory->ResetExploration();
	}

	mMeanTrackingReward = 0;
	mMeanParamReward = 0;

}
void
ReferenceManager::
InitOptimization(int nslaves, std::string save_path, bool adaptive) {
	isParametric = adaptive;
	mPath = save_path;
	
	mThresholdTracking = 0.85;

	mParamCur.resize(2); // jump height, jump distance
	mParamCur << 0.46, (   0.614314 -  0.016015 ); 	// com, 81th frame
	// mParamCur << 0.46, ( 0.378879 -  0.016015 ); // com , 41 th frame
	// mParamCur << 0.46, (0.718014 - 0.0566185); 	// (lf+rf)/2.

	// frame, (lf+rf)/2.
	// 0, middle: -0.739319 0.0438206 0.0566185
	// 45, middle: -0.739077  0.495829  0.710883
	// 50, middle: -0.740508  0.502092  0.718014

	mParamGoal.resize(2);
	mParamGoal = mParamCur;

	// character root:
	// 0: -8.63835e-05      1.04059     0.016015 / 41 : 0.00327486    1.34454   0.378879 / 81 : -0.0177552    1.48029   0.614314

	if(adaptive) {

		Eigen::VectorXd paramUnit(2);
		paramUnit << mParamGoal[0]*0.1, mParamGoal[1]*0.2;

		mParamBase.resize(2);
		mParamBase << mParamGoal[0]*0.8, mParamGoal[1]*0.8;

		mParamEnd.resize(2);
		mParamEnd << mParamGoal[0]*1.8, mParamGoal[1]*3.0;

		
		mRegressionMemory->InitParamSpace(mParamCur, std::pair<Eigen::VectorXd, Eigen::VectorXd> (mParamBase, mParamEnd), 
										  paramUnit, mDOF + 1, mPhaseLength);


		std::cout << "initial goal : " << mParamGoal.transpose() << std::endl;
	}

	ResetOptimizationParameters();

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
SaveTrajectories(std::vector<std::pair<Eigen::VectorXd,double>> data_raw, 
				 std::tuple<double, double, std::vector<double>> rewards,
				 Eigen::VectorXd parameters) {

	if(dart::math::isNan(std::get<0>(rewards)) || dart::math::isNan(std::get<1>(rewards))) {
		return;
	}

	mMeanTrackingReward = 0.99 * mMeanTrackingReward + 0.01 * std::get<0>(rewards);
	mMeanParamReward = 0.99 * mMeanParamReward + 0.01 * std::get<1>(rewards);

	if(std::get<0>(rewards) < mThresholdTracking) {
		return;
	}
	if(std::get<2>(rewards)[0] > 0.1) {
		return;
	}

	Eigen::Vector3d lf = mCharacter->GetSkeleton()->getBodyNode("LeftFoot")->getWorldTransform().translation();
	Eigen::Vector3d rf = mCharacter->GetSkeleton()->getBodyNode("RightFoot")->getWorldTransform().translation();
	Eigen::Vector3d ls = mCharacter->GetSkeleton()->getBodyNode("LeftShoulder")->getWorldTransform().translation();
	Eigen::Vector3d rs = mCharacter->GetSkeleton()->getBodyNode("RightShoulder")->getWorldTransform().translation();
	Eigen::Vector3d right_vector = ((rf-lf)+(rs-ls))/2.;
	right_vector[1]= 0;
	Eigen::Vector3d forward_vector=  Eigen::Vector3d::UnitY().cross(right_vector);
	double forward_angle= std::atan2(forward_vector[0], forward_vector[2]);
	if(std::abs(forward_angle) > M_PI/8.) return;

	if (std::abs(data_raw[0].second) > 1e-8) return ;
	

/*	Eigen::VectorXd restore = mCharacter->GetSkeleton()->getPositions();

		Eigen::VectorXd raw_41 = data_raw[41].first;
		mCharacter->GetSkeleton()->setPositions(raw_41);
		mCharacter->GetSkeleton()->computeForwardKinematics(true, false, false);
		Eigen::Vector3d raw_41_lf = mCharacter->GetSkeleton()->getBodyNode("LeftFoot")->getWorldTransform().translation();
		Eigen::Vector3d raw_41_rf = mCharacter->GetSkeleton()->getBodyNode("RightFoot")->getWorldTransform().translation();
		Eigen::Vector3d raw= (raw_41_lf+raw_41_rf)/2.;

*/
	double start_phase = std::fmod(data_raw[0].second, mPhaseLength);
	std::vector<Eigen::VectorXd> trajectory;
	for(int i = 0; i < data_raw.size(); i++) {
		trajectory.push_back(data_raw[i].first);
	}
	trajectory = Align(trajectory, this->GetPosition(start_phase).segment<6>(0));
	for(int i = 0; i < data_raw.size(); i++) {
		data_raw[i].first = trajectory[i];
	}

	std::vector<std::pair<Eigen::VectorXd,double>> data_uniform;
	int count = 0;
	for(int i = 0; i < mPhaseLength; i++) {
		while(count + 1 < data_raw.size() && i >= data_raw[count+1].second)
			count += 1;
		Eigen::VectorXd p(mDOF + 1);
		if(i < data_raw[count].second) {
			int size = data_raw.size();
			double t0 = data_raw[size-1].second - data_raw[size-2].second;
			double weight = 1.0 - (mPhaseLength + i - data_raw[size-1].second) / (mPhaseLength + data_raw[count].second - data_raw[size-1].second);
			double t1 = data_raw[count+1].second - data_raw[count].second;
			Eigen::VectorXd p_blend = DPhy::BlendPosition(data_raw[size-1].first, data_raw[0].first, weight);
			p_blend.segment<3>(3) = data_raw[0].first.segment<3>(3);
			double t_blend = (1 - weight) * t0 + weight * t1;
			p << p_blend, log(t_blend);
		} else if(count == data_raw.size() - 1 && i > data_raw[count].second) {
			double t0 = data_raw[count].second - data_raw[count-1].second;
			double weight = 1.0 - (data_raw[0].second + mPhaseLength - i) / (data_raw[0].second + mPhaseLength - data_raw[count].second);
			double t1 = data_raw[1].second - data_raw[0].second;
			
			Eigen::VectorXd p_blend = DPhy::BlendPosition(data_raw[count].first, data_raw[0].first, weight);
			p_blend.segment<3>(3) = data_raw[count].first.segment<3>(3);
	
			double t_blend = (1 - weight) * t0 + weight * t1;
			p << p_blend, log(t_blend);
		} else if(i == data_raw[count].second) {
			if(count < data_raw.size())
				p << data_raw[count].first, log(data_raw[count+1].second - data_raw[count].second);
			else
				p << data_raw[count].first, log(data_raw[0].second + mPhaseLength - data_raw[count].second);

		} else {
			double weight = 1.0 - (data_raw[count+1].second - i) / (data_raw[count+1].second - data_raw[count].second);
			Eigen::VectorXd p_blend = DPhy::BlendPosition(data_raw[count].first, data_raw[count+1].first, weight);
			double t_blend;
			if(count + 2 >= data_raw.size()) {
				double t0 = data_raw[count+1].second - data_raw[count].second;
				double t1 = data_raw[1].second - data_raw[0].second;
				t_blend = (1 - weight) * t0 + weight * t1;
			} else {
				double t0 = data_raw[count+1].second - data_raw[count].second;
				double t1 = data_raw[count+2].second - data_raw[count+1].second;
				t_blend = (1 - weight) * t0 + weight * t1;
			}
			p << p_blend, log(t_blend);
		}
		data_uniform.push_back(std::pair<Eigen::VectorXd,double>(p, i));
	}

/*
	mCharacter->GetSkeleton()->setPositions(restore);
	mCharacter->GetSkeleton()->computeForwardKinematics(true, false, false);
	

	Eigen::VectorXd test = data_uniform[40].first.segment<69>(0);
	mCharacter->GetSkeleton()->setPositions(test);
	mCharacter->GetSkeleton()->computeForwardKinematics(true, false, false);
	lf = mCharacter->GetSkeleton()->getBodyNode("LeftFoot")->getWorldTransform().translation();
	rf = mCharacter->GetSkeleton()->getBodyNode("RightFoot")->getWorldTransform().translation();
	// std::cout<<"RefM/ 40: "<<((lf+rf)/2.).transpose()<<std::endl;

	test = data_uniform[41].first.segment<69>(0);
	mCharacter->GetSkeleton()->setPositions(test);
	mCharacter->GetSkeleton()->computeForwardKinematics(true, false, false);
	lf = mCharacter->GetSkeleton()->getBodyNode("LeftFoot")->getWorldTransform().translation();
	rf = mCharacter->GetSkeleton()->getBodyNode("RightFoot")->getWorldTransform().translation();

	if((tmp_debug- (lf+rf)/2.).norm() > 0.05) {
		std::cout<<"!!!!!!!!"<<std::endl;
		std::cout<<"foot(tmp_debug): "<<tmp_debug_frame<<" : "<<tmp_debug.transpose()<<std::endl;
		std::cout<<"RefM/ 41: "<<((lf+rf)/2.).transpose()<<std::endl;
		std::cout<<raw.transpose()<<std::endl;
		
		raw_41 = data_raw[41].first;
		mCharacter->GetSkeleton()->setPositions(raw_41);
		mCharacter->GetSkeleton()->computeForwardKinematics(true, false, false);
		raw_41_lf = mCharacter->GetSkeleton()->getBodyNode("LeftFoot")->getWorldTransform().translation();
		raw_41_rf = mCharacter->GetSkeleton()->getBodyNode("RightFoot")->getWorldTransform().translation();
		raw= (raw_41_lf+raw_41_rf)/2.;
		std::cout<<raw.transpose()<<std::endl;
		
		// std::cout<<"rawraw/ 41: "<<raw.transpose()<<std::endl;
		// std::cout<<"raw_raw/ 41: "<<raw_raw.transpose()<<std::endl;

		// raw_41 = data_raw[41].first;
		// mCharacter->GetSkeleton()->setPositions(raw_41);
		// mCharacter->GetSkeleton()->computeForwardKinematics(true, false, false);
		// raw_41_lf = mCharacter->GetSkeleton()->getBodyNode("LeftFoot")->getWorldTransform().translation();
		// raw_41_rf = mCharacter->GetSkeleton()->getBodyNode("RightFoot")->getWorldTransform().translation();
		// raw= (raw_41_lf+raw_41_rf)/.2;
		
		std::cout<<std::endl;
	}

	mCharacter->GetSkeleton()->setPositions(restore);
	mCharacter->GetSkeleton()->computeForwardKinematics(true, false, false);
*/

	std::vector<std::pair<Eigen::VectorXd,double>> displacement;
	this->GetDisplacementWithBVH(data_uniform, displacement);

	std::vector<Eigen::VectorXd> d;
	int n_bnodes = mCharacter->GetSkeleton()->getNumBodyNodes();

	for(int i = 0; i < mPhaseLength; i++) {
		Eigen::VectorXd d_t(mDOF + 1);
		d_t << displacement[i].first, data_uniform[i].first.tail<1>();
		d.push_back(d_t);
	
	}
	double r_con =  exp(-std::get<2>(rewards)[0]); //exp(-std::get<2>(rewards)[0]);
	double r_slide = exp(-std::get<2>(rewards)[1] * 100); //exp(-std::get<2>(rewards)[0]);
	double r_foot = r_con;
	double r_delta = std::get<2>(rewards)[3];
	double r_pos = std::get<2>(rewards)[2];
	double reward_trajectory = r_foot * r_pos * r_delta;
	mLock.lock();

	if(isParametric) {
		mRegressionMemory->UpdateParamSpace(std::tuple<std::vector<Eigen::VectorXd>, Eigen::VectorXd, double>
											(d, parameters, reward_trajectory));

	}
	
	mLock.unlock();


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
void 
ReferenceManager::
SelectReference(){
	double r = 0.4;
	if(mUniform(mMT) < r) {
		LoadAdaptiveMotion(mCPS_reg);
	} else {
		LoadAdaptiveMotion(mCPS_exp);
	}
}
};