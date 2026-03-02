#include <set>
#include <string>
#include <vector>

#include <Loci.h>
#include <depend_graph.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

namespace {

using namespace Loci;

class buildA_rule : public pointwise_rule {
  const_store<int> initA;
  store<int> A;
public:
  buildA_rule() {
    name_store("initA", initA);
    name_store("A{it=0}", A);
    input("initA");
    output("A{it=0}");
  }
  void compute(const sequence &) {}
};

class buildB_rule : public pointwise_rule {
  const_store<int> initB;
  store<int> B;
public:
  buildB_rule() {
    name_store("initB", initB);
    name_store("B{it=0}", B);
    input("initB");
    output("B{it=0}");
  }
  void compute(const sequence &) {}
};

class advanceA_rule : public pointwise_rule {
  const_store<int> Aold;
  store<int> Anew;
public:
  advanceA_rule() {
    name_store("A{it}", Aold);
    name_store("A{it+1}", Anew);
    input("A{it}");
    output("A{it+1}");
  }
  void compute(const sequence &) {}
};

class advanceB_rule : public pointwise_rule {
  const_store<int> Bold;
  store<int> Bnew;
public:
  advanceB_rule() {
    name_store("B{it}", Bold);
    name_store("B{it+1}", Bnew);
    input("B{it}");
    output("B{it+1}");
  }
  void compute(const sequence &) {}
};

class collapseA_rule : public pointwise_rule {
  const_store<int> A;
  const_store<int> doneA;
  store<int> Aout;
public:
  collapseA_rule() {
    name_store("A{it}", A);
    name_store("doneA", doneA);
    name_store("Aout", Aout);
    input("A{it}");
    input("doneA");
    output("Aout");
    conditional("doneA");
  }
  void compute(const sequence &) {}
};

class collapseB_rule : public pointwise_rule {
  const_store<int> B;
  const_store<int> doneB;
  store<int> Bout;
public:
  collapseB_rule() {
    name_store("B{it}", B);
    name_store("doneB", doneB);
    name_store("Bout", Bout);
    input("B{it}");
    input("doneB");
    output("Bout");
    conditional("doneB");
  }
  void compute(const sequence &) {}
};

class collapseA_shared_cond_rule : public pointwise_rule {
  const_store<int> A;
  const_store<int> done;
  store<int> Aout;
public:
  collapseA_shared_cond_rule() {
    name_store("A{it}", A);
    name_store("done", done);
    name_store("Aout", Aout);
    input("A{it}");
    input("done");
    output("Aout");
    conditional("done");
  }
  void compute(const sequence &) {}
};

class collapseB_shared_cond_rule : public pointwise_rule {
  const_store<int> B;
  const_store<int> done;
  store<int> Bout;
public:
  collapseB_shared_cond_rule() {
    name_store("B{it}", B);
    name_store("done", done);
    name_store("Bout", Bout);
    input("B{it}");
    input("done");
    output("Bout");
    conditional("done");
  }
  void compute(const sequence &) {}
};

class advanceA_with_static_it_source_rule : public pointwise_rule {
  const_store<int> Aold;
  const_store<int> Sit;
  store<int> Anew;
public:
  advanceA_with_static_it_source_rule() {
    name_store("A{it}", Aold);
    name_store("S{it}", Sit);
    name_store("A{it+1}", Anew);
    input("A{it}");
    input("S{it}");
    output("A{it+1}");
  }
  void compute(const sequence &) {}
};

class buildZ_nested_rule : public pointwise_rule {
  const_store<int> Ait;
  store<int> Z0;
public:
  buildZ_nested_rule() {
    name_store("A{it}", Ait);
    name_store("Z{it,jt=0}", Z0);
    input("A{it}");
    output("Z{it,jt=0}");
  }
  void compute(const sequence &) {}
};

class advanceZ_nested_rule : public pointwise_rule {
  const_store<int> Zold;
  store<int> Znew;
public:
  advanceZ_nested_rule() {
    name_store("Z{it,jt}", Zold);
    name_store("Z{it,jt+1}", Znew);
    input("Z{it,jt}");
    output("Z{it,jt+1}");
  }
  void compute(const sequence &) {}
};

class collapseZ_to_A_rule : public pointwise_rule {
  const_store<int> Z;
  const_store<int> jt;
  store<int> Ait;
public:
  collapseZ_to_A_rule() {
    name_store("Z{it,jt}", Z);
    name_store("$jt{it,jt}", jt);
    name_store("A{it}", Ait);
    input("Z{it,jt}");
    input("$jt{it,jt}");
    output("A{it}");
    conditional("$jt{it,jt}");
  }
  void compute(const sequence &) {}
};

template <class RuleT>
void add_rule_impl(rule_db &rdb) {
  rdb.add_rule(rule_implP(new copy_rule_impl<RuleT>));
}

rule_db make_two_independent_iterations(bool shared_conditional) {
  rule_db rdb;

  add_rule_impl<buildA_rule>(rdb);
  add_rule_impl<buildB_rule>(rdb);
  add_rule_impl<advanceA_rule>(rdb);
  add_rule_impl<advanceB_rule>(rdb);

  if(shared_conditional) {
    add_rule_impl<collapseA_shared_cond_rule>(rdb);
    add_rule_impl<collapseB_shared_cond_rule>(rdb);
  } else {
    add_rule_impl<collapseA_rule>(rdb);
    add_rule_impl<collapseB_rule>(rdb);
  }

  return rdb;
}

variableSet make_given(bool shared_conditional) {
  variableSet given;
  given += variable("initA");
  given += variable("initB");
  if(shared_conditional) {
    given += variable("done");
  } else {
    given += variable("doneA");
    given += variable("doneB");
  }
  return given;
}

variableSet make_target() {
  variableSet target;
  target += variable("Aout");
  target += variable("Bout");
  return target;
}

rule_db make_iteration_with_promote_candidate() {
  rule_db rdb;
  add_rule_impl<buildA_rule>(rdb);
  add_rule_impl<advanceA_with_static_it_source_rule>(rdb);
  add_rule_impl<collapseA_shared_cond_rule>(rdb);
  return rdb;
}

variableSet make_promote_case_given() {
  variableSet given;
  given += variable("initA");
  given += variable("S");
  given += variable("done");
  return given;
}

variableSet make_promote_case_target() {
  variableSet target;
  target += variable("Aout");
  return target;
}

rule_db make_iteration_missing_build() {
  rule_db rdb;
  add_rule_impl<advanceA_rule>(rdb);
  add_rule_impl<collapseA_shared_cond_rule>(rdb);
  return rdb;
}

rule_db make_iteration_missing_collapse() {
  rule_db rdb;
  add_rule_impl<buildA_rule>(rdb);
  add_rule_impl<advanceA_rule>(rdb);
  return rdb;
}

variableSet make_time_target_A_next() {
  variableSet target;
  target += variable("A{it+1}");
  return target;
}

rule_db make_conflicting_iteration_with_nested_loop() {
  rule_db rdb = make_two_independent_iterations(false);
  add_rule_impl<buildZ_nested_rule>(rdb);
  add_rule_impl<advanceZ_nested_rule>(rdb);
  add_rule_impl<collapseZ_to_A_rule>(rdb);
  return rdb;
}

struct LoopSummary {
  int count = 0;
  std::set<std::string> levels;
};

LoopSummary summarize_loop_levels(const digraph &gr) {
  LoopSummary summary;
  const ruleSet rules = extract_rules(gr.get_all_vertices());
  for(ruleSet::const_iterator ri = rules.begin(); ri != rules.end(); ++ri) {
    if(ri->type() == rule::INTERNAL && ri->qualifier() == "looping") {
      ++summary.count;
      summary.levels.insert(ri->target_time().level_name());
    }
  }
  return summary;
}

ruleSet collect_rules_of_type(const digraph &gr, rule::rule_type type) {
  ruleSet selected;
  const ruleSet all = extract_rules(gr.get_all_vertices());
  for(ruleSet::const_iterator ri = all.begin(); ri != all.end(); ++ri) {
    if(ri->type() == type) {
      selected += *ri;
    }
  }
  return selected;
}

ruleSet collect_internal_rules_by_qualifier(const digraph &gr,
                                            const std::string &qualifier) {
  ruleSet selected;
  const ruleSet all = extract_rules(gr.get_all_vertices());
  for(ruleSet::const_iterator ri = all.begin(); ri != all.end(); ++ri) {
    if(ri->type() == rule::INTERNAL && ri->qualifier() == qualifier) {
      selected += *ri;
    }
  }
  return selected;
}

void add_rule_to_graph(digraph &gr, const rule &r) {
  gr.add_edges(r.sources(), r.ident());
  gr.add_edges(r.ident(), r.targets());
}

bool varset_has_name_at_level(const variableSet &vars,
                              const std::string &name,
                              const std::string &level) {
  for(variableSet::const_iterator vi = vars.begin(); vi != vars.end(); ++vi) {
    if(vi->get_info().name == name && vi->time().level_name() == level) {
      return true;
    }
  }
  return false;
}

bool rule_targets_name(const rule &r, const std::string &name) {
  for(variableSet::const_iterator vi = r.targets().begin();
      vi != r.targets().end();
      ++vi) {
    if(vi->get_info().name == name) {
      return true;
    }
  }
  return false;
}

} // namespace

