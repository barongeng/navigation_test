#include "navigation.h"
#include <sensor_msgs/LaserScan.h>
#include <sstream>

#define PI 3.14159265359
#define RAD2DEG 180/PI
#define ANGLE 20*(PI/180)


// Computes the angle error of the Frobit (relative to the line going throught the center of the row)
// a, b and c are distances (sides in triangle), A,C and E are angles;
float AngleError(float a, float b){	
	// When the rays of the laser scanner don't interesct the hough lines (=> no triangle)
	if(a == 0 || b == 0){
		std::cout << "ANGLE ERROR TOO BIG!\n";
	} 

	float A,E; 
	float c = sqrt((a*a)+(b*b)-2*a*b*cos(ANGLE)); //cosine formula in a triangle
	A = acos(((b*b)+(c*c)-(a*a))/(2*b*c))*RAD2DEG; //determine the angle from the cosine formula
	E = A-90; //error angle in degrees; In the perfect case, E=-10 => ANGLE/2+E=0;
	return A;
}

void Frobit::IMUCallback(const msgs::nmea::ConstPtr& imu){
	float f = 0;
	std::istringstream(imu->data[5]) >> f;
	std::cout << "f: " << f << "\n";
	if(imu->type == "SFIMU" && f > 15){
		angelTurned+=(f/14.375)*0.01;
	}
}

// get data from laser scanner for objects nearby 
void Frobit::scanCallback(const sensor_msgs::LaserScan::ConstPtr& scan_in){
	pid.dt = 0.1; // D gain
	pid.Kp = 0.02; // P gain
	pid.Ki = 0.019; // I gain
    
    // check if runing into wall - range[257] is the one straight ahead of the robot (should be parallel to the 2 hough lines)
	if(scan_in->ranges[257] <= 1 && scan_in->ranges[257] >= 0.5){
		stopCounter++; // if the distance is between 0.5 and 1, the Frobit is runing towards the wall;
	}
	float _20degForward = 0, _0deg = 0, errorDeg = 0;

	//range vector comes from LaserScan message; range[0] - 0 degree from right side; range[57] - 20 degree from right; range[456] - 20 degree from left;  range[513] - 0 degree from left;
	if(followLeftWall){
		_20degForward = scan_in->ranges[456]; 
		_0deg = scan_in->ranges[513];
	} else { // follow the right wall
		_20degForward = scan_in->ranges[57];
		_0deg = scan_in->ranges[0];
	}

	//Calculate the angle-error
	errorDeg = AngleError(_20degForward, _0deg);

	//publish error
	error_msg.data = errorDeg;
	error_pub_.publish(error_msg);

	pid.perror = errorDeg;

	pid.ierror += errorDeg*pid.dt;

	float limit = 1.5;
	if(pid.ierror > limit){
		pid.ierror = limit;
	}

	if(pid.ierror < -limit){
		pid.ierror = -limit;
	}

	std::cout << pid.ierror << "\n";
	z_axis = pid.perror*pid.Kp + pid.ierror*pid.Ki;	
}

// constructor for the Frobit
Frobit::Frobit(){

	angelTurned = 0; //related to IMU --- needs to be checked
	stopCounter=0;
	followLeftWall = true;

	deadman_button.data = false;
	z_axis = 0;
	x_axis = 0;
        v_max = 0.3;
        d_max = 4;

	//subScan = n.subscribe("/fmSensors/scan", 1000, &Frobit::scanCallback, this);
	sub_lines = n.subscribe("/measHough", 5, &Frobit::updateVel,this);
	twist_pub_ = n.advertise<geometry_msgs::TwistStamped>("/cmd_vel2", 1000);
	deadman_pub_ = n.advertise<std_msgs::Bool>("/fmCommand/deadman", 1000);
	error_pub_ = n.advertise<std_msgs::Float64>("/frobyte/error", 1000);

}

void Frobit::updateVel(const std_msgs::Float64MultiArray& houghInfo){
// called every 0.1 sec to publish cmd_vel
	twist.header.stamp = ros::Time::now();
	twist.twist.angular.z = z_axis;

	// Get values from the hough2line node (theta,r1,r2)
	float theta = houghInfo.data[0];
	float r1_ = houghInfo.data[1];
	float r2_ = houghInfo.data[2];

	// compute distances to the two lines and the desired "center lane"
	float r_dist_ = r1_+r2_;  
	float center_range = fabs(r1_)+fabs(r2_);  
	
	double err_ = 0;
	// compute the angle error	
	if(fabs(r_dist_)>center_range/7)
	{
		err_ = 0.35*r_dist_;
	}
	
	twist.twist.angular.z = -0.4*theta*DEG2RAD+err_;
	// compute the angle error	
	/*if(fabs(r_dist_)>center_range/7)
	{
		twist.twist.angular.z = 0.2*r_dist_;
	}		
	else
	{
		twist.twist.angular.z = -theta*0.4*.017453293;
	}*/
	
	// handle the velocity, in case robot is running close to an obstacle 
	float R=std::min(fabs(r1_),fabs(r2_)); //smallest r distance
	
	// determine the frontal distance to the obstacle	
	float phi = fabs(90-fabs(theta))*DEG2RAD;
	float front_dist_ = R/cos(phi);
	
	// change the velocity
	twist.twist.linear.x = std::min((float)fabs(front_dist_/d_max), 1.0f)*v_max;	

	std::cout << "theta: "<< theta <<  " and phi: = " << phi << std::endl;
	std::cout << "err: "<< err_ << std::endl;
	std::cout << "r1: "<< r1_ <<  " and r2: = " << r2_ << std::endl;
	std::cout << "R: "<< R << std::endl;
	std::cout << front_dist_ <<  " and division = " << front_dist_/d_max << std::endl;
	//twist.twist.linear.x = 0.2; //constant speed
	// send command to motors
	twist_pub_.publish(twist);
	//deadman_button.data = true;	
	//deadman_pub_.publish(deadman_button);
}
int main(int argc, char** argv){	
	ros::init(argc, argv, "navigation_node");
	ros::NodeHandle nh;
	ros::Publisher pub_imu_ = nh.advertise<sensor_msgs::Imu> ("/fmInformation/imu", 1);

	angleIntegrator angleintegrator(pub_imu_);
	ros::Subscriber sub = nh.subscribe("/fmData/nmea_from_imu", 1, &angleIntegrator::newMsgCallback, &angleintegrator);

    ros::Timer t;
    t = nh.createTimer(ros::Duration(0.05), &angleIntegrator::publishAngle,&angleintegrator);
	
	Frobit *frobo = new Frobit();
    ros::Rate loop_rate(10); // publish 10 messages per second
	// while (ros::ok()){			
		//frobo->updateVel(); //activate motors
		ros::spin();
		//loop_rate.sleep();
        //}*/
 return 0;
}
