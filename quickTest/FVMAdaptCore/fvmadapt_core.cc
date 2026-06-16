//#############################################################################
//#
//# Copyright 2008-2026, Mississippi State University
//#
//# This file is part of the Loci Framework.
//#
//# The Loci Framework is free software: you can redistribute it and/or modify
//# it under the terms of the Lesser GNU General Public License as published by
//# the Free Software Foundation, either version 3 of the License, or
//# (at your option) any later version.
//#
//# The Loci Framework is distributed in the hope that it will be useful,
//# but WITHOUT ANY WARRANTY; without even the implied warranty of
//# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//# Lesser GNU General Public License for more details.
//#
//# You should have received a copy of the Lesser GNU General Public License
//# along with the Loci Framework.  If not, see <http://www.gnu.org/licenses>
//#
//#############################################################################

#include <FVMAdapt/diamondcell.h>
#include <FVMAdapt/hexcell.h>
#include <FVMAdapt/prism.h>

#include <cerrno>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <vector>

void extract_quad_edge(const std::vector<char>& facePlan,
                       std::vector<char>& edgePlan,
                       unsigned int dd);
std::vector<char> extract_prism_face(const std::vector<char>& cellPlan, int dd);
std::vector<char> transfer_plan_g2q(std::vector<char>& facePlan);
std::vector<char> transfer_plan_q2g(const std::vector<char>& facePlan);

namespace {

struct HexFixture {
  HexFixture(): root(0) {}

  ~HexFixture() {
    delete root;
    cleanup_list(nodes, edges, faces);
  }

  std::list<Node*> nodes;
  std::list<Edge*> edges;
  std::list<QuadFace*> faces;
  HexCell* root;
};

struct PrismFixture {
  PrismFixture(): root(0) {}

  ~PrismFixture() {
    delete root;
    cleanup_list(nodes, edges);
    cleanup_list(qfaces);
    cleanup_list(gfaces);
  }

  std::list<Node*> nodes;
  std::list<Edge*> edges;
  std::list<QuadFace*> qfaces;
  std::list<Face*> gfaces;
  Prism* root;
};

struct GeneralFixture {
  GeneralFixture(): root(0) {}

  ~GeneralFixture() {
    delete root;
    cleanup_list(nodes, edges, faces);
  }

  std::list<Node*> nodes;
  std::list<Edge*> edges;
  std::list<Face*> faces;
  Cell* root;
};

struct CaseResult {
  std::string shape;
  std::string case_name;
  std::vector<char> input_plan;
  std::vector<char> canonical_plan;
  int expected_leaves;
  int resplit_leaves;
  int sorted_leaves;
  int fine_cell_count;
  int point_count;
  std::string vtk_file;
};

struct PlanCase {
  PlanCase(const std::string& n,
           const std::vector<char>& p,
           const std::vector<char>& c,
           bool vtk): name(n), plan(p), canonical(c), write_vtk(vtk) {}