TEST_CASE("dependency_graph2 partitions conflicting collapse conditionals") {
  const rule_db rdb = make_two_independent_iterations(false);
  const variableSet given = make_given(false);
  const variableSet target = make_target();

  const digraph gr = dependency_graph2(rdb, given, target).get_graph();
  REQUIRE(gr.get_target_vertices() != EMPTY);

  const LoopSummary loops = summarize_loop_levels(gr);
  CHECK(loops.count == 2);
  CHECK(loops.levels.size() == 2);
  CHECK(loops.levels.count("it") == 0);

  for(std::set<std::string>::const_iterator li = loops.levels.begin();
      li != loops.levels.end();
      ++li) {
    CHECK(li->find("_it_") != std::string::npos);
  }

  const ruleSet collapses = collect_rules_of_type(gr, rule::COLLAPSE);
  CHECK(collapses.size() == 2);
  for(ruleSet::const_iterator ri = collapses.begin(); ri != collapses.end(); ++ri) {
    CHECK(ri->source_time().level_name() != "it");
    CHECK(loops.levels.count(ri->source_time().level_name()) == 1);
  }
}

TEST_CASE("dependency_graph2 does not partition when collapse conditionals match") {
  const rule_db rdb = make_two_independent_iterations(true);
  const variableSet given = make_given(true);
  const variableSet target = make_target();

  const digraph gr = dependency_graph2(rdb, given, target).get_graph();
  REQUIRE(gr.get_target_vertices() != EMPTY);

  const LoopSummary loops = summarize_loop_levels(gr);
  CHECK(loops.count == 1);
  CHECK(loops.levels.size() == 1);
  CHECK(loops.levels.count("it") == 1);

  const ruleSet collapses = collect_rules_of_type(gr, rule::COLLAPSE);
  CHECK(collapses.size() == 2);
  for(ruleSet::const_iterator ri = collapses.begin(); ri != collapses.end(); ++ri) {
    CHECK(ri->source_time().level_name() == "it");
  }
}

