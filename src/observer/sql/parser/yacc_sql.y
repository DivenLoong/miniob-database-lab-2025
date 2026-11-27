
%{

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/log/log.h"
#include "common/lang/string.h"
#include "sql/parser/parse_defs.h"
#include "sql/parser/yacc_sql.hpp"
#include "sql/parser/lex_sql.h"
#include "sql/expr/expression.h"
#include "sql/expr/scalar_function_expr.h"
#include "sql/expr/subquery_expr.h"

using namespace std;

string token_name(const char *sql_string, YYLTYPE *llocp)
{
  return string(sql_string + llocp->first_column, llocp->last_column - llocp->first_column + 1);
}

int yyerror(YYLTYPE *llocp, const char *sql_string, ParsedSqlResult *sql_result, yyscan_t scanner, const char *msg)
{
  unique_ptr<ParsedSqlNode> error_sql_node = make_unique<ParsedSqlNode>(SCF_ERROR);
  error_sql_node->error.error_msg = msg;
  error_sql_node->error.line = llocp->first_line;
  error_sql_node->error.column = llocp->first_column;
  sql_result->add_sql_node(std::move(error_sql_node));
  return 0;
}

ArithmeticExpr *create_arithmetic_expression(ArithmeticExpr::Type type,
                                             Expression *left,
                                             Expression *right,
                                             const char *sql_string,
                                             YYLTYPE *llocp)
{
  ArithmeticExpr *expr = new ArithmeticExpr(type, left, right);
  expr->set_name(token_name(sql_string, llocp));
  return expr;
}

UnboundAggregateExpr *create_aggregate_expression(const char *aggregate_name,
                                           Expression *child,
                                           const char *sql_string,
                                           YYLTYPE *llocp)
{
  UnboundAggregateExpr *expr = new UnboundAggregateExpr(aggregate_name, child);
  expr->set_name(token_name(sql_string, llocp));
  return expr;
}

%}

%define api.pure full
%define parse.error verbose
/** 启用位置标识 **/
%locations
%lex-param { yyscan_t scanner }
/** 这些定义了在yyparse函数中的参数 **/
%parse-param { const char * sql_string }
%parse-param { ParsedSqlResult * sql_result }
%parse-param { void * scanner }

//标识tokens
%token  SEMICOLON
        BY
        CREATE
        DROP
        GROUP
        HAVING
        TABLE
        VIEW
        ALTER
        ADD
        COLUMN
        CHANGE
        RENAME
        TO
        TABLES
        INDEX
        UNIQUE
        FULLTEXT
        WITH
        PARSER
        MATCH
        AGAINST
        CALC
        SELECT
        UNION
        ALL
        DESC
        SHOW
        SYNC
        INSERT
        DELETE
        UPDATE
        LBRACE
        RBRACE
        COMMA
        TRX_BEGIN
        TRX_COMMIT
        TRX_ROLLBACK
        INT_T
        STRING_T
        FLOAT_T
        VECTOR_T
        TEXT_T
        HELP
        EXIT
        DOT //QUOTE
        INTO
        VALUES
        FROM
        WHERE
        AND
        IS
        IN
        EXISTS
        OR
        NOT
        NULL_T
        ORDER
        ASC
        LIMIT
        AS
        SET
        ON
        INNER
        JOIN
        DATE_T
        LOAD
        DATA
        INFILE
        EXPLAIN
        STORAGE
        FORMAT
        PRIMARY
        KEY
        ANALYZE
        FIELDS
        TERMINATED
        ENCLOSED
        EQ
        LT
        GT
        LE
        GE
        NE

