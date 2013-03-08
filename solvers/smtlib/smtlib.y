%lex-param { int startsym }
%parse-param { int startsym }
%{
  // A parser for smtlib responses

#include <string>

#include "smtlib_conv.h"

#include "y.tab.hpp"

int smtliblex(int startsym);
int smtliberror(int startsym, const std::string &error);

sexpr *smtlib_output = NULL;
%}

/* Values */
%union {
  char *text;
  sexpr *expr;
  std::string *str;
  std::list<std::string> *str_vec;
  std::list<sexpr> *sexpr_list;
};

/* Some tokens */
%token <text> TOK_LPAREN
%token <text> TOK_RPAREN
%token <text> TOK_NUMERAL
%token <text> TOK_DECIMAL
%token <text> TOK_HEXNUM
%token <text> TOK_BINNUM
%token <text> TOK_STRINGLIT
%token <text> TOK_SIMPLESYM
%token <text> TOK_QUOTEDSYM
%token <text> TOK_KEYWORD
%token <text> TOK_KW_PAR
%token <text> TOK_KW_NUMERAL
%token <text> TOK_KW_DECIMAL
%token <text> TOK_KW_STRING
%token <text> TOK_KW_USCORE
%token <text> TOK_KW_EXCL
%token <text> TOK_KW_AS
%token <text> TOK_KW_LET
%token <text> TOK_KW_FORALL
%token <text> TOK_KW_EXISTS
%token <text> TOK_KW_UNSUPPORTED
%token <text> TOK_KW_SUCCESS
%token <text> TOK_KW_ERROR
%token <text> TOK_KW_IMMEXIT
%token <text> TOK_KW_CONEXECUTION
%token <text> TOK_KW_MEMOUT
%token <text> TOK_KW_INCOMPLETE
%token <text> TOK_KW_SAT
%token <text> TOK_KW_UNSAT
%token <text> TOK_KW_UNKNOWN
%token <text> TOK_KW_TRUE
%token <text> TOK_KW_FALSE

%token <text> TOK_START_GEN
%token <text> TOK_START_INFO
%token <text> TOK_START_SAT
%token <text> TOK_START_ASSERTS
%token <text> TOK_START_UNSATS
%token <text> TOK_START_VALUE
%token <text> TOK_START_ASSIGN
%token <text> TOK_START_OPTION

/* Start token, for the response */
%start response

/* Types */

%type <text> error_behaviour reason_unknown info_response_arg
%type <str> symbol
%type <str_vec> symbol_list_empt numlist
%type <sexpr_list> sexpr_list info_response_list
%type <expr> s_expr spec_constant attribute attribute_value info_response
%type <expr> get_info_response response gen_response status check_sat_response
%type <expr> get_value_response
%%

/* Rules */

response: TOK_START_GEN gen_response
          | TOK_START_INFO get_info_response
          {
            yychar = YYEOF;
            $$ = $2;
            smtlib_output = $2;
          }
          | TOK_START_SAT check_sat_response
          {
            yychar = YYEOF;
            $$ = $2;
            smtlib_output = $2;
          }
          | TOK_START_ASSERTS get_assertions_response
          | TOK_START_UNSATS get_unsat_core_response
          | TOK_START_VALUE get_value_response
          {
            yychar = YYEOF;
            $$ = $2;
            smtlib_output = $2;
          }
          | TOK_START_ASSIGN get_assignment_response
          | TOK_START_OPTION get_option_response
          | TOK_START_INFO gen_response { yychar = YYEOF; smtlib_output = $2; }
          | TOK_START_SAT gen_response { yychar = YYEOF; smtlib_output = $2; }
          | TOK_START_ASSERTS gen_response {yychar = YYEOF; smtlib_output = $2;}
          | TOK_START_UNSATS gen_response {yychar = YYEOF; smtlib_output = $2; }
          | TOK_START_VALUE gen_response { yychar = YYEOF; smtlib_output = $2; }
          | TOK_START_ASSIGN gen_response {yychar = YYEOF; smtlib_output = $2; }
          | TOK_START_OPTION gen_response {yychar = YYEOF; smtlib_output = $2; }