TEST_CASE("dependency_graph2 inserts promote rules for static iteration sources") {
  const rule_db rdb = make_iteration_with_promote_candidate();
  const variableSet given = make_promote_case_given();
  const variableSet target = make_promote_case_target();

  const digraph gr = dependency_graph2(rdb, given, target).get_graph();
  REQUIRE(gr.get_target_vertices() != EMPTY);

  bool found_promote = false;
  const ruleSet promote_rules = collect_internal_rules_by_qualifier(gr, "promote");
  for(ruleSet::const_iterator ri = promote_rules.begin(); ri != promote_rules.end(); ++ri) {
    for(variableSet::const_iterator si = ri->sources().begin();
        si != ri->sources().end();
        ++si) {
      for(variableSet::const_iterator ti = ri->targets().begin();
          ti != ri->targets().end();
          ++ti) {
        if(si->get_info().name == "S" &&
           si->time() == time_ident() &&
           ti->get_info().name == "S" &&
           ti->time().level_name() == "it") {
          found_promote = true;
        }
      }
    }
  }
  CHECK(found_promote);
}

TEST_CASE("dependency_graph2 inserts generalize rules for build assignments") {
  const rule_db rdb = make_iteration_with_promote_candidate();
  const variableSet given = make_promote_case_given();
  const variableSet target = make_promote_case_target();

  const digraph gr = dependency_graph2(rdb, given, target).get_graph();
  REQUIRE(gr.get_target_vertices() != EMPTY);

  bool found_generalize = false;
  const ruleSet generalize_rules = collect_internal_rules_by_qualifier(gr, "generalize");
  for(ruleSet::const_iterator ri = generalize_rules.begin();
      ri != generalize_rules.end();
      ++ri) {
    for(variableSet::const_iterator si = ri->sources().begin();
        si != ri->sources().end();
        ++si) {
      for(variableSet::const_iterator ti = ri->targets().begin();
          ti != ri->targets().end();
          ++ti) {
        if(si->get_info().name == "A" &&
           si->time().level_name() == "it" &&
           si->get_info().assign &&
           ti->get_info().name == "A" &&
           ti->time().level_name() == "it" &&
           !ti->get_info().assign) {
          found_generalize = true;
        }
      }
    }
  }
  CHECK(found_generalize);
}

