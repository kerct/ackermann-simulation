#include "ros/ros.h"
#include <geometry_msgs/Twist.h>
#include <geometry_msgs/PoseStamped.h>
#include <nav_msgs/Path.h>

class XYVector {
public:
  double x;
  double y;

  XYVector() {
    x = 0.0;
    y = 0.0;
  }

  XYVector(double x, double y) {
    this->x = x;
    this->y = y;
  }

  XYVector operator+(const XYVector &other) {
    return XYVector(this->x + other.x, this->y + other.y);
  }

  XYVector operator-(const XYVector &other) {
    return XYVector(this->x - other.x, this->y - other.y);
  }

  XYVector operator*(double s) {
    return XYVector{this->x * s, this->y * s};
  }
};

XYVector curr_pos;
double curr_ang;
void cbPose(const geometry_msgs::PoseStamped::ConstPtr &msg) {
  auto &p = msg->pose.position;
  curr_pos.x = p.x;
  curr_pos.y = p.y;

  auto &q = msg->pose.orientation;
  double siny_cosp = 2 * (q.w * q.z + q.x * q.y);
  double cosy_cosp = 1 - 2 * (q.y * q.y + q.z * q.z);
  curr_ang = atan2(siny_cosp, cosy_cosp);
}

double dist_euc(XYVector p1, XYVector p2) {
  double dx = p1.x - p2.x;
  double dy = p1.y - p2.y;
  return sqrt(dx * dx + dy * dy);
}

double heading(XYVector src, XYVector tgt) {
  double dx = tgt.x - src.x;
  double dy = tgt.y - src.y;
  return atan2(dy, dx);
}

double limit_angle(double angle) {
  // limits from -PI to PI
  double result = fmod(angle + M_PI, M_PI * 2);
  return result >= 0 ? result - M_PI : result + M_PI;
}

int main(int argc, char **argv) {
  ros::init(argc, argv, "path_tracker");
  ros::NodeHandle nh;
  ros::Rate loop_rate(10);

  double close_enough;
  if (!nh.param("close_enough", close_enough, 0.1)) {
    ROS_WARN("PATH_TRACKER: Param close_enough not found, set to 0.1");
  }
  double Kp_lin;
  if (!nh.param("Kp_lin", Kp_lin, 1.0)) {
    ROS_WARN("PATH_TRACKER: Param Kp_lin not found, set to 1.0");
  }
  double Kd_lin;
  if (!nh.param("Kd_lin", Kd_lin, 0.0)) {
    ROS_WARN("PATH_TRACKER: Param Kd_lin not found, set to 0.0");
  }  
  double Kp_ang;
  if (!nh.param("Kp_ang", Kp_ang, 1.0)) {
    ROS_WARN("PATH_TRACKER: Param Kp_ang not found, set to 1.0");
  }
  double Kd_ang;
  if (!nh.param("Kd_ang", Kd_ang, 0.0)) {
    ROS_WARN("PATH_TRACKER: Param Kd_ang not found, set to 0.0");
  }

  ros::Subscriber pose_sub = nh.subscribe("pose", 1, &cbPose);
  ros::Publisher twist_pub = nh.advertise<geometry_msgs::Twist>("cmd_vel", 1, true);
  ros::Publisher path_pub = nh.advertise<nav_msgs::Path>("target_path", 1, true);

  // create path to track
  nav_msgs::Path nav_path;
  nav_path.header.frame_id = "world";
  geometry_msgs::PoseStamped path_pose;
  std::vector<XYVector> path;
  double dx = 0.1;
  for (int i = 0; i < 40; i++) {
    XYVector pt;
    pt.x = (dx * i);
    pt.y = 0.3 * sin(pt.x);
    path.push_back(pt);

    path_pose.pose.position.x = pt.x;
    path_pose.pose.position.y = pt.y;
    nav_path.poses.push_back(path_pose);
  }

  path_pub.publish(nav_path);

  geometry_msgs::Twist twist_rbt;

  int idx = 0;
  int total_pts = path.size();

  double dt;
  double prev_time = ros::Time::now().toSec();
  double pos_err = 0;
  double ang_err = 0;
  double pos_err_prev = 0;
  double ang_err_prev = 0;
  double D_lin = 0;
  double D_ang = 0;

  while (ros::ok()) {
    if (path.empty()) {
      ROS_WARN("PATH_TRACKER: Path to track is empty");
      return 0;
    }

    if (dist_euc(curr_pos, path[idx]) < close_enough) {
      idx++;
      if (idx == total_pts) {
        twist_rbt.linear.x = 0.0;
        twist_rbt.angular.z = 0.0;
        twist_pub.publish(twist_rbt);
        ROS_INFO("PATH_TRACKER: Path completed");
        return 0;
      }
    }

    dt = ros::Time::now().toSec() - prev_time;
    if (dt == 0) { // ros doesn't tick the time fast enough
      continue;
    } 
    prev_time += dt;

    XYVector target = path[idx];
    XYVector vec_to_target = target - curr_pos;
    XYVector heading_to_target{cos(curr_ang), sin(curr_ang)};
    pos_err = vec_to_target.x * heading_to_target.x + vec_to_target.y * heading_to_target.y;
    ang_err = limit_angle(heading(curr_pos, target) - curr_ang); 

    D_lin = (pos_err - pos_err_prev) / dt;
    D_ang = (ang_err - ang_err_prev) / dt;

    // PD controller
    twist_rbt.linear.x = Kp_lin * pos_err + Kd_lin * D_lin;
    twist_rbt.angular.z = Kp_ang * ang_err + Kd_ang * D_ang;

    twist_pub.publish(twist_rbt);

    // update topics
    ros::spinOnce();

    loop_rate.sleep();
  }

  return 0;
}
