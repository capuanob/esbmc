/*******************************************************************\

   Module:

   Author: Lucas Cordeiro, lcc08r@ecs.soton.ac.uk

\*******************************************************************/

#include <irep2.h>
#include <migrate.h>
#include "execution_state.h"
#include "reachability_tree.h"
#include <string>
#include <sstream>
#include <vector>
#include <i2string.h>
#include <string2array.h>
#include <std_expr.h>
#include <expr_util.h>
#include "../ansi-c/c_types.h"
#include <simplify_expr.h>
#include "config.h"

unsigned int execution_statet::node_count = 0;

execution_statet::execution_statet(const goto_functionst &goto_functions,
                                   const namespacet &ns,
                                   reachability_treet *art,
                                   symex_targett *_target,
                                   contextt &context,
                                   ex_state_level2t *l2init,
                                   const optionst &options) :
  goto_symext(ns, context, goto_functions, _target, options),
  owning_rt(art),
  state_level2(l2init)
{

  // XXXjmorse - C++s static initialization order trainwreck means
  // we can't initialize the id -> serializer map statically. Instead,
  // manually inspect and initialize. This is not thread safe.
  if (!execution_statet::expr_id_map_initialized) {
    execution_statet::expr_id_map_initialized = true;
    execution_statet::expr_id_map = init_expr_id_map();
  }

  CS_number = 0;
  TS_number = 0;
  node_id = 0;
  guard_execution = "execution_statet::\\guard_exec";
  interleaving_unviable = false;

  goto_functionst::function_mapt::const_iterator it =
    goto_functions.function_map.find("main");
  if (it == goto_functions.function_map.end())
    throw "main symbol not found; please set an entry point";

  const goto_programt *goto_program = &(it->second.body);

  // Initialize initial thread state
  goto_symex_statet state(*state_level2, global_value_set, ns);
  state.initialize((*goto_program).instructions.begin(),
             (*goto_program).instructions.end(),
             goto_program, 0);

  threads_state.push_back(state);
  cur_state = &threads_state.front();

  atomic_numbers.push_back(0);

  if (DFS_traversed.size() <= state.source.thread_nr) {
    DFS_traversed.push_back(false);
  } else {
    DFS_traversed[state.source.thread_nr] = false;
  }

  thread_start_data.push_back(expr2tc());

  // Initial mpor tracking.
  thread_last_reads.push_back(std::set<expr2tc>());
  thread_last_writes.push_back(std::set<expr2tc>());
  // One thread with one dependancy relation.
  dependancy_chain.push_back(std::vector<int>());
  dependancy_chain.back().push_back(0);
  mpor_schedulable.push_back(true);

  cswitch_forced = false;
  active_thread = 0;
  last_active_thread = 0;
  node_count = 0;
  nondet_count = 0;
  dynamic_counter = 0;
  DFS_traversed.reserve(1);
  DFS_traversed[0] = false;
}

execution_statet::execution_statet(const execution_statet &ex) :
  goto_symext(ex),
  owning_rt(ex.owning_rt),
  state_level2(ex.state_level2->clone())
{

  *this = ex;

  // Regenerate threads state using new objects state_level2 ref
  threads_state.clear();
  std::vector<goto_symex_statet>::const_iterator it;
  for (it = ex.threads_state.begin(); it != ex.threads_state.end(); it++) {
    goto_symex_statet state(*it, *state_level2, global_value_set);
    threads_state.push_back(state);
  }

  // Reassign which state is currently being worked on.
  cur_state = &threads_state[active_thread];
}

execution_statet&
execution_statet::operator=(const execution_statet &ex)
{
  // Don't copy level2, copy cons it in execution_statet(ref)
  //state_level2 = ex.state_level2;

  threads_state = ex.threads_state;
  atomic_numbers = ex.atomic_numbers;
  DFS_traversed = ex.DFS_traversed;
  thread_start_data = ex.thread_start_data;
  last_active_thread = ex.last_active_thread;
  active_thread = ex.active_thread;
  guard_execution = ex.guard_execution;
  nondet_count = ex.nondet_count;
  dynamic_counter = ex.dynamic_counter;
  node_id = ex.node_id;
  global_value_set = ex.global_value_set;
  interleaving_unviable = ex.interleaving_unviable;

  CS_number = ex.CS_number;
  TS_number = ex.TS_number;

  thread_last_reads = ex.thread_last_reads;
  thread_last_writes = ex.thread_last_writes;
  dependancy_chain = ex.dependancy_chain;
  mpor_schedulable = ex.mpor_schedulable;
  cswitch_forced = ex.cswitch_forced;

  // Vastly irritatingly, we have to iterate through existing level2t objects
  // updating their ex_state references. There isn't an elegant way of updating
  // them, it seems, while keeping the symex stuff ignorant of ex_state.
  // Oooooo, so this is where auto types would be useful...
  for (std::vector<goto_symex_statet>::iterator it = threads_state.begin();
       it != threads_state.end(); it++) {
    for (goto_symex_statet::call_stackt::iterator it2 = it->call_stack.begin();
         it2 != it->call_stack.end(); it2++) {
      for (goto_symex_statet::goto_state_mapt::iterator it3 = it2->goto_state_map.begin();
           it3 != it2->goto_state_map.end(); it3++) {
        for (goto_symex_statet::goto_state_listt::iterator it4 = it3->second.begin();
             it4 != it3->second.begin(); it4++) {
          ex_state_level2t &l2 = dynamic_cast<ex_state_level2t&>(it4->level2);
          l2.owner = this;
        }
      }
    }
  }

  state_level2->owner = this;

  return *this;
}

