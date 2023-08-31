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

#include <string>
#include <limits>
#include <memory>
#include <vector>
#include <mutex>
#include <algorithm>

#include "nav2_route/route_planner.hpp"

namespace nav2_route
{

void RoutePlanner::configure(nav2_util::LifecycleNode::SharedPtr node)
{
  nav2_util::declare_parameter_if_not_declared(
    node, "max_iterations", rclcpp::ParameterValue(0));
  max_iterations_ = node->get_parameter("max_iterations").as_int();

  if (max_iterations_ == 0) {
    max_iterations_ = std::numeric_limits<int>::max();
  }

  edge_scorer_ = std::make_unique<EdgeScorer>(node);
}

Route RoutePlanner::findRoute(
  Graph & graph, unsigned int start, unsigned int goal,
  const std::vector<unsigned int> & blocked_ids)
{
  if (graph.empty()) {
    throw nav2_core::NoValidGraph("Graph is invalid for routing!");
  }

  // Find the start and goal pointers, it is important in this function
  // that these are the actual pointers, so that copied addresses are
  // not lost in the route when this function goes out of scope.
  const NodePtr & start_node = &graph.at(start);
  const NodePtr & goal_node = &graph.at(goal);
  findShortestGraphTraversal(graph, start_node, goal_node, blocked_ids);

  EdgePtr & parent_edge = goal_node->search_state.parent_edge;
  if (!parent_edge) {
    throw nav2_core::NoValidRouteCouldBeFound("Could not find a route to the requested goal!");
  }

  // Convert graph traversal into a meaningful route (backtracking)
  Route route;
  while (parent_edge) {
    route.edges.push_back(parent_edge);
    parent_edge = parent_edge->start->search_state.parent_edge;
  }

  std::reverse(route.edges.begin(), route.edges.end());
  route.start_node = start_node;
  route.route_cost = goal_node->search_state.integrated_cost;
  return route;
}

void RoutePlanner::resetSearchStates(Graph & graph)
{
  // For graphs < 75,000 nodes, iterating through one time on initialization to reset the state
  // is neglibably different to allocating & deallocating the complimentary blocks of memory
  for (unsigned int i = 0; i != graph.size(); i++) {
    graph[i].search_state.reset();
  }
}

void RoutePlanner::findShortestGraphTraversal(
  Graph & graph, const NodePtr start, const NodePtr goal,
  const std::vector<unsigned int> & blocked_ids)
{
  // Setup the Dijkstra's search
  resetSearchStates(graph);
  goal_id_ = goal->nodeid;
  start->search_state.integrated_cost = 0.0;
  addNode(0.0, start);

  NodePtr neighbor{nullptr};
  EdgePtr edge{nullptr};
  float potential_cost = 0.0, traversal_cost = 0.0;
  int iterations = 0;
  while (!queue_.empty() && iterations < max_iterations_) {
    iterations++;

    // Get the next lowest cost node
    auto [curr_cost, node] = getNextNode();

    // This has been visited, thus already lowest cost
    if (curr_cost != node->search_state.integrated_cost) {
      continue;
    }

    // We have the shortest path
    if (isGoal(node)) {
      break;
    }

    // Expand to connected nodes
    EdgeVector & edges = getEdges(node);
    for (unsigned int edge_num = 0; edge_num != edges.size(); edge_num++) {
      edge = &edges[edge_num];
      neighbor = edge->end; // neighboring "node" of the edge

      // If edge is invalid (lane closed, occupied, etc), don't expand
      if (!getTraversalCost(edge, traversal_cost, blocked_ids)) {
        continue;
      }

      // Update Neighbor's Cost If the new potential cost (curr_cost + traversal_cost) is Lower
      //    curr_cost: the cost to reach the current node; 
      //    traversal_cost: the cost to traverse the edge to the neighbor "node"
      potential_cost = curr_cost + traversal_cost;
      if (potential_cost < neighbor->search_state.integrated_cost) {
        neighbor->search_state.parent_edge = edge;  // for backtracking to find the actual path
        neighbor->search_state.integrated_cost = potential_cost;
        neighbor->search_state.traversal_cost = traversal_cost;
        addNode(potential_cost, neighbor);  // Add the neighbor node to the priority queue (Nodes with lower costs will be processed before nodes with higher costs.)
      }
    }
  }

  // Reset states
  clearQueue();

  if (iterations >= max_iterations_) {
    throw nav2_core::TimedOut("Maximum iterations was exceeded!");
  }
}

bool RoutePlanner::getTraversalCost(
  const EdgePtr edge, float & score, const std::vector<unsigned int> & blocked_ids)
{
  // If edge or node is in the blocked list, as long as its not blocking the goal itself
  auto idBlocked = [&](unsigned int id) {return id == edge->edgeid || id == edge->end->nodeid;};
  auto is_blocked = std::find_if(blocked_ids.begin(), blocked_ids.end(), idBlocked);
  if (is_blocked != blocked_ids.end() && !isGoal(edge->end)) {
    return false;
  }

  if (!edge->edge_cost.overridable || edge_scorer_->numPlugins() == 0) {
    if (edge->edge_cost.cost == 0.0) {
      throw nav2_core::NoValidGraph(
              "Edge " + std::to_string(edge->edgeid) +
              " doesn't contain and cannot compute a valid edge cost!");
    }
    score = edge->edge_cost.cost;
    return true;
  }

  return edge_scorer_->score(edge, score);
}

NodeElement RoutePlanner::getNextNode()
{
  NodeElement data = queue_.top();
  queue_.pop();
  return data;
}

void RoutePlanner::addNode(const float cost, const NodePtr node)
{
  queue_.emplace(cost, node);
}

EdgeVector & RoutePlanner::getEdges(const NodePtr node)
{
  return node->neighbors;
}

void RoutePlanner::clearQueue()
{
  NodeQueue q;
  std::swap(queue_, q);
}

bool RoutePlanner::isGoal(const NodePtr node)
{
  return node->nodeid == goal_id_;
}

}  // namespace nav2_route
