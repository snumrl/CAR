#include <GL/glew.h>
#include "SplineWindow.h"
#include "dart/external/lodepng/lodepng.h"
#include "Functions.h"
#include <algorithm>
#include <fstream>
#include <boost/filesystem.hpp>
#include <GL/glut.h>
using namespace GUI;
using namespace dart::simulation;
using namespace dart::dynamics;

SplineWindow::
SplineWindow(std::string motion, std::string record, std::string record_type)
	:GLUTWindow(),mTrackCamera(false),mIsRotate(false),mIsAuto(false), mTimeStep(1 / 30.0), mDrawRef2(false)
{
	this->mTotalFrame = 0;

	std::string skel_path = std::string(CAR_DIR)+std::string("/character/") + std::string(REF_CHARACTER_TYPE) + std::string(".xml");
	this->mRef = new DPhy::Character(skel_path);
	if(record_type.compare("position") == 0) {
		mDrawRef2 = true;
		this->mRef2 = new DPhy::Character(skel_path); 
	}

	int n_bnodes = mRef->GetSkeleton()->getNumBodyNodes();
	int dof = this->mRef->GetSkeleton()->getPositions().rows();

	DPhy::ReferenceManager* referenceManager = new DPhy::ReferenceManager(this->mRef);
	referenceManager->LoadMotionFromBVH(std::string("/motion/") + motion);

	std::vector<double> knots;
	knots.push_back(0);
	knots.push_back(9);
	knots.push_back(20);
	knots.push_back(27);
	knots.push_back(35);

	DPhy::MultilevelSpline* s = new DPhy::MultilevelSpline(1, referenceManager->GetPhaseLength());
	s->SetKnots(0, knots);

	std::vector<Eigen::VectorXd> cps;
	int cps_counter = 0;
	
	std::ifstream is(record);
		
	char buffer[256];

	int length = 0;
	double reward = 0;
	std::vector<Eigen::VectorXd> pos;
	std::vector<double> step;

	while(!is.eof()) {
		if(record_type.compare("spline") == 0) {
			Eigen::VectorXd cp(dof);
			for(int j = 0; j < dof; j++) 
			{
				is >> buffer;
				cp[j] = atof(buffer);
			}
			is >> buffer;

			cps.push_back(cp);
			cps_counter++;

			if(cps_counter == 5) {

				s->SetControlPoints(0, cps);
				std::vector<Eigen::VectorXd> displacement = s->ConvertSplineToMotion();	
				std::vector<Eigen::VectorXd> position;

				for(int i = 0; i < displacement.size(); i++) {
					Eigen::VectorXd p = referenceManager->GetPosition(i);
					for(int j = 0; j < n_bnodes; j++) {
						int idx = mRef->GetSkeleton()->getBodyNode(j)->getParentJoint()->getIndexInSkeleton(0);
						int j_dof = mRef->GetSkeleton()->getBodyNode(j)->getParentJoint()->getNumDofs();
						
						if(j_dof == 6) {
							p.segment<3>(idx) = DPhy::Rotate3dVector(p.segment<3>(idx), displacement[i].segment<3>(idx));
							p.segment<3>(idx + 3) += displacement[i].segment<3>(idx + 3);
						} else if (j_dof == 3) {
							p.segment<3>(idx) = DPhy::Rotate3dVector(p.segment<3>(idx), displacement[i].segment<3>(idx));
						} else {
							p(idx) += displacement[i](idx);
						}
								
					}
					position.push_back(p);
				}
				for(int i = 0; i < position.size(); i++) {
					mMemoryRef.push_back(position[i]);
					length += 1;
				}	

				cps_counter = 0;
				cps.clear();
			}
		} else if(record_type.compare("position") == 0) {
			Eigen::VectorXd p(dof);
			for(int j = 0; j < dof; j++) 
			{
				is >> buffer;
				p[j] = atof(buffer);
			}

			is >> buffer;
			double cur_step = atof(buffer);			
			is >> buffer;
			double cur_reward = atof(buffer);
			if(reward == 0)
				reward = cur_reward;
			if(cur_reward != reward) {
				reward = cur_reward;
				double curFrame = 0;
				std::vector<std::pair<Eigen::VectorXd,double>> displacement;

				for(int i = 0; i < pos.size(); i++) {
					Eigen::VectorXd p = pos[i];
					Eigen::VectorXd p_bvh = referenceManager->GetPosition(curFrame);
					Eigen::VectorXd d(dof);
					for(int j = 0; j < n_bnodes; j++) {
						int idx = mRef->GetSkeleton()->getBodyNode(j)->getParentJoint()->getIndexInSkeleton(0);
						int dof = mRef->GetSkeleton()->getBodyNode(j)->getParentJoint()->getNumDofs();
						
						if(dof == 6) {
							d.segment<3>(idx) = DPhy::JointPositionDifferences(p.segment<3>(idx), p_bvh.segment<3>(idx));
							d.segment<3>(idx + 3) = p.segment<3>(idx + 3) -  p_bvh.segment<3>(idx + 3);
						} else if (dof == 3) {
							d.segment<3>(idx) = DPhy::JointPositionDifferences(p.segment<3>(idx), p_bvh.segment<3>(idx));
						} else {
							d(idx) = p(idx) - p_bvh(idx);
						}
					}
					displacement.push_back(std::pair<Eigen::VectorXd,double>(d, curFrame));
					
					if(i < pos.size() - 1)
					  curFrame += 1 + step[i + 1];
				}
				s->ConvertMotionToSpline(displacement);
				std::vector<Eigen::VectorXd> displacement_s = s->ConvertSplineToMotion();
				std::vector<Eigen::VectorXd> new_pos;
				int l = std::min(pos.size(), displacement_s.size());
				for(int i = 0; i < l; i++) {
					Eigen::VectorXd p = referenceManager->GetPosition(i);
					for(int j = 0; j < n_bnodes; j++) {
						int idx = mRef->GetSkeleton()->getBodyNode(j)->getParentJoint()->getIndexInSkeleton(0);
						int j_dof = mRef->GetSkeleton()->getBodyNode(j)->getParentJoint()->getNumDofs();
						
						if(j_dof == 6) {
							p.segment<3>(idx) = DPhy::Rotate3dVector(p.segment<3>(idx), displacement_s[i].segment<3>(idx));
							p.segment<3>(idx + 3) += displacement_s[i].segment<3>(idx + 3);
						} else if (j_dof == 3) {
							p.segment<3>(idx) = DPhy::Rotate3dVector(p.segment<3>(idx), displacement_s[i].segment<3>(idx));
						} else {
							p(idx) += displacement_s[i](idx);
						}
								
					}
					new_pos.push_back(p);
				}

				for(int i = 0; i < l; i++) {
					length += 1;
					mMemoryRef.push_back(pos[i]);
					new_pos[i][3] += 1.5;
					mMemoryRef2.push_back(new_pos[i]);
				}
				pos.clear();
				step.clear();
			}
			pos.push_back(p);
			step.push_back(cur_step);
		}
	}

	is.close();
	if(this->mTotalFrame == 0 || length < mTotalFrame) {
		mTotalFrame = length;
	}

	DPhy::SetSkeletonColor(this->mRef->GetSkeleton(), Eigen::Vector4d(235./255., 235./255., 235./255., 1.0));
	DPhy::SetSkeletonColor(this->mRef2->GetSkeleton(), Eigen::Vector4d(235./255., 87./255., 87./255., 1.0));

	this->mCurFrame = 0;
	this->mDisplayTimeout = 33;

	this->SetFrame(this->mCurFrame);

}
void
SplineWindow::
SetFrame(int n)
{
	if( n < 0 || n >= this->mTotalFrame )
	{
	 	std::cout << "Frame exceeds limits" << std::endl;
	 	return;
	}

    mRef->GetSkeleton()->setPositions(mMemoryRef[n]);
    if(mDrawRef2) 
    	mRef2->GetSkeleton()->setPositions(mMemoryRef2[n]);

}