execution_statet::~execution_statet()
{
  delete state_level2;
};

void
execution_statet::symex_step(reachability_treet &art)
{

  statet &state = get_active_state();
  const goto_programt::instructiont &instruction = *state.source.pc;

  merge_gotos();

  if (config.options.get_option("break-at") != "") {
    unsigned int insn_num = strtol(config.options.get_option("break-at").c_str(), NULL, 10);
    if (instruction.location_number == insn_num) {
      // If you're developing ESBMC on a machine that isn't x86, I'll send you
      // cookies.
#ifndef _WIN32
      __asm__("int $3");
#else
      std::cerr << "Can't trap on windows, sorry" << std::endl;
      abort();
#endif
    }
  }

  if (options.get_bool_option("symex-trace")) {
    const goto_programt p_dummy;
    goto_functions_templatet<goto_programt>::function_mapt::const_iterator it =
      goto_functions.function_map.find(instruction.function);

    const goto_programt &p_real = it->second.body;
    const goto_programt &p = (it == goto_functions.function_map.end()) ? p_dummy : p_real;
    p.output_instruction(ns, "", std::cout, state.source.pc, false, false);
  }

  switch (instruction.type) {
    case END_FUNCTION:
      if (instruction.function == "main") {
        end_thread();
        force_cswitch();
      } else {
        // Fall through to base class
        goto_symext::symex_step(art);
      }
      break;
    case ATOMIC_BEGIN:
      state.source.pc++;
      increment_active_atomic_number();
      break;
    case ATOMIC_END:
      decrement_active_atomic_number();
      state.source.pc++;

      // Don't context switch if the guard is false. This instruction hasn't
      // actually been executed, so context switching achieves nothing. (We
      // don't do this for the active_atomic_number though, because it's cheap,
      // and should be balanced under all circumstances anyway).
      if (!state.guard.is_false())
        force_cswitch();

      break;
    case RETURN:
      state.source.pc++;
      if(!state.guard.is_false()) {
        expr2tc thecode = instruction.code, assign;
        if (make_return_assignment(assign, thecode)) {
          goto_symext::symex_assign(assign);
        }

        symex_return();

        if (!is_nil_expr(assign))
          analyze_assign(assign);
      }
      break;
    default:
      goto_symext::symex_step(art);
  }

  return;
}

void
execution_statet::symex_assign(const expr2tc &code)
{

  goto_symext::symex_assign(code);

  if (threads_state.size() > 1)
    analyze_assign(code);

  return;
}

void
execution_statet::claim(const expr2tc &expr, const std::string &msg)
{

  goto_symext::claim(expr, msg);

  if (threads_state.size() > 1)
    analyze_read(expr);

  return;
}

void
execution_statet::symex_goto(const expr2tc &old_guard)
{

  goto_symext::symex_goto(old_guard);

  if (!is_nil_expr(old_guard)) {
    if (threads_state.size() > 1)
      analyze_read(old_guard);
  }

  return;
}

void
execution_statet::assume(const expr2tc &assumption)
{

  goto_symext::assume(assumption);

  if (threads_state.size() > 1)
    analyze_read(assumption);

  return;
}

unsigned int &
execution_statet::get_dynamic_counter(void)
{

  return dynamic_counter;
}

unsigned int &
execution_statet::get_nondet_counter(void)
{

  return nondet_count;
}

goto_symex_statet &
execution_statet::get_active_state() {

  return threads_state.at(active_thread);
}

const goto_symex_statet &
execution_statet::get_active_state() const
{
  return threads_state.at(active_thread);
}

unsigned int
execution_statet::get_active_atomic_number()
{

  return atomic_numbers.at(active_thread);
}

void
execution_statet::increment_active_atomic_number()
{

  atomic_numbers.at(active_thread)++;
}

