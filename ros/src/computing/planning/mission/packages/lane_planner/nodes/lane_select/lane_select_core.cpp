/*
 *  Copyright (c) 2015, Nagoya University

 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  * Neither the name of Autoware nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "lane_select_core.h"

namespace lane_planner
{
// Constructor
LaneSelectNode::LaneSelectNode()
  : private_nh_("~")
  , current_lane_idx_(-1)
  , right_lane_idx_(-1)
  , left_lane_idx_(-1)
  , current_change_flag_(ChangeFlag::unknown)
  , is_lane_array_subscribed_(false)
  , is_current_pose_subscribed_(false)
  , is_current_velocity_subscribed_(false)
  , is_current_state_subscribed_(false)
  , is_config_subscribed_(false)
  , distance_threshold_(3.0)
  , lane_change_interval_(10.0)
  , lane_change_target_ratio_(2.0)
  , lane_change_target_minimum_(5.0)
  , vlength_hermite_curve_(10)
  , current_state_("UNKNOWN")
  , previous_state_("UNKNOWN")
{
  initForROS();
}

// Destructor
LaneSelectNode::~LaneSelectNode()
{
}

void LaneSelectNode::initForROS()
{
  // setup subscriber
  sub1_ = nh_.subscribe("traffic_waypoints_array", 1, &LaneSelectNode::callbackFromLaneArray, this);
  sub2_ = nh_.subscribe("current_pose", 1, &LaneSelectNode::callbackFromPoseStamped, this);
  sub3_ = nh_.subscribe("current_velocity", 1, &LaneSelectNode::callbackFromTwistStamped, this);
  sub4_ = nh_.subscribe("state", 1, &LaneSelectNode::callbackFromState, this);
  sub5_ = nh_.subscribe("/config/lane_select", 1, &LaneSelectNode::callbackFromConfig, this);

  // setup publisher
  pub1_ = nh_.advertise<waypoint_follower::lane>("base_waypoints", 1,true);
  pub2_ = nh_.advertise<std_msgs::Int32>("closest_waypoint", 1);
  pub3_ = nh_.advertise<std_msgs::Int32>("change_flag", 1);
  vis_pub1_ = nh_.advertise<visualization_msgs::MarkerArray>("lane_select_marker", 1);

  // get from rosparam
  private_nh_.param<double>("lane_change_interval", lane_change_interval_, double(2));
  private_nh_.param<double>("distance_threshold", distance_threshold_, double(3.0));
}

bool LaneSelectNode::isAllTopicsSubscribed()
{
  if (!is_current_pose_subscribed_ || !is_lane_array_subscribed_ || !is_current_velocity_subscribed_)
  {
    ROS_WARN("Necessary topics are not subscribed yet. Waiting...");
    return false;
  }
  return true;
}

void LaneSelectNode::initForLaneSelect()
{
  if(!isAllTopicsSubscribed())
    return;

  // search closest waypoint number for each lane
  if (!getClosestWaypointNumberForEachLane())
  {
    publishClosestWaypoint(-1);
    resetLaneIdx();
    return;
  }

  findCurrentLane();
  findRightAndLeftLanes();

  publishAll(std::get<0>(tuple_vec_.at(current_lane_idx_)), std::get<1>(tuple_vec_.at(current_lane_idx_)),
             std::get<2>(tuple_vec_.at(current_lane_idx_)));
  current_change_flag_ = std::get<2>(tuple_vec_.at(current_lane_idx_));
  publishVisualizer();
  resetSubscriptionFlag();
  return;
}

void LaneSelectNode::resetLaneIdx()
{
  current_lane_idx_ = -1;
  right_lane_idx_ = -1;
  left_lane_idx_ = -1;
  publishVisualizer();
}

void LaneSelectNode::resetSubscriptionFlag()
{
  is_current_pose_subscribed_ = false;
  is_current_velocity_subscribed_ = false;
  is_current_state_subscribed_ = false;
}

void LaneSelectNode::processing()
{
  if(!isAllTopicsSubscribed())
    return;

  // search closest waypoint number for each lane
  if (!getClosestWaypointNumberForEachLane())
  {
    publishClosestWaypoint(-1);
    resetLaneIdx();
    return;
  }

  // if closest waypoint on current lane is -1,
  if (std::get<1>(tuple_vec_.at(current_lane_idx_)) == -1)
  {
    publishClosestWaypoint(-1);
    resetLaneIdx();
    return;
  }

  ROS_INFO("current_lane_idx: %d", current_lane_idx_);
  ROS_INFO("right_lane_idx: %d", right_lane_idx_);
  ROS_INFO("left_lane_idx: %d", left_lane_idx_);

  getCurrentChangeFlagForEachLane();
  decideActionFromState();
  publishVisualizer();
  resetSubscriptionFlag();
}

void LaneSelectNode::decideActionFromState()
{
  if (current_state_ == "LANE_CHANGE")
  {
    ROS_INFO("LANE_CHANGE...");

    std::get<1>(lane_for_change_) =
        getClosestWaypointNumber(std::get<0>(lane_for_change_), current_pose_.pose, current_velocity_.twist,
                                 std::get<1>(lane_for_change_), distance_threshold_);
    std::get<2>(lane_for_change_) = getCurrentChangeFlag(std::get<0>(lane_for_change_), std::get<1>(lane_for_change_));
    ROS_INFO("closest: %d", std::get<1>(lane_for_change_));

    if(previous_state_ == "MOVE_FORWARD")
      publishLane(std::get<0>(lane_for_change_));

    publishClosestWaypoint(std::get<1>(lane_for_change_));
    publishChangeFlag(std::get<2>(lane_for_change_));
    current_change_flag_ = std::get<2>(lane_for_change_);
  }
  else
  {
    ROS_INFO("MOVE_FORWARD...");

    if(previous_state_ == "LANE_CHANGE")
    {
      findCurrentLane();
      findRightAndLeftLanes();
      publishLane(std::get<0>(tuple_vec_.at(current_lane_idx_)));
    }

    createLaneForChange();
    publishClosestWaypoint(std::get<1>(tuple_vec_.at(current_lane_idx_)));
    publishChangeFlag(std::get<2>(tuple_vec_.at(current_lane_idx_)));
    current_change_flag_ = std::get<2>(tuple_vec_.at(current_lane_idx_));
  }
}

int32_t LaneSelectNode::getClosestLaneChangeWaypointNumber(const std::vector<waypoint_follower::waypoint> &wps, const int32_t &cl_wp)
{

  for (uint32_t i = cl_wp; i < wps.size(); i++)
  {
    if (static_cast<ChangeFlag>(wps.at(i).change_flag) == ChangeFlag::right ||
      static_cast<ChangeFlag>(wps.at(i).change_flag) == ChangeFlag::left)
    {
      return i;
    }
  }
  return -1;
}

int32_t LaneSelectNode::findWaypointAhead(const std::vector<waypoint_follower::waypoint> &wps, const uint32_t &start,
                                          const double &distance)
{
  for (uint32_t i = start; i < wps.size(); i++)
  {
    if (i == wps.size() - 1 ||
        distance < getTwoDimensionalDistance(wps.at(start).pose.pose.position, wps.at(i).pose.pose.position))
    {
      return i;
    }
  }
  return -1;
}

void LaneSelectNode::createLaneForChange()
{
  std::get<0>(lane_for_change_).waypoints.clear();
  std::get<0>(lane_for_change_).waypoints.shrink_to_fit();
  std::get<1>(lane_for_change_) = -1;

  const std::tuple<waypoint_follower::lane, int32_t, ChangeFlag> cur = tuple_vec_.at(current_lane_idx_);
  int32_t num_lane_change = getClosestLaneChangeWaypointNumber(std::get<0>(cur).waypoints, std::get<1>(cur));
  ROS_INFO("num_lane_change: %d", num_lane_change);
  int32_t offset =
      current_velocity_.twist.linear.x * 1 > 3 ? static_cast<int32_t>(current_velocity_.twist.linear.x * 1) : 3;
  if (num_lane_change < 0 ||
      num_lane_change + offset > static_cast<int32_t>(std::get<0>(cur).waypoints.size() - 1)
      || getTwoDimensionalDistance(std::get<0>(cur).waypoints.at(num_lane_change).pose.pose.position,
                                   current_pose_.pose.position) > 500)
  {
    ROS_WARN("current lane doesn't have valid right or left flag");
    return;
  }
  const ChangeFlag &flag = static_cast<ChangeFlag>(std::get<0>(cur).waypoints.at(num_lane_change).change_flag);
  if ((flag == ChangeFlag::right && right_lane_idx_ < 0) || (flag == ChangeFlag::left && left_lane_idx_ < 0))
  {
    ROS_WARN("current lane doesn't have the lane for lane change");
    return;
  }

  double dt = getTwoDimensionalDistance(std::get<0>(cur).waypoints.at(num_lane_change).pose.pose.position,
                                        std::get<0>(cur).waypoints.at(std::get<1>(cur)).pose.pose.position);
  double dt_by_vel = current_velocity_.twist.linear.x * lane_change_target_ratio_ > lane_change_target_minimum_
                         ? current_velocity_.twist.linear.x * lane_change_target_ratio_
                         : lane_change_target_minimum_;
  ROS_INFO("dt : %lf, dt_by_vel : %lf", dt, dt_by_vel);
  const std::tuple<waypoint_follower::lane, int32_t, ChangeFlag> &nghbr =
      static_cast<ChangeFlag>(std::get<0>(cur).waypoints.at(num_lane_change).change_flag) == ChangeFlag::right ?
          tuple_vec_.at(right_lane_idx_) :
          tuple_vec_.at(left_lane_idx_);

  int32_t target_num = findWaypointAhead(std::get<0>(nghbr).waypoints, std::get<1>(nghbr), dt + dt_by_vel);
  ROS_INFO("target_num : %d", target_num);
  if (target_num < 0 || target_num + lane_change_interval_ > std::get<0>(nghbr).waypoints.size() - 1)
    return;

  //create lane for lane change
  std::get<0>(lane_for_change_).header.stamp = std::get<0>(nghbr).header.stamp;

  std::copy(std::get<0>(cur).waypoints.begin(), std::get<0>(cur).waypoints.begin() + num_lane_change + offset,
            std::back_inserter(std::get<0>(lane_for_change_).waypoints));
  for(auto itr = std::get<0>(lane_for_change_).waypoints.rbegin(); itr != std::get<0>(lane_for_change_).waypoints.rbegin() + offset ;itr++)
    itr->change_flag = std::get<0>(cur).waypoints.at(num_lane_change).change_flag;

  std::vector<waypoint_follower::waypoint> hermite_wps = generateHermiteCurveForROS(
      std::get<0>(cur).waypoints.at(num_lane_change + offset).pose.pose, std::get<0>(nghbr).waypoints.at(target_num).pose.pose,
      std::get<0>(cur).waypoints.at(num_lane_change + offset).twist.twist.linear.x, vlength_hermite_curve_);
  for (auto &&el : hermite_wps)
    el.change_flag = std::get<0>(cur).waypoints.at(num_lane_change).change_flag;
  std::move(hermite_wps.begin(), hermite_wps.end(), std::back_inserter(std::get<0>(lane_for_change_).waypoints));

  for(auto i = target_num; i < static_cast<int32_t>(std::get<0>(nghbr).waypoints.size()); i++)
  {
    std::get<0>(lane_for_change_).waypoints.push_back(std::get<0>(nghbr).waypoints.at(i));
    if(i < target_num + lane_change_interval_)
      std::get<0>(lane_for_change_).waypoints.rbegin()->change_flag = std::get<0>(cur).waypoints.at(num_lane_change).change_flag;
    else
      std::get<0>(lane_for_change_).waypoints.rbegin()->change_flag = 0;
  }
}

ChangeFlag LaneSelectNode::getCurrentChangeFlag(const waypoint_follower::lane &lane, const int32_t &cl_wp)
{
  ChangeFlag flag;
  flag = (cl_wp != -1) ? static_cast<ChangeFlag>(lane.waypoints.at(cl_wp).change_flag) : ChangeFlag::unknown;

  if (flag == ChangeFlag::right && right_lane_idx_ == -1)
    flag = ChangeFlag::unknown;
  else if (flag == ChangeFlag::left && left_lane_idx_ == -1)
    flag = ChangeFlag::unknown;

  return flag;
}

void LaneSelectNode::getCurrentChangeFlagForEachLane()
{
  for (auto &el : tuple_vec_)
  {
    std::get<2>(el) = getCurrentChangeFlag(std::get<0>(el), std::get<1>(el));
    ROS_INFO("change_flag: %d", enumToInteger(std::get<2>(el)));
  }

  if(std::get<0>(lane_for_change_).waypoints.empty())
    std::get<2>(tuple_vec_.at(current_lane_idx_)) = ChangeFlag::straight;
}

bool LaneSelectNode::getClosestWaypointNumberForEachLane()
{
  for (auto &el : tuple_vec_)
  {
    std::get<1>(el) = getClosestWaypointNumber(std::get<0>(el), current_pose_.pose, current_velocity_.twist,
                                               std::get<1>(el), distance_threshold_);
    ROS_INFO("closest: %d", std::get<1>(el));
  }

  // confirm if all closest waypoint numbers are -1. If so, output warning
  int32_t accum = 0;
  for (const auto &el : tuple_vec_)
  {
    accum += std::get<1>(el);
  }
  if (accum == (-1) * static_cast<int32_t>(tuple_vec_.size()))
  {
    ROS_WARN("Cannot get closest waypoints. All closest waypoints are changed to -1...");
    return false;
  }

  return true;
}

void LaneSelectNode::findCurrentLane()
{
  std::vector<uint32_t> idx_vec;
  idx_vec.reserve(tuple_vec_.size());
  for (uint32_t i = 0; i < tuple_vec_.size(); i++)
  {
    if (std::get<1>(tuple_vec_.at(i)) == -1)
      continue;
    idx_vec.push_back(i);
  }
  current_lane_idx_ = findMostClosestLane(idx_vec, current_pose_.pose.position);
}

int32_t LaneSelectNode::findMostClosestLane(const std::vector<uint32_t> idx_vec, const geometry_msgs::Point p)
{
  std::vector<double> dist_vec;
  dist_vec.reserve(idx_vec.size());
  for (const auto &el : idx_vec)
  {
    int32_t closest_number = std::get<1>(tuple_vec_.at(el));
    dist_vec.push_back(
        getTwoDimensionalDistance(p, std::get<0>(tuple_vec_.at(el)).waypoints.at(closest_number).pose.pose.position));
  }
  std::vector<double>::iterator itr = std::min_element(dist_vec.begin(), dist_vec.end());
  return idx_vec.at(std::distance(dist_vec.begin(), itr));
}

int32_t LaneSelectNode::findNeighborLane(const std::string &str)
{
  const geometry_msgs::Pose &cur_clst_pose = std::get<0>(tuple_vec_.at(current_lane_idx_))
                                                 .waypoints.at(std::get<1>(tuple_vec_.at(current_lane_idx_)))
                                                 .pose.pose;

  std::vector<uint32_t> lane_idx_vec;
  lane_idx_vec.reserve(tuple_vec_.size());

  for (uint32_t i = 0; i < tuple_vec_.size(); i++)
  {
    if (i == static_cast<uint32_t>(current_lane_idx_) || std::get<1>(tuple_vec_.at(i)) == -1)
      continue;

    const geometry_msgs::Point &target_p =
        std::get<0>(tuple_vec_.at(i)).waypoints.at(std::get<1>(tuple_vec_.at(i))).pose.pose.position;
    geometry_msgs::Point converted_p = convertPointIntoRelativeCoordinate(target_p, cur_clst_pose);

    ROS_INFO("distance: %lf", converted_p.y);
    if (fabs(converted_p.y) > distance_threshold_)
    {
      ROS_INFO("%d lane is far from current lane...", i);
      continue;
    }

    if (converted_p.y > 0 && str == "left")
      lane_idx_vec.push_back(i);
    else if (converted_p.y < 0 && str == "right")
      lane_idx_vec.push_back(i);
  }

  if (!lane_idx_vec.empty())
    return findMostClosestLane(lane_idx_vec, cur_clst_pose.position);
  else
    return -1;
}

void LaneSelectNode::findRightAndLeftLanes()
{
  right_lane_idx_ = findNeighborLane("right");
  left_lane_idx_ = findNeighborLane("left");
}
visualization_msgs::Marker LaneSelectNode::createCurrentLaneMarker()
{
  visualization_msgs::Marker marker;
  marker.header.frame_id = "map";
  marker.header.stamp = ros::Time();
  marker.ns = "current_lane_marker";

  if (current_lane_idx_ == -1 || std::get<0>(tuple_vec_.at(current_lane_idx_)).waypoints.empty())
  {
    marker.action = visualization_msgs::Marker::DELETE;
    return marker;
  }

  marker.type = visualization_msgs::Marker::LINE_STRIP;
  marker.action = visualization_msgs::Marker::ADD;
  marker.scale.x = 0.1;

  std_msgs::ColorRGBA color_current;
  color_current.b = 1.0;
  color_current.g = 0.7;
  color_current.a = 1.0;

  std_msgs::ColorRGBA color_change;
  color_change.r = 0.5;
  color_change.b = 0.5;
  color_change.g = 0.5;
  color_change.a = 1.0;

  marker.color = current_state_ == "LANE_CHANGE" ? color_change : color_current;

  for(const auto &em : std::get<0>(tuple_vec_.at(current_lane_idx_)).waypoints)
    marker.points.push_back(em.pose.pose.position);

  return marker;
}

visualization_msgs::Marker LaneSelectNode::createRightLaneMarker()
{
  visualization_msgs::Marker marker;
  marker.header.frame_id = "map";
  marker.header.stamp = ros::Time();
  marker.ns = "right_lane_marker";

  if (right_lane_idx_ == -1 || std::get<0>(tuple_vec_.at(current_lane_idx_)).waypoints.empty())
  {
    marker.action = visualization_msgs::Marker::DELETE;
    return marker;
  }

  marker.type = visualization_msgs::Marker::LINE_STRIP;
  marker.action = visualization_msgs::Marker::ADD;
  marker.scale.x = 0.1;

  std_msgs::ColorRGBA color_neighbor;
  color_neighbor.r = 0.5;
  color_neighbor.b = 0.5;
  color_neighbor.g = 0.5;
  color_neighbor.a = 1.0;

  std_msgs::ColorRGBA color_neighbor_change;
  color_neighbor_change.b = 0.7;
  color_neighbor_change.g = 1.0;
  color_neighbor_change.a = 1.0;

  marker.color = current_change_flag_ == ChangeFlag::right ? color_neighbor_change : color_neighbor;

  for(const auto &em : std::get<0>(tuple_vec_.at(right_lane_idx_)).waypoints)
    marker.points.push_back(em.pose.pose.position);

  return marker;
}

visualization_msgs::Marker LaneSelectNode::createLeftLaneMarker()
{
  visualization_msgs::Marker marker;
  marker.header.frame_id = "map";
  marker.header.stamp = ros::Time();
  marker.ns = "left_lane_marker";

  if (left_lane_idx_ == -1 || std::get<0>(tuple_vec_.at(current_lane_idx_)).waypoints.empty())
  {
    marker.action = visualization_msgs::Marker::DELETE;
    return marker;
  }

  marker.type = visualization_msgs::Marker::LINE_STRIP;
  marker.action = visualization_msgs::Marker::ADD;
  marker.scale.x = 0.1;

  std_msgs::ColorRGBA color_neighbor;
  color_neighbor.r = 0.5;
  color_neighbor.b = 0.5;
  color_neighbor.g = 0.5;
  color_neighbor.a = 1.0;

  std_msgs::ColorRGBA color_neighbor_change;
  color_neighbor_change.b = 0.7;
  color_neighbor_change.g = 1.0;
  color_neighbor_change.a = 1.0;

  marker.color = current_change_flag_ == ChangeFlag::left ? color_neighbor_change : color_neighbor;

  for(const auto &em : std::get<0>(tuple_vec_.at((left_lane_idx_))).waypoints)
    marker.points.push_back(em.pose.pose.position);

  return marker;
}

visualization_msgs::Marker LaneSelectNode::createChangeLaneMarker()
{
  visualization_msgs::Marker marker;
  marker.header.frame_id = "map";
  marker.header.stamp = ros::Time();
  marker.ns = "change_lane_marker";

  if (std::get<0>(lane_for_change_).waypoints.empty())
  {
    marker.action = visualization_msgs::Marker::DELETE;
    return marker;
  }

  marker.type = visualization_msgs::Marker::LINE_STRIP;
  marker.action = visualization_msgs::Marker::ADD;
  marker.scale.x = 0.1;

  std_msgs::ColorRGBA color;
  color.r = 1.0;
  color.a = 0.7;

  std_msgs::ColorRGBA color_current;
  color_current.b = 1.0;
  color_current.g = 0.7;
  color_current.a = 1.0;

  marker.color = current_state_ == "LANE_CHANGE" ? color_current : color;
  for(const auto &em : std::get<0>(lane_for_change_).waypoints)
  {
    marker.points.push_back(em.pose.pose.position);
    marker.points.rbegin()->z-=0.1;
  }

  return marker;
}

visualization_msgs::Marker LaneSelectNode::createClosestWaypointsMarker()
{
  visualization_msgs::Marker marker;
  std_msgs::ColorRGBA color_closest_wp;
  color_closest_wp.r = 1.0;
  color_closest_wp.b = 1.0;
  color_closest_wp.g = 1.0;
  color_closest_wp.a = 1.0;

  marker.header.frame_id = "map";
  marker.header.stamp = ros::Time();
  marker.ns = "closest_waypoints_marker";
  marker.type = visualization_msgs::Marker::POINTS;
  marker.action = visualization_msgs::Marker::ADD;
  marker.scale.x = 0.5;
  marker.color = color_closest_wp;

  marker.points.reserve(tuple_vec_.size());
  for (uint32_t i = 0; i < tuple_vec_.size(); i++)
  {
    if (std::get<1>(tuple_vec_.at(i)) == -1)
      continue;

    marker.points.push_back(
        std::get<0>(tuple_vec_.at(i)).waypoints.at(std::get<1>(tuple_vec_.at(i))).pose.pose.position);
  }

  return marker;
}

void LaneSelectNode::publishVisualizer()
{
  visualization_msgs::MarkerArray marker_array;
  marker_array.markers.push_back(createChangeLaneMarker());
  marker_array.markers.push_back(createCurrentLaneMarker());
  marker_array.markers.push_back(createRightLaneMarker());
  marker_array.markers.push_back(createLeftLaneMarker());
  marker_array.markers.push_back(createClosestWaypointsMarker());

  vis_pub1_.publish(marker_array);
}

void LaneSelectNode::publishAll(const waypoint_follower::lane &lane, const int32_t clst_wp, const ChangeFlag flag)
{
  publishLane(lane);
  publishClosestWaypoint(clst_wp);
  publishChangeFlag(flag);
}

void LaneSelectNode::publishLane(const waypoint_follower::lane &lane)
{
  // publish global lane
  pub1_.publish(lane);
}

void LaneSelectNode::publishClosestWaypoint(const int32_t clst_wp)
{
  // publish closest waypoint
  std_msgs::Int32 closest_waypoint;
  closest_waypoint.data = clst_wp;
  pub2_.publish(closest_waypoint);
}

void LaneSelectNode::publishChangeFlag(const ChangeFlag flag)
{
  std_msgs::Int32 change_flag;
  change_flag.data = enumToInteger(flag);
  pub3_.publish(change_flag);
}

void LaneSelectNode::callbackFromLaneArray(const waypoint_follower::LaneArrayConstPtr &msg)
{
  tuple_vec_.clear();
  tuple_vec_.shrink_to_fit();
  tuple_vec_.reserve(msg->lanes.size());
  for (const auto &el : msg->lanes)
  {
    auto t = std::make_tuple(el, -1, ChangeFlag::straight);
    tuple_vec_.push_back(t);
  }

  resetLaneIdx();
  is_lane_array_subscribed_ = true;

  if(current_lane_idx_ == -1)
    initForLaneSelect();
  else
    processing();
}

void LaneSelectNode::callbackFromPoseStamped(const geometry_msgs::PoseStampedConstPtr &msg)
{
  current_pose_ = *msg;
  is_current_pose_subscribed_ = true;

  if(current_lane_idx_ == -1)
    initForLaneSelect();
  else
    processing();
}

void LaneSelectNode::callbackFromTwistStamped(const geometry_msgs::TwistStampedConstPtr &msg)
{
  current_velocity_ = *msg;
  is_current_velocity_subscribed_ = true;

  if(current_lane_idx_ == -1)
    initForLaneSelect();
  else
    processing();
}

void LaneSelectNode::callbackFromState(const std_msgs::StringConstPtr &msg)
{
  previous_state_ = current_state_;
  current_state_ = msg->data;
  is_current_state_subscribed_ = true;

  if(current_lane_idx_ == -1)
    initForLaneSelect();
  else
    processing();
}

void LaneSelectNode::callbackFromConfig(const runtime_manager::ConfigLaneSelectConstPtr &msg)
{
  distance_threshold_ = msg-> distance_threshold_neighbor_lanes;
  lane_change_interval_= msg->lane_change_interval;
    lane_change_target_ratio_ = msg->lane_change_target_ratio;
  lane_change_target_minimum_ = msg->lane_change_target_minimum;
    vlength_hermite_curve_= msg->vector_length_hermite_curve;
  is_config_subscribed_ = true;

  if(current_lane_idx_ == -1)
    initForLaneSelect();
  else
    processing();
}

void LaneSelectNode::run()
{
  ros::spin();
}

// distance between target 1 and target2 in 2-D
double getTwoDimensionalDistance(const geometry_msgs::Point &target1, const geometry_msgs::Point &target2)
{
  double distance = sqrt(pow(target1.x - target2.x, 2) + pow(target1.y - target2.y, 2));
  return distance;
}

geometry_msgs::Point convertPointIntoRelativeCoordinate(const geometry_msgs::Point &input_point, const geometry_msgs::Pose &pose)
{
  tf::Transform inverse;
  tf::poseMsgToTF(pose, inverse);
  tf::Transform transform = inverse.inverse();

  tf::Point p;
  pointMsgToTF(input_point, p);
  tf::Point tf_p = transform * p;
  geometry_msgs::Point tf_point_msg;
  pointTFToMsg(tf_p, tf_point_msg);
  return tf_point_msg;
}

geometry_msgs::Point convertPointIntoWorldCoordinate(const geometry_msgs::Point &input_point,
                                                                      const geometry_msgs::Pose &pose)
{
  tf::Transform inverse;
  tf::poseMsgToTF(pose, inverse);

  tf::Point p;
  pointMsgToTF(input_point, p);
  tf::Point tf_p = inverse * p;

  geometry_msgs::Point tf_point_msg;
  pointTFToMsg(tf_p, tf_point_msg);
  return tf_point_msg;
}

double getRelativeAngle(const geometry_msgs::Pose &waypoint_pose, const geometry_msgs::Pose &current_pose)
{
  tf::Vector3 x_axis(1, 0, 0);
  tf::Transform waypoint_tfpose;
  tf::poseMsgToTF(waypoint_pose, waypoint_tfpose);
  tf::Vector3 waypoint_v = waypoint_tfpose.getBasis() * x_axis;
  tf::Transform current_tfpose;
  tf::poseMsgToTF(current_pose, current_tfpose);
  tf::Vector3 current_v = current_tfpose.getBasis() * x_axis;

  return current_v.angle(waypoint_v) * 180 / M_PI;
}

// get closest waypoint from current pose
int32_t getClosestWaypointNumber(const waypoint_follower::lane &current_lane, const geometry_msgs::Pose &current_pose,
                                 const geometry_msgs::Twist &current_velocity, const int32_t previous_number,
                                 const double distance_threshold)
{
  if (current_lane.waypoints.empty())
    return -1;

  std::vector<uint32_t> idx_vec;
  // if previous number is -1, search closest waypoint from waypoints in front of current pose
  if (previous_number == -1)
  {
    idx_vec.reserve(current_lane.waypoints.size());
    for (uint32_t i = 0; i < current_lane.waypoints.size(); i++)
    {
      geometry_msgs::Point converted_p =
          convertPointIntoRelativeCoordinate(current_lane.waypoints.at(i).pose.pose.position, current_pose);
      double angle = getRelativeAngle(current_lane.waypoints.at(i).pose.pose, current_pose);
      if (converted_p.x > 0 && angle < 90)
        idx_vec.push_back(i);
    }
  }
  else
  {
    if (distance_threshold * 2 <
        getTwoDimensionalDistance(current_lane.waypoints.at(previous_number).pose.pose.position, current_pose.position))
    {
      ROS_WARN("current_pose is far away from previous closest waypoint. Initilized...");
      return -1;
    }

    double ratio = 3;
    double minimum_dt = 5.0;
    double dt = current_velocity.linear.x * ratio > minimum_dt ? current_velocity.linear.x * ratio : minimum_dt;

    auto range_max = static_cast<uint32_t>(previous_number + dt) < current_lane.waypoints.size()
                         ? static_cast<uint32_t>(previous_number + dt)
                         : current_lane.waypoints.size();
    for (uint32_t i = static_cast<uint32_t>(previous_number); i < range_max; i++)
    {
      geometry_msgs::Point converted_p =
          convertPointIntoRelativeCoordinate(current_lane.waypoints.at(i).pose.pose.position, current_pose);
      double angle = getRelativeAngle(current_lane.waypoints.at(i).pose.pose, current_pose);
      if (converted_p.x > 0 && angle < 90)
        idx_vec.push_back(i);
    }
  }

  if (idx_vec.empty())
    return -1;

  std::vector<double> dist_vec;
  dist_vec.reserve(idx_vec.size());
  for (const auto &el : idx_vec)
  {
    double dt = getTwoDimensionalDistance(current_pose.position, current_lane.waypoints.at(el).pose.pose.position);
    dist_vec.push_back(dt);
  }
  std::vector<double>::iterator itr = std::min_element(dist_vec.begin(), dist_vec.end());
  int32_t found_number = idx_vec.at(static_cast<uint32_t>(std::distance(dist_vec.begin(), itr)));
  return found_number;
}

// let the linear equation be "ax + by + c = 0"
// if there are two points (x1,y1) , (x2,y2), a = "y2-y1, b = "(-1) * x2 - x1" ,c = "(-1) * (y2-y1)x1 + (x2-x1)y1"
bool getLinearEquation(geometry_msgs::Point start, geometry_msgs::Point end, double *a, double *b, double *c)
{
  //(x1, y1) = (start.x, star.y), (x2, y2) = (end.x, end.y)
  double sub_x = fabs(start.x - end.x);
  double sub_y = fabs(start.y - end.y);
  double error = pow(10, -5);  // 0.00001

  if (sub_x < error && sub_y < error)
  {
    ROS_INFO("two points are the same point!!");
    return false;
  }

  *a = end.y - start.y;
  *b = (-1) * (end.x - start.x);
  *c = (-1) * (end.y - start.y) * start.x + (end.x - start.x) * start.y;

  return true;
}
double getDistanceBetweenLineAndPoint(geometry_msgs::Point point, double a, double b, double c)
{
  double d = fabs(a * point.x + b * point.y + c) / sqrt(pow(a, 2) + pow(b, 2));

  return d;
}

}  // lane_planner