TEST_CASE("dependency_graph2 returns empty graph for malformed iteration missing build") {
  const rule_db rdb = make_iteration_missing_build();
  variableSet given;
  given += variable("done");
  const variableSet target = make_promote_case_target();

  const digraph gr = dependency_graph2(rdb, given, target).get_graph();
  CHECK(gr.get_target_vertices() == EMPTY);
}

TEST_CASE("dependency_graph2 returns empty graph for malformed iteration missing collapse") {
  const rule_db rdb = make_iteration_missing_collapse();
  variableSet given;
  given += variable("initA");
  const variableSet target = make_time_target_A_next();

  const digraph gr = dependency_graph2(rdb, given, target).get_graph();
  CHECK(gr.get_target_vertices() == EMPTY);
}

TEST_CASE("dependency_graph2 returns empty graph when target is not inferable") {
  rule_db rdb;
  variableSet given;
  given += variable("initA");
  variableSet target;
  target += variable("never_produced");

  const digraph gr = dependency_graph2(rdb, given, target).get_graph();
  CHECK(gr.get_target_vertices() == EMPTY);
}

TEST_CASE("dependency_graph2 keeps outer conflict unsplit when nested iteration is involved") {
  const rule_db rdb = make_conflicting_iteration_with_nested_loop();
  const variableSet given = make_given(false);
  const variableSet target = make_target();

  const digraph gr = dependency_graph2(rdb, given, target).get_graph();
  REQUIRE(gr.get_target_vertices() != EMPTY);

  const LoopSummary loops = summarize_loop_levels(gr);
  CHECK(loops.levels.count("it") == 1);
  for(std::set<std::string>::const_iterator li = loops.levels.begin();
      li != loops.levels.end();
      ++li) {
    CHECK(li->find("_it_") == std::string::npos);
  }

  const ruleSet collapses = collect_rules_of_type(gr, rule::COLLAPSE);
  bool checked_outer_collapses = false;
  for(ruleSet::const_iterator ri = collapses.begin(); ri != collapses.end(); ++ri) {
    if(rule_targets_name(*ri, "Aout") || rule_targets_name(*ri, "Bout")) {
      checked_outer_collapses = true;
      CHECK(ri->source_time().level_name() == "it");
    }
  }
  CHECK(checked_outer_collapses);
}

TEST_CASE("clean_graph prunes disconnected rules outside given->target component") {
  digraph gr;
  const rule r_live1("source(g),target(m),qualifier(live1)");
  const rule r_live2("source(m),target(t),qualifier(live2)");
  const rule r_dead("source(x),target(y),qualifier(dead)");
  add_rule_to_graph(gr, r_live1);
  add_rule_to_graph(gr, r_live2);
  add_rule_to_graph(gr, r_dead);

  variableSet given;
  given += variable("g");
  variableSet target;
  target += variable("t");

  clean_graph(gr, given, target);

  const ruleSet remaining = extract_rules(gr.get_all_vertices());
  CHECK(remaining.inSet(r_live1));
  CHECK(remaining.inSet(r_live2));
  CHECK(!remaining.inSet(r_dead));
}

TEST_CASE("clean_graph restructures looping rule by dropping unused source variables") {
  digraph gr;
  const rule r_adv("source(A{it}),target(A{it+1}),qualifier(adv)");
  const rule r_loop("source(A{it+1},X{it}),target(A{it}),qualifier(looping)");
  const rule r_col("source(A{it},done),target(out),qualifier(col)");
  add_rule_to_graph(gr, r_adv);
  add_rule_to_graph(gr, r_loop);
  add_rule_to_graph(gr, r_col);

  variableSet given;
  given += variable("A{it+1}");
  given += variable("done");
  variableSet target;
  target += variable("out");

  clean_graph(gr, given, target);

  const ruleSet loops = collect_internal_rules_by_qualifier(gr, "looping");
  REQUIRE(loops.size() == 1);
  const rule loop = *loops.begin();
  CHECK(varset_has_name_at_level(loop.sources(), "A", "it"));
  CHECK(!varset_has_name_at_level(loop.sources(), "X", "it"));
}