/** union 中定义各种数据类型，真实生成的代码也是union类型，所以不能有非POD类型的数据 **/
%union {
  ParsedSqlNode *                            sql_node;
  ConditionSqlNode *                         condition;
  Value *                                    value;
  enum CompOp                                comp;
  RelAttrSqlNode *                           rel_attr;
  vector<AttrInfoSqlNode> *                  attr_infos;
  AttrInfoSqlNode *                          attr_info;
  Expression *                               expression;
  vector<unique_ptr<Expression>> *           expression_list;
  vector<Value> *                            value_list;
  vector<UpdateAssignmentSqlNode> *          assignments;
  vector<ConditionSqlNode> *                 condition_list;
  vector<RelAttrSqlNode> *                   rel_attr_list;
  FromSqlNode *                              from;
  RelationSqlNode *                          relation;
  vector<OrderBySqlNode> *                   order_by;
  vector<string> *                           key_list;
  char *                                     cstring;
  int                                        number;
  float                                      floats;
}

%destructor { delete $$; } <condition>
%destructor { delete $$; } <value>
%destructor { delete $$; } <rel_attr>
%destructor { delete $$; } <attr_infos>
%destructor { delete $$; } <expression>
%destructor { delete $$; } <expression_list>
%destructor { delete $$; } <value_list> <assignments>
%destructor { delete $$; } <condition_list>
// %destructor { delete $$; } <rel_attr_list>
%destructor { delete $$; } <from> <relation>
%destructor { delete $$; } <order_by>
%destructor { delete $$; } <key_list>

%token <number> NUMBER
%token <floats> FLOAT
%token <cstring> ID
%token <cstring> SSS
//非终结符

/** type 定义了各种解析后的结果输出的是什么类型。类型对应了 union 中的定义的成员变量名称 **/
%type <number>              type nullability order_direction limit_clause index_unique
%type <condition>           condition
%type <value>               value signed_value
%type <number>              number
%type <relation>            relation
%type <comp>                comp_op
%type <rel_attr>            rel_attr
%type <attr_infos>          attr_def_list
%type <attr_info>           attr_def
%type <value_list>          value_list
%type <assignments>         assignments
%type <condition_list>      where having
%type <condition_list>      condition_list
%type <cstring>             storage_format
%type <key_list>            primary_key
%type <key_list>            attr_list
%type <from>                rel_list
%type <expression>          expression select_expression subquery
%type <expression>          aggregate_expression
%type <expression_list>     expression_list select_list
%type <order_by>            order_by order_list
%type <expression_list>     group_by
%type <cstring>             fields_terminated_by
%type <cstring>             enclosed_by
%type <sql_node>            calc_stmt
%type <sql_node>            select_stmt select_core
%type <number>              union_mode
%type <sql_node>            insert_stmt
%type <sql_node>            update_stmt
%type <sql_node>            delete_stmt
%type <sql_node>            create_table_stmt
%type <sql_node>            drop_table_stmt alter_table_stmt create_view_stmt drop_view_stmt
%type <sql_node>            analyze_table_stmt
%type <sql_node>            show_tables_stmt
%type <sql_node>            desc_table_stmt
%type <sql_node>            create_index_stmt
%type <sql_node>            drop_index_stmt
%type <sql_node>            sync_stmt
%type <sql_node>            begin_stmt
%type <sql_node>            commit_stmt
%type <sql_node>            rollback_stmt
%type <sql_node>            load_data_stmt
%type <sql_node>            explain_stmt
%type <sql_node>            set_variable_stmt
%type <sql_node>            help_stmt
%type <sql_node>            exit_stmt
%type <sql_node>            command_wrapper
// commands should be a list but I use a single command instead
%type <sql_node>            commands

%left '+' '-'
%left '*' '/'
%right UMINUS
%%

commands: command_wrapper opt_semicolon  //commands or sqls. parser starts here.
  {
    unique_ptr<ParsedSqlNode> sql_node = unique_ptr<ParsedSqlNode>($1);
    sql_result->add_sql_node(std::move(sql_node));
  }
  ;

command_wrapper:
    calc_stmt
  | select_stmt
  | insert_stmt
  | update_stmt
  | delete_stmt
  | create_table_stmt
  | drop_table_stmt
  | alter_table_stmt
  | create_view_stmt
  | drop_view_stmt
  | analyze_table_stmt
  | show_tables_stmt
  | desc_table_stmt
  | create_index_stmt
  | drop_index_stmt
  | sync_stmt
  | begin_stmt
  | commit_stmt
  | rollback_stmt
  | load_data_stmt
  | explain_stmt
  | set_variable_stmt
  | help_stmt
  | exit_stmt
    ;

