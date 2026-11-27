/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "storage/db/db.h"

#include <fstream>
#include <unordered_set>

#include "json/json.h"
#include "sql/parser/parse.h"
#include "sql/expr/expression_iterator.h"
#include "sql/expr/subquery_expr.h"
#include "sql/stmt/calc_stmt.h"
#include "sql/stmt/select_stmt.h"
#include "storage/table/table.h"

namespace {
string view_file(const string &directory, const string &name)
{
  return (filesystem::path(directory) / (name + ".view")).string();
}
}

const ViewDefinition *Db::find_view(const string &name) const
{
  auto found = views_.find(name);
  return found == views_.end() ? nullptr : &found->second;
}

RC Db::prepare_view_query(const string &query, unique_ptr<Stmt> &statement)
{
  static thread_local int depth = 0;
  if (depth >= 32) {
    return RC::INVALID_ARGUMENT;
  }
  struct DepthGuard
  {
    int &depth;
    explicit DepthGuard(int &value) : depth(value) { ++depth; }
    ~DepthGuard() { --depth; }
  } guard(depth);

  ParsedSqlResult parsed;
  RC rc = parse(query.c_str(), &parsed);
  if (OB_FAIL(rc) || parsed.sql_nodes().size() != 1) {
    return RC::SQL_SYNTAX;
  }
  ParsedSqlNode &node = *parsed.sql_nodes().front();
  if (node.flag != SCF_SELECT && node.flag != SCF_CALC) {
    return RC::UNSUPPORTED;
  }
  Stmt *raw = nullptr;
  rc = Stmt::create_stmt(this, node, raw);
  statement.reset(raw);
  return rc;
}

RC Db::create_view(const CreateViewSqlNode &request)
{
  if (find_table(request.name.c_str()) || find_view(request.name)) {
    return RC::SCHEMA_TABLE_EXIST;
  }
  unique_ptr<Stmt> definition;
  RC rc = prepare_view_query(request.query, definition);
  if (OB_FAIL(rc)) {
    return rc;
  }
  auto *select = definition->type() == StmtType::SELECT ? static_cast<SelectStmt *>(definition.get()) : nullptr;
  const auto &expressions = select ? select->query_expressions() : static_cast<CalcStmt *>(definition.get())->expressions();
  if (expressions.empty() || (!request.columns.empty() && request.columns.size() != expressions.size())) {
    return RC::SCHEMA_FIELD_MISSING;
  }

  bool updatable = select && !select->tables().empty() && select->group_by().empty() &&
      select->order_by().empty() && select->limit() < 0;
  ViewDefinition view;
  view.query = request.query;
  vector<AttrInfoSqlNode> columns;
  std::unordered_set<string> names;
  Json::Value fields(Json::arrayValue);
  for (size_t i = 0; i < expressions.size(); ++i) {
    const auto &expression = expressions[i];
    const auto *field = expression->type() == ExprType::FIELD ? static_cast<const FieldExpr *>(expression.get()) : nullptr;
    const char *default_name = field && !*expression->alias() ? field->field_name() : expression->display_name();
    const string column_name = request.columns.empty() ? default_name : request.columns[i];
    if (column_name.empty() || !names.insert(column_name).second) {
      return RC::INVALID_ARGUMENT;
    }
    updatable = updatable && field != nullptr;
    ViewWriteColumn source;
    if (field != nullptr && find_table(field->field().table()->name())) {
      source = {field->field_name(), field->field().table()->name(), field->table_name()};
    }
    view.write_columns.emplace_back(std::move(source));
    AttrType type = expression->value_type();
    if (type == AttrType::UNDEFINED || type == AttrType::NULLS) {
      type = AttrType::CHARS;
    }
    size_t length = expression->value_length() > 0 ? expression->value_length() : 4;
    if (type == AttrType::VECTORS) {
      length = field ? field->field().meta()->vector_dimension() : 2048;
    }
    columns.push_back({type, column_name, length, true, true});
    Json::Value item;
    item["name"] = column_name;
    item["type"] = attr_type_to_string(type);
    item["length"] = static_cast<Json::UInt>(length);
    fields.append(std::move(item));
  }
  if (updatable && select->tables().size() == 1 && find_table(select->tables().front()->name())) {
    view.write_table = select->tables().front()->name();
  }
  for (Json::ArrayIndex i = 0; i < fields.size(); ++i) {
    auto &source = view.write_columns[i];
    if (!updatable) {
      source = {};
    }
    fields[i]["source"] = source.name;
    fields[i]["source_table"] = source.table;
    fields[i]["source_qualifier"] = source.qualifier;
  }
  auto schema = make_unique<Table>();
  rc = schema->init_view_schema(this, request.name, columns);
  if (OB_FAIL(rc)) {
    return rc;
  }

  Json::Value json;
  json["name"] = request.name;
  json["query"] = request.query;
  json["write_table"] = view.write_table;
  json["fields"] = std::move(fields);
  const string path = view_file(path_, request.name);
  const string temporary = path + ".tmp";
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  if (!output) {
    return RC::IOERR_OPEN;
  }
  output << json;
  output.close();
  if (!output || rename(temporary.c_str(), path.c_str()) != 0) {
    std::error_code unused;
    filesystem::remove(temporary, unused);
    return RC::IOERR_WRITE;
  }
  view.schema = schema.release();
  views_.emplace(request.name, std::move(view));
  return RC::SUCCESS;
}

