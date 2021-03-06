//
// Created by vlad on 05.02.2021.
//
#include <ros/ros.h>
#include <ros/console.h>

#include <tf2_msgs/TFMessage.h>
#include <tf2_ros/transform_listener.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <cds_msgs/PathStamped.h>
#include <geometry_msgs/PoseArray.h>
#include <nav_msgs/Odometry.h>
#include "visualization_msgs/Marker.h"

#include "planner_cds_core/mission.h"

#include <chrono>

double yaw(geometry_msgs::Quaternion q){
    double siny_cosp = 2 * (q.w * q.z + q.x * q.y);
    double cosy_cosp = 1 - 2 * (q.y * q.y + q.z * q.z);
    return std::atan2(siny_cosp, cosy_cosp);
}

template <typename T> int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}


class Planner{
private:
    ros::Subscriber     taskSub;
    ros::Subscriber     gridSub;
    ros::Subscriber     odomSub;
    ros::Publisher      trajPub;
    ros::Publisher      visTrajPub;

    tf2_ros::Buffer&                tfBuffer;
    nav_msgs::OccupancyGrid         grid;
    nav_msgs::Odometry              odom;
    visualization_msgs::Marker      vis_path;

    Map         map;
    Astar       search = Astar(1,1);
    ISearch*    search1;
//    Mission mission;

    std::string odomTopic;
    std::string taskTopic;
    std::string gridTopic;
    std::string pathTopic;
    std::string pathTopicVis;

    std::string searchType;

    bool gridSet;
//    cds_msgs::PathStamped path;
    geometry_msgs::PoseArray path;
public:
    Planner(tf2_ros::Buffer& _tfBuffer);
    void setTask(const geometry_msgs::PoseStamped::ConstPtr& goalMsg);
    void setGrid(const nav_msgs::OccupancyGrid::ConstPtr& gridMsg);
    void setOdom(const nav_msgs::Odometry::ConstPtr& odomMsg);

    bool plan();
    void fillPath(std::list<Node> lppath);
    void fillPathVis();
    void transformPath();
    void publish();
    geometry_msgs::Pose transformPoseToTargetFrame(geometry_msgs::Pose poseIn, std::string poseFrame, std::string targetFrame);
    geometry_msgs::Pose rescaleToGrid(geometry_msgs::Pose Pose);
    geometry_msgs::Pose rescaleFromGrid(geometry_msgs::Pose Pose);

};

Planner::Planner(tf2_ros::Buffer& _tfBuffer): tfBuffer(_tfBuffer){
    ros::NodeHandle nh;


    nh.param<std::string>("/node_params/odom_topic", odomTopic, "some_odom");
    nh.param<std::string>("/node_params/task_topic", taskTopic, "some_task");
    nh.param<std::string>("/node_params/grid_topic", gridTopic, "some_grid");
    nh.param<std::string>("/node_params/path_topic", pathTopic, "some_path");
    nh.param<std::string>("/node_params/path_topic_vis", pathTopicVis, "some_path");

    nh.param<std::string>("/node_params/search_type", searchType, "dijkstra");


    vis_path.ns = "planning";
    vis_path.id = 0;
    vis_path.type = 4;
    vis_path.action = 0;
    vis_path.pose.position.x = 0;
    vis_path.pose.position.y = 0;
    vis_path.pose.position.z = 0;
    vis_path.pose.orientation.x = 0;
    vis_path.pose.orientation.y = 0;
    vis_path.pose.orientation.z = 0;
    vis_path.pose.orientation.w = 1;
    vis_path.scale.x = 0.05;
    vis_path.scale.y = 0.05;
    vis_path.scale.z = 0;
    vis_path.color.r = 1;
    vis_path.color.g = 0;
    vis_path.color.b = 0;
    vis_path.color.a = 1;
    vis_path.lifetime.sec = 0;
    vis_path.lifetime.nsec = 0;
    vis_path.frame_locked = false;
    vis_path.colors.clear();
    vis_path.text = "";
    vis_path.mesh_resource = "";
    vis_path.mesh_use_embedded_materials = false;

    taskSub                     = nh.subscribe<geometry_msgs::PoseStamped>      (taskTopic,
                                                                                50,
                                                                                &Planner::setTask,
                                                                                this);

    gridSub                     = nh.subscribe<nav_msgs::OccupancyGrid>         (gridTopic,
                                                                                50,
                                                                                &Planner::setGrid,
                                                                                this);

    odomSub                     = nh.subscribe<nav_msgs::Odometry>              (odomTopic,
                                                                                50,
                                                                                &Planner::setOdom,
                                                                                this);

//    trajPub                     = nh.advertise<cds_msgs::PathStamped>           (pathTopic,
//                                                                                50);

    trajPub                     = nh.advertise<geometry_msgs::PoseArray>        (pathTopic,
                                                                                50);

    visTrajPub                  = nh.advertise<visualization_msgs::Marker>      (pathTopicVis,
                                                                                 50);

//    if (search1)
//        delete search1;
//    if (searchType == "bfs")
//        search1 = new BFS();
//    else if (searchType  == "dijkstra")
//        search1 = new Dijkstra();
//    else if (searchType  == "astar")
//        search1 = new Astar(1, 1);
//    else if (searchType  == "jp_search")
//        search1 = new JP_Search(1, 1);
//    else if (searchType  == "theta")
//        search1 = new Theta(1, 1);
}