exit_stmt:      
    EXIT {
      (void)yynerrs;  // 这么写为了消除yynerrs未使用的告警。如果你有更好的方法欢迎提PR
      $$ = new ParsedSqlNode(SCF_EXIT);
    };

help_stmt:
    HELP {
      $$ = new ParsedSqlNode(SCF_HELP);
    };

sync_stmt:
    SYNC {
      $$ = new ParsedSqlNode(SCF_SYNC);
    }
    ;

begin_stmt:
    TRX_BEGIN  {
      $$ = new ParsedSqlNode(SCF_BEGIN);
    }
    ;

commit_stmt:
    TRX_COMMIT {
      $$ = new ParsedSqlNode(SCF_COMMIT);
    }
    ;

rollback_stmt:
    TRX_ROLLBACK  {
      $$ = new ParsedSqlNode(SCF_ROLLBACK);
    }
    ;

drop_table_stmt:    /*drop table 语句的语法解析树*/
    DROP TABLE ID {
      $$ = new ParsedSqlNode(SCF_DROP_TABLE);
      $$->drop_table.relation_name = $3;
    };

analyze_table_stmt:  /* analyze table 语法的语法解析树*/
    ANALYZE TABLE ID {
      $$ = new ParsedSqlNode(SCF_ANALYZE_TABLE);
      $$->analyze_table.relation_name = $3;
    }
    ;

show_tables_stmt:
    SHOW TABLES {
      $$ = new ParsedSqlNode(SCF_SHOW_TABLES);
    }
    ;

desc_table_stmt:
    DESC ID  {
      $$ = new ParsedSqlNode(SCF_DESC_TABLE);
      $$->desc_table.relation_name = $2;
    }
    ;

create_index_stmt:
    CREATE index_unique INDEX ID ON ID LBRACE attr_list RBRACE {
      $$ = new ParsedSqlNode(SCF_CREATE_INDEX);
      auto &index = $$->create_index;
      index.unique = $2;
      index.index_name = $4;
      index.relation_name = $6;
      index.attributes.swap(*$8);
      delete $8;
    }
    | ALTER TABLE ID ADD FULLTEXT INDEX ID LBRACE ID RBRACE WITH PARSER ID {
      if (strcasecmp($13, "JIEBA") != 0) {
        yyerror(&@$, sql_string, sql_result, scanner, "unsupported fulltext parser");
        YYERROR;
      }
      $$ = new ParsedSqlNode(SCF_CREATE_INDEX);
      auto &index = $$->create_index;
      index.full_text = true;
      index.index_name = $7;
      index.relation_name = $3;
      index.attributes.emplace_back($9);
    }
    ;
index_unique:
    /* empty */ { $$ = 0; }
    | UNIQUE { $$ = 1; }
    ;

drop_index_stmt:      /*drop index 语句的语法解析树*/
    DROP INDEX ID ON ID
    {
      $$ = new ParsedSqlNode(SCF_DROP_INDEX);
      $$->drop_index.index_name = $3;
      $$->drop_index.relation_name = $5;
    }
    ;
create_table_stmt:    /*create table 语句的语法解析树*/
    CREATE TABLE ID LBRACE attr_def_list primary_key RBRACE storage_format
    {
      $$ = new ParsedSqlNode(SCF_CREATE_TABLE);
      CreateTableSqlNode &create_table = $$->create_table;
      create_table.relation_name = $3;
      //free($3);

      create_table.attr_infos.swap(*$5);
      delete $5;

      if ($6 != nullptr) {
        create_table.primary_keys.swap(*$6);
        delete $6;
      }
      if ($8 != nullptr) {
        create_table.storage_format = $8;
      }
    }
    ;
    
