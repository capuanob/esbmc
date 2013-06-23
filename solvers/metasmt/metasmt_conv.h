#ifndef _ESBMC_SOLVERS_METASMT_MESTASMT_CONV_H_
#define _ESBMC_SOLVERS_METASMT_MESTASMT_CONV_H_

#include <solvers/smt/smt_conv.h>

#include <metaSMT/DirectSolver_Context.hpp>
#include <metaSMT/frontend/Logic.hpp>
#include <metaSMT/API/Assertion.hpp>
#include <metaSMT/Instantiate.hpp>
#include <metaSMT/backend/Z3_Backend.hpp>

// Crazyness: the desired solver is selected by some template meta programming.
// Therefore we have to statically know what solver we want to be using. To do
// that, we use the following war-def
typedef metaSMT::DirectSolver_Context< metaSMT::solver::Z3_Backend > solvertype;
// Which defines our solvertype as being a Z3 solver.

// copy+paste directly from the metaSMT documentation:
struct Lookup {
  typedef std::unordered_map<unsigned, std::string, std::hash<unsigned> >symmap;
  symmap &map_;

  Lookup(symmap &map)
    : map_(map) {}

  std::string operator()(unsigned id) {
    return map_[id];
  }

  void insert(metaSMT::logic::predicate p, std::string const &name) {
    map_.insert(std::make_pair(boost::proto::value(p).id, name));
  }
};

class metasmt_smt_sort : public smt_sort {
public:
  // Only three kinds of sorts supported: bools, bv's and arrays. Only
  // additional desireable information is the width.
  metasmt_smt_sort(smt_sort_kind i, unsigned int _width = 0)
    : smt_sort(i), width(_width)
  { }
  virtual ~metasmt_smt_sort() { }
  unsigned int width; // bv width
  unsigned int arrdom_width, arrrange_width; // arr sort widths
};

class metasmt_convt : public smt_convt
{
public:
  metasmt_convt(bool int_encoding, bool is_cpp, const namespacet &ns);
  virtual ~metasmt_convt();

  virtual void set_to(const expr2tc &expr, bool value);
  virtual resultt dec_solve();

  virtual expr2tc get(const expr2tc &expr);

  virtual tvt l_get(literalt a);
  virtual const std::string solver_text();

  virtual void assert_lit(const literalt &l);
  virtual smt_ast *mk_func_app(const smt_sort *s, smt_func_kind k,
                               const smt_ast **args, unsigned int numargs);
  virtual smt_sort *mk_sort(const smt_sort_kind k, ...);
  virtual literalt mk_lit(const smt_ast *s);
  virtual smt_ast *mk_smt_int(const mp_integer &theint, bool sign);
  virtual smt_ast *mk_smt_real(const std::string &str);
  virtual smt_ast *mk_smt_bvint(const mp_integer &theint, bool sign,
                                unsigned int w);
  virtual smt_ast *mk_smt_bool(bool val);
  virtual smt_ast *mk_smt_symbol(const std::string &name, const smt_sort *s);
  virtual smt_sort *mk_struct_sort(const type2tc &type) ;
  virtual smt_sort *mk_union_sort(const type2tc &type);
  virtual smt_ast *mk_extract(const smt_ast *a, unsigned int high,
                              unsigned int low, const smt_sort *s);

  // Members
  solvertype ctx;
  Lookup::symmap symbols;
  Lookup sym_lookup;
};

#endif
