#include "SimEnv.h"
#include <omp.h>
#include "dart/math/math.hpp"
#include "Functions.h"
#include <iostream>
SimEnv::
SimEnv(int num_slaves, std::string ref, std::string training_path, bool adaptive)
	:mNumSlaves(num_slaves)
{
	std::string path = std::string(CAR_DIR)+std::string("/character/") + std::string(REF_CHARACTER_TYPE) + std::string(".xml");

	dart::math::seedRand();
	omp_set_num_threads(num_slaves);

	DPhy::Character* character = new DPhy::Character(path);
	mReferenceManager = new DPhy::ReferenceManager(character);
	mReferenceManager->LoadMotionFromBVH(ref);

	if(adaptive) {
		mReferenceManager->InitOptimization(num_slaves, training_path);
	}
	
	for(int i =0;i<num_slaves;i++)
	{
		mSlaves.push_back(new DPhy::Controller(mReferenceManager, adaptive, false, i));
	//	mSlaves.push_back(new DPhy::SimpleController());

	}
	
	mNumState = mSlaves[0]->GetNumState();
	mNumAction = mSlaves[0]->GetNumAction();

	if(adaptive) {
		mParamStack = 0;
		mParamBase = mReferenceManager->GetTargetBase();
		mParamUnit = mReferenceManager->GetTargetUnit();
		nDim = mParamBase.rows();

		mParamGoalIdx.resize(nDim);
		Eigen::VectorXd p = mReferenceManager->GetTargetGoal();
		for(int j = 0; j < nDim; j++) {
			mParamGoalIdx(j) = std::floor((p(j) - mParamBase(j)) / mParamUnit(j));
		}	

		Py_Initialize();
		np::initialize();
		try{
			p::object regression = p::import("regression");
			this->mRegression = regression.attr("Regression")();
			this->mRegression.attr("initTrain")(training_path, nDim + 1, mReferenceManager->GetDOF());
		}
		catch (const p::error_already_set&)
		{
			PyErr_Print();
		}
	}
	isAdaptive = adaptive;
	mNeedRefUpdate = true;
}
//For general properties
int
SimEnv::
GetNumState()
{
	return mNumState;
}
int
SimEnv::
GetNumAction()
{
	return mNumAction;
}