alter_table_stmt:
    ALTER TABLE ID ADD COLUMN attr_def {
      $$ = new ParsedSqlNode(SCF_ALTER_TABLE);
      $$->alter_table.operation = AlterTableSqlNode::Operation::ADD;
      $$->alter_table.table_name = $3;
      $$->alter_table.column = std::move(*$6);
      delete $6;
    }
    | ALTER TABLE ID DROP COLUMN ID {
      $$ = new ParsedSqlNode(SCF_ALTER_TABLE);
      $$->alter_table.operation = AlterTableSqlNode::Operation::DROP;
      $$->alter_table.table_name = $3;
      $$->alter_table.old_column = $6;
    }
    | ALTER TABLE ID CHANGE COLUMN ID attr_def {
      $$ = new ParsedSqlNode(SCF_ALTER_TABLE);
      $$->alter_table.operation = AlterTableSqlNode::Operation::CHANGE;
      $$->alter_table.table_name = $3;
      $$->alter_table.old_column = $6;
      $$->alter_table.column = std::move(*$7);
      delete $7;
    }
    | ALTER TABLE ID RENAME TO ID {
      $$ = new ParsedSqlNode(SCF_ALTER_TABLE);
      $$->alter_table.operation = AlterTableSqlNode::Operation::RENAME;
      $$->alter_table.table_name = $3;
      $$->alter_table.new_table = $6;
    }
    ;
create_view_stmt:
    CREATE VIEW ID AS select_stmt {
      $$ = new ParsedSqlNode(SCF_CREATE_VIEW);
      $$->create_view.name = $3;
      $$->create_view.query = token_name(sql_string, &@5);
      delete $5;
    }
    | CREATE VIEW ID LBRACE attr_list RBRACE AS select_stmt {
      $$ = new ParsedSqlNode(SCF_CREATE_VIEW);
      $$->create_view.name = $3;
      $$->create_view.columns.swap(*$5);
      $$->create_view.query = token_name(sql_string, &@8);
      delete $5;
      delete $8;
    }
    ;
drop_view_stmt:
    DROP VIEW ID {
      $$ = new ParsedSqlNode(SCF_DROP_VIEW);
      $$->create_view.name = $3;
    }
    ;
attr_def_list:
    attr_def
    {
      $$ = new vector<AttrInfoSqlNode>;
      $$->emplace_back(*$1);
      delete $1;
    }
    | attr_def_list COMMA attr_def
    {
      $$ = $1;
      $$->emplace_back(*$3);
      delete $3;
    }
    ;
    
attr_def:
    ID type LBRACE number RBRACE nullability
    {
      $$ = new AttrInfoSqlNode;
      $$->type = (AttrType)$2;
      $$->name = $1;
      $$->length = $4;
      $$->nullable = $6 != 2;
      $$->nullable_explicit = $6 != 0;
    }
    | ID type nullability
    {
      $$ = new AttrInfoSqlNode;
      $$->type = (AttrType)$2;
      $$->name = $1;
      $$->length = $$->type == AttrType::VECTORS ? 2048 : 4;
      $$->nullable = $3 != 2;
      $$->nullable_explicit = $3 != 0;
    }
    ;
nullability:
    /* empty */ { $$ = 0; }
    | NULL_T { $$ = 1; }
    | NOT NULL_T { $$ = 2; }
    ;
number:
    NUMBER {$$ = $1;}
    ;
type:
    INT_T      { $$ = static_cast<int>(AttrType::INTS); }
    | STRING_T { $$ = static_cast<int>(AttrType::CHARS); }
    | FLOAT_T  { $$ = static_cast<int>(AttrType::FLOATS); }
    | DATE_T   { $$ = static_cast<int>(AttrType::DATES); }
    | VECTOR_T { $$ = static_cast<int>(AttrType::VECTORS); }
    | TEXT_T   { $$ = static_cast<int>(AttrType::TEXTS); }
    ;
primary_key:
    /* empty */
    {
      $$ = nullptr;
    }
    | COMMA PRIMARY KEY LBRACE attr_list RBRACE
    {
      $$ = $5;
    }
    ;

