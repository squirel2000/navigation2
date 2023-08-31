// Copyright (c) 2023, Samsung Research America
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <memory>
#include <string>

#include "nav2_route/plugins/edge_cost_functions/adjust_edges_scorer.hpp"

namespace nav2_route
{

void AdjustEdgesScorer::configure(
  const rclcpp_lifecycle::LifecycleNode::SharedPtr node,
  const std::string & name)
{
  RCLCPP_INFO(node->get_logger(), "Configuring adjust edges scorer.");
  name_ = name;
  logger_ = node->get_logger();
  service_ =
    node->create_service<nav2_msgs::srv::AdjustEdges>(
    std::string(node->get_name()) + "/" + getName() + "/adjust_edges", std::bind(
      &AdjustEdgesScorer::closedEdgesCb, this,
      std::placeholders::_1, std::placeholders::_2));

  dynamic_penalties_.clear();
  closed_edges_.clear();
}

void AdjustEdgesScorer::closedEdgesCb(
  const std::shared_ptr<nav2_msgs::srv::AdjustEdges::Request> request,
  std::shared_ptr<nav2_msgs::srv::AdjustEdges::Response> response)
{
  RCLCPP_INFO(logger_, "Edge closure and cost adjustment in progress!");

  // Add new closed edges
  for (unsigned int edge : request->closed_edges) {
    closed_edges_.insert(edge);
  }

  // Removed now opened edges, if stored
  for (unsigned int edge : request->opened_edges) {
    if (closed_edges_.find(edge) != closed_edges_.end()) {
      closed_edges_.erase(edge);
    }
  }

  // Add dynamic costs from application system for edges
  for (auto & edge : request->adjust_edges) {
    dynamic_penalties_[edge.edgeid] = edge.cost;
  }

  response->success = true;
}

bool AdjustEdgesScorer::score(const EdgePtr edge, float & cost)
{
  // Find if this edge is in the closed set of edges
  if (closed_edges_.find(edge->edgeid) != closed_edges_.end()) {
    return false;
  }

  // TODO(sm) does this critic make efficiency sense at a
  // reasonable sized graph? Maybe use robinhood stuff?
  const auto & dyn_pen = dynamic_penalties_.find(edge->edgeid);
  if (dyn_pen != dynamic_penalties_.end()) {
    cost = dyn_pen->second;
  }

  return true;
}

std::string AdjustEdgesScorer::getName()
{
  return name_;
}

}  // namespace nav2_route

#include "pluginlib/class_list_macros.hpp"
PLUGINLIB_EXPORT_CLASS(nav2_route::AdjustEdgesScorer, nav2_route::EdgeCostFunction)
