/*******************************************************************\

Module: Jimple Language

Author: Rafael Sá Menezes, rafael.sa.menezes@outlook.com

\*******************************************************************/

#pragma once

#include <langapi/mode.h>
#include <util/language.h>

class jimple_language : public languaget
{
public:
  bool parse(const std::string &path, const messaget &msg) override;

  bool final(contextt &context, const messaget &msg) override;

  bool typecheck(
    contextt &context,
    const std::string &module,
    const messaget &msg) override;

  std::string id() const override
  {
    return "jimple_lang";
  }

  void show_parse(std::ostream &out) override;

  // conversion from expression into string
  bool from_expr(const exprt &expr, std::string &code, const namespacet &ns)
    override;

  // conversion from type into string
  bool from_type(const typet &type, std::string &code, const namespacet &ns)
    override;

  languaget *new_language(const messaget &msg) override
  {
    return new jimple_language(msg);
  }

  explicit jimple_language(const messaget &msg);

  // store AST json in nlohmann::json data structure
  //nlohmann::json ast_json;
  //nlohmann::json intrinsic_json;
  //void print_json(const nlohmann::json &json_in);
};
