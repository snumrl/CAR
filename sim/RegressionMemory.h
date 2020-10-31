#ifndef __REGMEM_H__
#define __REGMEM_H__
#include <vector>
#include <string>
#include <map>
#include <Eigen/Dense>
#include <random>

template<>
struct std::less<Eigen::VectorXd>
{ 
	bool operator()(Eigen::VectorXd const& a, Eigen::VectorXd const& b) const {
	    assert(a.size() == b.size());
	    for(size_t i = 0; i < a.size(); i++)
	    {
	        if(a[i] < b[i]) 
	        	return true;
	        if(a[i] > b[i]) 
	        	return false;
	    }
	    return false;
	}
};

namespace DPhy
{
struct GoalInfo
{
	Eigen::VectorXd param;
	int numSamples;
	double rewards;
	double density;
	double value;
};
struct Param
{
	Eigen::VectorXd param_normalized;
	std::vector<Eigen::VectorXd> cps;
	double reward;
	bool update;
};
class ParamCube
{
public:
	ParamCube(Eigen::VectorXd i) { idx = i; activated = false; }
	Eigen::VectorXd GetIdx(){ return idx; }
	void PutParam(Param* p) { param.push_back(p); }
	int GetNumParams() { return param.size(); }
	std::vector<Param*> GetParams() {return param;}
	void PutParams(std::vector<Param*> ps);
	void SetActivated(bool ac) { activated = ac; }
	bool GetActivated() { return activated;}
private:
	Eigen::VectorXd idx;
	std::vector<Param*> param;
	bool activated;
};
class RegressionMemory
{
public:
	
	RegressionMemory();
	void InitParamSpace(Eigen::VectorXd paramBvh, std::pair<Eigen::VectorXd, Eigen::VectorXd> paramSpace , Eigen::VectorXd paramUnit, 
						double nDOF, double nknots);
	void SaveParamSpace(std::string path);
	void LoadParamSpace(std::string path);

	Eigen::VectorXd UniformSample(int n=2);
	bool UpdateParamSpace(std::tuple<std::vector<Eigen::VectorXd>, Eigen::VectorXd, double> candidate);
	void SelectNewParamGoalCandidate();

	void AddMapping(Param* p);
	void AddMapping(Eigen::VectorXd nearest, Param* p);
	void DeleteMappings(Eigen::VectorXd nearest, std::vector<Param*> ps);

	double GetDistanceNorm(Eigen::VectorXd p0, Eigen::VectorXd p1);	
	double GetDensity(Eigen::VectorXd p);
	Eigen::VectorXd GetNearestPointOnGrid(Eigen::VectorXd p);
	Eigen::VectorXd GetNearestActivatedParam(Eigen::VectorXd p);
	std::vector<Eigen::VectorXd> GetNeighborPointsOnGrid(Eigen::VectorXd p, double radius);
	std::vector<Eigen::VectorXd> GetNeighborPointsOnGrid(Eigen::VectorXd p, Eigen::VectorXd nearest, double radius);
	std::vector<Eigen::VectorXd> GetNeighborParams(Eigen::VectorXd p);
	std::vector<std::pair<double, Param*>> GetNearestParams(Eigen::VectorXd p, int n, bool search_neighbor=false);

	Eigen::VectorXd Normalize(Eigen::VectorXd p);
	Eigen::VectorXd Denormalize(Eigen::VectorXd p);
	void SaveContinuousParamSpace(std::string path);

	bool IsSpaceExpanded();
	bool IsSpaceFullyExplored();

	Eigen::VectorXd GetParamGoal() {return mParamGoalCur; }
	void SetParamGoal(Eigen::VectorXd paramGoal);
	void SetGoalInfo(double v);
	void SetRadius(double rn) { mRadiusNeighbor = rn; }
	void SetParamGridUnit(Eigen::VectorXd gridUnit) { mParamGridUnit = gridUnit;}
	int GetDim() {return mDim; }
	void ResetExploration();
	std::tuple<std::vector<Eigen::VectorXd>, 
			   std::vector<Eigen::VectorXd>, 
			   std::vector<double>> GetTrainingData(bool update=false);
	int GetTimeFromLastUpdate() { return mTimeFromLastUpdate; }

	double GetParamReward(Eigen::VectorXd p, Eigen::VectorXd p_goal);
	std::vector<Eigen::VectorXd> GetCPSFromNearestParams(Eigen::VectorXd p_goal);
	void SaveLog(std::string path);
	double GetTrainedRatio() {return (double)mParamActivated.size() / (mParamDeactivated.size() + mParamActivated.size()); }
	void SaveGoalInfo(std::string path);
	void EvalExplorationStep();
	bool SetNextCandidate();

private:
	std::map<Eigen::VectorXd, int> mParamActivated;
	std::map<Eigen::VectorXd, int> mParamDeactivated;
	std::map<Eigen::VectorXd, Param*> mParamNew;

	Eigen::VectorXd mParamScale;
	Eigen::VectorXd mParamScaleInv;
	Eigen::VectorXd mParamGoalCur;
	Eigen::VectorXd mParamMin;
	Eigen::VectorXd mParamMax;
	Eigen::VectorXd mParamGridUnit;
	Param* mParamBVH;

	std::map< Eigen::VectorXd, ParamCube* > mGridMap;

	std::vector<std::pair<double, Param*>> mPrevElite;
	std::vector<Eigen::VectorXd> mPrevCPS;   
	double mPrevReward;

	double mRadiusNeighbor;
	int mDim;
	int mDimDOF;
	int mNumKnots;
	int mNumActivatedPrev;
	int mThresholdUpdate;
	int mThresholdActivate;
	int mTimeFromLastUpdate;
	int mNumElite;
	int mNumSamples;

	int mExplorationStep;
	int mNumGoalCandidate;
	int mIdxCandidate;
	std::vector<Eigen::VectorXd> mGoalCandidate;
	std::vector<bool> mGoalExplored;
	std::vector<double> mGoalProgress;
	std::vector<double> mGoalReward;

	std::random_device mRD;
	std::mt19937 mMT;
	std::uniform_real_distribution<double> mUniform;

	std::vector<std::string> mRecordLog;

	GoalInfo mGoalInfo; 
};
}
#endif