attr_list:
    ID {
      $$ = new vector<string>();
      $$->push_back($1);
    }
    | ID COMMA attr_list {
      if ($3 != nullptr) {
        $$ = $3;
      } else {
        $$ = new vector<string>;
      }

      $$->insert($$->begin(), $1);
    }
    ;

insert_stmt:        /*insert   语句的语法解析树*/
    INSERT INTO ID VALUES LBRACE value_list RBRACE 
    {
      $$ = new ParsedSqlNode(SCF_INSERT);
      $$->insertion.relation_name = $3;
      $$->insertion.values.swap(*$6);
      delete $6;
    }
    | INSERT INTO ID LBRACE attr_list RBRACE VALUES LBRACE value_list RBRACE {
      $$ = new ParsedSqlNode(SCF_INSERT);
      $$->insertion.relation_name = $3;
      $$->insertion.columns.swap(*$5);
      $$->insertion.values.swap(*$9);
      delete $5;
      delete $9;
    }
    ;

value_list:
    signed_value
    {
      $$ = new vector<Value>;
      $$->emplace_back(*$1);
      delete $1;
    }
    | value_list COMMA signed_value {
      $$ = $1;
      $$->emplace_back(*$3);
      delete $3;
    }
    ;
signed_value:
    value { $$ = $1; }
    | '-' NUMBER { $$ = new Value(-$2); }
    | '-' FLOAT { $$ = new Value(-$2); }
    | '+' NUMBER { $$ = new Value($2); }
    | '+' FLOAT { $$ = new Value($2); }
    | ID LBRACE value RBRACE {
      unique_ptr<Value> argument($3);
      auto result = make_unique<Value>();
      if ((strcasecmp($1, "STRING_TO_VECTOR") != 0 && strcasecmp($1, "TO_VECTOR") != 0) ||
          argument->attr_type() != AttrType::CHARS ||
          Value::cast_to(*argument, AttrType::VECTORS, *result) != RC::SUCCESS) {
        result->set_type(AttrType::UNDEFINED);
      }
      $$ = result.release();
    }
    ;
value:
    NULL_T { $$ = new Value(); $$->set_null(); }
    | NUMBER {
      $$ = new Value((int)$1);
      @$ = @1;
    }
    |FLOAT {
      $$ = new Value((float)$1);
      @$ = @1;
    }
    |SSS {
      char *tmp = common::substr($1,1,strlen($1)-2);
      $$ = new Value(tmp);
      free(tmp);
    }
    ;
storage_format:
    /* empty */
    {
      $$ = nullptr;
    }
    | STORAGE FORMAT EQ ID
    {
      $$ = $4;
    }
    ;
    
delete_stmt:    /*  delete 语句的语法解析树*/
    DELETE FROM ID where 
    {
      $$ = new ParsedSqlNode(SCF_DELETE);
      $$->deletion.relation_name = $3;
      if ($4 != nullptr) {
        $$->deletion.conditions.swap(*$4);
        delete $4;
      }
    }
    ;
update_stmt:
    UPDATE ID SET assignments where {
      $$ = new ParsedSqlNode(SCF_UPDATE);
      $$->update.relation_name = $2;
      $$->update.assignments.swap(*$4);
      delete $4;
      if ($5 != nullptr) {
        $$->update.conditions.swap(*$5);
        delete $5;
      }
    }
    ;
assignments:
    ID EQ signed_value {
      $$ = new vector<UpdateAssignmentSqlNode>;
      $$->push_back({$1, std::move(*$3)});
      delete $3;
    }
    | assignments COMMA ID EQ signed_value {
      $$ = $1;
      $$->push_back({$3, std::move(*$5)});
      delete $5;
    }
    ;

select_stmt:
    select_core { $$ = $1; }
    | select_stmt UNION union_mode select_core {
      $$ = new ParsedSqlNode(SCF_UNION);
      $$->union_left.reset($1);
      $$->union_right.reset($4);
      $$->union_all = $3 != 0;
    }
    ;
union_mode:
    /* empty */ { $$ = 0; }
    | ALL { $$ = 1; }
    ;
