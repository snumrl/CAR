#include "SubController.h"
#include "MetaController.h"

namespace DPhy{

SubController::SubController(std::string type, std::string motion, std::string ppo) : mType(type), mSimPerCon(5)
{
	std::string path = std::string(CAR_DIR)+std::string("/character/") + std::string(REF_CHARACTER_TYPE) + std::string(".xml");
    DPhy::Character* character = new DPhy::Character(path);

    mReferenceManager = new DPhy::ReferenceManager(character);
	mReferenceManager->LoadMotionFromBVH(std::string("/motion/") + motion);
    path = std::string(CAR_DIR)+ std::string("/network/output/") + DPhy::split(ppo, '/')[0] + std::string("/");
    mRegressionMemory = new DPhy::RegressionMemory();

    if(mType.compare("Idle") != 0){
		mReferenceManager->SetRegressionMemory(mRegressionMemory);
		mReferenceManager->InitOptimization(1, path, true, mType);
    }
    else{
    	mReferenceManager->InitOptimization(1, path, false, mType);
    }

	mEndEffectors.clear();
	mEndEffectors.push_back("RightFoot");
	mEndEffectors.push_back("LeftFoot");
	mEndEffectors.push_back("LeftHand");
	mEndEffectors.push_back("RightHand");
	mEndEffectors.push_back("Head");

    numStates = GetState(character).rows();
	mInterestedDof = character->GetSkeleton()->getNumDofs() - 6;
    numActions = mInterestedDof + 1;  

    Py_Initialize();
    np::initialize();
    try {

		mRegressionMemory->LoadParamSpace(path + "param_space");

    	if(ppo != "") {
    
    		p::object ppo_main = p::import("ppo");
			this->mPPO = ppo_main.attr("PPO")();
			std::string path = std::string(CAR_DIR)+ std::string("/network/output/") + ppo;

			this->mPPO.attr("initRun")(path, 
									   numStates, 
									   numActions);
			
			std::cout<<"DONE restore ppo (initRun)"<<std::endl;
			// RunPPO();
    	}
    
    } catch (const p::error_already_set&) {
        PyErr_Print();
    }  

}
void
SubController::
Synchronize(Character* character, double frame) {
	std::cout << 1 << std::endl;
	Eigen::VectorXd pos = character->GetSkeleton()->getPositions();
	std::cout << frame << std::endl;

	Eigen::VectorXd pos_not_aligned = mReferenceManager->GetPosition(frame, true);
	std::cout << 2 << std::endl;

	Eigen::Isometry3d T0_phase = dart::dynamics::FreeJoint::convertToTransform(pos_not_aligned.head<6>());
	Eigen::Isometry3d T1_phase = dart::dynamics::FreeJoint::convertToTransform(pos.head<6>());

	Eigen::Isometry3d T01 = T1_phase*T0_phase.inverse();

	Eigen::Vector3d p01 = dart::math::logMap(T01.linear());			
	T01.linear() =  dart::math::expMapRot(DPhy::projectToXZ(p01));
	T01.translation()[1] = 0;
	Eigen::Isometry3d T0_gen = T01*T0_phase;

	std::vector<Eigen::VectorXd> p;
	std::vector<double> t;
	for(int i = 0; i < mReferenceManager->GetPhaseLength(); i++) {
		Eigen::VectorXd p_tmp = mReferenceManager->GetPosition(i, true);
		Eigen::Isometry3d T_current = dart::dynamics::FreeJoint::convertToTransform(p_tmp.head<6>());
		T_current = T0_phase.inverse()*T_current;
		T_current = T0_gen*T_current;

		p_tmp.head<6>() = dart::dynamics::FreeJoint::convertToPositions(T_current);
		p.push_back(p_tmp);
		t.push_back(mReferenceManager->GetTimeStep(i, true));
	}
	std::cout << 3 << std::endl;

	mReferenceManager->LoadAdaptiveMotion(p, t);
	mCurrentFrameOnPhase = frame;
	mCurrentFrame = frame;
	mPrevFrame = mCurrentFrame;
	std::cout << 4 << std::endl;

	mAdaptiveStep = mReferenceManager->GetTimeStep(frame, true);
	mTargetPositions = mReferenceManager->GetPosition(frame, true);
	mPrevTargetPositions = mTargetPositions;
	std::cout << 5 << std::endl;

}
bool
SubController::
Step(dart::simulation::WorldPtr world, Character* character) {
	Eigen::VectorXd state = GetState(character);
	p::object a = mPPO.attr("run")(DPhy::toNumPyArray(state));
	np::ndarray na = np::from_object(a);
	Eigen::VectorXd action = DPhy::toEigenVector(na, numActions);

	// set action target pos
	int num_body_nodes = mInterestedDof / 3;
	int dof = character->GetSkeleton()->getNumDofs(); 

	for(int i = 0; i < mInterestedDof; i++){
		action[i] = dart::math::clip(action[i]*0.2, -0.7*M_PI, 0.7*M_PI);
	}

	action[mInterestedDof] = dart::math::clip(action[mInterestedDof]*1.2, -2.0, 1.0);
	action[mInterestedDof] = exp(action[mInterestedDof]);
	mAdaptiveStep = action[mInterestedDof];

	mCurrentFrame += mAdaptiveStep;
	mCurrentFrameOnPhase += mAdaptiveStep;

	int n_bnodes = character->GetSkeleton()->getNumBodyNodes();

	Motion* p_v_target = mReferenceManager->GetMotion(mCurrentFrame, true);
	this->mTargetPositions = p_v_target->GetPosition();
	Eigen::VectorXd TargetVelocities = character->GetSkeleton()->getPositionDifferences(mTargetPositions, mPrevTargetPositions) / 0.033 * (mCurrentFrame - mPrevFrame);
	delete p_v_target;

	p_v_target = mReferenceManager->GetMotion(mCurrentFrame, false);

	Eigen::VectorXd PDTargetPositions = p_v_target->GetPosition();
	Eigen::VectorXd PDTargetVelocities = p_v_target->GetVelocity();
	delete p_v_target;

	int count_dof = 0;

	for(int i = 1; i <= num_body_nodes; i++){
		int idx = character->GetSkeleton()->getBodyNode(i)->getParentJoint()->getIndexInSkeleton(0);
		int dof = character->GetSkeleton()->getBodyNode(i)->getParentJoint()->getNumDofs();
		PDTargetPositions.block(idx, 0, dof, 1) += action.block(count_dof, 0, dof, 1);
		count_dof += dof;
	}
	
	for(int i = 0; i < this->mSimPerCon; i += 2){
		for(int j = 0; j < 2; j++) {
			Eigen::VectorXd torque = character->GetSkeleton()->getSPDForces(PDTargetPositions, 600, 49, world->getConstraintSolver());
			character->GetSkeleton()->setForces(torque);
			world->step(false);
		}

	}
	if(mCurrentFrameOnPhase >= mReferenceManager->GetPhaseLength()){
		mCurrentFrameOnPhase -= mReferenceManager->GetPhaseLength();
	}
	mPrevTargetPositions = mTargetPositions;
	mPrevFrame = mCurrentFrame;
}
Eigen::VectorXd 
SubController::
GetEndEffectorStatePosAndVel(Character* character, Eigen::VectorXd pos, Eigen::VectorXd vel) {
	Eigen::VectorXd ret;
	auto& skel = character->GetSkeleton();
	dart::dynamics::BodyNode* root = skel->getRootBodyNode();
	Eigen::Isometry3d cur_root_inv = root->getWorldTransform().inverse();

	int num_ee = mEndEffectors.size();
	Eigen::VectorXd p_save = skel->getPositions();
	Eigen::VectorXd v_save = skel->getVelocities();

	skel->setPositions(pos);
	skel->setVelocities(vel);
	skel->computeForwardKinematics(true, true, false);

	ret.resize((num_ee)*12+15);
//	ret.resize((num_ee)*9+12);

	for(int i=0;i<num_ee;i++)
	{		
		Eigen::Isometry3d transform = cur_root_inv * skel->getBodyNode(mEndEffectors[i])->getWorldTransform();
		//Eigen::Quaterniond q(transform.linear());
		// Eigen::Vector3d rot = QuaternionToDARTPosition(Eigen::Quaterniond(transform.linear()));
		ret.segment<9>(9*i) << transform.linear()(0,0), transform.linear()(0,1), transform.linear()(0,2),
							   transform.linear()(1,0), transform.linear()(1,1), transform.linear()(1,2), 
							   transform.translation();
//		ret.segment<6>(6*i) << rot, transform.translation();
	}


	for(int i=0;i<num_ee;i++)
	{
	    int idx = skel->getBodyNode(mEndEffectors[i])->getParentJoint()->getIndexInSkeleton(0);
		ret.segment<3>(9*num_ee + 3*i) << vel.segment<3>(idx);
//	    ret.segment<3>(6*num_ee + 3*i) << vel.segment<3>(idx);

	}

	// root diff with target com
	Eigen::Isometry3d transform = cur_root_inv * skel->getRootBodyNode()->getWorldTransform();
	//Eigen::Quaterniond q(transform.linear());

	Eigen::Vector3d rot = QuaternionToDARTPosition(Eigen::Quaterniond(transform.linear()));
	Eigen::Vector3d root_angular_vel_relative = cur_root_inv.linear() * skel->getRootBodyNode()->getAngularVelocity();
	Eigen::Vector3d root_linear_vel_relative = cur_root_inv.linear() * skel->getRootBodyNode()->getCOMLinearVelocity();

	ret.tail<15>() << transform.linear()(0,0), transform.linear()(0,1), transform.linear()(0,2),
					  transform.linear()(1,0), transform.linear()(1,1), transform.linear()(1,2),
					  transform.translation(), root_angular_vel_relative, root_linear_vel_relative;
//	ret.tail<12>() << rot, transform.translation(), root_angular_vel_relative, root_linear_vel_relative;

	// restore
	skel->setPositions(p_save);
	skel->setVelocities(v_save);
	skel->computeForwardKinematics(true, true, false);

	return ret;
}
Eigen::VectorXd 
SubController::
GetState(Character* character) {

	auto& skel = character->GetSkeleton();
	
	double root_height = skel->getRootBodyNode()->getCOM()[1];
	Eigen::VectorXd p,v;

	int n_bnodes = character->GetSkeleton()->getNumBodyNodes();
	int num_p = (n_bnodes - 1) * 6;
	p.resize(num_p);
	v = skel->getVelocities();
	for(int i = 1; i < n_bnodes; i++){
		Eigen::Isometry3d transform = skel->getBodyNode(i)->getRelativeTransform();
		// Eigen::Quaterniond q(transform.linear());
		p.segment<6>(6*(i-1)) << transform.linear()(0,0), transform.linear()(0,1), transform.linear()(0,2),
								 transform.linear()(1,0), transform.linear()(1,1), transform.linear()(1,2);
	}

	dart::dynamics::BodyNode* root = skel->getRootBodyNode();
	Eigen::Isometry3d cur_root_inv = root->getWorldTransform().inverse();
	Eigen::VectorXd ee;
	ee.resize(mEndEffectors.size()*3);
	for(int i=0;i<mEndEffectors.size();i++)
	{
		Eigen::Isometry3d transform = cur_root_inv * skel->getBodyNode(mEndEffectors[i])->getWorldTransform();
		ee.segment<3>(3*i) << transform.translation();
	}
	double t = mReferenceManager->GetTimeStep(mCurrentFrameOnPhase, true);
	Motion* p_v_target = mReferenceManager->GetMotion(mCurrentFrame+t, true);

	Eigen::VectorXd p_next = GetEndEffectorStatePosAndVel(character, p_v_target->GetPosition(), p_v_target->GetVelocity()*t);

	delete p_v_target;

	Eigen::Vector3d up_vec = root->getTransform().linear()*Eigen::Vector3d::UnitY();
	double up_vec_angle = atan2(std::sqrt(up_vec[0]*up_vec[0]+up_vec[2]*up_vec[2]),up_vec[1]);
	Eigen::VectorXd state;

	if(mType.compare("Idle") != 0) {
		Eigen::VectorXd param = mReferenceManager->GetParamGoal();
		state.resize(p.rows()+v.rows()+1+1+p_next.rows()+ee.rows()+2+param.rows());
		state<< p, v, up_vec_angle, root_height, p_next, mAdaptiveStep, ee, mCurrentFrameOnPhase, param;
		// std::cout<<"@ "<<mCurrentFrame<<" / "<<mCurrentFrameOnPhase<<" / goal ; "<<param.transpose()<<std::endl;

	} else {
 		state.resize(p.rows()+v.rows()+1+1+p_next.rows()+ee.rows()+2);
		state<< p, v, up_vec_angle, root_height, p_next, mAdaptiveStep, ee, mCurrentFrameOnPhase;
 	}


	return state;
}

//////////////////////////////////// PUNCH  ////////////////////////////////////

PUNCH_Controller::PUNCH_Controller(std::string motion, std::string ppo)
: SubController(std::string("Punch"), motion, ppo)
{}
bool PUNCH_Controller::Synchronizable(std::string) {
	if(mCurrentFrameOnPhase <= 10 || mCurrentFrameOnPhase >= mReferenceManager->GetPhaseLength() - 5)
		return true;
	return false;
}
//////////////////////////////////// IDLE  ////////////////////////////////////

IDLE_Controller::IDLE_Controller(std::string motion, std::string ppo)
: SubController(std::string("Idle"),  motion, ppo)
{}
bool IDLE_Controller::Synchronizable(std::string) {
	return true;
}
//////////////////////////////////// BLOCK  ////////////////////////////////////

BLOCK_Controller::BLOCK_Controller(std::string motion, std::string ppo)
: SubController(std::string("Block"),motion, ppo)
{}
bool BLOCK_Controller::Synchronizable(std::string) {
	if(mCurrentFrameOnPhase <= 10 || mCurrentFrameOnPhase >= mReferenceManager->GetPhaseLength() - 5)
		return true;
	return false;
}

//////////////////////////////////// KICK  ////////////////////////////////////

KICK_Controller::KICK_Controller(std::string motion, std::string ppo)
: SubController(std::string("Kick"), motion, ppo)
{}
bool KICK_Controller::Synchronizable(std::string) {
	if(mCurrentFrameOnPhase <= 10 || mCurrentFrameOnPhase >= mReferenceManager->GetPhaseLength() - 5)
		return true;
	return false;
}

//////////////////////////////////// PIVOT  ////////////////////////////////////

PIVOT_Controller::PIVOT_Controller(std::string motion, std::string ppo)
: SubController(std::string("Pivot"), motion, ppo)
{}
bool PIVOT_Controller::Synchronizable(std::string) {
	if(mCurrentFrameOnPhase <= 10 || mCurrentFrameOnPhase >= mReferenceManager->GetPhaseLength() - 5)
		return true;
	return false;
}

} // end namespace DPhy