spec_constant: TOK_NUMERAL
{$$ = new sexpr(); $$->token = TOK_NUMERAL;$$->data = std::string($1);free($1);}
               | TOK_DECIMAL
{$$ = new sexpr(); $$->token = TOK_DECIMAL;$$->data = std::string($1);free($1);}
               | TOK_HEXNUM
{$$ = new sexpr(); $$->token = TOK_HEXNUM;$$->data = std::string($1);free($1);}
               | TOK_BINNUM
{$$ = new sexpr(); $$->token = TOK_BINNUM;$$->data = std::string($1);free($1);}
               | TOK_STRINGLIT
{$$ = new sexpr(); $$->token = TOK_STRINGLIT; $$->data = std::string($1); free($1);}
               | TOK_KW_TRUE
{$$ = new sexpr(); $$->token = TOK_KW_TRUE; }
               | TOK_KW_FALSE
{$$ = new sexpr(); $$->token = TOK_KW_FALSE; }

symbol: TOK_SIMPLESYM { $$ = new std::string($1); free($1); }
        | TOK_QUOTEDSYM {
           // Strip off the | characters.
           std::string tmp($1);
           free($1);
           std::string substr = tmp.substr(1, tmp.size() - 2);
           $$ = new std::string(substr);
         }

symbol_list_empt:
         {
           $$ = new std::list<std::string>();
         }
         | symbol_list_empt symbol
         {
           $1->push_back(*$2);
           delete $2;
           $$ = $1;
         }

numlist: TOK_NUMERAL
         {
           $$ = new std::list<std::string>();
           $$->push_back(std::string($1));
           free($1);
         }
         | numlist TOK_NUMERAL
         {
           $$ = $1;
           $$->push_back(std::string($2));
           free($2);
         }

identifier: symbol | TOK_LPAREN TOK_KW_USCORE symbol numlist TOK_RPAREN

sexpr_list:
         {
           $$ = new std::list<sexpr>();
         }
         | sexpr_list s_expr
         {
           $$ = $1;
           $1->push_back(*$2);
           delete $2;
         }

s_expr: spec_constant
       | symbol
         {
           $$ = new sexpr();
           $$->token = TOK_SIMPLESYM;
           $$->data = std::string(*$1);
           delete $1;
         }
       | TOK_KEYWORD
         {
           $$ = new sexpr();
           $$->token = TOK_KEYWORD;
           $$->data = std::string($1);
           free($1);
         }
       | TOK_LPAREN sexpr_list TOK_RPAREN
         {
           $$ = new sexpr();
           $$->sexpr_list = *$2;
           delete $2;
           $$->token = 0;
         }

attribute_value: spec_constant
       | symbol
         {
           $$ = new sexpr();
           $$->data = *$1;
           delete $1;
           $$->token = TOK_SIMPLESYM;
         }
       | TOK_LPAREN sexpr_list TOK_RPAREN
         {
           $$ = new sexpr();
           $$->sexpr_list = *$2;
           $$->token = 0;
           delete $2;
         }

attribute: TOK_KEYWORD
         {
           $$ = new sexpr();
           $$->token = TOK_KEYWORD;
           $$->data = std::string($1);
           free($1);
         }
       | TOK_KEYWORD attribute_value
         {
           sexpr *s = new sexpr();
           s->token = TOK_KEYWORD;
           s->data = std::string($1);
           free($1);
           $$ = new sexpr();
           $$->sexpr_list.push_front(*s);
           if ($2->token == 0)
             $$->sexpr_list.push_back(*$2);
           else
             $$->sexpr_list.push_back(*$2);
           delete $2;
           delete s;
         }

attr_list: attribute | attr_list attribute

sort_list: sort | sort_list sort

sort: identifier | TOK_LPAREN identifier sort_list TOK_RPAREN

qual_identifier: identifier | TOK_LPAREN TOK_KW_AS identifier sort TOK_RPAREN