  std::string name;
  std::vector<char> plan;
  std::vector<char> canonical;
  bool write_vtk;
};

enum TraceKind {
  TRACE_HEX,
  TRACE_PRISM,
  TRACE_GENERAL
};

struct TraceState {
  int id;
  int parent_id;
  int child_slot;
  int depth;
  int local_fold;
  std::string path;
};

struct TraceRow {
  int id;
  int parent_id;
  int child_slot;
  int depth;
  int local_fold;
  std::string path;
  int plan_index;
  bool explicit_code;
  int code;
  int child_count;
  int first_child_id;
  int leaf_id;
};

struct PlanReplayTrace {
  std::vector<TraceRow> rows;
  int consumed_plan_entries;
  int leaf_count;
};

template<class Fixture>
Node* add_node(Fixture& fixture, double x, double y, double z) {
  vect3d p(x, y, z);
  Node* node = new Node(p, int32(fixture.nodes.size() + 1));
  fixture.nodes.push_back(node);
  return node;
}

template<class Fixture>
Edge* add_edge(Fixture& fixture, Node* head, Node* tail) {
  Edge* edge = new Edge(head, tail);
  fixture.edges.push_back(edge);
  return edge;
}

void build_unit_hex(HexFixture& fixture) {
  Node* node[8];
  node[0] = add_node(fixture, 0.0, 0.0, 0.0);
  node[1] = add_node(fixture, 0.0, 0.0, 1.0);
  node[2] = add_node(fixture, 0.0, 1.0, 0.0);
  node[3] = add_node(fixture, 0.0, 1.0, 1.0);
  node[4] = add_node(fixture, 1.0, 0.0, 0.0);
  node[5] = add_node(fixture, 1.0, 0.0, 1.0);
  node[6] = add_node(fixture, 1.0, 1.0, 0.0);
  node[7] = add_node(fixture, 1.0, 1.0, 1.0);

  const int edge_head[12] = {0, 1, 2, 3, 0, 1, 4, 5, 0, 2, 4, 6};
  const int edge_tail[12] = {4, 5, 6, 7, 2, 3, 6, 7, 1, 3, 5, 7};

  Edge* edge[12];
  for(int i = 0; i < 12; ++i) {
    edge[i] = add_edge(fixture, node[edge_head[i]], node[edge_tail[i]]);
  }

  const int face_to_edge[6][4] = {
    {6, 11, 7, 10},
    {4, 9, 5, 8},
    {2, 11, 3, 9},
    {0, 10, 1, 8},
    {1, 7, 3, 5},
    {0, 6, 2, 4}
  };

  QuadFace** face = new QuadFace*[6];
  for(int i = 0; i < 6; ++i) {
    face[i] = new QuadFace(4);
    fixture.faces.push_back(face[i]);
    for(int j = 0; j < 4; ++j) {
      face[i]->edge[j] = edge[face_to_edge[i][j]];
    }
  }

  fixture.root = new HexCell(face);
}

void build_unit_prism(PrismFixture& fixture) {
  Node* node[6];
  node[0] = add_node(fixture, 0.0, 0.0, 0.0);
  node[1] = add_node(fixture, 1.0, 0.0, 0.0);
  node[2] = add_node(fixture, 0.0, 1.0, 0.0);
  node[3] = add_node(fixture, 0.0, 0.0, 1.0);
  node[4] = add_node(fixture, 1.0, 0.0, 1.0);
  node[5] = add_node(fixture, 0.0, 1.0, 1.0);

  const int edge_head[9] = {0, 1, 2, 3, 4, 5, 0, 1, 2};
  const int edge_tail[9] = {1, 2, 0, 4, 5, 3, 3, 4, 5};

  Edge* edge[9];
  for(int i = 0; i < 9; ++i) {
    edge[i] = add_edge(fixture, node[edge_head[i]], node[edge_tail[i]]);
  }

  const int gface_to_edge[2][3] = {{0, 1, 2}, {3, 4, 5}};
  const int qface_to_edge[3][4] = {{0, 7, 3, 6}, {1, 8, 4, 7}, {2, 6, 5, 8}};

  Prism* prism = new Prism(3);
  for(int i = 0; i < 2; ++i) {
    Face* face = new Face(3);
    fixture.gfaces.push_back(face);
    for(int j = 0; j < 3; ++j) {
      face->edge[j] = edge[gface_to_edge[i][j]];
      face->needReverse[j] = false;
    }
    prism->setFace(i, face);
  }

  for(int i = 0; i < 3; ++i) {
    QuadFace* face = new QuadFace(4);
    fixture.qfaces.push_back(face);
    for(int j = 0; j < 4; ++j) {
      face->edge[j] = edge[qface_to_edge[i][j]];
    }
    prism->setFace(i, face);
  }

  fixture.root = prism;
}

Face* add_general_face(GeneralFixture& fixture,
                       const std::vector<Edge*>& edges,
                       const std::vector<bool>& need_reverse) {
  Face* face = new Face(int(edges.size()));
  fixture.faces.push_back(face);
  for(size_t i = 0; i < edges.size(); ++i) {
    face->edge[i] = edges[i];
    face->needReverse[i] = need_reverse[i];
  }
  return face;
}

void build_tetra_cell(GeneralFixture& fixture) {
  Node* node[4];
  node[0] = add_node(fixture, 0.0, 0.0, 0.0);
  node[1] = add_node(fixture, 1.0, 0.0, 0.0);
  node[2] = add_node(fixture, 0.0, 1.0, 0.0);
  node[3] = add_node(fixture, 0.0, 0.0, 1.0);

  Edge* edge[6];
  edge[0] = add_edge(fixture, node[0], node[1]);
  edge[1] = add_edge(fixture, node[1], node[2]);
  edge[2] = add_edge(fixture, node[2], node[0]);
  edge[3] = add_edge(fixture, node[0], node[3]);
  edge[4] = add_edge(fixture, node[1], node[3]);
  edge[5] = add_edge(fixture, node[2], node[3]);

  Face** face = new Face*[4];
  face[0] = add_general_face(fixture, {edge[0], edge[1], edge[2]},
                             {false, false, false});
  face[1] = add_general_face(fixture, {edge[0], edge[4], edge[3]},
                             {false, false, true});
  face[2] = add_general_face(fixture, {edge[1], edge[5], edge[4]},
                             {false, false, true});
  face[3] = add_general_face(fixture, {edge[2], edge[3], edge[5]},
                             {false, false, true});

  Node** cell_nodes = new Node*[4];
  for(int i = 0; i < 4; ++i) {
    cell_nodes[i] = node[i];
  }

  Edge** cell_edges = new Edge*[6];
  for(int i = 0; i < 6; ++i) {
    cell_edges[i] = edge[i];
  }

  char* face_orient = new char[4];
  for(int i = 0; i < 4; ++i) {
    face_orient[i] = 0;
  }

  fixture.root = new Cell(4, 6, 4, cell_nodes, cell_edges, face, face_orient);
}

void build_pyramid_cell(GeneralFixture& fixture) {
  Node* node[5];
  node[0] = add_node(fixture, 0.0, 0.0, 0.0);
  node[1] = add_node(fixture, 1.0, 0.0, 0.0);
  node[2] = add_node(fixture, 1.0, 1.0, 0.0);
  node[3] = add_node(fixture, 0.0, 1.0, 0.0);
  node[4] = add_node(fixture, 0.5, 0.5, 1.0);

  Edge* edge[8];
  edge[0] = add_edge(fixture, node[0], node[1]);
  edge[1] = add_edge(fixture, node[1], node[2]);
  edge[2] = add_edge(fixture, node[2], node[3]);
  edge[3] = add_edge(fixture, node[3], node[0]);
  edge[4] = add_edge(fixture, node[0], node[4]);
  edge[5] = add_edge(fixture, node[1], node[4]);
  edge[6] = add_edge(fixture, node[2], node[4]);
  edge[7] = add_edge(fixture, node[3], node[4]);

  Face** face = new Face*[5];
  face[0] = add_general_face(fixture, {edge[0], edge[1], edge[2], edge[3]},
                             {false, false, false, false});
  face[1] = add_general_face(fixture, {edge[0], edge[5], edge[4]},
                             {false, false, true});
  face[2] = add_general_face(fixture, {edge[1], edge[6], edge[5]},
                             {false, false, true});
  face[3] = add_general_face(fixture, {edge[2], edge[7], edge[6]},
                             {false, false, true});
  face[4] = add_general_face(fixture, {edge[3], edge[4], edge[7]},
                             {false, false, true});

  Node** cell_nodes = new Node*[5];
  for(int i = 0; i < 5; ++i) {
    cell_nodes[i] = node[i];
  }

  Edge** cell_edges = new Edge*[8];
  for(int i = 0; i < 8; ++i) {
    cell_edges[i] = edge[i];
  }

  char* face_orient = new char[5];
  for(int i = 0; i < 5; ++i) {
    face_orient[i] = 0;
  }

  fixture.root = new Cell(5, 8, 5, cell_nodes, cell_edges, face, face_orient);
}

typedef void (*GeneralBuilder)(GeneralFixture&);

std::vector<char> root_plan(int split_code) {
  return std::vector<char>(1, char(split_code));
}

std::vector<char> canonical_root_plan(int split_code) {
  if(split_code == 0) {
    return std::vector<char>();
  }
  return root_plan(split_code);
}

std::string plan_string(const std::vector<char>& plan) {
  std::ostringstream out;
  out << "[";
  for(size_t i = 0; i < plan.size(); ++i) {
    if(i != 0) {
      out << ",";
    }
    out << int(plan[i]);
  }
  out << "]";
  return out.str();
}

std::string int_vector_string(const std::vector<int32>& values) {
  std::ostringstream out;
  out << "[";
  for(size_t i = 0; i < values.size(); ++i) {
    if(i != 0) {
      out << ",";
    }
    out << values[i];
  }
  out << "]";
  return out.str();
}

std::string plan_tail_string(const std::vector<char>& plan, int start) {
  if(start >= int(plan.size())) {
    return "[]";
  }
  std::vector<char> tail;
  for(size_t i = size_t(start); i < plan.size(); ++i) {
    tail.push_back(plan[i]);
  }
  return plan_string(tail);
}

std::string plan_file_token(const std::string& shape, const std::string& name) {
  return shape + "_" + name;
}

int hex_trace_child_count(int code) {
  switch(code) {
  case 0:
    return 0;
  case 1:
  case 2:
  case 4:
    return 2;
  case 3:
  case 5:
  case 6:
    return 4;
  case 7:
    return 8;
  default:
    break;
  }

  std::ostringstream msg;
  msg << "unsupported hex plan code " << code;
  throw std::runtime_error(msg.str());
}

int prism_trace_child_count(int code, int nfold) {
  switch(code) {
  case 0:
    return 0;
  case 1:
    return 2;
  case 2:
    return nfold;
  case 3:
    return 2*nfold;
  default:
    break;
  }

  std::ostringstream msg;
  msg << "unsupported prism plan code " << code;
  throw std::runtime_error(msg.str());
}

int general_trace_child_count(int code,
                              int local_fold,
                              int depth,
                              const std::vector<int>& root_child_folds) {
  switch(code) {
  case 0:
    return 0;
  case 1:
    return depth == 0 ? int(root_child_folds.size()) : 2*local_fold + 2;
  default:
    break;
  }

  std::ostringstream msg;
  msg << "unsupported general-cell plan code " << code;
  throw std::runtime_error(msg.str());
}

int trace_child_count(TraceKind kind,
                      int code,
                      int local_fold,
                      int depth,
                      const std::vector<int>& root_child_folds) {
  switch(kind) {
  case TRACE_HEX:
    return hex_trace_child_count(code);
  case TRACE_PRISM:
    return prism_trace_child_count(code, local_fold);
  case TRACE_GENERAL:
    return general_trace_child_count(code, local_fold, depth, root_child_folds);
  }
  throw std::runtime_error("unsupported trace kind");
}

std::vector<int> trace_child_folds(TraceKind kind,
                                   int code,
                                   int local_fold,
                                   int depth,
                                   const std::vector<int>& root_child_folds) {
  const int count =
    trace_child_count(kind, code, local_fold, depth, root_child_folds);
  std::vector<int> folds(count, -1);

  if(kind == TRACE_HEX || code == 0) {
    return folds;
  }

  if(kind == TRACE_PRISM) {
    if(code == 1) {
      for(int i = 0; i < count; ++i) {
        folds[i] = local_fold;
      }
    } else {
      for(int i = 0; i < count; ++i) {
        folds[i] = 4;
      }
    }
    return folds;
  }

  if(depth == 0) {
    return root_child_folds;
  }

  if(count >= 1) {
    folds[0] = local_fold;
  }
  if(count >= 2) {
    folds[1] = local_fold;
  }
  for(int i = 2; i < count; ++i) {
    folds[i] = 3;
  }
  return folds;
}

std::string code_meaning(TraceKind kind, int code, int depth) {
  switch(kind) {
  case TRACE_HEX:
    switch(code) {
    case 0: return "leaf";
    case 1: return "split_z";
    case 2: return "split_y";
    case 3: return "split_yz";
    case 4: return "split_x";
    case 5: return "split_xz";
    case 6: return "split_xy";
    case 7: return "split_xyz";
    default: return "unknown";
    }
  case TRACE_PRISM:
    switch(code) {
    case 0: return "leaf";
    case 1: return "split_z";
    case 2: return "split_xy_ring";
    case 3: return "split_xy_and_z";
    default: return "unknown";
    }
  case TRACE_GENERAL:
    switch(code) {
    case 0: return "leaf";
    case 1: return depth == 0 ? "split_cell_to_diamonds"
                              : "split_diamond_isotropic";
    default: return "unknown";
    }
  }
  return "unknown";
}

PlanReplayTrace make_plan_replay_trace(TraceKind kind,
                                       const std::vector<char>& plan,
                                       int root_fold,
                                       const std::vector<int>& root_child_folds) {
  std::vector<TraceState> queue;
  queue.push_back({0, -1, -1, 0, root_fold, "R"});

  PlanReplayTrace trace;
  trace.consumed_plan_entries = 0;
  trace.leaf_count = 0;

  for(size_t cursor = 0; cursor < queue.size(); ++cursor) {
    const TraceState state = queue[cursor];
    const bool explicit_code = trace.consumed_plan_entries < int(plan.size());
    const int plan_index = explicit_code ? trace.consumed_plan_entries : -1;
    const int code = explicit_code ? int(plan[trace.consumed_plan_entries]) : 0;
    if(explicit_code) {
      ++trace.consumed_plan_entries;
    }

    const int child_count = trace_child_count(kind,
                                              code,
                                              state.local_fold,
                                              state.depth,
                                              root_child_folds);
    TraceRow row;
    row.id = state.id;
    row.parent_id = state.parent_id;
    row.child_slot = state.child_slot;
    row.depth = state.depth;
    row.local_fold = state.local_fold;
    row.path = state.path;
    row.plan_index = plan_index;
    row.explicit_code = explicit_code;
    row.code = code;
    row.child_count = child_count;
    row.first_child_id = child_count == 0 ? -1 : int(queue.size());
    row.leaf_id = child_count == 0 ? ++trace.leaf_count : 0;
    trace.rows.push_back(row);

    const std::vector<int> child_folds =
      trace_child_folds(kind, code, state.local_fold, state.depth, root_child_folds);
    for(int child = 0; child < child_count; ++child) {
      std::ostringstream path;
      path << state.path << "." << child;
      TraceState child_state;
      child_state.id = int(queue.size());
      child_state.parent_id = state.id;
      child_state.child_slot = child;
      child_state.depth = state.depth + 1;
      child_state.local_fold = child_folds[child];
      child_state.path = path.str();
      queue.push_back(child_state);
    }
  }

  return trace;
}

double coord(const Node* node, int axis) {
  if(axis == 0) {
    return node->p.x;
  }
  if(axis == 1) {
    return node->p.y;
  }
  return node->p.z;
}

std::vector<Node*> unique_leaf_nodes(HexCell* leaf) {
  std::map<Node*, bool> seen;
  std::vector<Node*> nodes;
  std::vector<Edge*> edges = leaf->get_edges();

  for(size_t i = 0; i < edges.size(); ++i) {
    if(edges[i] == 0 || edges[i]->head == 0 || edges[i]->tail == 0) {
      throw std::runtime_error("leaf has an incomplete edge");
    }
    if(seen.find(edges[i]->head) == seen.end()) {
      seen[edges[i]->head] = true;
      nodes.push_back(edges[i]->head);
    }
    if(seen.find(edges[i]->tail) == seen.end()) {
      seen[edges[i]->tail] = true;
      nodes.push_back(edges[i]->tail);
    }
  }

  if(nodes.size() != 8) {
    std::ostringstream msg;
    msg << "expected 8 nodes for a hex leaf, found " << nodes.size();
    throw std::runtime_error(msg.str());
  }

  return nodes;
}

bool close_to(double lhs, double rhs) {
  return std::fabs(lhs - rhs) <= 1.0e-10;
}

Node* find_corner(const std::vector<Node*>& nodes,
                  const double min_coord[3],
                  const double max_coord[3],
                  const int corner[3]) {
  for(size_t i = 0; i < nodes.size(); ++i) {
    bool match = true;
    for(int axis = 0; axis < 3; ++axis) {
      const double target = corner[axis] ? max_coord[axis] : min_coord[axis];
      match = match && close_to(coord(nodes[i], axis), target);
    }
    if(match) {
      return nodes[i];
    }
  }

  throw std::runtime_error("could not identify a VTK hex corner");
}

std::vector<Node*> vtk_hex_nodes(HexCell* leaf) {
  std::vector<Node*> nodes = unique_leaf_nodes(leaf);
  double min_coord[3] = {coord(nodes[0], 0), coord(nodes[0], 1), coord(nodes[0], 2)};
  double max_coord[3] = {min_coord[0], min_coord[1], min_coord[2]};

  for(size_t i = 1; i < nodes.size(); ++i) {
    for(int axis = 0; axis < 3; ++axis) {
      min_coord[axis] = std::min(min_coord[axis], coord(nodes[i], axis));
      max_coord[axis] = std::max(max_coord[axis], coord(nodes[i], axis));
    }
  }

  const int corner_bits[8][3] = {
    {0, 0, 0},
    {1, 0, 0},
    {1, 1, 0},
    {0, 1, 0},
    {0, 0, 1},
    {1, 0, 1},
    {1, 1, 1},
    {0, 1, 1}
  };

  std::vector<Node*> ordered(8);
  for(int i = 0; i < 8; ++i) {
    ordered[i] = find_corner(nodes, min_coord, max_coord, corner_bits[i]);
  }
  return ordered;
}

void make_directory(const std::string& path) {
  if(path.empty()) {
    return;
  }
  if(mkdir(path.c_str(), 0775) != 0 && errno != EEXIST) {
    std::ostringstream msg;
    msg << "could not create directory '" << path << "': " << std::strerror(errno);
    throw std::runtime_error(msg.str());
  }
}

std::string join_path(const std::string& dir, const std::string& file) {
  if(dir.empty()) {
    return file;
  }
  if(dir[dir.size() - 1] == '/') {
    return dir + file;
  }
  return dir + "/" + file;
}

int write_hex_vtk(const std::string& path,
                  const std::string& title,
                  int root_split_code,
                  const std::vector<HexCell*>& leaves) {
  std::vector<std::vector<Node*> > cell_nodes(leaves.size());
  std::vector<Node*> points;
  std::map<Node*, int> point_id;

  for(size_t cell = 0; cell < leaves.size(); ++cell) {
    cell_nodes[cell] = vtk_hex_nodes(leaves[cell]);
    for(size_t n = 0; n < cell_nodes[cell].size(); ++n) {
      Node* node = cell_nodes[cell][n];
      if(point_id.find(node) == point_id.end()) {
        const int id = int(points.size());
        point_id[node] = id;
        points.push_back(node);
      }
    }
  }

  std::ofstream out(path.c_str());
  if(!out) {
    throw std::runtime_error("could not open VTK output file");
  }

  out << "# vtk DataFile Version 3.0\n";
  out << title << "\n";
  out << "ASCII\n";
  out << "DATASET UNSTRUCTURED_GRID\n";
  out << "POINTS " << points.size() << " float\n";
  out << std::setprecision(17);
  for(size_t i = 0; i < points.size(); ++i) {
    out << points[i]->p.x << " " << points[i]->p.y << " " << points[i]->p.z << "\n";
  }

  out << "CELLS " << cell_nodes.size() << " " << cell_nodes.size() * 9 << "\n";
  for(size_t cell = 0; cell < cell_nodes.size(); ++cell) {
    out << "8";
    for(size_t n = 0; n < cell_nodes[cell].size(); ++n) {
      out << " " << point_id[cell_nodes[cell][n]];
    }
    out << "\n";
  }

  out << "CELL_TYPES " << cell_nodes.size() << "\n";
  for(size_t cell = 0; cell < cell_nodes.size(); ++cell) {
    out << "12\n";
  }

  out << "CELL_DATA " << cell_nodes.size() << "\n";
  out << "SCALARS root_split_code int 1\n";
  out << "LOOKUP_TABLE default\n";
  for(size_t cell = 0; cell < cell_nodes.size(); ++cell) {
    out << root_split_code << "\n";
  }
  out << "SCALARS fvmadapt_cell_index int 1\n";
  out << "LOOKUP_TABLE default\n";
  for(size_t cell = 0; cell < leaves.size(); ++cell) {
    out << leaves[cell]->getCellIndex() << "\n";
  }

  return int(points.size());
}

std::vector<Edge*> edge_leaves(std::list<Edge*>& root_edges) {
  std::set<Edge*> seen;
  std::vector<Edge*> leaves;

  for(std::list<Edge*>::iterator edge = root_edges.begin();
      edge != root_edges.end(); ++edge) {
    std::list<Edge*> local_leaves;
    (*edge)->sort_leaves(local_leaves);
    for(std::list<Edge*>::iterator leaf = local_leaves.begin();
        leaf != local_leaves.end(); ++leaf) {
      if(seen.insert(*leaf).second) {
        leaves.push_back(*leaf);
      }
    }
  }

  return leaves;
}

int point_id(Node* node, std::map<Node*, int>& ids, std::vector<Node*>& points) {
  std::map<Node*, int>::const_iterator existing = ids.find(node);
  if(existing != ids.end()) {
    return existing->second;
  }

  const int id = int(points.size());
  ids[node] = id;
  points.push_back(node);
  return id;
}

int write_wireframe_vtk(const std::string& path,
                        const std::string& title,
                        std::list<Node*>& root_nodes,
                        std::list<Edge*>& root_edges) {
  std::vector<Node*> points;
  std::map<Node*, int> ids;
  for(std::list<Node*>::iterator node = root_nodes.begin();
      node != root_nodes.end(); ++node) {
    point_id(*node, ids, points);
  }

  const std::vector<Edge*> leaves = edge_leaves(root_edges);
  for(size_t i = 0; i < leaves.size(); ++i) {
    point_id(leaves[i]->head, ids, points);
    point_id(leaves[i]->tail, ids, points);
  }

  std::ofstream out(path.c_str());
  if(!out) {
    throw std::runtime_error("could not open wireframe VTK output file");
  }

  out << "# vtk DataFile Version 3.0\n";
  out << title << "\n";
  out << "ASCII\n";
  out << "DATASET POLYDATA\n";
  out << "POINTS " << points.size() << " float\n";
  out << std::setprecision(17);
  for(size_t i = 0; i < points.size(); ++i) {
    out << points[i]->p.x << " " << points[i]->p.y << " " << points[i]->p.z << "\n";
  }

  out << "LINES " << leaves.size() << " " << leaves.size() * 3 << "\n";
  for(size_t i = 0; i < leaves.size(); ++i) {
    out << "2 " << ids[leaves[i]->head] << " " << ids[leaves[i]->tail] << "\n";
  }

  return int(points.size());
}

int root_split_code(const std::vector<char>& plan) {
  return plan.empty() ? 0 : int(plan[0]);
}

CaseResult run_hex_case(const PlanCase& plan_case,
                        bool write_examples,
                        const std::string& output_dir) {
  HexFixture fixture;
  build_unit_hex(fixture);

  std::vector<HexCell*> resplit_leaves;
  fixture.root->resplit(plan_case.plan,
                        fixture.nodes,
                        fixture.edges,
                        fixture.faces,
                        resplit_leaves);

  std::list<HexCell*> sorted_leaves;
  fixture.root->sort_leaves(sorted_leaves);

  HexCell counter;
  const int fine_cell_count = counter.num_fine_cells(plan_case.plan);
  const int expected = fine_cell_count;

  if(int(resplit_leaves.size()) != expected) {
    std::ostringstream msg;
    msg << "hex plan " << plan_case.name << " produced "
        << resplit_leaves.size() << " resplit leaves, expected " << expected;
    throw std::runtime_error(msg.str());
  }

  if(int(sorted_leaves.size()) != expected) {
    std::ostringstream msg;
    msg << "hex plan " << plan_case.name << " produced "
        << sorted_leaves.size() << " sorted leaves, expected " << expected;
    throw std::runtime_error(msg.str());
  }

  if(fine_cell_count != expected) {
    std::ostringstream msg;
    msg << "hex plan " << plan_case.name << " has num_fine_cells="
        << fine_cell_count << ", expected " << expected;
    throw std::runtime_error(msg.str());
  }

  const std::vector<char> round_trip = fixture.root->make_cellplan();
  if(round_trip != plan_case.canonical) {
    std::ostringstream msg;
    msg << "hex plan " << plan_case.name << " round-tripped as "
        << plan_string(round_trip) << ", expected "
        << plan_string(plan_case.canonical);
    throw std::runtime_error(msg.str());
  }

  CaseResult result;
  result.shape = "hex";
  result.case_name = plan_case.name;
  result.input_plan = plan_case.plan;
  result.canonical_plan = round_trip;
  result.expected_leaves = expected;
  result.resplit_leaves = int(resplit_leaves.size());
  result.sorted_leaves = int(sorted_leaves.size());
  result.fine_cell_count = fine_cell_count;
  result.point_count = 0;

  if(write_examples && plan_case.write_vtk) {
    std::ostringstream name;
    name << plan_file_token(result.shape, plan_case.name) << ".vtk";
    result.vtk_file = join_path(output_dir, name.str());
    std::ostringstream title;
    title << "FVMAdapt HexCell " << plan_case.name
          << " plan " << plan_string(plan_case.plan);
    result.point_count = write_hex_vtk(result.vtk_file,
                                       title.str(),
                                       root_split_code(plan_case.plan),
                                       resplit_leaves);
  }

  return result;
}

CaseResult run_prism_case(const PlanCase& plan_case,
                          bool write_examples,
                          const std::string& output_dir) {
  PrismFixture fixture;
  build_unit_prism(fixture);

  std::vector<Prism*> resplit_leaves;
  fixture.root->resplit(plan_case.plan,
                        fixture.nodes,
                        fixture.edges,
                        fixture.qfaces,
                        fixture.gfaces,
                        resplit_leaves);

  std::list<Prism*> sorted_leaves;
  fixture.root->sort_leaves(sorted_leaves);

  PrismFixture counter;
  build_unit_prism(counter);
  const int fine_cell_count = counter.root->empty_resplit(plan_case.plan);
  const int expected = fine_cell_count;

  if(int(resplit_leaves.size()) != expected) {
    std::ostringstream msg;
    msg << "prism plan " << plan_case.name << " produced "
        << resplit_leaves.size() << " resplit leaves, expected " << expected;
    throw std::runtime_error(msg.str());
  }

  if(int(sorted_leaves.size()) != expected) {
    std::ostringstream msg;
    msg << "prism plan " << plan_case.name << " produced "
        << sorted_leaves.size() << " sorted leaves, expected " << expected;
    throw std::runtime_error(msg.str());
  }

  if(fine_cell_count != expected) {
    std::ostringstream msg;
    msg << "prism plan " << plan_case.name << " has empty_resplit="
        << fine_cell_count << ", expected " << expected;
    throw std::runtime_error(msg.str());
  }

  const std::vector<char> round_trip = fixture.root->make_cellplan();
  if(round_trip != plan_case.canonical) {
    std::ostringstream msg;
    msg << "prism plan " << plan_case.name << " round-tripped as "
        << plan_string(round_trip) << ", expected "
        << plan_string(plan_case.canonical);
    throw std::runtime_error(msg.str());
  }

  CaseResult result;
  result.shape = "prism";
  result.case_name = plan_case.name;
  result.input_plan = plan_case.plan;
  result.canonical_plan = round_trip;
  result.expected_leaves = expected;
  result.resplit_leaves = int(resplit_leaves.size());
  result.sorted_leaves = int(sorted_leaves.size());
  result.fine_cell_count = fine_cell_count;
  result.point_count = 0;

  if(write_examples && plan_case.write_vtk) {
    std::ostringstream name;
    name << plan_file_token(result.shape, plan_case.name) << "_wire.vtk";
    result.vtk_file = join_path(output_dir, name.str());
    std::ostringstream title;
    title << "FVMAdapt Prism " << plan_case.name
          << " plan " << plan_string(plan_case.plan) << " wireframe";
    result.point_count = write_wireframe_vtk(result.vtk_file, title.str(),
                                             fixture.nodes, fixture.edges);
  }

  return result;
}

CaseResult run_general_case(const std::string& shape,
                            const std::string& title_shape,
                            GeneralBuilder build_cell,
                            const PlanCase& plan_case,
                            bool write_examples,
                            const std::string& output_dir) {
  GeneralFixture fixture;
  build_cell(fixture);

  std::vector<DiamondCell*> resplit_leaves;
  if(!plan_case.plan.empty()) {
    fixture.root->resplit(plan_case.plan,
                          fixture.nodes,
                          fixture.edges,
                          fixture.faces,
                          resplit_leaves);
  }

  std::list<DiamondCell*> sorted_leaves;
  if(!plan_case.plan.empty()) {
    fixture.root->sort_leaves(sorted_leaves);
  }

  GeneralFixture counter;
  build_cell(counter);
  const int fine_cell_count = counter.root->empty_resplit(plan_case.plan);
  const int expected = fine_cell_count;
  const int resplit_count = plan_case.plan.empty() ? 1 : int(resplit_leaves.size());
  const int sorted_count = plan_case.plan.empty() ? 1 : int(sorted_leaves.size());

  if(resplit_count != expected) {
    std::ostringstream msg;
    msg << title_shape << " plan " << plan_case.name << " produced "
        << resplit_count << " resplit leaves, expected " << expected;
    throw std::runtime_error(msg.str());
  }

  if(sorted_count != expected) {
    std::ostringstream msg;
    msg << title_shape << " plan " << plan_case.name << " produced "
        << sorted_count << " sorted leaves, expected " << expected;
    throw std::runtime_error(msg.str());
  }

  if(fine_cell_count != expected) {
    std::ostringstream msg;
    msg << title_shape << " plan " << plan_case.name << " has empty_resplit="
        << fine_cell_count << ", expected " << expected;
    throw std::runtime_error(msg.str());
  }

  const std::vector<char> round_trip = fixture.root->make_cellplan();
  if(round_trip != plan_case.canonical) {
    std::ostringstream msg;
    msg << title_shape << " plan " << plan_case.name << " round-tripped as "
        << plan_string(round_trip) << ", expected "
        << plan_string(plan_case.canonical);
    throw std::runtime_error(msg.str());
  }

  CaseResult result;
  result.shape = shape;
  result.case_name = plan_case.name;
  result.input_plan = plan_case.plan;
  result.canonical_plan = round_trip;
  result.expected_leaves = expected;
  result.resplit_leaves = resplit_count;
  result.sorted_leaves = sorted_count;
  result.fine_cell_count = fine_cell_count;
  result.point_count = 0;

  if(write_examples && plan_case.write_vtk) {
    std::ostringstream name;
    name << plan_file_token(shape, plan_case.name) << "_wire.vtk";
    result.vtk_file = join_path(output_dir, name.str());
    std::ostringstream title;
    title << "FVMAdapt " << title_shape << " " << plan_case.name
          << " plan " << plan_string(plan_case.plan)
          << " wireframe";
    result.point_count = write_wireframe_vtk(result.vtk_file, title.str(),
                                             fixture.nodes, fixture.edges);
  }

  return result;
}

std::vector<PlanCase> hex_plan_cases() {
  std::vector<PlanCase> cases;
  for(int split_code = 0; split_code <= 7; ++split_code) {
    std::ostringstream name;
    name << "split_" << split_code;
    cases.push_back(PlanCase(name.str(),
                             root_plan(split_code),
                             canonical_root_plan(split_code),
                             true));
  }

  cases.push_back(PlanCase("split_7_trailing_zeros",
                           {char(7), char(0), char(0), char(0), char(0),
                            char(0), char(0), char(0), char(0)},
                           {char(7)},
                           false));
  cases.push_back(PlanCase("split_7_child0_z",
                           {char(7), char(1)},
                           {char(7), char(1)},
                           true));
  cases.push_back(PlanCase("split_7_child7_z",
                           {char(7), char(0), char(0), char(0), char(0),
                            char(0), char(0), char(0), char(1)},
                           {char(7), char(0), char(0), char(0), char(0),
                            char(0), char(0), char(0), char(1)},
                           true));
  cases.push_back(PlanCase("split_4_child0_yz",
                           {char(4), char(3)},
                           {char(4), char(3)},
                           true));
  return cases;
}

std::vector<PlanCase> prism_plan_cases() {
  std::vector<PlanCase> cases;
  for(int split_code = 0; split_code <= 3; ++split_code) {
    std::ostringstream name;
    name << "split_" << split_code;
    cases.push_back(PlanCase(name.str(),
                             root_plan(split_code),
                             canonical_root_plan(split_code),
                             true));
  }

  cases.push_back(PlanCase("split_3_trailing_zeros",
                           {char(3), char(0), char(0), char(0), char(0),
                            char(0), char(0)},
                           {char(3)},
                           false));
  cases.push_back(PlanCase("split_3_child0_z",
                           {char(3), char(1)},
                           {char(3), char(1)},
                           true));
  cases.push_back(PlanCase("split_2_child0_z",
                           {char(2), char(1)},
                           {char(2), char(1)},
                           true));
  return cases;
}

std::vector<PlanCase> general_plan_cases() {
  std::vector<PlanCase> cases;
  cases.push_back(PlanCase("split_0", std::vector<char>(), std::vector<char>(), true));
  cases.push_back(PlanCase("split_1", {char(1)}, {char(1)}, true));
  cases.push_back(PlanCase("split_1_trailing_zeros",
                           {char(1), char(0), char(0), char(0), char(0)},
                           {char(1)},
                           false));
  cases.push_back(PlanCase("split_1_child0",
                           {char(1), char(1)},
                           {char(1), char(1)},
                           true));
  return cases;
}

int count_quad_face_leaves(const std::vector<char>& face_plan) {
  QuadFace face(4);
  std::vector<QuadFace*> leaves;
  face.empty_resplit(face_plan, 0, leaves);
  return int(leaves.size());
}

int count_general_face_leaves(const std::vector<char>& face_plan, int num_edges) {
  if(face_plan.empty()) {
    return 1;
  }

  std::vector<char> work;
  work.push_back(char(0));
  size_t index = 0;
  int leaves = 0;
  for(size_t cursor = 0; cursor < work.size(); ++cursor) {
    const char code = index < face_plan.size() ? face_plan[index++] : char(0);
    if(code == 0) {
      ++leaves;
    } else if(code == 1) {
      for(int child = 0; child < num_edges; ++child) {
        work.push_back(char(0));
      }
    } else if(code == 8) {
      work.push_back(char(0));
    } else {
      std::ostringstream msg;
      msg << "unsupported general face plan code " << int(code)
          << " in " << plan_string(face_plan);
      throw std::runtime_error(msg.str());
    }
  }
  return leaves;
}

int count_edge_leaves(const std::vector<char>& edge_plan) {
  Node head(vect3d(0.0, 0.0, 0.0));
  Node tail(vect3d(1.0, 0.0, 0.0));
  Edge edge(&head, &tail);
  std::list<Node*> nodes;
  edge.resplit(edge_plan, nodes);
  std::list<Edge*> leaves;
  edge.sort_leaves(leaves);
  const int count = int(leaves.size());
  for(std::list<Node*>::iterator node = nodes.begin(); node != nodes.end(); ++node) {
    delete *node;
  }
  nodes.clear();
  return count;
}

void check_plan_transfer_round_trips() {
  const std::vector<std::vector<char> > general_face_plans = {
    std::vector<char>(),
    {char(1)},
    {char(1), char(1)},
    {char(1), char(0), char(1)}
  };

  for(size_t i = 0; i < general_face_plans.size(); ++i) {
    std::vector<char> general_plan = general_face_plans[i];
    Face canonical_face(4);
    canonical_face.empty_resplit(general_plan);
    const std::vector<char> canonical = canonical_face.make_faceplan();
    const std::vector<char> quad_plan = transfer_plan_g2q(general_plan);
    const std::vector<char> round_trip = transfer_plan_q2g(quad_plan);
    if(round_trip != canonical) {
      std::ostringstream msg;
      msg << "general face plan " << plan_string(general_face_plans[i])
          << " transferred through quad as " << plan_string(round_trip)
          << ", expected " << plan_string(canonical);
      throw std::runtime_error(msg.str());
    }
  }

  const std::vector<std::vector<char> > quad_face_plans = {
    std::vector<char>(),
    {char(3)},
    {char(3), char(3)},
    {char(3), char(0), char(0), char(0), char(3)}
  };

  for(size_t i = 0; i < quad_face_plans.size(); ++i) {
    std::vector<char> general_plan = transfer_plan_q2g(quad_face_plans[i]);
    const std::vector<char> round_trip = transfer_plan_g2q(general_plan);
    if(round_trip != quad_face_plans[i]) {
      std::ostringstream msg;
      msg << "quad face plan " << plan_string(quad_face_plans[i])
          << " transferred through general as " << plan_string(round_trip)
          << ", expected " << plan_string(quad_face_plans[i]);
      throw std::runtime_error(msg.str());
    }
  }
}

void write_hex_plan_audit(std::ofstream& out, const CaseResult& result) {
  out << "case " << result.shape << " " << result.case_name << "\n";
  out << "  input_cell_plan " << plan_string(result.input_plan) << "\n";
  out << "  canonical_cell_plan " << plan_string(result.canonical_plan) << "\n";
  out << "  fine_cells " << result.fine_cell_count << "\n";
  if(!result.vtk_file.empty()) {
    out << "  vtk_file " << result.vtk_file << "\n";
  }
  for(int face = 0; face < 6; ++face) {
    const std::vector<char> face_plan =
      extract_hex_face(result.input_plan, DIRECTION(face));
    const std::vector<int32> c1 =
      get_c1_hex(result.input_plan, face_plan, 0, char(face));
    out << "  face " << face
        << " plan " << plan_string(face_plan)
        << " leaves " << count_quad_face_leaves(face_plan)
        << " c1 " << int_vector_string(c1) << "\n";
    for(int edge = 0; edge < 4; ++edge) {
      std::vector<char> edge_plan;
      extract_quad_edge(face_plan, edge_plan, unsigned(edge));
      out << "    edge " << edge
          << " plan " << plan_string(edge_plan)
          << " leaves " << count_edge_leaves(edge_plan) << "\n";
    }
  }
}

void write_prism_plan_audit(std::ofstream& out, const CaseResult& result) {
  out << "case " << result.shape << " " << result.case_name << "\n";
  out << "  input_cell_plan " << plan_string(result.input_plan) << "\n";
  out << "  canonical_cell_plan " << plan_string(result.canonical_plan) << "\n";
  out << "  fine_cells " << result.fine_cell_count << "\n";
  if(!result.vtk_file.empty()) {
    out << "  vtk_file " << result.vtk_file << "\n";
  }
  for(int face = 0; face < 5; ++face) {
    const std::vector<char> face_plan = extract_prism_face(result.input_plan, face);
    const std::vector<int32> c1 =
      get_c1_prism(result.input_plan, face_plan, 0, face);
    out << "  face " << face
        << " plan " << plan_string(face_plan)
        << " leaves "
        << (face < 2 ? count_general_face_leaves(face_plan, 3)
                     : count_quad_face_leaves(face_plan))
        << " c1 " << int_vector_string(c1) << "\n";
  }
}

void write_general_plan_audit(std::ofstream& out, const CaseResult& result) {
  out << "case " << result.shape << " " << result.case_name << "\n";
  out << "  input_cell_plan " << plan_string(result.input_plan) << "\n";
  out << "  canonical_cell_plan " << plan_string(result.canonical_plan) << "\n";
  out << "  fine_cells " << result.fine_cell_count << "\n";
  if(!result.vtk_file.empty()) {
    out << "  vtk_file " << result.vtk_file << "\n";
  }
}

void write_summary(const std::string& output_dir,
                   const std::vector<CaseResult>& results) {
  const std::string path = join_path(output_dir, "core_split_summary.dat");
  std::ofstream out(path.c_str());
  if(!out) {
    throw std::runtime_error("could not open summary output file");
  }

  out << "# shape case input_plan canonical_plan expected_leaves resplit_leaves "
      << "sorted_leaves num_fine_cells vtk_points vtk_file\n";
  for(size_t i = 0; i < results.size(); ++i) {
    out << results[i].shape << " "
        << results[i].case_name << " "
        << plan_string(results[i].input_plan) << " "
        << plan_string(results[i].canonical_plan) << " "
        << results[i].expected_leaves << " "
        << results[i].resplit_leaves << " "
        << results[i].sorted_leaves << " "
        << results[i].fine_cell_count << " "
        << results[i].point_count << " "
        << results[i].vtk_file << "\n";
  }
}

void write_plan_audit(const std::string& output_dir,
                      const std::vector<CaseResult>& results) {
  const std::string path = join_path(output_dir, "core_plan_audit.dat");
  std::ofstream out(path.c_str());
  if(!out) {
    throw std::runtime_error("could not open plan audit output file");
  }

  out << "# FVMAdapt core plan audit\n";
  out << "# Plans are char vectors printed as integer codes in breadth-first order.\n";
  for(size_t i = 0; i < results.size(); ++i) {
    if(results[i].shape == "hex") {
      write_hex_plan_audit(out, results[i]);
    } else if(results[i].shape == "prism") {
      write_prism_plan_audit(out, results[i]);
    } else {
      write_general_plan_audit(out, results[i]);
    }
    out << "\n";
  }
}

TraceKind trace_kind_for_shape(const std::string& shape) {
  if(shape == "hex") {
    return TRACE_HEX;
  }
  if(shape == "prism") {
    return TRACE_PRISM;
  }
  return TRACE_GENERAL;
}

int root_fold_for_shape(const std::string& shape) {
  if(shape == "prism") {
    return 3;
  }
  if(shape == "general_tet") {
    return 4;
  }
  if(shape == "general_pyramid") {
    return 5;
  }
  return -1;
}

std::vector<int> root_child_folds_for_shape(const std::string& shape) {
  if(shape == "general_tet") {
    return {3, 3, 3, 3};
  }
  if(shape == "general_pyramid") {
    return {3, 3, 3, 3, 4};
  }
  return std::vector<int>();
}

void write_one_plan_replay(std::ofstream& out, const CaseResult& result) {
  const TraceKind kind = trace_kind_for_shape(result.shape);
  const PlanReplayTrace trace =
    make_plan_replay_trace(kind,
                           result.input_plan,
                           root_fold_for_shape(result.shape),
                           root_child_folds_for_shape(result.shape));

  if(trace.leaf_count != result.fine_cell_count) {
    std::ostringstream msg;
    msg << "plan replay trace for " << result.shape << " " << result.case_name
        << " has " << trace.leaf_count << " leaves, expected "
        << result.fine_cell_count;
    throw std::runtime_error(msg.str());
  }

  out << "case " << result.shape << " " << result.case_name << "\n";
  out << "  input_cell_plan " << plan_string(result.input_plan) << "\n";
  out << "  canonical_cell_plan " << plan_string(result.canonical_plan) << "\n";
  out << "  consumed_plan_entries " << trace.consumed_plan_entries
      << " of " << result.input_plan.size() << "\n";
  if(trace.consumed_plan_entries < int(result.input_plan.size())) {
    out << "  unused_plan_tail "
        << plan_tail_string(result.input_plan, trace.consumed_plan_entries)
        << "\n";
  }
  out << "  replay_rows\n";
  out << "    # id parent child depth path local_fold plan_index source code "
      << "child_count first_child leaf meaning\n";
  for(size_t i = 0; i < trace.rows.size(); ++i) {
    const TraceRow& row = trace.rows[i];
    out << "    node "
        << row.id << " "
        << row.parent_id << " "
        << row.child_slot << " "
        << row.depth << " "
        << row.path << " "
        << row.local_fold << " "
        << row.plan_index << " "
        << (row.explicit_code ? "explicit" : "default") << " "
        << row.code << " "
        << row.child_count << " "
        << row.first_child_id << " "
        << row.leaf_id << " "
        << code_meaning(kind, row.code, row.depth) << "\n";
  }
}

void write_plan_replay(const std::string& output_dir,
                       const std::vector<CaseResult>& results) {
  const std::string path = join_path(output_dir, "core_plan_replay.dat");
  std::ofstream out(path.c_str());
  if(!out) {
    throw std::runtime_error("could not open plan replay output file");
  }

  out << "# FVMAdapt core plan replay trace\n";
  out << "# Each row is the breadth-first visit of one cell-tree node.\n";
  out << "# source=default means the plan vector was exhausted and replay used code 0.\n";
  out << "# local_fold is prism nfold, general Cell node count at depth 0, or\n";
  out << "# general DiamondCell nfold below depth 0. Hex rows use -1.\n";
  for(size_t i = 0; i < results.size(); ++i) {
    write_one_plan_replay(out, results[i]);
    out << "\n";
  }
}

} // namespace