void
execution_statet::decrement_active_atomic_number()
{

  atomic_numbers.at(active_thread)--;
}

expr2tc
execution_statet::get_guard_identifier()
{

  return expr2tc(new symbol2t(type_pool.get_bool(), guard_execution,
                              symbol2t::level1, CS_number, 0, node_id, 0));
}

void
execution_statet::switch_to_thread(unsigned int i)
{

  assert(i != active_thread);

  last_active_thread = active_thread;
  active_thread = i;
  cur_state = &threads_state[active_thread];
}

bool
execution_statet::dfs_explore_thread(unsigned int tid)
{

    if(DFS_traversed.at(tid))
      return false;

    if(threads_state.at(tid).call_stack.empty())
      return false;

    if(threads_state.at(tid).thread_ended)
      return false;

    DFS_traversed.at(tid) = true;
    return true;
}

bool
execution_statet::check_if_ileaves_blocked(void)
{

  if(owning_rt->get_CS_bound() != -1 && CS_number >= owning_rt->get_CS_bound())
    return true;

  if (get_active_atomic_number() > 0)
    return true;

  if (owning_rt->directed_interleavings)
    // Don't generate interleavings automatically - instead, the user will
    // inserts intrinsics identifying where they want interleavings to occur,
    // and to what thread.
    return true;

  if(threads_state.size() < 2)
    return true;

  return false;
}

void
execution_statet::end_thread(void)
{

  get_active_state().thread_ended = true;
  // If ending in an atomic block, the switcher fails to switch to another
  // live thread (because it's trying to be atomic). So, disable atomic blocks
  // when the thread ends.
  atomic_numbers[active_thread] = 0;
}

void
execution_statet::update_after_switch_point(void)
{

  execute_guard();
  resetDFS_traversed();

  // MPOR records the variables accessed in last transition taken; we're
  // starting a new transition, so for the current thread, clear records.
  thread_last_reads[active_thread].clear();
  thread_last_writes[active_thread].clear();

  cswitch_forced = false;
}

bool
execution_statet::is_cur_state_guard_false(void)
{

  // So, can the assumption actually be true? If enabled, ask the solver.
  if (options.get_bool_option("smt-thread-guard")) {
    expr2tc parent_guard = threads_state[active_thread].guard.as_expr();

    runtime_encoded_equationt *rte = dynamic_cast<runtime_encoded_equationt*>
                                                 (target);

    expr2tc the_question(new equality2t(true_expr, parent_guard));

    tvt res = rte->ask_solver_question(the_question);
    if (res.is_false())
      return true;
  }

  return false;
}

void
execution_statet::execute_guard(void)
{

  node_id = node_count++;
  expr2tc guard_expr = get_guard_identifier();
  exprt new_rhs, const_prop_val;
  expr2tc parent_guard;

  parent_guard = threads_state[last_active_thread].guard.as_expr();

  // Rename value, allows its use in other renamed exprs
  state_level2->make_assignment(guard_expr, expr2tc(), expr2tc());

  // Truth of this guard implies the parent is true.
  state_level2->rename(parent_guard);
  do_simplify(parent_guard);
  expr2tc assumpt = expr2tc(new implies2t(guard_expr, parent_guard));

  target->assumption(guardt().as_expr(), assumpt, get_active_state().source);

  guardt old_guard;
  old_guard.add(threads_state[last_active_thread].guard.as_expr());

  // If we simplified the global guard expr to false, write that to thread
  // guards, not the symbolic guard name. This is the only way to bail out of
  // evaulating a particular interleaving early right now.
  if (is_false(parent_guard))
    guard_expr = parent_guard;

  // copy the new guard exprt to every threads
  for (unsigned int i = 0; i < threads_state.size(); i++)
  {
    // remove the old guard first
    threads_state.at(i).guard -= old_guard;
    threads_state.at(i).guard.add(guard_expr);
  }

  // Finally, if we've determined execution from here on is unviable, then
  // mark this path as unviable.
  if (is_cur_state_guard_false())
    interleaving_unviable = true;
}