//For each slave
void 
SimEnv::
Step(int id)
{
	if(mSlaves[id]->IsTerminalState()){
		return;
	}
	mSlaves[id]->Step();
}
void 
SimEnv::
Reset(int id,bool RSI)
{
	mSlaves[id]->Reset(RSI);
}
p::tuple 
SimEnv::
IsNanAtTerminal(int id)
{
	bool t = mSlaves[id]->IsTerminalState();
	bool n = mSlaves[id]->IsNanAtTerminal();
	int start = mSlaves[id]->GetStartFrame();
	double e = mSlaves[id]->GetCurrentLength();
	double tt = mSlaves[id]->GetTimeElapsed();
	int term = mSlaves[id]->GetTerminationReason();
	return p::make_tuple(t, n, start, e, tt, term);
}
np::ndarray
SimEnv::
GetState(int id)
{
	return DPhy::toNumPyArray(mSlaves[id]->GetState());
}
void 
SimEnv::
SetAction(np::ndarray np_array,int id)
{
	mSlaves[id]->SetAction(DPhy::toEigenVector(np_array,mNumAction));
}
p::list 
SimEnv::
GetRewardLabels()
{
	p::list l;
	std::vector<std::string> sl = mSlaves[0]->GetRewardLabels();
	for(int i =0 ; i <sl.size(); i++) l.append(sl[i]);
	return l;
}
double 
SimEnv::
GetReward(int id)
{
	return mSlaves[id]->GetReward();
}
np::ndarray
SimEnv::
GetRewardByParts(int id)
{
	std::vector<double> ret;
	if(dynamic_cast<DPhy::Controller*>(mSlaves[id])!=nullptr){
		ret = dynamic_cast<DPhy::Controller*>(mSlaves[id])->GetRewardByParts();
	}
	return DPhy::toNumPyArray(ret);
}
void
SimEnv::
Steps()
{
	if( mNumSlaves == 1){
		this->Step(0);
	}
	else{
#pragma omp parallel for
		for (int id = 0; id < mNumSlaves; ++id)
		{
			this->Step(id);
		}
	}
}
void
SimEnv::
Resets(bool RSI)
{
	for (int id = 0; id < mNumSlaves; ++id)
	{
		this->Reset(id,RSI);
	}
}
np::ndarray
SimEnv::
GetStates()
{
	Eigen::MatrixXd states(mNumSlaves,mNumState);

	for (int id = 0; id < mNumSlaves; ++id)
	{
		states.row(id) = mSlaves[id]->GetState().transpose();
	}
	return DPhy::toNumPyArray(states);
}
void
SimEnv::
SetActions(np::ndarray np_array)
{
	Eigen::MatrixXd action = DPhy::toEigenMatrix(np_array,mNumSlaves,mNumAction);

	for (int id = 0; id < mNumSlaves; ++id)
	{
		mSlaves[id]->SetAction(action.row(id).transpose());
	}
}
np::ndarray
SimEnv::
GetRewards()
{
	std::vector<float> rewards(mNumSlaves);
	for (int id = 0; id < mNumSlaves; ++id)
	{
		rewards[id] = this->GetReward(id);
	}

	return DPhy::toNumPyArray(rewards);
}
np::ndarray
SimEnv::
GetRewardsByParts()
{
	std::vector<std::vector<double>> rewards(mNumSlaves);
	for (int id = 0; id < mNumSlaves; ++id)
	{
		if(dynamic_cast<DPhy::Controller*>(mSlaves[id])!=nullptr){
			rewards[id] = dynamic_cast<DPhy::Controller*>(mSlaves[id])->GetRewardByParts();
		}
	}

	return DPhy::toNumPyArray(rewards);
}
bool
SimEnv::
Optimize()
{
	bool t = mReferenceManager->Optimize();
	if(t) {
		Eigen::VectorXd tp = mReferenceManager->GetTargetGoal();
		for(int id = 0; id < mNumSlaves; ++id) {
			mSlaves[id]->SetTargetParameters(tp);
		}
	}
	return t;
}
bool cmp(const std::pair<Eigen::VectorXd, double> &p1, const std::pair<Eigen::VectorXd, double> &p2){
    if(p1.second > p2.second){
        return true;
    }
    else{
        return false;
    }
}
void
SimEnv::
AssignParamsToBins() 
{
	std::vector<bool> visited;
	std::vector<bool> assigned;

	Eigen::VectorXd p_cur = mReferenceManager->GetTargetCurMean();
	Eigen::VectorXd idx_cur(nDim);
	for(int j = 0; j < nDim; j++) {
		idx_cur(j) = std::floor((p_cur(j) - mParamBase(j)) / mParamUnit(j));
	}	

	for(int i = 0; i < mParamNotAssigned.size(); i++) {
		visited.push_back(false);
		assigned.push_back(false);
	}
	for(int i = 0; i < mParamNotAssigned.size(); i++) {
		if(!visited[i]) {
			Eigen::VectorXd idx = mParamNotAssigned[i].second;
			for(int j = 0; j < mParamBins.size(); j++) {
				if((mParamBins[j].GetIdx() - idx).norm() < 1e-2) {
					visited[i] = true;
					assigned[i] = true;

					mParamBins[j].PutParam(mParamNotAssigned[i].first);
					break;
				}
			}
			if(!assigned[i]) {
				double dist_cur = (idx_cur - idx).norm();
				if(dist_cur > 4)
					continue;

				std::vector<int> p_temp;
				p_temp.push_back(i);
				for(int j = i + 1; j < mParamNotAssigned.size(); j++) {
					if(( mParamNotAssigned[j].second - idx).norm() < 1e-2) {
						visited[j] = true;
						p_temp.push_back(j);
					}
				}
				if(p_temp.size() >= 10) {
					ParamBin pb = ParamBin(idx);
					for(int j = 0; j < p_temp.size(); j++) {
						pb.PutParam(mParamNotAssigned[p_temp[j]].first);
						assigned[p_temp[j]] = true;
					}
					mParamBins.push_back(pb);
					if((idx - mParamGoalIdx).norm() < 1e-2) 
						mNeedRefUpdate = false;
				}
			}
		}
	}

	std::vector<std::pair<Eigen::VectorXd, Eigen::VectorXd>> paramNotAssigned_new;
	for(int i = 0; i < mParamNotAssigned.size(); i++) {
		if(!assigned[i])
			paramNotAssigned_new.push_back(mParamNotAssigned[i]);
	}
	mParamNotAssigned = paramNotAssigned_new;

}
void 
SimEnv::
TrainRegressionNetwork()
{
	std::pair<std::vector<Eigen::VectorXd>, std::vector<Eigen::VectorXd>> x_y = mReferenceManager->GetRegressionSamples();
	for(int i = 0; i < x_y.first.size(); i += mReferenceManager->GetNumCPS()) {
		Eigen::VectorXd param = (x_y.first)[i].tail((x_y.first)[i].rows() - 1);
		Eigen::VectorXd idx(nDim);
		for(int j = 0; j < nDim; j++) {
			idx(j) = std::floor((param(j) - mParamBase(j)) / mParamUnit(j));
		}
		mParamNotAssigned.push_back(std::pair<Eigen::VectorXd, Eigen::VectorXd>(param, idx));
		mParamStack += 1;

	}

	np::ndarray x = DPhy::toNumPyArray(x_y.first);
	np::ndarray y = DPhy::toNumPyArray(x_y.second);
	
	p::list l;
	l.append(x);
	l.append(y);

	this->mRegression.attr("saveRegressionData")(l);
	this->mRegression.attr("updateRegressionData")(l);
	
	if(mParamStack > 10) {
	    this->AssignParamsToBins();
		this->mRegression.attr("train")();

	    mParamStack = 0;
	}

}
void
SimEnv::
LoadAdaptiveMotion()
{
	mReferenceManager->LoadAdaptiveMotion();
}
double 
SimEnv::
GetPhaseLength()
{
	return mReferenceManager->GetPhaseLength();
}
int
SimEnv::
GetDOF()
{
	return mReferenceManager->GetDOF();
}
np::ndarray
SimEnv::
GetTargetBase()
{
	return DPhy::toNumPyArray(mParamBase);
}
np::ndarray
SimEnv::
GetTargetUnit()
{
	return DPhy::toNumPyArray(mParamUnit);
}
p::list
SimEnv::
GetHindsightTuples()
{

	int nCps = mReferenceManager->GetNumCPS();
	int dof = mReferenceManager->GetDOF();
	p::list input_li;
	p::list result_li;
	std::vector<std::vector<Eigen::VectorXd>> targetParameters;
	for (int id = 0; id < mNumSlaves; ++id)
	{
		targetParameters.push_back(mSlaves[id]->GetHindsightTarget());
		int nInput = targetParameters[id].size()*nCps;

		p::tuple shape = p::make_tuple(nInput, targetParameters[id][0].rows() + 1);
		np::dtype dtype = np::dtype::get_builtin<float>();
		np::ndarray input = np::empty(shape, dtype);

		float* data = reinterpret_cast<float*>(input.get_data());
		
		int idx = 0;
		for(int i = 0; i < targetParameters[id].size(); i++)
		{
			for(int j = 0; j < nCps; j++) {
				data[idx++] = (float)j;
				for(int k = 0; k < targetParameters[id][i].rows(); k++)
					data[idx++] = (float)targetParameters[id][i][k];
			}
		}
		input_li.append(input);
	}

	p::object output_li = this->mRegression.attr("runBatch")(input_li);
	std::vector<std::vector<std::vector<std::tuple<Eigen::VectorXd, Eigen::VectorXd, Eigen::VectorXd, double>>>> ss;
	std::vector<std::vector<std::vector<Eigen::VectorXd>>> cps_;

	for (int id = 0; id < mNumSlaves; ++id)
	{
		int nInput = targetParameters[id].size()*nCps;

		np::ndarray na = np::from_object(output_li[id]);
		Eigen::VectorXd output = DPhy::toEigenVector(na, mNumSlaves*dof*nInput);
		std::vector<std::vector<Eigen::VectorXd>> cps;
		for(int i = 0; i < targetParameters[id].size(); i++)
		{
			std::vector<Eigen::VectorXd> cps_phase;
			for(int j = 0; j < nCps; j++) {
				cps_phase.push_back(output.block(i*nCps*dof + j*dof, 0, dof, 1));
			}
			cps.push_back(cps_phase);
		}
		cps_.push_back(cps);
	}
#pragma omp parallel for
	for (int id = 0; id < mNumSlaves; ++id)
	{
		std::vector<std::vector<std::tuple<Eigen::VectorXd, Eigen::VectorXd, Eigen::VectorXd, double>>> sar = mSlaves[id]->GetHindsightSAR(cps_[id]);
		ss.push_back(sar);
	}
	for (int id = 0; id < mNumSlaves; ++id)
	{
		auto sar = ss[id];
		for(int l = 0; l < sar.size(); l++) {
			p::list sar_episodes;

			for(int i = 0; i < sar[l].size(); i++) {
				p::list sar_tuples;

				sar_tuples.append(DPhy::toNumPyArray(std::get<0>(sar[l][i])));
				sar_tuples.append(DPhy::toNumPyArray(std::get<1>(sar[l][i])));
				sar_tuples.append(DPhy::toNumPyArray(std::get<2>(sar[l][i])));
				sar_tuples.append(std::get<3>(sar[l][i]));
				
				sar_episodes.append(sar_tuples);
			}
			result_li.append(sar_episodes);
		}
	}

	return result_li;
}
void 
SimEnv::
SetRefUpdateMode(bool t) {

	mReferenceManager->SetRefUpdateMode(t);
	if(t) {
		// load cps
		mReferenceManager->LoadAdaptiveMotion("updated");
		Eigen::VectorXd tp = mReferenceManager->GetTargetGoal();		
		for(int id = 0; id < mNumSlaves; ++id) {
			mSlaves[id]->SetTargetParameters(tp);
		}
	} else {
		mReferenceManager->SaveAdaptiveMotion("updated");
	}
}
bool 
SimEnv::
NeedRefUpdate() {
	return mNeedRefUpdate;
}
void 
SimEnv::
SetTargetParameters(np::ndarray np_array) {

	Eigen::VectorXd tp = DPhy::toEigenVector(np_array, nDim);
	std::cout << tp.transpose() << std::endl;
	int dof = mReferenceManager->GetDOF();

	std::vector<Eigen::VectorXd> cps;
	for(int j = 0; j < mReferenceManager->GetNumCPS(); j++) {
		Eigen::VectorXd input(1 + nDim);
		input << j, tp;
		p::object a = this->mRegression.attr("run")(DPhy::toNumPyArray(input));
		np::ndarray na = np::from_object(a);
		cps.push_back(DPhy::toEigenVector(na, dof));
	}

	mReferenceManager->LoadAdaptiveMotion(cps);
	for(int id = 0; id < mNumSlaves; ++id) {
		mSlaves[id]->SetTargetParameters(tp);
	}
}
p::list  
SimEnv::
GetTargetBound() {
	p::list bound;

	if(mParamBins.size() != 0) 
	{
		for(int i = 0; i < mParamBins.size(); i++) {
			bound.append(DPhy::toNumPyArray(mParamBins[i].GetIdx()));
		}
	}

	return bound;
}

