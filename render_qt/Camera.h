#ifndef __GUI_CAMERA_H__
#define __GUI_CAMERA_H__
#include <Eigen/Core>
#include <Eigen/Sparse>
#include <Eigen/Geometry>
class Camera
{
public:
	Camera(int w,int h);
		
	void SetSize(int w,int h);
	void SetCamera(const Eigen::Vector3d& lookAt,const Eigen::Vector3d& eye,const Eigen::Vector3d& up);
	void Apply();

	void Pan(int x,int y,int prev_x,int prev_y);
	void Zoom(int x,int y,int prev_x,int prev_y);
	void Rotate(int x,int y,int prev_x,int prev_y);
	void Translate(int x,int y,int prev_x,int prev_y);
	void SetCenter(Eigen::Vector3d c);
	void SetLookAt(const Eigen::Vector3d& lookAt);
	Eigen::Vector3d GetDeltaPosition(int x,int y,int prev_x,int prev_y);

	void updateLookVector();
	void trackEyeUpdate(Eigen::Vector3d newLookAt);
	Eigen::Vector3d GetLookAt(){return lookAt;} 

private:
	Eigen::Vector3d lookAt;
	Eigen::Vector3d eye;
	Eigen::Vector3d up;
	double fovy;
	int mw,mh;
	
	Eigen::Vector3d Rotateq(const Eigen::Vector3d& target, const Eigen::Vector3d& rotateVector,double angle);
	Eigen::Vector3d GetTrackballPoint(int mouseX, int mouseY,int w,int h);
	Eigen::Vector3d UnProject(const Eigen::Vector3d& vec);

	Eigen::Vector3d lookVector; 
};


#endif