select_core:
    SELECT select_list {
      $$ = new ParsedSqlNode(SCF_CALC);
      $$->calc.expressions.swap(*$2);
      delete $2;
    }
    | SELECT select_list FROM rel_list where group_by having order_by limit_clause
    {
      $$ = new ParsedSqlNode(SCF_SELECT);
      if ($2 != nullptr) {
        $$->selection.expressions.swap(*$2);
        delete $2;
      }

      if ($4 != nullptr) {
        $$->selection.relations = std::move($4->relations);
        $$->selection.conditions = std::move($4->conditions);
        delete $4;
      }

      if ($5 != nullptr) {
        for (auto &condition : *$5) {
          $$->selection.conditions.emplace_back(std::move(condition));
        }
        delete $5;
      }

      if ($6 != nullptr) {
        $$->selection.group_by.swap(*$6);
        delete $6;
      }
      if ($7 != nullptr) {
        $$->selection.having.swap(*$7);
        delete $7;
      }
      if ($8 != nullptr) {
        $$->selection.order_by.swap(*$8);
        delete $8;
      }
      $$->selection.limit = $9;
    }
    ;
calc_stmt:
    CALC expression_list
    {
      $$ = new ParsedSqlNode(SCF_CALC);
      $$->calc.expressions.swap(*$2);
      delete $2;
    }
    ;

select_list:
    select_expression {
      $$ = new vector<unique_ptr<Expression>>;
      $$->emplace_back($1);
    }
    | select_list COMMA select_expression {
      $$ = $1;
      $$->emplace_back($3);
    }
    ;
select_expression:
    expression { $$ = $1; }
    | expression AS ID {
      $$ = $1;
      $$->set_alias($3);
    }
    | expression ID {
      $$ = $1;
      $$->set_alias($2);
    }
    ;
expression_list:
    expression
    {
      $$ = new vector<unique_ptr<Expression>>;
      $$->emplace_back($1);
    }
    | expression COMMA expression_list
    {
      if ($3 != nullptr) {
        $$ = $3;
      } else {
        $$ = new vector<unique_ptr<Expression>>;
      }
      $$->emplace($$->begin(), $1);
    }
    ;
expression:
    expression '+' expression {
      $$ = create_arithmetic_expression(ArithmeticExpr::Type::ADD, $1, $3, sql_string, &@$);
    }
    | expression '-' expression {
      $$ = create_arithmetic_expression(ArithmeticExpr::Type::SUB, $1, $3, sql_string, &@$);
    }
    | expression '*' expression {
      $$ = create_arithmetic_expression(ArithmeticExpr::Type::MUL, $1, $3, sql_string, &@$);
    }
    | subquery { $$ = $1; }
    | expression '/' expression {
      $$ = create_arithmetic_expression(ArithmeticExpr::Type::DIV, $1, $3, sql_string, &@$);
    }
    | LBRACE expression RBRACE {
      $$ = $2;
      $$->set_name(token_name(sql_string, &@$));
    }
    | '-' expression %prec UMINUS {
      $$ = create_arithmetic_expression(ArithmeticExpr::Type::NEGATIVE, $2, nullptr, sql_string, &@$);
    }
    | '*' {
      $$ = new StarExpr();
    }
    | ID DOT '*' {
      $$ = new StarExpr($1);
    }
    | value {
      $$ = new ValueExpr(*$1);
      $$->set_name(token_name(sql_string, &@$));
      delete $1;
    }
    | rel_attr {
      RelAttrSqlNode *node = $1;
      $$ = new UnboundFieldExpr(node->relation_name, node->attribute_name);
      $$->set_name(token_name(sql_string, &@$));
      delete $1;
    }
    | aggregate_expression {
      $$ = $1;
    }
    | MATCH LBRACE expression RBRACE AGAINST LBRACE expression RBRACE {
      vector<unique_ptr<Expression>> args;
      args.emplace_back($3);
      args.emplace_back($7);
      $$ = new ScalarFunctionExpr("match", std::move(args));
      $$->set_name(token_name(sql_string, &@$));
    }
    ;