unsigned int
execution_statet::add_thread(const goto_programt *prog)
{

  goto_symex_statet new_state(*state_level2, global_value_set, ns);
  new_state.initialize(prog->instructions.begin(), prog->instructions.end(),
                      prog, threads_state.size());

  new_state.source.thread_nr = threads_state.size();
  threads_state.push_back(new_state);
  atomic_numbers.push_back(0);

  if (DFS_traversed.size() <= new_state.source.thread_nr) {
    DFS_traversed.push_back(false);
  } else {
    DFS_traversed[new_state.source.thread_nr] = false;
  }

  thread_start_data.push_back(expr2tc());

  // We invalidated all threads_state refs, so reset cur_state ptr.
  cur_state = &threads_state[active_thread];

  // Update MPOR tracking data with newly initialized thread
  thread_last_reads.push_back(std::set<expr2tc>());
  thread_last_writes.push_back(std::set<expr2tc>());
  // Unfortunately as each thread has a depenancy relation with every other
  // thread we have to do a lot of work to initialize a new one. And initially
  // all relations are '0', no transitions yet.
  for (std::vector<std::vector<int> >::iterator it = dependancy_chain.begin();
       it != dependancy_chain.end(); it++) {
    it->push_back(0);
  }
  // And the new threads dependancies,
  dependancy_chain.push_back(std::vector<int>());
  for (unsigned int i = 0; i < dependancy_chain.size(); i++)
    dependancy_chain.back().push_back(0);

  mpor_schedulable.push_back(true); // Has highest TID, so always schedulable.

  return threads_state.size() - 1; // thread ID, zero based
}

void
execution_statet::analyze_assign(const expr2tc &code)
{

  std::set<expr2tc> global_reads, global_writes;
  const code_assign2t &assign = to_code_assign2t(code);
  get_expr_globals(ns, assign.target, global_writes);
  get_expr_globals(ns, assign.source, global_reads);

  if (global_reads.size() > 0 || global_writes.size() > 0) {
    // Record read/written data
    thread_last_reads[active_thread].insert(global_reads.begin(),
                                            global_reads.end());
    thread_last_writes[active_thread].insert(global_writes.begin(),
                                             global_writes.end());
  }

  return;
}

void
execution_statet::analyze_read(const expr2tc &code)
{

  std::set<expr2tc> global_reads, global_writes;
  get_expr_globals(ns, code, global_reads);

  if (global_reads.size() > 0) {
    // Record read/written data
    thread_last_reads[active_thread].insert(global_reads.begin(),
                                            global_reads.end());
  }

  return;
}

void
execution_statet::get_expr_globals(const namespacet &ns, const expr2tc &expr,
                                   std::set<expr2tc> &globals_list)
{

  if (is_address_of2t(expr) || is_pointer_object2t(expr) ||
      is_dynamic_size2t(expr) || is_zero_string2t(expr) ||
      is_zero_length_string2t(expr)) {
    return;
  } else if (is_symbol2t(expr)) {
    expr2tc newexpr = expr;
    get_active_state().get_original_name(newexpr);
    const std::string &name = to_symbol2t(newexpr).thename.as_string();

    if (name == "goto_symex::\\guard!" +
        i2string(get_active_state().top().level1.thread_id))
      return;

    const symbolt *symbol;
    if (ns.lookup(name, symbol))
      return;

    if (name == "c::__ESBMC_alloc" || name == "c::__ESBMC_alloc_size" ||
        name == "c::__ESBMC_is_dynamic") {
      return;
    } else if ((symbol->static_lifetime || symbol->type.is_dynamic_set())) {
      globals_list.insert(expr);
    } else {
      return;
    }
  }

  forall_operands2(it, op_list, expr) {
    get_expr_globals(ns, **it, globals_list);
  }
}

bool
execution_statet::check_mpor_dependancy(unsigned int j, unsigned int l) const
{

  assert(j < threads_state.size());
  assert(l < threads_state.size());

  // Rules given on page 13 of MPOR paper, although they don't appear to
  // distinguish which thread is which correctly. Essentially, check that
  // the write(s) of the previous transition (l) don't intersect with this
  // transitions (j) reads or writes; and that the previous transitions reads
  // don't intersect with this transitions write(s).

  // Double write intersection
  for (std::set<expr2tc>::const_iterator it = thread_last_writes[j].begin();
       it != thread_last_writes[j].end(); it++)
    if (thread_last_writes[l].find(*it) != thread_last_writes[l].end())
      return true;

  // This read what that wrote intersection
  for (std::set<expr2tc>::const_iterator it = thread_last_reads[j].begin();
       it != thread_last_reads[j].end(); it++)
    if (thread_last_writes[l].find(*it) != thread_last_writes[l].end())
      return true;

  // We wrote what that reads intersection
  for (std::set<expr2tc>::const_iterator it = thread_last_writes[j].begin();
       it != thread_last_writes[j].end(); it++)
    if (thread_last_reads[l].find(*it) != thread_last_reads[l].end())
      return true;

  // No check for read-read intersection, it doesn't affect anything
  return false;
}