void
SplineWindow::
NextFrame()
{ 
	this->mCurFrame+=1;
	if (this->mCurFrame >= this->mTotalFrame) {
        this->mCurFrame = 0;
    }
	this->SetFrame(this->mCurFrame);
}
void
SplineWindow::
PrevFrame()
{
	this->mCurFrame-=1;
	if( this->mCurFrame < 0 ) {
        this->mCurFrame = this->mTotalFrame - 1;
    }
	this->SetFrame(this->mCurFrame);
}
void
SplineWindow::
DrawSkeletons()
{
	GUI::DrawSkeleton(this->mRef->GetSkeleton(), 0);
	if(mDrawRef2)
		GUI::DrawSkeleton(this->mRef2->GetSkeleton(), 0);
}
void
SplineWindow::
DrawGround()
{
	Eigen::Vector3d com_root;
	com_root = this->mRef->GetSkeleton()->getRootBodyNode()->getCOM();
	GUI::DrawGround((int)com_root[0], (int)com_root[2], 0);
}
void
SplineWindow::
Display() 
{

	glClearColor(1.0, 1.0, 1.0, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);

	dart::dynamics::SkeletonPtr skel = this->mRef->GetSkeleton();
	Eigen::Vector3d com_root = skel->getRootBodyNode()->getCOM();
	Eigen::Vector3d com_front = skel->getRootBodyNode()->getTransform()*Eigen::Vector3d(0.0, 0.0, 2.0);

	if(this->mTrackCamera){
		Eigen::Vector3d com = skel->getRootBodyNode()->getCOM();
		Eigen::Isometry3d transform = skel->getRootBodyNode()->getTransform();
		com[1] = 0.8;

		Eigen::Vector3d camera_pos;
		camera_pos << -3, 1, 1.5;
		camera_pos = camera_pos + com;
		camera_pos[1] = 2;

		mCamera->SetCenter(com);
	}
	mCamera->Apply();

	glUseProgram(program);
	glPushMatrix();
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glScalef(1.0, -1.0, 1.0);
	initLights(com_root[0], com_root[2], com_front[0], com_front[2]);
	// DrawSkeletons();
	glPopMatrix();
	initLights(com_root[0], com_root[2], com_front[0], com_front[2]);
	// glColor4f(0.7, 0.0, 0.0, 0.40);  /* 40% dark red floor color */
	DrawGround();
	DrawSkeletons();
	glDisable(GL_BLEND);

	glUseProgram(0);
	glutSwapBuffers();

}
void
SplineWindow::
Reset()
{
	this->mCurFrame = 0;
	this->SetFrame(this->mCurFrame);

}
void
SplineWindow::
Keyboard(unsigned char key,int x,int y) 
{
	switch(key)
	{
		case '`' :mIsRotate= !mIsRotate;break;
		case '[': mIsAuto=false;this->PrevFrame();break;
		case ']': mIsAuto=false;this->NextFrame();break;
		case 'o': this->mCurFrame-=99; this->PrevFrame();break;
		case 'p': this->mCurFrame+=99; this->NextFrame();break;
		case 's': std::cout << this->mCurFrame << std::endl;break;
		case 'r': Reset();break;
		case 't': mTrackCamera = !mTrackCamera; this->SetFrame(this->mCurFrame); break;
		case ' ':
			mIsAuto = !mIsAuto;
			break;
		case 27: exit(0);break;
		default : break;
	}
	// this->SetFrame(this->mCurFrame);

	// glutPostRedisplay();
}
void
SplineWindow::
Mouse(int button, int state, int x, int y) 
{
	if(button == 3 || button == 4){
		if (button == 3)
		{
			mCamera->Pan(0,-5,0,0);
		}
		else
		{
			mCamera->Pan(0,5,0,0);
		}
	}
	else{
		if (state == GLUT_DOWN)
		{
			mIsDrag = true;
			mMouseType = button;
			mPrevX = x;
			mPrevY = y;
		}
		else
		{
			mIsDrag = false;
			mMouseType = 0;
		}
	}

	// glutPostRedisplay();
}
void
SplineWindow::
Motion(int x, int y) 
{
	if (!mIsDrag)
		return;

	int mod = glutGetModifiers();
	if (mMouseType == GLUT_LEFT_BUTTON)
	{
		mCamera->Translate(x,y,mPrevX,mPrevY);
	}
	else if (mMouseType == GLUT_RIGHT_BUTTON)
	{
		mCamera->Rotate(x,y,mPrevX,mPrevY);
	

	}
	mPrevX = x;
	mPrevY = y;
}
void
SplineWindow::
Reshape(int w, int h) 
{
	glViewport(0, 0, w, h);
	mCamera->Apply();
}

void 
SplineWindow::
Step()
{	
	this->mCurFrame++;
	this->SetFrame(this->mCurFrame);
}
void
SplineWindow::
Timer(int value) 
{
	std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

	if( mIsAuto && this->mCurFrame == this->mTotalFrame - 1){
         Step();
	} else if( mIsAuto && this->mCurFrame < this->mTotalFrame - 1){
        this->mCurFrame++;
        SetFrame(this->mCurFrame);
        	
    }

	std::chrono::steady_clock::time_point end= std::chrono::steady_clock::now();
	double elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count()/1000.;
	
	glutTimerFunc(std::max(0.0,mDisplayTimeout-elapsed), TimerEvent,1);
	glutPostRedisplay();

}