aggregate_expression:
    ID LBRACE expression_list RBRACE {
      if (ScalarFunctionExpr::supports($1)) {
        $$ = new ScalarFunctionExpr($1, std::move(*$3));
        $$->set_name(token_name(sql_string, &@$));
      } else if ($3->size() == 1) {
        $$ = create_aggregate_expression($1, $3->front().release(), sql_string, &@$);
      } else {
        delete $3;
        yyerror(&@$, sql_string, sql_result, scanner, "invalid function arguments");
        YYERROR;
      }
      delete $3;
    }
    ;

rel_attr:
    ID {
      $$ = new RelAttrSqlNode;
      $$->attribute_name = $1;
    }
    | ID DOT ID {
      $$ = new RelAttrSqlNode;
      $$->relation_name  = $1;
      $$->attribute_name = $3;
    }
    ;

relation:
    ID { $$ = new RelationSqlNode{$1, {}}; }
    | ID AS ID { $$ = new RelationSqlNode{$1, $3}; }
    | ID ID { $$ = new RelationSqlNode{$1, $2}; }
    ;
rel_list:
    relation {
      $$ = new FromSqlNode;
      $$->relations.emplace_back(std::move(*$1));
      delete $1;
    }
    | rel_list COMMA relation {
      $$ = $1;
      $$->relations.emplace_back(std::move(*$3));
      delete $3;
    }
    | rel_list INNER JOIN relation ON condition_list {
      $$ = $1;
      $$->relations.emplace_back(std::move(*$4));
      delete $4;
      for (auto &condition : *$6) {
        $$->conditions.emplace_back(std::move(condition));
      }
      delete $6;
    }
    | rel_list JOIN relation ON condition_list {
      $$ = $1;
      $$->relations.emplace_back(std::move(*$3));
      delete $3;
      for (auto &condition : *$5) {
        $$->conditions.emplace_back(std::move(condition));
      }
      delete $5;
    }
    ;

subquery:
    LBRACE select_stmt RBRACE {
      $$ = new SubqueryExpr(token_name(sql_string, &@2));
      delete $2;
    }
    ;
where:
    /* empty */
    {
      $$ = nullptr;
    }
    | WHERE condition_list {
      $$ = $2;  
    }
    ;
having:
    /* empty */ { $$ = nullptr; }
    | HAVING condition_list { $$ = $2; }
    ;
condition_list:
    condition {
      $$ = new vector<ConditionSqlNode>;
      $$->emplace_back(std::move(*$1));
      delete $1;
    }
    | condition AND condition_list {
      $$ = $3;
      $$->emplace_back(std::move(*$1));
      delete $1;
    }
    | condition OR condition_list {
      $$ = $3;
      $1->disjunctive = true;
      $$->emplace_back(std::move(*$1));
      delete $1;
    }
    ;
condition:
    expression comp_op expression {
      $$ = new ConditionSqlNode{};
      $$->left_expression.reset($1);
      $$->right_expression.reset($3);
      $$->comp = $2;
    }
    | expression IN subquery {
      $$ = new ConditionSqlNode{};
      $$->left_expression.reset($1);
      $$->right_expression.reset($3);
      $$->comp = IN_SUBQUERY;
    }
    | expression NOT IN subquery {
      $$ = new ConditionSqlNode{};
      $$->left_expression.reset($1);
      $$->right_expression.reset($4);
      $$->comp = NOT_IN_SUBQUERY;
    }
    | EXISTS subquery {
      $$ = new ConditionSqlNode{};
      $$->left_expression.reset(new ValueExpr(Value(true)));
      $$->right_expression.reset($2);
      $$->comp = EXISTS_SUBQUERY;
    }
    | NOT EXISTS subquery {
      $$ = new ConditionSqlNode{};
      $$->left_expression.reset(new ValueExpr(Value(true)));
      $$->right_expression.reset($3);
      $$->comp = NOT_EXISTS_SUBQUERY;
    }
    | expression IS NULL_T {
      $$ = new ConditionSqlNode{};
      $$->left_expression.reset($1);
      $$->right_expression.reset(new ValueExpr(Value()));
      $$->comp = IS_NULL;
    }
    | expression IS NOT NULL_T {
      $$ = new ConditionSqlNode{};
      $$->left_expression.reset($1);
      $$->right_expression.reset(new ValueExpr(Value()));
      $$->comp = IS_NOT_NULL;
    }
    ;