void
execution_statet::calculate_mpor_constraints(void)
{

  std::vector<std::vector<int> > new_dep_chain = dependancy_chain;
  // Primary bit of MPOR logic - to be executed at the end of a transition to
  // update dependancy tracking and suchlike.

  // MPOR paper, page 12, create new dependancy chain record for this time step.

  // Start new dependancy chain for this thread
  for (unsigned int i = 0; i < new_dep_chain.size(); i++)
    new_dep_chain[active_thread][i] = -1;

  // This thread depends on this thread.
  new_dep_chain[active_thread][active_thread] = 1;

  // Mark un-run threads as continuing to be un-run. Otherwise, look for a
  // dependancy chain from each thread to the run thread.
  for (unsigned int j = 0; j < new_dep_chain.size(); j++) {
    if (j == active_thread)
      continue;

    if (dependancy_chain[j][active_thread] == 0) {
      // This thread hasn't been run; continue not having been run.
      new_dep_chain[j][active_thread] = 0;
    } else {
      // This is where the beef is. If there is any other thread (including
      // the active thread) that we depend on, that depends on the active
      // thread, then record a dependancy.
      // A direct dependancy occurs when l = j, as DCjj always = 1, and DEPji
      // is true.
      int res = 0;

      for (unsigned int l = 0; l < new_dep_chain.size(); l++) {
        if (dependancy_chain[j][l] != 1)
          continue; // No dependancy relation here

        // Now check for variable dependancy.
        if (!check_mpor_dependancy(active_thread, l))
          continue;

        res = 1;
        break;
      }

      // Don't overwrite if no match
      if (res != 0)
        new_dep_chain[j][active_thread] = res;
    }
  }

  // For /all other relations/, just propagate the dependancy it already has.
  // Achieved by initial duplication of dependancy_chain.

  // Voila, new dependancy chain.

  // Calculations of what threads are runnable. No need to consider first case,
  // because we always start with one thread, and it always starts without
  // considering POR.
  for (unsigned int i = 0; i < threads_state.size(); i++) {
    bool can_run = true;
    for (unsigned int j = i + 1; j < threads_state.size(); j++) {
      if (new_dep_chain[j][i] != -1)
        // Either no higher threads have been run, or a dependancy relation in
        // a higher thread justifies our out-of-order execution.
        continue;

      // Search for a dependancy chain in a lower thread that links us back to
      // a higher thread, justifying this order.
      bool dep_exists = false;
      for (unsigned int l = 0; l < i; l++) {
        if (dependancy_chain[j][l] == 1)
          dep_exists = true;
      }

      if (!dep_exists) {
        can_run = false;
        break;
      }
    }

    mpor_schedulable[i] = can_run;
  }

  dependancy_chain = new_dep_chain;
}

bool
execution_statet::has_cswitch_point_occured(void) const
{

  // Context switches can occur due to being forced, or by global state access

  if (cswitch_forced)
    return true;

  if (thread_last_reads[active_thread].size() != 0 ||
      thread_last_writes[active_thread].size() != 0)
    return true;

  return false;
}

bool
execution_statet::can_execution_continue(void) const
{

  if (threads_state[active_thread].thread_ended)
    return false;

  if (threads_state[active_thread].call_stack.empty())
    return false;

  return true;
}

crypto_hash
execution_statet::generate_hash(void) const
{

  state_hashing_level2t *l2 =dynamic_cast<state_hashing_level2t*>(state_level2);
  assert(l2 != NULL);
  crypto_hash state = l2->generate_l2_state_hash();
  std::string str = state.to_string();

  for (std::vector<goto_symex_statet>::const_iterator it = threads_state.begin();
       it != threads_state.end(); it++) {
    goto_programt::const_targett pc = it->source.pc;
    int id = pc->location_number;
    std::stringstream s;
    s << id;
    str += "!" + s.str();
  }

  crypto_hash h = crypto_hash(str);

  return h;
}

static std::string state_to_ignore[8] =
{
  "\\guard", "trds_count", "trds_in_run", "deadlock_wait", "deadlock_mutex",
  "count_lock", "count_wait", "unlocked"
};