void Planner::setTask(const geometry_msgs::PoseStamped::ConstPtr& goalMsg) {
    if(gridSet){
        auto transformedGoal = rescaleToGrid(transformPoseToTargetFrame(goalMsg->pose, goalMsg->header.frame_id, grid.header.frame_id));
        ROS_INFO_STREAM("Transformed goal is:" << rescaleFromGrid(transformedGoal));
        map.setGoal(int(transformedGoal.position.y),
                        int(transformedGoal.position.x));
        if(!plan()) ROS_WARN_STREAM("Planning error! Resulted path is empty.");
        publish();
    }else{
        ROS_WARN_STREAM("No grid received! Cannot set task.");
    }
}

void Planner::setGrid(const nav_msgs::OccupancyGrid::ConstPtr& gridMsg) {
    this->grid = *gridMsg;
    if(map.getMap(gridMsg)) {
        gridSet = true;
    }else {
        ROS_WARN_STREAM("Cannot set map!");
        gridSet = false;
    }
}

void Planner::setOdom(const nav_msgs::Odometry::ConstPtr& odomMsg) {
    this->odom = *odomMsg;
    if(gridSet){
        auto transformedOdom = rescaleToGrid(transformPoseToTargetFrame(odomMsg->pose.pose, odomMsg->header.frame_id, grid.header.frame_id));
        ROS_INFO_STREAM("Transformed odom is:" << rescaleFromGrid(transformedOdom));
        map.setStart(int(transformedOdom.position.y),
                        int(transformedOdom.position.x));
    }else{
        ROS_WARN_STREAM("No grid received! Cannot set odometry.");
    }
}

bool Planner::plan() {
    XmlLogger *logger = new XmlLogger("nope");
    const EnvironmentOptions options;

    if((map.goal_i > map.width) || (map.goal_j > map.height) || (map.goal_i * map.goal_j < 0)){
        ROS_WARN_STREAM("Wrong goal coordinates! Interrupting");
        return false;
    }

    ROS_INFO_STREAM(map.start_i << "  " << map.start_j);
    ROS_INFO_STREAM(map.goal_i << "  " << map.goal_j);
    auto searchRes = search.startSearch(logger, map, options);
    std::cout << "Planning time: " << searchRes.time << std::endl;

    const std::list<Node>* lppath = searchRes.lppath;
    if (lppath->size() != 0){
        fillPath(*lppath);
        transformPath();
        fillPathVis();
        return true;
    }
    else{
        return false;
    }
}