comp_op:
      EQ { $$ = EQUAL_TO; }
    | LT { $$ = LESS_THAN; }
    | GT { $$ = GREAT_THAN; }
    | LE { $$ = LESS_EQUAL; }
    | GE { $$ = GREAT_EQUAL; }
    | NE { $$ = NOT_EQUAL; }
    ;

// your code here
group_by:
    /* empty */
    {
      $$ = nullptr;
    }
    | GROUP BY expression_list
    {
      // group by 的表达式范围与select查询值的表达式范围是不同的，比如group by不支持 *
      // 但是这里没有处理。
      $$ = $3;
    }
    ;
order_by:
    /* empty */ { $$ = nullptr; }
    | ORDER BY order_list { $$ = $3; }
    ;
order_list:
    expression order_direction {
      $$ = new vector<OrderBySqlNode>;
      $$->push_back({unique_ptr<Expression>($1), $2 != 0});
    }
    | order_list COMMA expression order_direction {
      $$ = $1;
      $$->push_back({unique_ptr<Expression>($3), $4 != 0});
    }
    ;
order_direction:
    /* empty */ { $$ = 1; }
    | ASC { $$ = 1; }
    | DESC { $$ = 0; }
    ;
limit_clause:
    /* empty */ { $$ = -1; }
    | LIMIT NUMBER { $$ = $2; }
    ;
load_data_stmt:
    LOAD DATA INFILE SSS INTO TABLE ID fields_terminated_by enclosed_by
    {
      char *tmp_file_name = common::substr($4, 1, strlen($4) - 2);
      
      $$ = new ParsedSqlNode(SCF_LOAD_DATA);
      $$->load_data.relation_name = $7;
      $$->load_data.file_name = tmp_file_name;
      if ($8 != nullptr) {
        char *tmp = common::substr($8,1,strlen($8)-2);
        $$->load_data.terminated = $8;
        free(tmp);
      }
      if ($9 != nullptr) {
        char *tmp = common::substr($9,1,strlen($9)-2);
        $$->load_data.enclosed = $9;
        free(tmp);
      }
      free(tmp_file_name);
    }
    ;

fields_terminated_by:
    /* empty */
    {
      $$ = nullptr;
    }
    | FIELDS TERMINATED BY SSS
    {
      $$ = $4;
    };

enclosed_by:
    /* empty */
    {
      $$ = nullptr;
    }
    | ENCLOSED BY SSS
    {
      $$ = $3;
    };

explain_stmt:
    EXPLAIN command_wrapper
    {
      $$ = new ParsedSqlNode(SCF_EXPLAIN);
      $$->explain.sql_node = unique_ptr<ParsedSqlNode>($2);
    }
    ;

set_variable_stmt:
    SET ID EQ value
    {
      $$ = new ParsedSqlNode(SCF_SET_VARIABLE);
      $$->set_variable.name  = $2;
      $$->set_variable.value = *$4;
      delete $4;
    }
    ;

opt_semicolon: /*empty*/
    | SEMICOLON
    ;
%%
//_____________________________________________________________________
extern void scan_string(const char *str, yyscan_t scanner);

int sql_parse(const char *s, ParsedSqlResult *sql_result) {
  yyscan_t scanner;
  std::vector<char *> allocated_strings;
  yylex_init_extra(static_cast<void*>(&allocated_strings),&scanner);
  scan_string(s, scanner);
  int result = yyparse(s, sql_result, scanner);

  for (char *ptr : allocated_strings) {
    free(ptr);
  }
  allocated_strings.clear();

  yylex_destroy(scanner);
  return result;
}