std::string
execution_statet::serialise_expr(const exprt &rhs __attribute__((unused)))
{
  // FIXME: some way to disambiguate what's part of a hash / const /whatever,
  // and what's part of an operator

  // The plan: serialise this expression into the identifiers of its operations,
  // replacing symbol names with the hash of their value.
  std::cerr << "Serialise expr is a victim of string migration" << std::endl;
  abort();
#if 0

  std::string str;
  uint64_t val;
  int i;

  if (rhs.id() == exprt::symbol) {

    str = rhs.identifier().as_string();
    for (i = 0; i < 8; i++)
      if (str.find(state_to_ignore[i]) != std::string::npos)
	return "(ignore)";

    // If this is something we've already encountered, use the hash of its
    // value.
    exprt tmp = rhs;
    expr2tc new_tmp;
    migrate_expr(tmp, new_tmp);
    get_active_state().get_original_name(new_tmp);
    tmp = migrate_expr_back(new_tmp);
    if (state_level2->current_hashes.find(tmp.identifier().as_string()) !=
        state_level2->current_hashes.end()) {
      crypto_hash h = state_level2->current_hashes.find(
        tmp.identifier().as_string())->second;
      return "hash(" + h.to_string() + ")";
    }

    /* Otherwise, it's something that's been assumed, or some form of
       nondeterminism. Just return its name. */
    return rhs.identifier().as_string();
  } else if (rhs.id() == exprt::arrayof) {
    /* An array of the same set of values: generate all of them. */
    str = "array(";
    irept array = rhs.type();
    exprt size = (exprt &)array.size_irep();
    str += "sz(" + serialise_expr(size) + "),";
    str += "elem(" + serialise_expr(rhs.op0()) + "))";
  } else if (rhs.id() == exprt::with) {
    exprt rec = rhs;

    if (rec.type().id() == typet::t_array) {
      str = "array(";
      str += "prev(" + serialise_expr(rec.op0()) + "),";
      str += "idx(" + serialise_expr(rec.op1()) + "),";
      str += "val(" + serialise_expr(rec.op2()) + "))";
    } else if (rec.type().id() == typet::t_struct) {
      str = "struct(";
      str += "prev(" + serialise_expr(rec.op0()) + "),";
      str += "member(" + serialise_expr(rec.op1()) + "),";
      str += "val(" + serialise_expr(rec.op2()) + "),";
    } else if (rec.type().id() ==  typet::t_union) {
      /* We don't care about previous assignments to this union, because they're
         overwritten by this one, and leads to undefined side effects anyway.
         So, just serialise the identifier, the member assigned to, and the
         value assigned */
      str = "union_set(";
      str += "union_sym(" + rec.op0().identifier().as_string() + "),";
      str += "field(" + serialise_expr(rec.op1()) + "),";
      str += "val(" + serialise_expr(rec.op2()) + "))";
    } else {
      throw "Unrecognised type of with expression: " +
            rec.op0().type().id().as_string();
    }
  } else if (rhs.id() == exprt::index) {
    str = "index(";
    str += serialise_expr(rhs.op0());
    str += ",idx(" + serialise_expr(rhs.op1()) + ")";
  } else if (rhs.id() == "member_name") {
    str = "component(" + rhs.component_name().as_string() + ")";
  } else if (rhs.id() == exprt::member) {
    str = "member(entity(" + serialise_expr(rhs.op0()) + "),";
    str += "member_name(" + rhs.component_name().as_string() + "))";
  } else if (rhs.id() == "nondet_symbol") {
    /* Just return the identifier: it'll be unique to this particular piece of
       entropy */
    exprt tmp = rhs;
    expr2tc new_tmp;
    migrate_expr(tmp, new_tmp);
    get_active_state().get_original_name(new_tmp);
    tmp = migrate_expr_back(new_tmp);
    str = "nondet_symbol(" + tmp.identifier().as_string() + ")";
  } else if (rhs.id() == exprt::i_if) {
    str = "cond(if(" + serialise_expr(rhs.op0()) + "),";
    str += "then(" + serialise_expr(rhs.op1()) + "),";
    str += "else(" + serialise_expr(rhs.op2()) + "))";
  } else if (rhs.id() == "struct") {
    str = rhs.type().tag().as_string();
    str = "struct(tag(" + str + "),";
    forall_operands(it, rhs) {
      str = str + "(" + serialise_expr(*it) + "),";
    }
    str += ")";
  } else if (rhs.id() == "union") {
    str = rhs.type().tag().as_string();
    str = "union(tag(" + str + "),";
    forall_operands(it, rhs) {
      str = str + "(" + serialise_expr(*it) + "),";
    }
  } else if (rhs.id() == exprt::constant) {
    // It appears constants can be "true", "false", or a bit vector. Parse that,
    // and then print the value as a base 10 integer.

    irep_idt idt_val = rhs.value();
    if (idt_val == exprt::i_true) {
      val = 1;
    } else if (idt_val == exprt::i_false) {
      val = 0;
    } else {
      val = strtol(idt_val.c_str(), NULL, 2);
    }

    std::stringstream tmp;
    tmp << val;
    str = "const(" + tmp.str() + ")";
  } else if (rhs.id() == "pointer_offset") {
    str = "pointer_offset(" + serialise_expr(rhs.op0()) + ")";
  } else if (rhs.id() == "string-constant") {
    exprt tmp;
    string2array(rhs, tmp);
    return serialise_expr(tmp);
  } else if (rhs.id() == "same-object") {
    str = "same-obj((" + serialise_expr(rhs.op0()) + "),(";
    str += serialise_expr(rhs.op1()) + "))";
  } else if (rhs.id() == "byte_update_little_endian") {
    str = "byte_up_le((" + serialise_expr(rhs.op0()) + "),(";
    str += serialise_expr(rhs.op1()) + "))";
  } else if (rhs.id() == "byte_update_big_endian") {
    str = "byte_up_be((" + serialise_expr(rhs.op0()) + "),(";
    str += serialise_expr(rhs.op1()) + "))";
  } else if (rhs.id() == "byte_extract_little_endian") {
    str = "byte_up_le((" + serialise_expr(rhs.op0()) + "),(";
    str += serialise_expr(rhs.op1()) + "),";
    str += serialise_expr(rhs.op2()) + "))";
  } else if (rhs.id() == "byte_extract_big_endian") {
    str = "byte_up_be((" + serialise_expr(rhs.op0()) + "),(";
    str += serialise_expr(rhs.op1()) + "),";
    str += serialise_expr(rhs.op2()) + "))";
  } else if (rhs.id() == "infinity") {
    return "inf";
  } else if (rhs.id() == "nil") {
    return "nil";
  } else {
    execution_statet::expr_id_map_t::const_iterator it;
    it = expr_id_map.find(rhs.id());
    if (it != expr_id_map.end())
      return it->second(*this, rhs);

    std::cout << "Unrecognized expression when generating state hash:\n";
    std::cout << rhs.pretty(0) << std::endl;
    abort();
  }

  return str;
#endif
}