const ViewWriteColumn *Db::view_write_source(const ViewDefinition &view, const string &exposed) const
{
  for (int i = 0; i < view.schema->table_meta().field_num(); ++i) {
    const auto &source = view.write_columns[i];
    if (!source.table.empty() &&
        strcasecmp(exposed.c_str(), view.schema->table_meta().field(i)->name()) == 0) {
      return &source;
    }
  }
  return nullptr;
}

string Db::view_write_column(const ViewDefinition &view, const string &exposed) const
{
  const auto *source = view_write_source(view, exposed);
  return source == nullptr ? string{} : source->name;
}

RC Db::joined_view_write_condition(const ViewDefinition &view, const ViewWriteColumn &target, const string &outer,
    const vector<ConditionSqlNode> &given, vector<ConditionSqlNode> &combined)
{
  Table *table = find_table(target.table.c_str());
  if (table == nullptr) {
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }
  // A single joined scan produces the record IDs of the writable relation.
  auto subquery = make_unique<SubqueryExpr>(view.query);
  subquery->project_record_id(target.qualifier);
  auto &predicates = subquery->additional_conditions();

  std::function<RC(unique_ptr<Expression> &)> map_field = [&](unique_ptr<Expression> &expression) {
    if (expression->type() != ExprType::UNBOUND_FIELD) {
      return ExpressionIterator::iterate_child_expr(*expression, map_field);
    }
    const auto &column = static_cast<const UnboundFieldExpr &>(*expression);
    if (*column.table_name() != '\0' && strcasecmp(column.table_name(), view.schema->name()) != 0) {
      return RC::SCHEMA_TABLE_NOT_EXIST;
    }
    const auto *source = view_write_source(view, column.field_name());
    if (source == nullptr) {
      return RC::SCHEMA_FIELD_NOT_EXIST;
    }
    expression = make_unique<UnboundFieldExpr>(source->qualifier, source->name);
    return RC::SUCCESS;
  };
  for (const auto &given_term : given) {
    if (given_term.disjunctive) {
      return RC::UNSUPPORTED;
    }
    auto left = given_term.left_expression->copy();
    auto right = given_term.right_expression->copy();
    RC rc = map_field(left);
    if (OB_SUCC(rc)) {
      rc = map_field(right);
    }
    if (OB_FAIL(rc)) {
      return rc;
    }
    ConditionSqlNode term{};
    term.comp = given_term.comp;
    term.left_expression = std::move(left);
    term.right_expression = std::move(right);
    predicates.emplace_back(std::move(term));
  }
  ConditionSqlNode condition{};
  condition.comp = IN_SUBQUERY;
  condition.left_expression = make_unique<RowIdExpr>(outer);
  condition.right_expression = std::move(subquery);
  combined.emplace_back(std::move(condition));
  return RC::SUCCESS;
}