int main(int argc, char** argv) {
  bool write_examples = false;
  std::string output_dir = "output";

  for(int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if(arg == "--check-only") {
      write_examples = false;
    } else if(arg == "--write-dir") {
      if(i + 1 >= argc) {
        std::cerr << "--write-dir requires a path\n";
        std::cout << "FAILURE!\n";
        return 1;
      }
      write_examples = true;
      output_dir = argv[++i];
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      std::cout << "FAILURE!\n";
      return 1;
    }
  }

  try {
    if(write_examples) {
      make_directory(output_dir);
    }

    check_plan_transfer_round_trips();

    std::vector<CaseResult> results;
    const std::vector<PlanCase> hex_cases = hex_plan_cases();
    for(size_t i = 0; i < hex_cases.size(); ++i) {
      results.push_back(run_hex_case(hex_cases[i], write_examples, output_dir));
    }

    const std::vector<PlanCase> prism_cases = prism_plan_cases();
    for(size_t i = 0; i < prism_cases.size(); ++i) {
      results.push_back(run_prism_case(prism_cases[i], write_examples, output_dir));
    }

    const std::vector<PlanCase> general_cases = general_plan_cases();
    for(size_t i = 0; i < general_cases.size(); ++i) {
      results.push_back(run_general_case("general_tet",
                                         "general tetra",
                                         build_tetra_cell,
                                         general_cases[i],
                                         write_examples,
                                         output_dir));
    }
    for(size_t i = 0; i < general_cases.size(); ++i) {
      results.push_back(run_general_case("general_pyramid",
                                         "general pyramid",
                                         build_pyramid_cell,
                                         general_cases[i],
                                         write_examples,
                                         output_dir));
    }

    if(write_examples) {
      write_summary(output_dir, results);
      write_plan_audit(output_dir, results);
      write_plan_replay(output_dir, results);
      for(size_t i = 0; i < results.size(); ++i) {
        if(!results[i].vtk_file.empty()) {
          std::cout << "wrote " << results[i].vtk_file << "\n";
        }
      }
      std::cout << "wrote " << join_path(output_dir, "core_split_summary.dat") << "\n";
      std::cout << "wrote " << join_path(output_dir, "core_plan_audit.dat") << "\n";
      std::cout << "wrote " << join_path(output_dir, "core_plan_replay.dat") << "\n";
    }

    std::cout << "SUCCESS!\n";
    return 0;
  } catch(const std::exception& error) {
    std::cerr << error.what() << "\n";
    std::cout << "FAILURE!\n";
    return 1;
  }
}