// If we have a normal expression, either arithmatic, binary, comparision,
// or whatever, just take the operator and append its operands.
std::string
serialise_normal_operation(execution_statet &ex_state, const exprt &rhs)
{
  std::string str;

  str = rhs.id().as_string();
  forall_operands(it, rhs) {
    str = str + "(" + ex_state.serialise_expr(*it) + ")";
  }

  return str;
}


crypto_hash
execution_statet::update_hash_for_assignment(const exprt &rhs)
{

  return crypto_hash(serialise_expr(rhs));
}

execution_statet::expr_id_map_t execution_statet::expr_id_map;

execution_statet::expr_id_map_t
execution_statet::init_expr_id_map()
{
  execution_statet::expr_id_map_t m;
  m[exprt::plus] = serialise_normal_operation;
  m[exprt::minus] = serialise_normal_operation;
  m[exprt::mult] = serialise_normal_operation;
  m[exprt::div] = serialise_normal_operation;
  m[exprt::mod] = serialise_normal_operation;
  m[exprt::equality] = serialise_normal_operation;
  m[exprt::implies] = serialise_normal_operation;
  m[exprt::i_and] = serialise_normal_operation;
  m[exprt::i_xor] = serialise_normal_operation;
  m[exprt::i_or] = serialise_normal_operation;
  m[exprt::i_not] = serialise_normal_operation;
  m[exprt::notequal] = serialise_normal_operation;
  m["unary-"] = serialise_normal_operation;
  m["unary+"] = serialise_normal_operation;
  m[exprt::abs] = serialise_normal_operation;
  m[exprt::isnan] = serialise_normal_operation;
  m[exprt::i_ge] = serialise_normal_operation;
  m[exprt::i_gt] = serialise_normal_operation;
  m[exprt::i_le] = serialise_normal_operation;
  m[exprt::i_lt] = serialise_normal_operation;
  m[exprt::i_bitand] = serialise_normal_operation;
  m[exprt::i_bitor] = serialise_normal_operation;
  m[exprt::i_bitxor] = serialise_normal_operation;
  m[exprt::i_bitnand] = serialise_normal_operation;
  m[exprt::i_bitnor] = serialise_normal_operation;
  m[exprt::i_bitnxor] = serialise_normal_operation;
  m[exprt::i_bitnot] = serialise_normal_operation;
  m[exprt::i_shl] = serialise_normal_operation;
  m[exprt::i_lshr] = serialise_normal_operation;
  m[exprt::i_ashr] = serialise_normal_operation;
  m[exprt::typecast] = serialise_normal_operation;
  m[exprt::addrof] = serialise_normal_operation;
  m["pointer_obj"] = serialise_normal_operation;
  m["pointer_object"] = serialise_normal_operation;

  return m;
}