RC Db::view_write_conditions(const ViewDefinition &view, const vector<ConditionSqlNode> &given,
    vector<ConditionSqlNode> &combined)
{
  if (view.write_table.empty()) {
    return RC::UNSUPPORTED;
  }
  ParsedSqlResult parsed;
  RC rc = parse(view.query.c_str(), &parsed);
  if (OB_FAIL(rc) || parsed.sql_nodes().size() != 1 || parsed.sql_nodes().front()->flag != SCF_SELECT) {
    return RC::SQL_SYNTAX;
  }

  const auto rewrite = [&](const vector<ConditionSqlNode> &conditions, bool mapped) {
    std::function<RC(unique_ptr<Expression> &)> visit = [&](unique_ptr<Expression> &expression) {
      if (expression->type() == ExprType::UNBOUND_FIELD) {
        const auto &original = static_cast<UnboundFieldExpr &>(*expression);
        if (mapped && *original.table_name() && strcasecmp(original.table_name(), view.schema->name()) != 0) {
          return RC::SCHEMA_TABLE_NOT_EXIST;
        }
        const string name = mapped ? view_write_column(view, original.field_name()) : original.field_name();
        if (name.empty()) {
          return RC::SCHEMA_FIELD_NOT_EXIST;
        }
        expression = make_unique<UnboundFieldExpr>(view.write_table, name);
        return RC::SUCCESS;
      }
      return ExpressionIterator::iterate_child_expr(*expression, visit);
    };
    for (const auto &item : conditions) {
      // The statement binder handles conjunctions of these two predicate sources.
      if (item.disjunctive) {
        return RC::UNSUPPORTED;
      }
      auto left = item.left_expression->copy();
      auto right = item.right_expression->copy();
      RC rc = visit(left);
      if (OB_SUCC(rc)) {
        rc = visit(right);
      }
      if (OB_FAIL(rc)) {
        return rc;
      }
      ConditionSqlNode updated{};
      updated.comp = item.comp;
      updated.left_expression = std::move(left);
      updated.right_expression = std::move(right);
      combined.push_back(std::move(updated));
    }
    return RC::SUCCESS;
  };

  rc = rewrite(parsed.sql_nodes().front()->selection.conditions, false);
  return OB_SUCC(rc) ? rewrite(given, true) : rc;
}

RC Db::drop_view(const string &name)
{
  auto found = views_.find(name);
  if (found == views_.end()) {
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }
  std::error_code error;
  filesystem::remove(view_file(path_, name), error);
  if (error) {
    return RC::IOERR_WRITE;
  }
  delete found->second.schema;
  views_.erase(found);
  return RC::SUCCESS;
}

RC Db::open_views()
{
  std::error_code error;
  for (const auto &file : filesystem::directory_iterator(path_, error)) {
    if (file.path().extension() != ".view") {
      continue;
    }
    Json::CharReaderBuilder builder;
    std::ifstream input(file.path(), std::ios::binary);
    Json::Value json;
    string message;
    if (!input || !Json::parseFromStream(builder, input, &json, &message)) {
      return RC::IOERR_READ;
    }
    const string name = file.path().stem().string();
    if (name != json["name"].asString() || find_table(name.c_str())) {
      return RC::INVALID_ARGUMENT;
    }
    vector<AttrInfoSqlNode> columns;
    ViewDefinition view;
    view.query = json["query"].asString();
    view.write_table = json["write_table"].asString();
    for (const auto &field : json["fields"]) {
      columns.push_back({attr_type_from_string(field["type"].asCString()), field["name"].asString(),
          field["length"].asUInt(), true, true});
      ViewWriteColumn source{field["source"].asString(), field["source_table"].asString(),
          field["source_qualifier"].asString()};
      if (!field.isMember("source_table") && !source.name.empty() && !view.write_table.empty()) {
        source.table = view.write_table;
        source.qualifier = view.write_table;
      }
      view.write_columns.emplace_back(std::move(source));
    }
    auto table = make_unique<Table>();
    RC rc = table->init_view_schema(this, name, columns);
    if (OB_FAIL(rc)) {
      return rc;
    }
    view.schema = table.release();
    views_.emplace(name, std::move(view));
  }
  return error ? RC::IOERR_READ : RC::SUCCESS;
}