var_binding: TOK_LPAREN symbol term TOK_RPAREN

varbind_list: var_binding | varbind_list var_binding

sorted_var: TOK_LPAREN symbol sort TOK_RPAREN

sortvar_list: sorted_var | sortvar_list sorted_var

term_list: term | term_list term

term_list_empt: | term | term_list term

term: spec_constant | qual_identifier | TOK_LPAREN qual_identifier TOK_RPAREN |
      TOK_LPAREN TOK_KW_LET TOK_LPAREN varbind_list TOK_RPAREN term TOK_RPAREN |
      TOK_LPAREN TOK_KW_FORALL TOK_LPAREN sortvar_list TOK_RPAREN term TOK_RPAREN |
      TOK_LPAREN TOK_KW_EXISTS TOK_LPAREN sortvar_list TOK_RPAREN term TOK_RPAREN |
      TOK_LPAREN TOK_KW_EXCL term attr_list TOK_RPAREN

gen_response: TOK_KW_UNSUPPORTED
         {
           $$ = new sexpr();
           $$->token = TOK_KW_UNSUPPORTED;
         }
        | TOK_KW_SUCCESS
         {
           $$ = new sexpr();
           $$->token = TOK_KW_SUCCESS;
         }
        | TOK_LPAREN TOK_KW_ERROR TOK_STRINGLIT TOK_RPAREN
         {
           $$ = new sexpr();
           $$->token = TOK_KW_ERROR;
           $$->data = std::string($3);
           free($3);
         }

error_behaviour: TOK_KW_IMMEXIT | TOK_KW_CONEXECUTION

reason_unknown: TOK_KW_MEMOUT | TOK_KW_INCOMPLETE

status: TOK_KW_SAT
         {
           $$ = new sexpr();
           $$->token = TOK_KW_SAT;
           free($1);
         }
        | TOK_KW_UNSAT
         {
           $$ = new sexpr();
           $$->token = TOK_KW_UNSAT;
           free($1);
         }
        | TOK_KW_UNKNOWN
         {
           $$ = new sexpr();
           $$->token = TOK_KW_UNKNOWN;
           free($1);
         }

info_response_arg: error_behaviour | reason_unknown

info_response: attribute
       | TOK_KEYWORD info_response_arg
         {
           $$ = new sexpr();
           sexpr *s = new sexpr();
           s->token = TOK_KEYWORD;
           s->data = std::string($1);
           free($1);
           $$->token = 0;
           $$->sexpr_list.push_front(*s);
           free(s);
           s = new sexpr();
           s->token = TOK_KEYWORD;
           s->data = std::string($2);
           $$->sexpr_list.push_back(*s);
           free($2);
         }

info_response_list: info_response
         {
           $$ = new std::list<sexpr>();
           $$->push_back(*$1);
           delete $1;
         }
       | info_response_list info_response
         {
           $$ = $1;
           $$->push_back(*$2);
           delete $2;
         }

get_info_response: TOK_LPAREN info_response_list TOK_RPAREN
         {
           $$ = new sexpr();
           $$->sexpr_list = *$2;
           $$->token = 0;
           delete $2;
         }

check_sat_response: status

get_assertions_response: TOK_LPAREN term_list_empt TOK_RPAREN

/* get_proof_response: we're not going to be doing this */

get_unsat_core_response: TOK_LPAREN symbol_list_empt TOK_RPAREN

valuation_pair: TOK_LPAREN term term TOK_RPAREN

valuation_pair_list: valuation_pair | valuation_pair_list valuation_pair

get_value_response: TOK_LPAREN valuation_pair_list TOK_RPAREN

b_value: TOK_KW_TRUE | TOK_KW_FALSE

t_valuation_pair: TOK_LPAREN symbol b_value TOK_RPAREN

t_valuation_pair_empt: | t_valuation_pair_empt t_valuation_pair

get_assignment_response: TOK_LPAREN t_valuation_pair_empt TOK_RPAREN

get_option_response: attribute_value