void
execution_statet::print_stack_traces(unsigned int indent) const
{
  std::vector<goto_symex_statet>::const_iterator it;
  std::string spaces = std::string("");
  unsigned int i;

  for (i = 0; i < indent; i++)
    spaces += " ";

  i = 0;
  for (it = threads_state.begin(); it != threads_state.end(); it++) {
    std::cout << spaces << "Thread " << i++ << ":" << std::endl;
    it->print_stack_trace(indent + 2);
    std::cout << std::endl;
  }

  return;
}

bool execution_statet::expr_id_map_initialized = false;

execution_statet::ex_state_level2t::ex_state_level2t(
    execution_statet &ref)
  : renaming::level2t(),
    owner(&ref)
{
}

execution_statet::ex_state_level2t::~ex_state_level2t(void)
{
}

execution_statet::ex_state_level2t *
execution_statet::ex_state_level2t::clone(void) const
{

  return new ex_state_level2t(*this);
}

void
execution_statet::ex_state_level2t::rename(expr2tc &lhs_sym, unsigned count)
{
  renaming::level2t::coveredinbees(lhs_sym, count, owner->node_id);
}

void
execution_statet::ex_state_level2t::rename(expr2tc &identifier)
{
  renaming::level2t::rename(identifier);
}

dfs_execution_statet::~dfs_execution_statet(void)
{

  // Delete target; or if we're encoding at runtime, pop a context.
  if (options.get_bool_option("smt-during-symex"))
    target->pop_ctx();
  else
    delete target;
}

dfs_execution_statet* dfs_execution_statet::clone(void) const
{
  dfs_execution_statet *d;

  d = new dfs_execution_statet(*this);

  // Duplicate target equation; or if we're encoding at runtime, push a context.
  if (options.get_bool_option("smt-during-symex")) {
    d->target = target;
    d->target->push_ctx();
  } else {
    d->target = target->clone();
  }

  return d;
}

dfs_execution_statet::dfs_execution_statet(const dfs_execution_statet &ref)
  :  execution_statet(ref)
{
}

schedule_execution_statet::~schedule_execution_statet(void)
{
  // Don't delete equation. Schedule requires all this data.
}

schedule_execution_statet* schedule_execution_statet::clone(void) const
{
  schedule_execution_statet *s;

  s = new schedule_execution_statet(*this);

  // Don't duplicate target equation.
  s->target = target;
  return s;
}

schedule_execution_statet::schedule_execution_statet(const schedule_execution_statet &ref)
  :  execution_statet(ref),
     ptotal_claims(ref.ptotal_claims),
     premaining_claims(ref.premaining_claims)
{
}

void
schedule_execution_statet::claim(const expr2tc &expr, const std::string &msg)
{
  unsigned int tmp_total, tmp_remaining;

  tmp_total = total_claims;
  tmp_remaining = remaining_claims;

  execution_statet::claim(expr, msg);

  tmp_total = total_claims - tmp_total;
  tmp_remaining = remaining_claims - tmp_remaining;

  *ptotal_claims += tmp_total;
  *premaining_claims += tmp_remaining;
  return;
}

execution_statet::state_hashing_level2t::state_hashing_level2t(
                                         execution_statet &ref)
    : ex_state_level2t(ref)
{
}

execution_statet::state_hashing_level2t::~state_hashing_level2t(void)
{
}

execution_statet::state_hashing_level2t *
execution_statet::state_hashing_level2t::clone(void) const
{

  return new state_hashing_level2t(*this);
}

void
execution_statet::state_hashing_level2t::make_assignment(expr2tc &lhs_sym,
                                       const expr2tc &const_value,
                                       const expr2tc &assigned_value)
{
//  crypto_hash hash;

  renaming::level2t::make_assignment(lhs_sym, const_value, assigned_value);

  // XXX - consider whether to use l1 names instead. Recursion, reentrancy.
#if 0
#warning XXXjmorse - state hashing is a casualty of irep2
  hash = owner->update_hash_for_assignment(assigned_value);
  std::string orig_name =
    owner->get_active_state().get_original_name(l1_ident).as_string();
  current_hashes[orig_name] = hash;
#endif
}

crypto_hash
execution_statet::state_hashing_level2t::generate_l2_state_hash() const
{
  unsigned int total;

  uint8_t *data = (uint8_t*)alloca(current_hashes.size() * CRYPTO_HASH_SIZE * sizeof(uint8_t));

  total = 0;
  for (current_state_hashest::const_iterator it = current_hashes.begin();
        it != current_hashes.end(); it++) {
    int j;

    for (j = 0 ; j < 8; j++)
      if (it->first.as_string().find(state_to_ignore[j]) != std::string::npos)
        continue;

    memcpy(&data[total * CRYPTO_HASH_SIZE], it->second.hash, CRYPTO_HASH_SIZE);
    total++;
  }

  return crypto_hash(data, total * CRYPTO_HASH_SIZE);
}