void Planner::fillPath(std::list<Node> lppath){
    path.poses.clear();
    path.header.frame_id = grid.header.frame_id;
    geometry_msgs::Pose point;
    for (auto node : lppath){
        point.position.x = node.j;
        point.position.y = node.i;
        path.poses.push_back(point);
    }
}
void Planner::fillPathVis(){
    vis_path.points.clear();
    vis_path.header.frame_id = path.header.frame_id;
    for (auto pose : path.poses){
        geometry_msgs::Point point;
        point = pose.position;
        vis_path.points.push_back(point);
    }
}

void Planner::transformPath(){
    geometry_msgs::TransformStamped transform;
    try {
        transform = tfBuffer.lookupTransform(path.header.frame_id, odom.header.frame_id, ros::Time(0));
    }catch (tf2::TransformException &ex) {
        ROS_WARN("%s", ex.what());
        //return poseIn;
    }
    auto angle = yaw(transform.transform.rotation);
    double angle_orig;
    geometry_msgs::Pose point;
    for(int i=0; i<path.poses.size(); ++i) {
        point = path.poses[i];
        point = rescaleFromGrid(point);
        point = transformPoseToTargetFrame(point, path.header.frame_id, odom.header.frame_id);


        path.poses[i] = point;
    }


    path.header.frame_id = odom.header.frame_id;
}

void Planner::publish(){
    path.header.stamp = ros::Time::now();
    trajPub.publish(path);
    visTrajPub.publish(vis_path);
}

geometry_msgs::Pose Planner::transformPoseToTargetFrame(geometry_msgs::Pose poseIn, std::string poseFrame, std::string targetFrame){
    geometry_msgs::TransformStamped transform;
    try {
        transform = tfBuffer.lookupTransform(poseFrame, targetFrame, ros::Time(0));
    }catch (tf2::TransformException &ex) {
        ROS_WARN("%s", ex.what());
        return poseIn;
    }

    geometry_msgs::Pose poseOut;
    poseOut = poseIn;
    poseOut.position.x -= transform.transform.translation.x;
    poseOut.position.y -= transform.transform.translation.y;
    auto angle = yaw(transform.transform.rotation);
    auto new_x = poseOut.position.x*cos(angle) + poseOut.position.y*sin(angle);
    auto new_y = -poseOut.position.x*sin(angle) + poseOut.position.y*cos(angle);
    poseOut.position.x = new_x;
    poseOut.position.y = new_y;


    auto angle_orig = yaw(poseIn.orientation);
    angle_orig -= angle;
    poseOut.orientation.x = 0;
    poseOut.orientation.y = 0;
    poseOut.orientation.z = sin(angle_orig/2);
    poseOut.orientation.w = cos(angle_orig/2);
//
//    tf2::Quaternion q_orig, q_rot, q_new;
//    tf2::convert(poseOut.orientation , q_orig);
//    tf2::convert(transform.transform.rotation, q_rot);
//    q_new = q_rot*q_orig;
//    q_new.normalize();
//    tf2::convert(q_new, poseOut.orientation);

    return poseOut;
}

geometry_msgs::Pose Planner::rescaleToGrid(geometry_msgs::Pose Pose){

    Pose.position.x -= grid.info.origin.position.x;
    Pose.position.y -= grid.info.origin.position.y;
    Pose.position.x = int(Pose.position.x / grid.info.resolution);
    Pose.position.y = int(Pose.position.y / grid.info.resolution);
    return Pose;
}

geometry_msgs::Pose Planner::rescaleFromGrid(geometry_msgs::Pose Pose){
    Pose.position.x = (Pose.position.x) * grid.info.resolution;
    Pose.position.y = (Pose.position.y) * grid.info.resolution;
    Pose.position.x += grid.info.origin.position.x;
    Pose.position.y += grid.info.origin.position.y;
    return Pose;
}



int main(int argc, char **argv){
    ros::init(argc, argv, "planner");
    tf2_ros::Buffer tfBuffer(ros::Duration(5)); //todo: remake with pointers
    tf2_ros::TransformListener tfListener(tfBuffer);


    Planner AStar_Planner(tfBuffer);
    ros::Rate r(5);

    while(ros::ok()){
        ros::spinOnce();
        r.sleep();
    }
}