using namespace boost::python;

BOOST_PYTHON_MODULE(simEnv)
{
	Py_Initialize();
	np::initialize();

	class_<SimEnv>("Env",init<int, std::string, std::string, bool>())
		.def("GetPhaseLength",&SimEnv::GetPhaseLength)
		.def("GetNumState",&SimEnv::GetNumState)
		.def("GetNumAction",&SimEnv::GetNumAction)
		.def("Step",&SimEnv::Step)
		.def("Reset",&SimEnv::Reset)
		.def("GetState",&SimEnv::GetState)
		.def("SetAction",&SimEnv::SetAction)
		.def("GetRewardLabels",&SimEnv::GetRewardLabels)
		.def("GetReward",&SimEnv::GetReward)
		.def("GetRewardByParts",&SimEnv::GetRewardByParts)
		.def("Steps",&SimEnv::Steps)
		.def("Resets",&SimEnv::Resets)
		.def("IsNanAtTerminal",&SimEnv::IsNanAtTerminal)
		.def("GetStates",&SimEnv::GetStates)
		.def("SetActions",&SimEnv::SetActions)
		.def("GetRewards",&SimEnv::GetRewards)
		.def("GetHindsightTuples",&SimEnv::GetHindsightTuples)
		.def("TrainRegressionNetwork",&SimEnv::TrainRegressionNetwork)
		.def("Optimize",&SimEnv::Optimize)
		.def("GetDOF",&SimEnv::GetDOF)
		.def("LoadAdaptiveMotion",&SimEnv::LoadAdaptiveMotion)
		.def("SetTargetParameters",&SimEnv::SetTargetParameters)
		.def("SetRefUpdateMode",&SimEnv::SetRefUpdateMode)
		.def("GetTargetBound",&SimEnv::GetTargetBound)
		.def("NeedRefUpdate",&SimEnv::NeedRefUpdate)
		.def("GetRewardsByParts",&SimEnv::GetRewardsByParts)
		.def("GetTargetBase",&SimEnv::GetTargetBase)
		.def("GetTargetUnit",&SimEnv::GetTargetUnit);

}